// ATrackBuilder's imported scenery: meshes[], scenery.instances, signs, lights and the sea (Docs/Port/03 §5, §8), and
// the hooks ATrackLife uses to take scenery over. Assets come from Tools/ue/import_track_assets.py; when they are
// missing the builder keeps its graybox.
#include "TrackBuilder.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "KartAssetLoad.h"
#include "Track/TrackBuildHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackScenery, Log, All);

using namespace MovecadeKart;
using namespace MovecadeKart::TrackBuild;

namespace
{
	/** Small props: no shadow under ShadowMinRadius (cm), and culled from a distance that grows with their size under
	 *  CullMaxRadius (CullPerRadius cm per cm, between CullMin and CullMax, dithered over the last fifth). Larger
	 *  pieces (buildings, cliffs, ships) keep drawing and casting. */
	constexpr double ShadowMinRadius = 60.0;
	constexpr double CullMaxRadius = 160.0;
	constexpr double CullPerRadius = 120.0;
	constexpr double CullMin = 6000.0;
	constexpr double CullMax = 20000.0;
	constexpr double CullFadeStart = 0.8;

	/** scenery.lights defaults, and their draw distance: a small light's pool isn't readable beyond ~4x its reach (but
	 *  always to 60 m), fading over the last fifth. */
	constexpr double DefaultLightCandela = 500.0;
	constexpr double DefaultLightRadius = 25.0;
	constexpr float LightSourceRadius = 20.f;
	constexpr float LightMinDrawDistance = 6000.f;
	constexpr float LightDrawPerReach = 4.f;
	constexpr float LightFadeShare = 0.2f;

	/** The sea's look when the export gives no single layer water material (HarborTrack.tsx:957-960). */
	constexpr double SeaRoughness = 0.34;
	constexpr double SeaMetalness = 0.15;
	constexpr double SeaTiling = 160.0;

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

	/** Nanite draws opaque and masked materials only: a translucent part (glass, spray, beams) renders through its
	 *  fallback mesh instead. */
	void MatchNaniteToMaterial(UStaticMeshComponent* Component, const UMaterialInterface* Material)
	{
		if (Material && IsTranslucentBlendMode(Material->GetBlendMode()))
		{
			Component->SetForceDisableNanite(true);
		}
	}

	/** Shadows and cull distances by the group's largest instance. */
	void TunePrefabGroup(UInstancedStaticMeshComponent* Group, const TArray<FTransform>& Transforms)
	{
		const UStaticMesh* Mesh = Group->GetStaticMesh();
		if (!Mesh)
		{
			return;
		}
		double Largest = 0.0;
		for (const FTransform& T : Transforms)
		{
			Largest = FMath::Max(Largest, T.GetScale3D().GetAbsMax());
		}
		const double Radius = Mesh->GetBounds().SphereRadius * Largest;
		if (Radius < ShadowMinRadius)
		{
			Group->SetCastShadow(false);
		}
		if (Radius < CullMaxRadius)
		{
			const int32 End = static_cast<int32>(FMath::Clamp(Radius * CullPerRadius, CullMin, CullMax));
			Group->SetCullDistances(static_cast<int32>(End * CullFadeStart), End);
		}
	}

	/** A scenery.instances entry's transform (three.js space). Placement yaw turns the prefab's local +Z toward the
	 *  road (trackside.ts:145-148). */
	FTransform InstanceTransform(const TSharedPtr<FJsonObject>& I)
	{
		FVector Scale = FVector::OneVector;
		const TArray<TSharedPtr<FJsonValue>>& S = Arr(I, TEXT("scale"));
		if (S.Num() == 3)
		{
			Scale = Vec3(S);
		}
		return Three(FVector(Num(I, TEXT("x")), Num(I, TEXT("y")), Num(I, TEXT("z"))), Euler(0.0, Num(I, TEXT("yaw")), 0.0), Scale);
	}

	/** The sea plane lies flat (the plane faces +Z); its scale is the instance's x by z. */
	FTransform SeaTransform(const TSharedPtr<FJsonObject>& I)
	{
		const FVector Size = Vec3(Arr(I, TEXT("scale")));
		return Three(FVector(Num(I, TEXT("x")), Num(I, TEXT("y")), Num(I, TEXT("z"))), Euler(-UE_DOUBLE_HALF_PI, 0.0, 0.0), FVector(Size.X, Size.Z, 1.0));
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
	check(Mesh);
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), *Name));
	Component->SetStaticMesh(Mesh);
	Component->SetupAttachment(RootComponent);
	Component->SetRelativeTransform(Transform);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->ComponentTags.Add(SceneryTag);
	if (Tag != NAME_None)
	{
		// Tagged parts are found again later (the sea, the moving scenery), and the movers move.
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
		const int32 Variant = VariantOf(I);
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
		Group.Transforms.Add(ToUETransform(InstanceTransform(I), Rebase));
	}
	int32 GroupCount = 0, InstanceCount = 0;
	for (const TPair<FPrefabGroupKey, FPrefabGroup>& Pair : Groups)
	{
		if (UInstancedStaticMeshComponent* Group = Pair.Value.Component)
		{
			TunePrefabGroup(Group, Pair.Value.Transforms);
			Group->AddInstances(Pair.Value.Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
			++GroupCount;
			InstanceCount += Pair.Value.Transforms.Num();
		}
	}
	UE_LOG(LogTrackScenery, Log, TEXT("%s: %d prefab instances in %d groups"), *TrackId, InstanceCount, GroupCount);
}

void ATrackBuilder::BuildSigns()
{
	UStaticMesh* Plane = LoadAsset<UStaticMesh>(SignPlaneAsset);
	if (!Plane)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : Arr(Obj(Json, TEXT("scenery")), TEXT("signs")))
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
		Light->SetRelativeLocation(Coords::ToUE(FVector(Num(L, TEXT("x")), Num(L, TEXT("y")) + Rebase, Num(L, TEXT("z")))));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(static_cast<float>(Num(L, TEXT("candela"), DefaultLightCandela)));
		Light->SetLightColor(HexColor(Str(L, TEXT("color"))));
		Light->SetAttenuationRadius(static_cast<float>(Num(L, TEXT("radius"), DefaultLightRadius) * 100.0));
		Light->SetSourceRadius(LightSourceRadius);
		// Many small lights across four split-screen views: no shadows, no volumetric scattering.
		Light->SetCastShadows(false);
		Light->SetVolumetricScatteringIntensity(0.f);
		Light->MaxDrawDistance = FMath::Max(LightMinDrawDistance, Light->AttenuationRadius * LightDrawPerReach);
		Light->MaxDistanceFadeRange = Light->MaxDrawDistance * LightFadeShare;
		Light->ComponentTags.Add(SceneryTag);
		AddInstanceComponent(Light);
		Light->RegisterComponent();
		++SceneryCount;
	}
}

void ATrackBuilder::BuildSea()
{
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
			UStaticMeshComponent* Surface = AddMeshComponent(TEXT("Sea"), Plane, ToUETransform(SeaTransform(I), Track->SuggestedRebaseY), MaterialFor(Water), TEXT("TrackSea"));
			Surface->SetCastShadow(false);
			continue;
		}
		TSharedPtr<FJsonObject> Hint = MakeShared<FJsonObject>();
		Hint->SetStringField(TEXT("shader"), TEXT("textured"));
		Hint->SetStringField(TEXT("color"), Str(I, TEXT("tint")));
		Hint->SetStringField(TEXT("texture"), Str(Sea, TEXT("texture")));
		Hint->SetNumberField(TEXT("roughness"), Num(Sea, TEXT("roughness"), SeaRoughness));
		Hint->SetNumberField(TEXT("metalness"), Num(Sea, TEXT("metalness"), SeaMetalness));
		UMaterialInterface* Material = MaterialFor(Hint);
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material))
		{
			const TArray<TSharedPtr<FJsonValue>>& Scroll = Arr(Sea, TEXT("scroll"));
			MID->SetScalarParameterValue(TEXT("Tiling"), Num(Sea, TEXT("repeat"), SeaTiling));
			if (Scroll.Num() == 2)
			{
				MID->SetScalarParameterValue(TEXT("PanU"), Scroll[0]->AsNumber());
				MID->SetScalarParameterValue(TEXT("PanV"), Scroll[1]->AsNumber());
			}
		}
		AddMeshComponent(TEXT("Sea"), Plane, ToUETransform(SeaTransform(I), Track->SuggestedRebaseY), Material, TEXT("TrackSea"));
	}
}

TArray<FTransform> ATrackBuilder::TakeOverPrefab(const FString& Prefab)
{
	TArray<FTransform> Out;
	const TSharedPtr<FJsonObject> Def = Obj(Obj(Obj(Json, TEXT("scenery")), TEXT("prefabs")), *Prefab);
	const UStaticMesh* Mesh = Def.IsValid() ? LoadAsset<UStaticMesh>(Str(Def, TEXT("ueAsset"))) : nullptr;
	if (!Mesh)
	{
		return Out;
	}
	// Match by mesh, not by name or tag: a level saved by an older build keeps its own component names.
	TArray<UInstancedStaticMeshComponent*> Groups;
	GetComponents(Groups);
	for (UInstancedStaticMeshComponent* Group : Groups)
	{
		if (Group->GetStaticMesh() != Mesh || !Group->ComponentTags.Contains(SceneryTag))
		{
			continue;
		}
		for (int32 i = 0; i < Group->GetInstanceCount(); ++i)
		{
			FTransform X;
			if (Group->GetInstanceTransform(i, X, /*bWorldSpace*/ true))
			{
				Out.Add(X);
			}
		}
		Group->SetVisibility(false);
		Group->SetHiddenInGame(true);
	}
	return Out;
}

void ATrackBuilder::EnableFoliageWind()
{
	UMaterialInterface* Windy = LoadKartAsset<UMaterialInterface>(*MaterialPath(TEXT("foliageWind")), AssetSource::TrackAssets);
	UMaterialInterface* Still = LoadKartAsset<UMaterialInterface>(*MaterialPath(TEXT("foliage")), AssetSource::TrackAssets);
	if (!Windy || !Still || Windy == Still)
	{
		return;
	}
	TMap<TPair<UMaterialInterface*, int32>, UMaterialInstanceDynamic*> Made;
	TArray<UInstancedStaticMeshComponent*> Groups;
	GetComponents(Groups);
	for (UInstancedStaticMeshComponent* Group : Groups)
	{
		if (!Group->ComponentTags.Contains(SceneryTag) || !Group->GetStaticMesh())
		{
			continue;
		}
		// Taller plants sway less per metre, so every crown tip travels a similar few centimetres (sway is bucketed
		// in quarters so plants of similar heights share an instance).
		const double HeightM = FMath::Max(Group->GetStaticMesh()->GetBoundingBox().Max.Z / 100.0, 0.4);
		const bool bTall = HeightM > 2.0;
		const double TipCm = bTall ? 9.0 : 4.5;
		const int32 Bucket = FMath::RoundToInt32(TipCm / HeightM * 4.0);
		bool bWindy = false;
		for (int32 Slot = 0; Slot < Group->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Current = Group->GetMaterial(Slot);
			if (!Current || Current->GetMaterial() != Still->GetMaterial())
			{
				continue;
			}
			UMaterialInstanceDynamic*& Mid = Made.FindOrAdd({ Current, Bucket });
			if (!Mid)
			{
				Mid = UMaterialInstanceDynamic::Create(Windy, this);
				if (UMaterialInstance* From = Cast<UMaterialInstance>(Current))
				{
					Mid->CopyParameterOverrides(From);
				}
				Mid->SetScalarParameterValue(TEXT("WindSway"), Bucket / 4.0);
				Mid->SetScalarParameterValue(TEXT("WindFlutter"), bTall ? 0.8 : 0.3);
			}
			Group->SetMaterial(Slot, Mid);
			bWindy = true;
		}
		// M_TrackFoliageWind sways about each instance's origin, from custom data 0..2.
		if (bWindy && Group->NumCustomDataFloats < 3)
		{
			Group->SetNumCustomDataFloats(3);
			for (int32 i = 0; i < Group->GetInstanceCount(); ++i)
			{
				FTransform X;
				Group->GetInstanceTransform(i, X, /*bWorldSpace*/ true);
				const FVector O = X.GetLocation();
				Group->SetCustomData(i, { static_cast<float>(O.X), static_cast<float>(O.Y), static_cast<float>(O.Z) }, i == Group->GetInstanceCount() - 1);
			}
		}
	}
}
