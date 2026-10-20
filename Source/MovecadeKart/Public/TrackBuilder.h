#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/KartTrack.h"
#include "TrackBuilder.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture;
class USkyAtmosphereComponent;
class USkyLightComponent;

/**
 * Loads Data/Tracks/<TrackId>.track.json, owns the sim's track data, and builds the track:
 * - the imported meshes in the JSON's meshes[] (Violet's decoded GLB, kerbs; Harbor's ribbons and prop
 *   batches), each part with the material its JSON hint asks for (Content/Tracks/Shared/Materials);
 * - scenery.instances as instanced prefabs (grandstands, lamps, banners, trees), scenery.signs,
 *   and Harbor's animated actors (scenery.animated, HarborMotion.tsx), moved in Tick;
 * - a graybox of anything not imported (the Harbor ribbons as procedural sections, start line),
 *   the sea, boost pads and item boxes, plus a basic sky and sun from the track's look.
 * Built in the editor too (OnConstruction). Run Tools/ue/import_track_assets.py after an export.
 */
UCLASS()
class MOVECADEKART_API ATrackBuilder : public AActor
{
	GENERATED_BODY()

public:
	ATrackBuilder();

	UPROPERTY(EditAnywhere, Category = "Track")
	FString TrackId = TEXT("sunset-harbor");

	/** Sun, sky light, atmosphere and fog, for an otherwise empty level. */
	UPROPERTY(EditAnywhere, Category = "Track")
	bool bAddLighting = true;

	/** Draw the checkpoint radii while playing. */
	UPROPERTY(EditAnywhere, Category = "Track")
	bool bShowCheckpoints = false;

	/** The parsed track, loading it on first use. Null if the file is missing or invalid. */
	TSharedPtr<const MovecadeKart::FTrack> GetTrack();

	/** The static item boxes are an editor preview; the race's item presenter draws the live ones. */
	void SetPreviewItemBoxesVisible(bool bVisible);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static FString TrackPath(const FString& Id);

	/** How many imported scenery components the last build made (0 when the assets aren't imported). */
	int32 NumSceneryComponents() const { return SceneryCount; }

private:
	bool Load();
	void Build();
	void AddSection(const FString& Name, const FLinearColor& Color, TFunctionRef<void(TArray<FVector>&, TArray<int32>&, TArray<FVector>&)> Emit);

	// Imported scenery (TrackScenery.cpp).
	void ClearScenery();
	/** Returns true when an imported mesh replaces the procedural ribbons. */
	bool BuildImportedMeshes();
	void BuildPrefabInstances();
	void BuildSigns();
	void BuildSea();
	void BuildMotion();
	void GatherMotion();
	void TickMotion(double Time);
	UStaticMeshComponent* AddMeshComponent(const FString& Name, UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, const FName Tag = NAME_None);
	UMaterialInterface* MaterialFor(const TSharedPtr<class FJsonObject>& Hint, const FString& TintOverride = FString(), int32 Variant = 0);
	UTexture* TextureNamed(const FString& Name);

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> MaterialCache;

	struct FMotionPart
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		FName Part;
		int32 Index = 0;
		FVector Base = FVector::ZeroVector; // three.js metres
		double Yaw = 0.0;
		double Phase = 0.0;
		TWeakObjectPtr<UMaterialInstanceDynamic> Lamp;
	};
	TArray<FMotionPart> Motion;
	int32 SceneryCount = 0;

	UPROPERTY(VisibleAnywhere, Category = "Track")
	TObjectPtr<UProceduralMeshComponent> Ground;

	UPROPERTY(VisibleAnywhere, Category = "Track")
	TObjectPtr<UInstancedStaticMeshComponent> ItemBoxes;

	UPROPERTY(VisibleAnywhere, Category = "Track")
	TObjectPtr<UInstancedStaticMeshComponent> BoostPads;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	TSharedPtr<MovecadeKart::FTrack> Track;
	TSharedPtr<class FJsonObject> Json;
	FString LoadedId;
	TArray<FVector> ItemBoxBase;
	int32 SectionCount = 0;
};
