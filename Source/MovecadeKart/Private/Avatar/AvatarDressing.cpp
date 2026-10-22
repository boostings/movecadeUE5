#include "Avatar/AvatarDressing.h"

#include "Algo/Find.h"
#include "Avatar/AvatarProps.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "KartDriverAnimInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

DEFINE_LOG_CATEGORY_STATIC(LogAvatarDressing, Log, All);

namespace
{
	/** The skin tone instances of M_AvatarSkin (each with its own subsurface profile), in ESkinTone order. */
	const TCHAR* const SkinToneMaterials[] = { TEXT("/Game/Avatars/Materials/MI_AvatarSkin_Light.MI_AvatarSkin_Light"),
		TEXT("/Game/Avatars/Materials/MI_AvatarSkin_Medium.MI_AvatarSkin_Medium"), TEXT("/Game/Avatars/Materials/MI_AvatarSkin_Deep.MI_AvatarSkin_Deep") };
	/** The skin's maps, per driver, carried over onto the tone instance. */
	const TCHAR* const SkinMaps[] = { TEXT("BaseTex"), TEXT("NormalTex"), TEXT("AOTex") };
	/** Skin luminance (linear) above which a tone is light, and below which it is deep. */
	constexpr float LightToneFrom = 0.42f;
	constexpr float DeepToneBelow = 0.15f;
	/** The stock tones' luminance range (KartLook.cpp: deep 6b3d28 .. porcelain f9e4d2), over which the tuning eases. */
	constexpr float DeepestLuma = 0.06f;
	constexpr float PalestLuma = 0.8f;
	/** Shading that follows the skin's depth (from deep to light): scatter, flush, warm rim and the lift in shadow. */
	constexpr float ScatterDeep = 0.55f, ScatterLight = 0.95f;
	constexpr float FlushDeep = 0.45f, FlushLight = 1.f;
	constexpr float RimDeep = 0.05f, RimLight = 0.12f;
	constexpr float LiftDeep = 0.012f, LiftLight = 0.006f;
	constexpr float SaturationDeep = 0.78f, SaturationLight = 0.98f;
	/** The lips: a satin finish over the skin. */
	constexpr float LipRoughness = 0.36f;
	constexpr float LipSpecular = 0.5f;
	/** Brows and lashes (the "hair" slot) get their own material: the hair colour deepened, the lashes near black. */
	const TCHAR* const BrowMaterial = TEXT("/Game/Avatars/Materials/M_AvatarBrow.M_AvatarBrow");
	constexpr float BrowDarken = 0.55f;
	/** The eyes are re-aimed when they turn by more than this (radians); finer turns aren't visible. */
	constexpr float EyeAimEpsilon = 0.004f;

	/** The eye material's gaze parameter (M_AvatarEye): the eyes' direction in the mesh's rest space. */
	const FName& EyeGazeParam()
	{
		static const FName Name(TEXT("EyeGaze"));
		return Name;
	}

	FLinearColor Lerp3(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return FLinearColor(FMath::Lerp(A.R, B.R, T), FMath::Lerp(A.G, B.G, T), FMath::Lerp(A.B, B.B, T), 1.f);
	}

	/** Shows or hides every render section that draws material slot Slot. */
	void ShowSlot(USkeletalMeshComponent* Mesh, FName Slot, bool bShow)
	{
		USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		const int32 Material = Mesh ? Mesh->GetMaterialIndex(Slot) : INDEX_NONE;
		FSkeletalMeshRenderData* Render = Asset ? Asset->GetResourceForRendering() : nullptr;
		if (Material == INDEX_NONE || !Render)
		{
			return;
		}
		for (int32 Lod = 0; Lod < Render->LODRenderData.Num(); ++Lod)
		{
			const TArray<FSkelMeshRenderSection>& Sections = Render->LODRenderData[Lod].RenderSections;
			for (int32 Section = 0; Section < Sections.Num(); ++Section)
			{
				if (Sections[Section].MaterialIndex == Material)
				{
					Mesh->ShowMaterialSection(Material, Section, bShow, Lod);
				}
			}
		}
	}
}

namespace MovecadeKart::Avatar
{
	FLinearColor LipColour(const FLinearColor& Skin)
	{
		// A muted rose: lips read as the skin's own, a little deeper and pinker, not painted on.
		const FLinearColor Rose = FLinearColor::FromSRGBColor(FColor(0xb8, 0x5a, 0x66));
		return Lerp3(Skin, Rose, 0.3f) * 0.86f;
	}

	FLinearColor BrowColour(const FLinearColor& Hair)
	{
		FLinearColor C = Hair * BrowDarken;
		C.A = 1.f;
		return C;
	}

	FSkinTuning SkinTuning(const FLinearColor& Skin)
	{
		const float Luma = Skin.GetLuminance();
		FSkinTuning T;
		T.Tone = Luma >= LightToneFrom ? ESkinTone::Light : (Luma < DeepToneBelow ? ESkinTone::Deep : ESkinTone::Medium);
		// 0 at the deepest stock tone .. 1 at the palest; the tuning eases between the two ends.
		const float Light01 = FMath::Clamp((Luma - DeepestLuma) / (PalestLuma - DeepestLuma), 0.f, 1.f);
		T.Scatter = FMath::Lerp(ScatterDeep, ScatterLight, Light01);
		T.Flush = FMath::Lerp(FlushDeep, FlushLight, Light01);
		T.Rim = FMath::Lerp(RimDeep, RimLight, Light01);
		T.Lift = FMath::Lerp(LiftDeep, LiftLight, Light01);
		T.Saturation = FMath::Lerp(SaturationDeep, SaturationLight, Light01);
		return T;
	}

	FEyeRig EyeRig(EKartFace Face)
	{
		// Fitted to each face's eyeWhite sphere (both bodies share the head); AvatarEyeRigTest checks them against the meshes.
		static const FEyeRig Rigs[] = {
			{ FVector(6.18, -9.075, 224.105), 4.37f },   // Kai
			{ FVector(6.40, -8.795, 224.073), 4.385f },  // Leo
			{ FVector(5.99, -9.125, 224.053), 4.33f },   // Mira
			{ FVector(6.69, -8.954, 224.062), 4.274f },  // Nia
			{ FVector(6.50, -8.674, 224.083), 4.39f },   // Omar
			{ FVector(5.90, -8.860, 224.075), 4.405f },  // Rose
		};
		return Rigs[FMath::Min(static_cast<int32>(Face), static_cast<int32>(UE_ARRAY_COUNT(Rigs)) - 1)];
	}

	FLinearColor IrisColour(EKartFace Face)
	{
		static const FColor Irises[] = { FColor(0x6b, 0x44, 0x26), FColor(0x3d, 0x6e, 0x8c), FColor(0x4a, 0x2e, 0x1a),
			FColor(0x5f, 0x7a, 0x3a), FColor(0x2e, 0x1c, 0x12), FColor(0x7a, 0x5a, 0x2e) };
		return FLinearColor::FromSRGBColor(Irises[static_cast<int32>(Face) % UE_ARRAY_COUNT(Irises)]);
	}

	FLinearColor DetailColour(const FLinearColor& Jersey)
	{
		FLinearColor C = Jersey * 0.32f;
		C.A = 1.f;
		return C;
	}

	FQuat Facing(const USkeletalMesh* Mesh, const FVector& Forward, const FVector& Up)
	{
		FVector F, R, U;
		if (!Mesh || !MovecadeKart::DriverRig::ComputeBasis(Mesh->GetRefSkeleton(), F, R, U))
		{
			return FQuat::Identity;
		}
		const FQuat Avatar = FRotationMatrix::MakeFromXZ(F, U).ToQuat();
		const FQuat Target = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
		return Target * Avatar.Inverse();
	}

	EDressResult FDressing::Dress(USkeletalMeshComponent* Driver, USkeletalMeshComponent* Outfit, const FKartLook& Look)
	{
		if (!ensureMsgf(Driver, TEXT("FDressing::Dress needs the driver mesh component")))
		{
			return EDressResult::Missing;
		}
		EDressResult Result = EDressResult::Unchanged;
		const FString Key = FString::Printf(TEXT("%s_%s"), FKartLook::FaceId(Look.Face), FKartLook::BodyId(Look.Body));
		if (BuiltDriver != Key)
		{
			const FString Path = FString::Printf(TEXT("/Game/Avatars/Drivers/%s/SK_Driver_%s.SK_Driver_%s"), *Key, *Key, *Key);
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			if (!Mesh)
			{
				UE_LOG(LogAvatarDressing, Warning, TEXT("No driver %s (run Tools/ue/import_karts.py)."), *Path);
				Driver->SetVisibility(false, true);
				return EDressResult::Missing;
			}
			BuiltDriver = Key;
			bRebuildOutfit = true;
			Driver->EmptyOverrideMaterials();
			Driver->SetSkeletalMeshAsset(Mesh);
			Driver->SetVisibility(true);
			DriverTints.Reset();
			BuiltTone = ESkinTone::Count;
			static const FName Tinted[] = { TEXT("lips"), TEXT("hair_short"), TEXT("hair_bangs"),
				TEXT("hair_puff"), TEXT("hair_tail"), TEXT("hair_crop"), TEXT("hair_curls"), TEXT("hair_bob"), TEXT("hair_bun"),
				TEXT("jersey"), TEXT("pants"), TEXT("waist"), TEXT("shoes"), TEXT("acc_cap"), TEXT("acc_headband"), TEXT("iris"), TEXT("eyeWhite") };
			for (const FName Slot : Tinted)
			{
				const int32 Index = Driver->GetMaterialIndex(Slot);
				if (Index != INDEX_NONE)
				{
					DriverTints.Add(Slot, Driver->CreateDynamicMaterialInstance(Index));
				}
			}
			DressFace(Driver, Look.Face);
			// The face's resting expression (avatar4 appeal pass: a gentle smile and cheek lift).
			Driver->SetMorphTarget(TEXT("mouthSmile"), 0.22f);
			Driver->SetMorphTarget(TEXT("cheekSquint"), 0.12f);
			Result = EDressResult::NewMesh;
		}

		// Outfit overlay: the paid garment replaces the stock clothes and bare arms (clothingAsset.ts).
		if (Outfit && (bRebuildOutfit || Look.Outfit != BuiltOutfit))
		{
			bRebuildOutfit = false;
			BuiltOutfit = Look.Outfit;
			Outfit->EmptyOverrideMaterials();
			USkeletalMesh* Garment = nullptr;
			if (Look.Outfit != EKartOutfit::Classic)
			{
				const FString Id = FString(FKartLook::OutfitId(Look.Outfit)).Replace(TEXT("-"), TEXT("_"));
				const FString Name = FString::Printf(TEXT("%s_%s"), *Id, FKartLook::BodyId(Look.Body));
				const FString Path = FString::Printf(TEXT("/Game/Avatars/Outfits/%s/SK_Outfit_%s.SK_Outfit_%s"), *Name, *Name, *Name);
				Garment = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			}
			Outfit->SetSkeletalMeshAsset(Garment);
			Outfit->SetLeaderPoseComponent(Garment ? Driver : nullptr);
			Outfit->SetVisibility(Garment != nullptr);
			OutfitTints.Reset();
			BuiltTone = ESkinTone::Count; // the garment's bare skin needs its instance too
		}
		DressSkin(Driver, Outfit, Look);

		// Which sections show: one hair style (none under a cap), the accessory, and the stock clothes only
		// without an outfit.
		const bool bGarment = Outfit && Outfit->GetSkeletalMeshAsset() != nullptr;
		for (uint8 Hair = 0; Hair <= static_cast<uint8>(EKartHair::Bun); ++Hair)
		{
			const bool bShow = Hair == static_cast<uint8>(Look.Hair) && !HidesHair(Look.Accessory);
			ShowSlot(Driver, FName(FString(TEXT("hair_")) + FKartLook::HairId(static_cast<EKartHair>(Hair))), bShow);
		}
		ShowSlot(Driver, TEXT("acc_cap"), Look.Accessory == EKartAccessory::Cap);
		ShowSlot(Driver, TEXT("acc_glasses"), Look.Accessory == EKartAccessory::Glasses);
		ShowSlot(Driver, TEXT("acc_headband"), Look.Accessory == EKartAccessory::Headband);
		for (const TCHAR* Slot : { TEXT("skin_body"), TEXT("jersey"), TEXT("pants"), TEXT("waist"), TEXT("shoes"), TEXT("rubber"), TEXT("lace") })
		{
			ShowSlot(Driver, Slot, !bGarment);
		}
		DressProp(Driver, Look);
		return Result;
	}

	UMaterialInstanceDynamic* FDressing::MakeSkin(USkeletalMeshComponent* Mesh, int32 Index, ESkinTone Tone)
	{
		UMaterialInterface* Current = Mesh->GetSkeletalMeshAsset() ? Mesh->GetSkeletalMeshAsset()->GetMaterials()[Index].MaterialInterface.Get() : nullptr;
		UMaterialInterface* ToneParent = LoadObject<UMaterialInterface>(nullptr, SkinToneMaterials[static_cast<int32>(Tone)], nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!ToneParent || !Current)
		{
			// Materials from before the tone instances (import_karts.py avatarfaces not run): the mesh's own skin.
			return Mesh->CreateDynamicMaterialInstance(Index);
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(ToneParent, Mesh);
		for (const TCHAR* Map : SkinMaps)
		{
			UTexture* Texture = nullptr;
			if (Current->GetTextureParameterValue(FHashedMaterialParameterInfo(Map), Texture) && Texture)
			{
				Mid->SetTextureParameterValue(Map, Texture);
			}
		}
		Mesh->SetMaterial(Index, Mid);
		return Mid;
	}

	void FDressing::DressSkin(USkeletalMeshComponent* Driver, USkeletalMeshComponent* Outfit, const FKartLook& Look)
	{
		const ESkinTone Tone = SkinTuning(Look.Skin).Tone;
		if (Tone == BuiltTone)
		{
			return;
		}
		BuiltTone = Tone;
		const FEyeRig Rig = EyeRig(Look.Face);
		auto Make = [Tone, &Rig](USkeletalMeshComponent* Mesh, FName Slot, TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>>& Tints)
		{
			const int32 Index = Mesh && Mesh->GetSkeletalMeshAsset() ? Mesh->GetMaterialIndex(Slot) : INDEX_NONE;
			if (Index == INDEX_NONE)
			{
				return;
			}
			UMaterialInstanceDynamic* Mid = MakeSkin(Mesh, Index, Tone);
			// The face's landmarks for the skin's regions (oily T-zone, matte cheeks, cavities) hang off the eyes.
			Mid->SetVectorParameterValue(TEXT("EyeCentre"), FLinearColor(Rig.Centre));
			Tints.Add(Slot, Mid);
		};
		Make(Driver, TEXT("skin"), DriverTints);
		Make(Driver, TEXT("skin_body"), DriverTints);
		if (Outfit && Outfit->GetSkeletalMeshAsset())
		{
			Make(Outfit, TEXT("skin"), OutfitTints);
		}
	}

	void FDressing::DressFace(USkeletalMeshComponent* Driver, EKartFace Face)
	{
		const FEyeRig Rig = EyeRig(Face);
		// Brows and lashes: their own material (lashes dark whatever the hair), told apart by height over the eyes.
		const int32 BrowIndex = Driver->GetMaterialIndex(TEXT("hair"));
		UMaterialInterface* Brow = BrowIndex != INDEX_NONE ? LoadObject<UMaterialInterface>(nullptr, BrowMaterial, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
		if (Brow)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Brow, Driver);
			Mid->SetVectorParameterValue(TEXT("EyeCentre"), FLinearColor(Rig.Centre));
			Driver->SetMaterial(BrowIndex, Mid);
			DriverTints.Add(TEXT("hair"), Mid);
		}
		else if (BrowIndex != INDEX_NONE)
		{
			DriverTints.Add(TEXT("hair"), Driver->CreateDynamicMaterialInstance(BrowIndex)); // before M_AvatarBrow: the hair's own
		}

		// The lips are skin too: their regions (the mouth corners) hang off this face's eyes.
		if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Lips = DriverTints.Find(TEXT("lips")); Lips && Lips->IsValid())
		{
			(*Lips)->SetVectorParameterValue(TEXT("EyeCentre"), FLinearColor(Rig.Centre));
		}

		const TWeakObjectPtr<UMaterialInstanceDynamic>* Found = DriverTints.Find(TEXT("eyeWhite"));
		UMaterialInstanceDynamic* Mid = Found ? Found->Get() : nullptr;
		FLinearColor Probe;
		Eyes = nullptr;
		AimedEyes = FVector::ZeroVector;
		if (!Mid || !Mid->GetVectorParameterValue(FHashedMaterialParameterInfo(EyeGazeParam()), Probe))
		{
			return; // the old eye material: keep the modelled irises
		}
		// The eye material paints the iris, limbus and pupil itself (so they can turn): the modelled ones go.
		Mid->SetVectorParameterValue(TEXT("EyeCentre"), FLinearColor(Rig.Centre));
		Mid->SetScalarParameterValue(TEXT("EyeRadius"), Rig.Radius);
		ShowSlot(Driver, TEXT("iris"), false);
		ShowSlot(Driver, TEXT("eyeDark"), false);
		Eyes = Mid;
	}

	void FDressing::AimEyes(const FVector& MeshDirection)
	{
		UMaterialInstanceDynamic* Mid = Eyes.Get();
		const FVector Direction = MeshDirection.GetSafeNormal();
		if (!Mid || Direction.IsNearlyZero() || FVector::DotProduct(Direction, AimedEyes) > FMath::Cos(EyeAimEpsilon))
		{
			return;
		}
		AimedEyes = Direction;
		Mid->SetVectorParameterValue(EyeGazeParam(), FLinearColor(Direction));
	}

	void FDressing::DressProp(USkeletalMeshComponent* Driver, const FKartLook& Look)
	{
		UStaticMesh* Mesh = PropMesh(Look.Accessory);
		if (!Mesh)
		{
			if (Prop.IsValid())
			{
				Prop->SetVisibility(false);
			}
			BuiltProp = Look.Accessory;
			return;
		}
		if (!Prop.IsValid())
		{
			UObject* Outer = Driver->GetOwner() ? static_cast<UObject*>(Driver->GetOwner()) : static_cast<UObject*>(Driver);
			UStaticMeshComponent* Made = NewObject<UStaticMeshComponent>(Outer, MakeUniqueObjectName(Outer, UStaticMeshComponent::StaticClass(), TEXT("AvatarProp")));
			Made->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Made->SetGenerateOverlapEvents(false);
			Made->SetMobility(EComponentMobility::Movable);
			Made->SetupAttachment(Driver, TEXT("head"));
			Made->RegisterComponent();
			Prop = Made;
			BuiltProp = EKartAccessory::None;
		}
		UStaticMeshComponent* P = Prop.Get();
		if (P->GetAttachParent() != Driver)
		{
			P->AttachToComponent(Driver, FAttachmentTransformRules::SnapToTargetIncludingScale, TEXT("head"));
		}
		P->SetVisibility(Driver->IsVisible());
		if (BuiltProp != Look.Accessory || P->GetStaticMesh() != Mesh)
		{
			BuiltProp = Look.Accessory;
			P->SetStaticMesh(Mesh);
			PropTints.Reset();
			static const TCHAR* const Parents[2] = { TEXT("/Game/Avatars/Materials/M_AvatarSurface.M_AvatarSurface"), TEXT("/Game/Avatars/Materials/M_AvatarCloth.M_AvatarCloth") };
			for (int32 Slot = 0; Slot < static_cast<int32>(EPropSlot::Count) && Slot < P->GetNumMaterials(); ++Slot)
			{
				const bool bCloth = PropIsCloth(Look.Accessory, static_cast<EPropSlot>(Slot));
				UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, Parents[bCloth ? 1 : 0], nullptr, LOAD_NoWarn | LOAD_Quiet);
				UMaterialInstanceDynamic* Mid = Parent ? UMaterialInstanceDynamic::Create(Parent, P) : nullptr;
				if (Mid)
				{
					const bool bGold = Look.Accessory == EKartAccessory::Crown && Slot != static_cast<int32>(EPropSlot::Accent);
					const bool bLight = Look.Accessory == EKartAccessory::Headphones && Slot == static_cast<int32>(EPropSlot::Accent);
					Mid->SetScalarParameterValue(TEXT("Metallic"), bGold ? 1.f : 0.f);
					Mid->SetScalarParameterValue(TEXT("Roughness"), bGold ? 0.22f : (bCloth ? 0.85f : 0.35f));
					Mid->SetScalarParameterValue(TEXT("Specular"), bGold ? 0.5f : 0.45f);
					Mid->SetScalarParameterValue(TEXT("Glow"), bLight ? 2.5f : (Look.Accessory == EKartAccessory::Crown && !bGold ? 0.35f : 0.f));
					Mid->SetScalarParameterValue(TEXT("Catchlight"), Look.Accessory == EKartAccessory::Crown ? 2.f : 0.f);
					P->SetMaterial(Slot, Mid);
				}
				PropTints.Add(Mid);
			}
		}
	}

	FLinearColor FDressing::RimColour(const FKartLook& Look)
	{
		const FLinearColor Jersey = Look.JerseyColor();
		return FLinearColor(FMath::Lerp(Jersey.R, 1.f, 0.35f), FMath::Lerp(Jersey.G, 1.f, 0.35f), FMath::Lerp(Jersey.B, 1.f, 0.35f), 1.f);
	}

	void FDressing::Colour(const FKartLook& Look) const
	{
		const FLinearColor Jersey = Look.JerseyColor();
		auto Tint = [](const TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>>& Map, const TCHAR* Slot, const FLinearColor& Colour)
		{
			if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Found = Map.Find(Slot); Found && Found->IsValid())
			{
				(*Found)->SetVectorParameterValue(TEXT("Tint"), Colour);
			}
		};
		Tint(DriverTints, TEXT("skin"), Look.Skin);
		Tint(DriverTints, TEXT("skin_body"), Look.Skin);
		Tint(DriverTints, TEXT("lips"), LipColour(Look.Skin));
		Tint(DriverTints, TEXT("hair"), BrowColour(Look.HairColor));
		for (uint8 Hair = 0; Hair <= static_cast<uint8>(EKartHair::Bun); ++Hair)
		{
			Tint(DriverTints, *(FString(TEXT("hair_")) + FKartLook::HairId(static_cast<EKartHair>(Hair))), Look.HairColor);
		}
		Tint(DriverTints, TEXT("jersey"), Jersey);
		Tint(DriverTints, TEXT("pants"), Look.Pants);
		Tint(DriverTints, TEXT("waist"), DetailColour(Jersey));
		Tint(DriverTints, TEXT("shoes"), Look.Shoes);
		Tint(DriverTints, TEXT("acc_cap"), Jersey);
		Tint(DriverTints, TEXT("acc_headband"), DetailColour(Jersey));
		Tint(DriverTints, TEXT("iris"), IrisColour(Look.Face));
		Tint(OutfitTints, TEXT("skin"), Look.Skin);
		if (UMaterialInstanceDynamic* Mid = Eyes.Get())
		{
			Mid->SetVectorParameterValue(TEXT("IrisColor"), IrisColour(Look.Face));
		}

		// The skin's shading for its tone, the lips' satin, and the skin the brows are softened toward.
		const FSkinTuning Skin = SkinTuning(Look.Skin);
		auto Scalars = [](const TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>>& Map, const TCHAR* Slot, std::initializer_list<TPair<const TCHAR*, float>> Values)
		{
			if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Found = Map.Find(Slot); Found && Found->IsValid())
			{
				for (const TPair<const TCHAR*, float>& Value : Values)
				{
					(*Found)->SetScalarParameterValue(Value.Key, Value.Value);
				}
			}
		};
		const std::initializer_list<TPair<const TCHAR*, float>> SkinValues = { { TEXT("Scatter"), Skin.Scatter }, { TEXT("Flush"), Skin.Flush },
			{ TEXT("Rim"), Skin.Rim }, { TEXT("Lift"), Skin.Lift }, { TEXT("Saturation"), Skin.Saturation } };
		for (const TCHAR* Slot : { TEXT("skin"), TEXT("skin_body"), TEXT("lips") })
		{
			Scalars(DriverTints, Slot, SkinValues);
		}
		Scalars(OutfitTints, TEXT("skin"), SkinValues);
		Scalars(DriverTints, TEXT("lips"), { { TEXT("Roughness"), LipRoughness }, { TEXT("Specular"), LipSpecular } });
		if (const TWeakObjectPtr<UMaterialInstanceDynamic>* Brows = DriverTints.Find(TEXT("hair")); Brows && Brows->IsValid())
		{
			(*Brows)->SetVectorParameterValue(TEXT("SkinTint"), Look.Skin);
		}

		// The player's rim colour on the clothes and hair (a faint edge on the skin), so the avatar reads at a glance.
		const FLinearColor Rim = RimColour(Look);
		static const FName SkinSlots[] = { TEXT("skin"), TEXT("skin_body"), TEXT("lips") };
		static const FName EyeSlots[] = { TEXT("iris"), TEXT("eyeWhite") };
		for (const TPair<FName, TWeakObjectPtr<UMaterialInstanceDynamic>>& Pair : DriverTints)
		{
			if (UMaterialInstanceDynamic* Mid = Pair.Value.Get())
			{
				const bool bSkin = Algo::Find(SkinSlots, Pair.Key) != nullptr;
				const bool bEye = Algo::Find(EyeSlots, Pair.Key) != nullptr;
				Mid->SetVectorParameterValue(TEXT("RimColor"), Rim);
				Mid->SetScalarParameterValue(TEXT("RimBoost"), bEye ? 0.f : (bSkin ? 0.05f : 0.22f));
			}
		}
		for (int32 Slot = 0; Slot < PropTints.Num(); ++Slot)
		{
			if (UMaterialInstanceDynamic* Mid = PropTints[Slot].Get())
			{
				Mid->SetVectorParameterValue(TEXT("Tint"), PropColour(BuiltProp, static_cast<EPropSlot>(Slot), Look));
			}
		}
	}
}
