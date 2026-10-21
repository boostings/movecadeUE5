#include "Avatar/AvatarDressing.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "KartDriverAnimInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

DEFINE_LOG_CATEGORY_STATIC(LogAvatarDressing, Log, All);

namespace
{
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
		const FLinearColor Rose = FLinearColor::FromSRGBColor(FColor(0xc8, 0x50, 0x6a));
		return Lerp3(Skin, Rose, 0.42f) * 0.9f;
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
			static const FName Tinted[] = { TEXT("skin"), TEXT("skin_body"), TEXT("lips"), TEXT("hair"), TEXT("hair_short"), TEXT("hair_bangs"),
				TEXT("hair_puff"), TEXT("hair_tail"), TEXT("hair_crop"), TEXT("hair_curls"), TEXT("hair_bob"), TEXT("hair_bun"),
				TEXT("jersey"), TEXT("pants"), TEXT("waist"), TEXT("shoes"), TEXT("acc_cap"), TEXT("acc_headband"), TEXT("iris") };
			for (const FName Slot : Tinted)
			{
				const int32 Index = Driver->GetMaterialIndex(Slot);
				if (Index != INDEX_NONE)
				{
					DriverTints.Add(Slot, Driver->CreateDynamicMaterialInstance(Index));
				}
			}
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
			if (Garment)
			{
				const int32 Index = Outfit->GetMaterialIndex(TEXT("skin"));
				if (Index != INDEX_NONE)
				{
					OutfitTints.Add(TEXT("skin"), Outfit->CreateDynamicMaterialInstance(Index));
				}
			}
		}

		// Which sections show: one hair style (none under a cap), the accessory, and the stock clothes only
		// without an outfit.
		const bool bGarment = Outfit && Outfit->GetSkeletalMeshAsset() != nullptr;
		for (uint8 Hair = 0; Hair <= static_cast<uint8>(EKartHair::Bun); ++Hair)
		{
			const bool bShow = Hair == static_cast<uint8>(Look.Hair) && Look.Accessory != EKartAccessory::Cap;
			ShowSlot(Driver, FName(FString(TEXT("hair_")) + FKartLook::HairId(static_cast<EKartHair>(Hair))), bShow);
		}
		ShowSlot(Driver, TEXT("acc_cap"), Look.Accessory == EKartAccessory::Cap);
		ShowSlot(Driver, TEXT("acc_glasses"), Look.Accessory == EKartAccessory::Glasses);
		ShowSlot(Driver, TEXT("acc_headband"), Look.Accessory == EKartAccessory::Headband);
		for (const TCHAR* Slot : { TEXT("skin_body"), TEXT("jersey"), TEXT("pants"), TEXT("waist"), TEXT("shoes"), TEXT("rubber"), TEXT("lace") })
		{
			ShowSlot(Driver, Slot, !bGarment);
		}
		return Result;
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
		Tint(DriverTints, TEXT("hair"), Look.HairColor);
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
	}
}
