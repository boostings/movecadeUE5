// ATrackBuilder's imported scenery: meshes[], scenery.instances / signs / animated (Docs/Port/03 §5, §8).
// Assets come from Tools/ue/import_track_assets.py; when they are missing the builder keeps its graybox.
#include "TrackBuilder.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "FX/KartFx.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackScenery, Log, All);

namespace
{
	const TCHAR* const SceneryTag = TEXT("TrackScenery");
	const TCHAR* const SignPlaneAsset = TEXT("/Game/Tracks/Shared/violet_prefabs/StaticMeshes/sign_plane");

	FString MaterialPath(const FString& Shader)
	{
		static const TMap<FString, FString> Paths = {
			{ TEXT("vertexColor"), TEXT("M_TrackVertexColor") },
			{ TEXT("textured"), TEXT("M_TrackTextured") },
			{ TEXT("unlitTexture"), TEXT("M_TrackUnlitTexture") },
			{ TEXT("translucent"), TEXT("M_TrackTranslucent") },
			{ TEXT("additive"), TEXT("M_TrackAdditive") },
			{ TEXT("surface"), TEXT("M_TrackSurface") },
			{ TEXT("surfaceWall"), TEXT("M_TrackSurfaceWall") },
			{ TEXT("boostPad"), TEXT("M_TrackBoostPad") },
			{ TEXT("foliage"), TEXT("M_TrackFoliage") },
			{ TEXT("crystal"), TEXT("M_TrackCrystal") },
			{ TEXT("glass"), TEXT("M_TrackGlass") },
			{ TEXT("water"), TEXT("M_TrackWater") },
		};
		const FString* Name = Paths.Find(Shader);
		const FString Asset = Name ? *Name : TEXT("M_TrackVertexColor");
		return FString::Printf(TEXT("/Game/Tracks/Shared/Materials/%s.%s"), *Asset, *Asset);
	}

	/** Groups prefab instances exactly as the old "<prefab>_<tint without '#'>_<variant>" FString key did (FName
	 *  compares case-insensitively, like FString), without formatting a string per instance. */
	struct FPrefabGroupKey
	{
		FName Prefab;
		FName Tint;
		int32 Variant = 0;

		static FPrefabGroupKey Make(const FString& Prefab, const FString& Tint, int32 Variant)
		{
			TStringBuilder<16> Bare;
			for (const TCHAR Char : Tint)
			{
				if (Char != TEXT('#'))
				{
					Bare.AppendChar(Char);
				}
			}
			return { FName(*Prefab), FName(Bare.ToView()), Variant };
		}

		bool operator==(const FPrefabGroupKey& Other) const = default;

		friend uint32 GetTypeHash(const FPrefabGroupKey& Key)
		{
			return HashCombineFast(HashCombineFast(GetTypeHash(Key.Prefab), GetTypeHash(Key.Tint)), ::GetTypeHash(Key.Variant));
		}
	};

	struct FPrefabGroup
	{
		UInstancedStaticMeshComponent* Component = nullptr;
		TArray<FTransform> Transforms;
	};

	template <typename T>
	T* LoadAsset(const FString& PackagePath)
	{
		// "/Game/A/B" -> "/Game/A/B.B"
		FString Path = PackagePath;
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPaths::GetBaseFilename(PackagePath);
		}
		return LoadObject<T>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	FLinearColor HexColor(const FString& Hex, const FLinearColor& Default = FLinearColor::White)
	{
		if (Hex.IsEmpty())
		{
			return Default;
		}
		// Every '#' dropped, as the content tools write them.
		TStringBuilder<16> Digits;
		for (const TCHAR C : Hex)
		{
			if (C != TEXT('#'))
			{
				Digits.AppendChar(C);
			}
		}
		return MovecadeKart::Fx::Hex(Digits.ToView());
	}

	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default = 0.0)
	{
		double V = Default;
		return O.IsValid() && O->TryGetNumberField(Key, V) ? V : Default;
	}

	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		FString V;
		if (O.IsValid())
		{
			O->TryGetStringField(Key, V);
		}
		return V;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return O.IsValid() && O->TryGetObjectField(Key, Out) ? *Out : nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>& Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		static const TArray<TSharedPtr<FJsonValue>> Empty;
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return O.IsValid() && O->TryGetArrayField(Key, Out) ? *Out : Empty;
	}

	FVector Vec3(const TArray<TSharedPtr<FJsonValue>>& A)
	{
		return A.Num() >= 3 ? FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()) : FVector::ZeroVector;
	}

	// ---- three.js space -> UE ----------------------------------------------------------------
	// UE's glTF import maps glTF (x, y, z) to UE (x, z, y); the port's convention is UE (-z, x, y)
	// (KartCoords.h), so every imported mesh sits under a +90 degree yaw.

	const FQuat& ImportFix()
	{
		static const FQuat Fix = FRotator(0.0, 90.0, 0.0).Quaternion();
		return Fix;
	}

	/** A rotation built in three.js space (right-handed, Y up), as a UE rotation. */
	FQuat QuatToUE(const FQuat& Q)
	{
		return FQuat(Q.Z, -Q.X, -Q.Y, Q.W);
	}

	/** three.js Euler 'XYZ' (Object3D.rotation) as a quaternion in three.js space. */
	FQuat Euler(double X, double Y, double Z)
	{
		return FQuat(FVector::XAxisVector, X) * FQuat(FVector::YAxisVector, Y) * FQuat(FVector::ZAxisVector, Z);
	}

	/** A three.js-space transform (position in metres) as a component transform for an imported mesh. */
	FTransform ToUETransform(const FTransform& Three, double RebaseY)
	{
		const FVector P = Three.GetTranslation();
		const FVector S = Three.GetScale3D();
		return FTransform(QuatToUE(Three.GetRotation()) * ImportFix(),
			MovecadeKart::Coords::ToUE(FVector(P.X, P.Y + RebaseY, P.Z)),
			FVector(S.X, S.Z, S.Y)); // imported mesh axes are (x, z, y)
	}

	FTransform Three(const FVector& Position, const FQuat& Rotation = FQuat::Identity, const FVector& Scale = FVector::OneVector)
	{
		return FTransform(Rotation, Position, Scale);
	}

	/** Nanite draws opaque and masked materials only: a translucent part (glass, spray, beams) renders
	 *  through its fallback mesh instead. */
	/** How each animated part moves. GatherMotion strips colour-variant suffixes ("ferris_gondola_3" -> "ferris_gondola"),
	 *  so a part is one of the names spawned for the animated scenery; any other part stays where it was placed. */
	enum class EMotion : uint8
	{
		Fixed,
		LighthouseBeams,
		FerrisFrame,
		FerrisWheel,
		FerrisGondola,
		Ferry,
		CargoShip,
		JetSki,
		JetWake,
		GullBody,
		GullWingL,
		GullWingR,
		FountainJet,
		FountainSpray,
		Buoy,
		BuoyLamp,
	};

	/** An FName compare is an integer compare, so this costs no string work per part per frame. */
	EMotion MotionOf(FName Part)
	{
		static const TPair<FName, EMotion> Parts[] = {
			{ TEXT("lighthouse_beams"), EMotion::LighthouseBeams },
			{ TEXT("ferris_frame"), EMotion::FerrisFrame },
			{ TEXT("ferris_wheel"), EMotion::FerrisWheel },
			{ TEXT("ferris_gondola"), EMotion::FerrisGondola },
			{ TEXT("ferry"), EMotion::Ferry },
			{ TEXT("ferry_smoke"), EMotion::Ferry },
			{ TEXT("cargo_ship"), EMotion::CargoShip },
			{ TEXT("jet_ski"), EMotion::JetSki },
			{ TEXT("jet_wake"), EMotion::JetWake },
			{ TEXT("gull_body"), EMotion::GullBody },
			{ TEXT("gull_wing_l"), EMotion::GullWingL },
			{ TEXT("gull_wing_r"), EMotion::GullWingR },
			{ TEXT("fountain_jet"), EMotion::FountainJet },
			{ TEXT("fountain_spray"), EMotion::FountainSpray },
			{ TEXT("buoy"), EMotion::Buoy },
			{ TEXT("buoy_lamp"), EMotion::BuoyLamp },
		};
		for (const TPair<FName, EMotion>& Entry : Parts)
		{
			if (Entry.Key == Part)
			{
				return Entry.Value;
			}
		}
		return EMotion::Fixed;
	}

	void MatchNaniteToMaterial(UStaticMeshComponent* Component, const UMaterialInterface* Material)
	{
		if (Material && IsTranslucentBlendMode(Material->GetBlendMode()))
		{
			Component->SetForceDisableNanite(true);
		}
	}
}

void ATrackBuilder::ClearScenery()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), SceneryTag);
	for (UActorComponent* Component : Old)
	{
		RemoveInstanceComponent(Component);
		Component->DestroyComponent();
	}
	Motion.Reset();
	SceneryCount = 0;
}

UStaticMeshComponent* ATrackBuilder::AddMeshComponent(const FString& Name, UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, const FName Tag)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), *Name));
	Component->SetStaticMesh(Mesh);
	Component->SetupAttachment(RootComponent);
	Component->SetRelativeTransform(Transform);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->ComponentTags.Add(SceneryTag);
	if (Tag != NAME_None)
	{
		Component->ComponentTags.Add(Tag);
		Component->SetMobility(EComponentMobility::Movable);
	}
	if (Material)
	{
		for (int32 i = 0; i < FMath::Max(1, Mesh->GetStaticMaterials().Num()); ++i)
		{
			Component->SetMaterial(i, Material);
		}
		MatchNaniteToMaterial(Component, Material);
	}
	AddInstanceComponent(Component);
	Component->RegisterComponent();
	++SceneryCount;
	return Component;
}

UTexture* ATrackBuilder::TextureNamed(const FString& Name)
{
	for (const TSharedPtr<FJsonValue>& V : Arr(Json, TEXT("textures")))
	{
		const TSharedPtr<FJsonObject> T = V->AsObject();
		if (Str(T, TEXT("name")) == Name)
		{
			return LoadAsset<UTexture>(Str(T, TEXT("ueAsset")));
		}
	}
	return nullptr;
}

UMaterialInterface* ATrackBuilder::MaterialFor(const TSharedPtr<FJsonObject>& Hint, const FString& TintOverride, int32 Variant)
{
	FString Shader = Str(Hint, TEXT("shader"));
	bool bTriplanar = false;
	if (Hint.IsValid() && Hint->TryGetBoolField(TEXT("triplanar"), bTriplanar) && bTriplanar && Shader == TEXT("surface"))
	{
		Shader = TEXT("surfaceWall");
	}
	const FString Color = TintOverride.IsEmpty() ? Str(Hint, TEXT("color")) : TintOverride;
	FString Texture = Str(Hint, TEXT("texture")).Replace(TEXT("{variant}"), *FString::FromInt(Variant));
	const FString TextureAsset = Str(Hint, TEXT("textureAsset"));
	const FString Detail = Str(Hint, TEXT("detail"));
	const double Roughness = Num(Hint, TEXT("roughness"), 0.8);
	const double Metallic = Num(Hint, TEXT("metalness"), 0.0);
	const double Emissive = Num(Hint, TEXT("emissive"), 0.0);
	const double Opacity = Num(Hint, TEXT("opacity"), 1.0);
	// Free-form master parameters (hint.params): numbers, "#rrggbb" colours, [r, g, b] vectors, texture names.
	const TSharedPtr<FJsonObject> Params = Obj(Hint, TEXT("params"));
	FString ParamsKey;
	if (Params.IsValid())
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ParamsKey);
		FJsonSerializer::Serialize(Params.ToSharedRef(), Writer);
	}
	const FString Key = FString::Printf(TEXT("%s|%s|%s|%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f"), *Shader, *Color, *Texture, *TextureAsset, *Detail,
		Roughness, Metallic, Emissive, Opacity, Num(Hint, TEXT("tileM")), Num(Hint, TEXT("albedo")), Num(Hint, TEXT("normal")), Num(Hint, TEXT("rough")),
		Num(Hint, TEXT("desaturate")), Num(Hint, TEXT("pulse"))) + FString::Printf(TEXT("|%.3f|"), Num(Hint, TEXT("baseDesaturate"))) + ParamsKey;
	if (TObjectPtr<UMaterialInterface>* Found = MaterialCache.Find(Key))
	{
		return *Found;
	}
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *MaterialPath(Shader), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetVectorParameterValue(TEXT("Color"), HexColor(Color));
	MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	MID->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	MID->SetScalarParameterValue(TEXT("Emissive"), Emissive);
	MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	UTexture* Map = !TextureAsset.IsEmpty() ? LoadAsset<UTexture>(TextureAsset) : (!Texture.IsEmpty() ? TextureNamed(Texture) : nullptr);
	if (Map)
	{
		MID->SetTextureParameterValue(TEXT("Map"), Map);
	}
	// World-space CC0 detail (M_TrackSurface*): the set's maps and linear mean from detailSurfaces[].
	const TSharedPtr<FJsonObject> Set = Obj(Obj(Json, TEXT("detailSurfaces")), *Detail);
	if (Set.IsValid())
	{
		for (const TCHAR* const Kind : { TEXT("color"), TEXT("normal"), TEXT("rough") })
		{
			if (UTexture* T = LoadAsset<UTexture>(Str(Set, Kind)))
			{
				MID->SetTextureParameterValue(*(FString(TEXT("Detail")) + (FString(Kind) == TEXT("color") ? TEXT("Color") : FString(Kind) == TEXT("normal") ? TEXT("Normal") : TEXT("Rough"))), T);
			}
		}
		const FVector Mean = Vec3(Arr(Set, TEXT("mean")));
		MID->SetVectorParameterValue(TEXT("DetailMean"), FLinearColor(Mean.X, Mean.Y, Mean.Z, 1.0));
		MID->SetScalarParameterValue(TEXT("DetailTileM"), Num(Hint, TEXT("tileM"), 4.0));
		MID->SetScalarParameterValue(TEXT("DetailAlbedo"), Num(Hint, TEXT("albedo"), 0.5));
		MID->SetScalarParameterValue(TEXT("DetailNormal"), Num(Hint, TEXT("normal"), 0.5));
		MID->SetScalarParameterValue(TEXT("DetailRough"), Num(Hint, TEXT("rough"), 0.5));
		MID->SetScalarParameterValue(TEXT("Desaturate"), Num(Hint, TEXT("desaturate"), 0.0));
		MID->SetScalarParameterValue(TEXT("BaseDesaturate"), Num(Hint, TEXT("baseDesaturate"), 0.0));
	}
	if (Params.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Params->Values)
		{
			const FName Name(*P.Key);
			FString Text;
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (P.Value->Type == EJson::Number)
			{
				MID->SetScalarParameterValue(Name, P.Value->AsNumber());
			}
			else if (P.Value->TryGetArray(Values) && Values->Num() >= 3)
			{
				MID->SetVectorParameterValue(Name, FLinearColor((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber(), Values->Num() > 3 ? (*Values)[3]->AsNumber() : 1.0));
			}
			else if (P.Value->TryGetString(Text))
			{
				if (Text.StartsWith(TEXT("#")))
				{
					MID->SetVectorParameterValue(Name, HexColor(Text));
				}
				else if (UTexture* T = TextureNamed(Text))
				{
					MID->SetTextureParameterValue(Name, T);
				}
			}
		}
	}
	if (Shader == TEXT("boostPad"))
	{
		// boostPadLook: chevrons scroll forward (texture up) and pulse.
		MID->SetScalarParameterValue(TEXT("PanV"), Num(Hint, TEXT("scrollPerS"), 1.6)); // + moves the art toward v = 0, the image top = forward
		MID->SetScalarParameterValue(TEXT("Pulse"), Num(Hint, TEXT("pulse"), 0.5));
		MID->SetScalarParameterValue(TEXT("PulseRate"), Num(Hint, TEXT("pulseRadPerS"), 6.0));
	}
	MaterialCache.Add(Key, MID);
	return MID;
}

void ATrackBuilder::BuildBoostPads()
{
	using namespace MovecadeKart;
	const FTrack& T = *Track;
	BoostPads->ClearInstances();
	BoostPads->SetCastShadow(false);
	// The props prefab: a 1 m plane on the ground whose texture's up is the pad's forward (local -Z).
	const TSharedPtr<FJsonObject> Def = Obj(Obj(Obj(Json, TEXT("scenery")), TEXT("prefabs")), TEXT("boost_pad"));
	UStaticMesh* Mesh = Def.IsValid() ? LoadAsset<UStaticMesh>(Str(Def, TEXT("ueAsset"))) : nullptr;
	if (Mesh)
	{
		BoostPads->SetStaticMesh(Mesh);
		TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>(*Obj(Def, TEXT("material")));
		if (const TSharedPtr<FJsonObject> Look = Obj(Json, TEXT("boostPadLook")))
		{
			for (const auto& Field : Look->Values)
			{
				Hint->SetField(Field.Key, Field.Value);
			}
		}
		BoostPads->SetMaterial(0, MaterialFor(Hint));
		for (const FBoostPad& Pad : T.BoostPads)
		{
			const FTransform Local = Three(FVector(Pad.Position.X, T.GroundY + 0.045, Pad.Position.Y), Euler(0.0, Pad.Yaw, 0.0), FVector(Pad.Width, 1.0, Pad.Length));
			BoostPads->AddInstance(ToUETransform(Local, T.SuggestedRebaseY), true);
		}
		return;
	}
	// Graybox: the engine Plane (1 m square in UE X/Y); X runs along the pad (length), Y across (width).
	for (const FBoostPad& Pad : T.BoostPads)
	{
		BoostPads->AddInstance(FTransform(FRotator(0.0, Coords::YawToUE(Pad.Yaw), 0.0),
			Coords::ToUE(Pad.Position, 0.04 + T.SuggestedRebaseY), FVector(Pad.Length, Pad.Width, 1.0)), true);
	}
}

bool ATrackBuilder::BuildImportedMeshes()
{
	MaterialCache.Reset();
	bool bReplacesRibbons = false;
	const double Rebase = Track->SuggestedRebaseY;
	for (const TSharedPtr<FJsonValue>& V : Arr(Json, TEXT("meshes")))
	{
		const TSharedPtr<FJsonObject> Entry = V->AsObject();
		bool bBaked = false, bReplaces = false;
		Entry->TryGetBoolField(TEXT("transformBaked"), bBaked);
		Entry->TryGetBoolField(TEXT("replacesRibbons"), bReplaces);
		// Parts carry a material hint; without one the mesh keeps its imported (GLB) materials.
		TMap<FString, TSharedPtr<FJsonObject>> Hints;
		for (const TSharedPtr<FJsonValue>& P : Arr(Entry, TEXT("parts")))
		{
			Hints.Add(Str(P->AsObject(), TEXT("ueAsset")), Obj(P->AsObject(), TEXT("material")));
		}
		// World-space meshes: the GLB is in source coordinates (Violet's is already rebased).
		const FTransform Placement = ToUETransform(FTransform::Identity, bBaked ? 0.0 : Rebase);
		int32 Loaded = 0;
		for (const TSharedPtr<FJsonValue>& A : Arr(Entry, TEXT("ueAssets")))
		{
			const FString Asset = A->AsString();
			UStaticMesh* Mesh = LoadAsset<UStaticMesh>(Asset);
			if (!Mesh)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Hint = Hints.Find(Asset);
			AddMeshComponent(FPaths::GetBaseFilename(Asset), Mesh, Placement, Hint ? MaterialFor(*Hint) : nullptr);
			++Loaded;
		}
		if (Loaded == 0 && Arr(Entry, TEXT("ueAssets")).Num() > 0)
		{
			UE_LOG(LogTrackScenery, Warning, TEXT("%s: %s is not imported; run Tools/ue/import_track_assets.py"), *TrackId, *Str(Entry, TEXT("file")));
		}
		bReplacesRibbons |= bReplaces && Loaded > 0;
	}
	return bReplacesRibbons;
}

void ATrackBuilder::BuildPrefabInstances()
{
	const TSharedPtr<FJsonObject> Scenery = Obj(Json, TEXT("scenery"));
	const TSharedPtr<FJsonObject> Prefabs = Obj(Scenery, TEXT("prefabs"));
	if (!Prefabs.IsValid())
	{
		return;
	}
	const double Rebase = Track->SuggestedRebaseY;
	// One instanced component per prefab, tint and variant (trees by foliage swatch, banners by face). Thousands of
	// instances fall into a few dozen groups, so each group's transforms are gathered and added in one call.
	TMap<FPrefabGroupKey, FPrefabGroup> Groups;
	for (const TSharedPtr<FJsonValue>& V : Arr(Scenery, TEXT("instances")))
	{
		const TSharedPtr<FJsonObject> I = V->AsObject();
		const FString Prefab = Str(I, TEXT("prefab"));
		const TSharedPtr<FJsonObject> Def = Obj(Prefabs, *Prefab);
		if (!Def.IsValid())
		{
			continue;
		}
		const FString Tint = Str(I, TEXT("tint"));
		const int32 Variant = static_cast<int32>(Num(I, TEXT("variant")));
		FPrefabGroup& Group = Groups.FindOrAdd(FPrefabGroupKey::Make(Prefab, Tint, Variant));
		if (!Group.Component)
		{
			UStaticMesh* Mesh = LoadAsset<UStaticMesh>(Str(Def, TEXT("ueAsset")));
			if (!Mesh)
			{
				continue;
			}
			const FString Name = FString::Printf(TEXT("%s_%s_%d"), *Prefab, *Tint.Replace(TEXT("#"), TEXT("")), Variant);
			UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), *Name));
			Group.Component = Component;
			Component->SetStaticMesh(Mesh);
			Component->SetupAttachment(RootComponent);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->ComponentTags.Add(SceneryTag);
			// Sky dressing (balloons, pennants high on masts) would throw detached shadows far across the road
			// under a low sun; such prefabs opt out with "castShadow": false.
			bool bCastShadow = true;
			if (Def->TryGetBoolField(TEXT("castShadow"), bCastShadow) && !bCastShadow)
			{
				Component->SetCastShadow(false);
			}
			if (UMaterialInterface* Material = MaterialFor(Obj(Def, TEXT("material")), Tint, Variant))
			{
				Component->SetMaterial(0, Material);
				MatchNaniteToMaterial(Component, Material);
			}
			AddInstanceComponent(Component);
			Component->RegisterComponent();
			++SceneryCount;
		}
		FVector Scale = FVector::OneVector;
		const TArray<TSharedPtr<FJsonValue>>& S = Arr(I, TEXT("scale"));
		if (S.Num() == 3)
		{
			Scale = Vec3(S);
		}
		// Placement yaw turns the prefab's local +Z toward the road (trackside.ts:145-148).
		const FTransform Local = Three(FVector(Num(I, TEXT("x")), Num(I, TEXT("y")), Num(I, TEXT("z"))), Euler(0.0, Num(I, TEXT("yaw")), 0.0), Scale);
		Group.Transforms.Add(ToUETransform(Local, Rebase));
	}
	int32 GroupCount = 0, InstanceCount = 0;
	for (const TPair<FPrefabGroupKey, FPrefabGroup>& Pair : Groups)
	{
		if (Pair.Value.Component)
		{
			Pair.Value.Component->AddInstances(Pair.Value.Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
			++GroupCount;
			InstanceCount += Pair.Value.Transforms.Num();
		}
	}
	UE_LOG(LogTrackScenery, Log, TEXT("%s: %d prefab instances in %d groups"), *TrackId, InstanceCount, GroupCount);
}

void ATrackBuilder::BuildSigns()
{
	const TSharedPtr<FJsonObject> Scenery = Obj(Json, TEXT("scenery"));
	UStaticMesh* Plane = LoadAsset<UStaticMesh>(SignPlaneAsset);
	if (!Plane)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : Arr(Scenery, TEXT("signs")))
	{
		const TSharedPtr<FJsonObject> Sign = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& Size = Arr(Sign, TEXT("size"));
		if (Size.Num() < 2)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>();
		Hint->SetStringField(TEXT("shader"), TEXT("unlitTexture"));
		Hint->SetStringField(TEXT("texture"), Str(Sign, TEXT("texture")));
		// The plane faces +Z, like the web's planeGeometry; size is width x height in metres.
		const FTransform Local = Three(Vec3(Arr(Sign, TEXT("position"))), Euler(0.0, Num(Sign, TEXT("yaw")), 0.0), FVector(Size[0]->AsNumber(), Size[1]->AsNumber(), 1.0));
		AddMeshComponent(Str(Sign, TEXT("name")), Plane, ToUETransform(Local, Track->SuggestedRebaseY), MaterialFor(Hint));
	}
}

void ATrackBuilder::BuildLights()
{
	const double Rebase = Track->SuggestedRebaseY;
	for (const TSharedPtr<FJsonValue>& V : Arr(Obj(Json, TEXT("scenery")), TEXT("lights")))
	{
		const TSharedPtr<FJsonObject> L = V->AsObject();
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("TrackLight")));
		Light->SetupAttachment(RootComponent);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetRelativeLocation(MovecadeKart::Coords::ToUE(FVector(Num(L, TEXT("x")), Num(L, TEXT("y")) + Rebase, Num(L, TEXT("z")))));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(static_cast<float>(Num(L, TEXT("candela"), 500.0)));
		Light->SetLightColor(HexColor(Str(L, TEXT("color"))));
		Light->SetAttenuationRadius(static_cast<float>(Num(L, TEXT("radius"), 25.0) * 100.0));
		Light->SetSourceRadius(20.f);
		// Many small lights across four split-screen views: no shadows, no volumetric scattering.
		Light->SetCastShadows(false);
		Light->SetVolumetricScatteringIntensity(0.f);
		Light->ComponentTags.Add(SceneryTag);
		AddInstanceComponent(Light);
		Light->RegisterComponent();
		++SceneryCount;
	}
}

void ATrackBuilder::BuildSea()
{
	// HarborTrack.tsx:957-960: a 5000 m plane at (100, -7, -40), #5fb3b3, water texture x160, scrolling.
	const TSharedPtr<FJsonObject> Scenery = Obj(Json, TEXT("scenery"));
	const TSharedPtr<FJsonObject> Sea = Obj(Scenery, TEXT("sea"));
	UStaticMesh* Plane = LoadAsset<UStaticMesh>(SignPlaneAsset);
	if (!Sea.IsValid() || !Plane)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : Arr(Scenery, TEXT("instances")))
	{
		const TSharedPtr<FJsonObject> I = V->AsObject();
		if (Str(I, TEXT("prefab")) != TEXT("sea_plane"))
		{
			continue;
		}
		// sea.material (single layer water) when the export gives one; else the web's scrolling texture.
		if (const TSharedPtr<FJsonObject> Water = Obj(Sea, TEXT("material")))
		{
			const FVector Size = Vec3(Arr(I, TEXT("scale")));
			const FTransform Local = Three(FVector(Num(I, TEXT("x")), Num(I, TEXT("y")), Num(I, TEXT("z"))), Euler(-UE_DOUBLE_HALF_PI, 0.0, 0.0), FVector(Size.X, Size.Z, 1.0));
			UStaticMeshComponent* Surface = AddMeshComponent(TEXT("Sea"), Plane, ToUETransform(Local, Track->SuggestedRebaseY), MaterialFor(Water), TEXT("TrackSea"));
			Surface->SetCastShadow(false);
			continue;
		}
		TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>();
		Hint->SetStringField(TEXT("shader"), TEXT("textured"));
		Hint->SetStringField(TEXT("color"), Str(I, TEXT("tint")));
		Hint->SetStringField(TEXT("texture"), Str(Sea, TEXT("texture")));
		Hint->SetNumberField(TEXT("roughness"), Num(Sea, TEXT("roughness"), 0.34));
		Hint->SetNumberField(TEXT("metalness"), Num(Sea, TEXT("metalness"), 0.15));
		UMaterialInterface* Material = MaterialFor(Hint);
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material))
		{
			const TArray<TSharedPtr<FJsonValue>>& Scroll = Arr(Sea, TEXT("scroll"));
			MID->SetScalarParameterValue(TEXT("Tiling"), Num(Sea, TEXT("repeat"), 160));
			if (Scroll.Num() == 2)
			{
				MID->SetScalarParameterValue(TEXT("PanU"), Scroll[0]->AsNumber());
				MID->SetScalarParameterValue(TEXT("PanV"), Scroll[1]->AsNumber());
			}
		}
		const FVector Size = Vec3(Arr(I, TEXT("scale")));
		const FTransform Local = Three(FVector(Num(I, TEXT("x")), Num(I, TEXT("y")), Num(I, TEXT("z"))), Euler(-UE_DOUBLE_HALF_PI, 0.0, 0.0), FVector(Size.X, Size.Z, 1.0));
		AddMeshComponent(TEXT("Sea"), Plane, ToUETransform(Local, Track->SuggestedRebaseY), Material, TEXT("TrackSea"));
	}
}

// ---- Harbor's animated actors (HarborMotion.tsx) --------------------------------------------------

void ATrackBuilder::BuildMotion()
{
	const TSharedPtr<FJsonObject> Scenery = Obj(Json, TEXT("scenery"));
	const TSharedPtr<FJsonObject> Parts = Obj(Scenery, TEXT("motionParts"));
	if (!Parts.IsValid())
	{
		return;
	}
	// Each moving part is its own component, tagged "Motion" plus "<part>:<index>:<animated entry>".
	auto Spawn = [&](const FString& Part, int32 Index, int32 Entry)
	{
		const TSharedPtr<FJsonObject> Def = Obj(Parts, *Part);
		UStaticMesh* Mesh = Def.IsValid() ? LoadAsset<UStaticMesh>(Str(Def, TEXT("ueAsset"))) : nullptr;
		if (!Mesh)
		{
			return;
		}
		UMaterialInterface* Material = MaterialFor(Obj(Def, TEXT("material")));
		if (Part == TEXT("buoy_lamp") && Material)
		{
			// Each lamp blinks on its own, so it gets its own instance of the master.
			UMaterialInstanceDynamic* Own = UMaterialInstanceDynamic::Create(Material->GetMaterial(), this);
			Own->CopyParameterOverrides(Cast<UMaterialInstance>(Material));
			Material = Own;
		}
		UStaticMeshComponent* C = AddMeshComponent(Part, Mesh, FTransform::Identity, Material, TEXT("Motion"));
		C->ComponentTags.Add(*FString::Printf(TEXT("%s:%d:%d"), *Part, Index, Entry));
		C->SetCastShadow(!Part.Contains(TEXT("beam")) && !Part.Contains(TEXT("wake")) && !Part.Contains(TEXT("smoke")) && !Part.Contains(TEXT("fountain")));
	};
	const TArray<TSharedPtr<FJsonValue>>& Animated = Arr(Scenery, TEXT("animated"));
	for (int32 E = 0; E < Animated.Num(); ++E)
	{
		const TSharedPtr<FJsonObject> A = Animated[E]->AsObject();
		const FString Type = Str(A, TEXT("type"));
		if (Type == TEXT("lighthouseBeam"))
		{
			Spawn(TEXT("lighthouse_beams"), 0, E);
			Spawn(TEXT("lighthouse_lamp"), 0, E);
		}
		else if (Type == TEXT("ferrisWheel"))
		{
			Spawn(TEXT("ferris_frame"), 0, E);
			Spawn(TEXT("ferris_wheel"), 0, E);
			for (int32 i = 0; i < 10; ++i)
			{
				Spawn(FString::Printf(TEXT("ferris_gondola_%d"), i % 5), i, E);
			}
		}
		else if (Type == TEXT("ferry"))
		{
			Spawn(TEXT("ferry"), 0, E);
			Spawn(TEXT("ferry_smoke"), 0, E);
		}
		else if (Type == TEXT("cargoShip"))
		{
			Spawn(TEXT("cargo_ship"), 0, E);
		}
		else if (Type == TEXT("jetSki"))
		{
			for (int32 i = 0; i < 3; ++i)
			{
				Spawn(FString::Printf(TEXT("jet_ski_%d"), i), i, E);
				Spawn(TEXT("jet_wake"), i, E);
			}
		}
		else if (Type == TEXT("gullFlock"))
		{
			for (int32 i = 0; i < 9; ++i)
			{
				Spawn(TEXT("gull_body"), i, E);
				Spawn(TEXT("gull_wing_l"), i, E);
				Spawn(TEXT("gull_wing_r"), i, E);
			}
		}
		else if (Type == TEXT("fountain"))
		{
			Spawn(TEXT("fountain_jet"), 0, E);
			Spawn(TEXT("fountain_spray"), 0, E);
		}
		else if (Type == TEXT("buoy"))
		{
			Spawn(FString::Printf(TEXT("buoy_%d"), static_cast<int32>(Num(A, TEXT("variant")))), 0, E);
			Spawn(TEXT("buoy_lamp"), 0, E);
		}
	}
	GatherMotion();
	TickMotion(0.0); // pose them for the editor viewport
}

void ATrackBuilder::GatherMotion()
{
	Motion.Reset();
	const TArray<TSharedPtr<FJsonValue>>& Animated = Arr(Obj(Json, TEXT("scenery")), TEXT("animated"));
	for (UActorComponent* Component : GetComponentsByTag(UStaticMeshComponent::StaticClass(), TEXT("Motion")))
	{
		for (const FName& Tag : Component->ComponentTags)
		{
			TArray<FString> Bits;
			if (Tag.ToString().ParseIntoArray(Bits, TEXT(":")) != 3)
			{
				continue;
			}
			const int32 Entry = FCString::Atoi(*Bits[2]);
			if (!Animated.IsValidIndex(Entry))
			{
				continue;
			}
			const TSharedPtr<FJsonObject> A = Animated[Entry]->AsObject();
			FMotionPart& M = Motion.AddDefaulted_GetRef();
			M.Component = Cast<UStaticMeshComponent>(Component);
			// Colour variants share a motion: "ferris_gondola_3" moves as "ferris_gondola".
			FString Part = Bits[0];
			int32 Underscore = INDEX_NONE;
			if (Part.FindLastChar(TEXT('_'), Underscore) && Part.Mid(Underscore + 1).IsNumeric())
			{
				Part.LeftInline(Underscore);
			}
			M.Part = *Part;
			M.Index = FCString::Atoi(*Bits[1]);
			M.Base = Vec3(Arr(A, TEXT("position")));
			M.Yaw = Num(A, TEXT("yaw"));
			M.Phase = Num(Obj(A, TEXT("params")), TEXT("phase"));
			M.Lamp = Cast<UMaterialInstanceDynamic>(M.Component->GetMaterial(0));
		}
	}
}

void ATrackBuilder::TickMotion(double T)
{
	if (Motion.Num() == 0 || !Track.IsValid())
	{
		return;
	}
	const double Rebase = Track->SuggestedRebaseY;
	for (const FMotionPart& M : Motion)
	{
		UStaticMeshComponent* C = M.Component.Get();
		if (!C)
		{
			continue;
		}
		const int32 i = M.Index;
		FTransform X = Three(M.Base);
		const EMotion Kind = MotionOf(M.Part);
		switch (Kind)
		{
		case EMotion::Fixed:
			break;
		case EMotion::LighthouseBeams:
			X = Three(M.Base, Euler(0.0, 0.7 * T, 0.0)); // :23-25
			break;
		case EMotion::FerrisFrame:
		case EMotion::FerrisWheel:
		case EMotion::FerrisGondola:
		{
			// :52-62: rim rolls 0.25 rad/s about the hub at y 11.6; gondolas counter-rotate to stay level.
			const FTransform BaseX = Three(M.Base, Euler(0.0, M.Yaw, 0.0));
			const double Roll = 0.25 * T;
			const FTransform Wheel = Three(FVector(0.0, 11.6, 0.0), Euler(0.0, 0.0, Roll));
			if (Kind == EMotion::FerrisFrame)
			{
				X = BaseX;
			}
			else if (Kind == EMotion::FerrisWheel)
			{
				X = Wheel * BaseX;
			}
			else
			{
				const double Angle = i / 10.0 * UE_DOUBLE_TWO_PI;
				X = Three(FVector(FMath::Cos(Angle) * 9.0, FMath::Sin(Angle) * 9.0, 0.0), Euler(0.0, 0.0, -Roll)) * Wheel * BaseX;
			}
			break;
		}
		case EMotion::Ferry:
		{
			const double A = T * 0.02; // :124-128
			X = Three(FVector(130.0 + FMath::Cos(A) * 330.0, -3.5 + FMath::Sin(T * 0.9) * 0.25, -35.0 + FMath::Sin(A) * 300.0),
				Euler(FMath::Sin(T * 0.7) * 0.015, -A, FMath::Sin(T * 0.9) * 0.02));
			break;
		}
		case EMotion::CargoShip:
		{
			const double A = -T * 0.012 + 2.2; // :129-133
			X = Three(FVector(130.0 + FMath::Cos(A) * 420.0, -3.5 + FMath::Sin(T * 0.6) * 0.2, -35.0 + FMath::Sin(A) * 400.0),
				Euler(0.0, -A + UE_DOUBLE_PI, FMath::Sin(T * 0.6) * 0.015));
			break;
		}
		case EMotion::JetSki:
		case EMotion::JetWake:
		{
			// :165-178. The web yaws the wake after laying it flat (Euler XYZ), which rolls it about its
			// length; here the wake stays flat and yaws with its ski.
			const double A = T * 0.55 + i * (UE_DOUBLE_TWO_PI / 3.0);
			const double R = 24.0 + FMath::Sin(T * 0.3 + i) * 5.0;
			const double PX = 110.0 + FMath::Cos(A) * R, PZ = -45.0 + FMath::Sin(A) * R * 0.75;
			X = Kind == EMotion::JetSki
				? Three(FVector(PX, -6.6 + FMath::Sin(T * 4.0 + i) * 0.12, PZ), Euler(FMath::Sin(T * 4.0 + i) * 0.05, -A, -0.25))
				: Three(FVector(PX, -6.92, PZ), Euler(0.0, -A, 0.0));
			break;
		}
		case EMotion::GullBody:
		case EMotion::GullWingL:
		case EMotion::GullWingR:
		{
			// :206-220.
			const double A = T * (0.35 + (i % 3) * 0.08) + i * 0.8;
			const double R = 14.0 + (i % 4) * 5.0;
			const FTransform Bird = Three(FVector(M.Base.X + FMath::Cos(A) * R, 18.0 + (i % 5) * 2.5 + FMath::Sin(T * 1.3 + i) * 0.8, M.Base.Z + FMath::Sin(A) * R), Euler(0.0, -A, 0.0));
			const double Flap = FMath::Sin(T * 6.0 + i) * 0.55;
			X = Kind == EMotion::GullBody ? Bird : Three(FVector::ZeroVector, Euler(0.0, 0.0, Kind == EMotion::GullWingL ? Flap : -Flap)) * Bird;
			break;
		}
		case EMotion::FountainJet:
		case EMotion::FountainSpray:
		{
			// :246-257.
			const double Pulse = 0.7 + FMath::Sin(T * 1.6) * 0.3;
			X = Kind == EMotion::FountainJet
				? Three(M.Base + FVector(0.0, 4.6 + Pulse * 1.4, 0.0), FQuat::Identity, FVector(1.0, Pulse, 1.0))
				: Three(M.Base + FVector(0.0, 6.4, 0.0), Euler(0.0, T * 0.8, 0.0), FVector(0.8 + Pulse * 0.5));
			break;
		}
		case EMotion::Buoy:
		case EMotion::BuoyLamp:
		{
			// :278-289: bob and rock; the lamp shows when sin(2.5 t + phase) > 0.6 (a MID ignores an unchanged value).
			static const FName OpacityName(TEXT("Opacity"));
			X = Three(FVector(M.Base.X, -6.6 + FMath::Sin(T * 1.4 + M.Phase) * 0.35, M.Base.Z),
				Euler(FMath::Sin(T * 1.1 + M.Phase) * 0.12, 0.0, FMath::Cos(T * 0.9 + M.Phase) * 0.12));
			if (Kind == EMotion::BuoyLamp && M.Lamp.IsValid())
			{
				M.Lamp->SetScalarParameterValue(OpacityName, FMath::Sin(T * 2.5 + M.Phase) > 0.6 ? 1.0 : 0.15);
			}
			break;
		}
		}
		C->SetRelativeTransform(ToUETransform(X, Rebase));
	}
}
