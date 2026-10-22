#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/KartTrack.h"
#include "TrackBuilder.generated.h"

class FJsonObject;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture;
class USkyAtmosphereComponent;
class USkyLightComponent;

/**
 * Loads Data/Tracks/<TrackId>.track.json, owns the sim's track data, and builds the track:
 * - the imported meshes in the JSON's meshes[] (Violet's decoded GLB, kerbs; Harbor's ribbons and prop
 *   batches), each part with the material its JSON hint asks for (Content/Tracks/Shared/Materials);
 * - scenery.instances as instanced prefabs (grandstands, lamps, banners, trees), scenery.signs, scenery.lights,
 *   and Harbor's animated actors (scenery.animated, HarborMotion.tsx), moved in Tick;
 * - a graybox of anything not imported (the Harbor ribbons as procedural sections, start line),
 *   the sea, boost pads (emissive chevrons, BoostPads.jsx) and preview item boxes;
 * - the track's look (JSON `look`): sun, sky light, painted sky dome, height fog and an unbound post process
 *   volume with this track's colour tint.
 * Built in the editor too (OnConstruction). Run Tools/ue/import_track_assets.py after an export.
 *
 * In game worlds BeginPlay also spawns the track's ATrackLife (grass, crowds, wind, fireworks; -KartNoLife: none).
 * -KartTrack=<id> swaps the track at BeginPlay (any level with a builder, or none: AKartGameMode spawns one), so local
 * play can pick a track that has no level of its own.
 *
 * Source files: TrackBuilder.cpp (loading, build order, the frame), TrackGraybox.cpp (procedural ribbons, sea, start
 * line, boost pads, item boxes), TrackLook.cpp (lighting, sky, fog, grade), TrackScenery.cpp (imported meshes,
 * prefabs, signs, lights, sea, ATrackLife hooks), TrackSceneryMaterials.cpp (JSON hints to material instances) and
 * TrackMotion.cpp (Harbor's moving scenery).
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

	/** A material for a JSON-style hint ({shader, color, detail, ...}), from the same cache as the scenery's. */
	UMaterialInterface* MaterialForHint(const TSharedPtr<FJsonObject>& Hint) { return MaterialFor(Hint); }

	/**
	 * Hides a scenery prefab's instanced components (scenery.instances) and returns its instances' world
	 * transforms, for ATrackLife to rebuild that prefab with more life in it (the grandstands' crowds).
	 */
	TArray<FTransform> TakeOverPrefab(const FString& Prefab);

	/** Repoints the instanced plants (foliage prefabs) at M_TrackFoliageWind, swaying with ATrackLife's wind. */
	void EnableFoliageWind();

private:
	/** How each animated part moves (TrackMotion.cpp); any part not listed stays where it was placed. */
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

	struct FMotionPart
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		EMotion Kind = EMotion::Fixed;
		int32 Index = 0;
		FVector Base = FVector::ZeroVector; // three.js metres
		double Yaw = 0.0;
		double Phase = 0.0;
		TWeakObjectPtr<UMaterialInstanceDynamic> Lamp;
	};

	bool Load();
	void Build();
	void TickSkyCapture(double Time);
	void TickTrackCamera();
	void TickPreviewItemBoxes(double Time);
	void DrawCheckpoints();

	// Graybox (TrackGraybox.cpp).
	void AddSection(const FString& Name, const FLinearColor& Color, TFunctionRef<void(TArray<FVector>&, TArray<int32>&, TArray<FVector>&)> Emit);
	void BuildRibbons();
	void BuildGrayboxSea();
	void BuildStartLine();
	void BuildBoostPads();
	void BuildItemBoxes();
	void ApplyGrayboxMaterials();

	/** Lights, sky, fog and post from the JSON look (TrackLook.cpp). Also runs at BeginPlay, so a look change needs
	 *  no level rebuild. */
	void ApplyLook();

	// Imported scenery (TrackScenery.cpp, TrackSceneryMaterials.cpp).
	void ClearScenery();
	/** Returns true when an imported mesh replaces the procedural ribbons. */
	bool BuildImportedMeshes();
	void BuildPrefabInstances();
	void BuildSigns();
	/** scenery.lights: unshadowed point lights (street lamps, arch uplights) for the night track. */
	void BuildLights();
	void BuildSea();
	UStaticMeshComponent* AddMeshComponent(const FString& Name, UStaticMesh* Mesh, const FTransform& Transform, UMaterialInterface* Material, const FName Tag = NAME_None);
	UMaterialInterface* MaterialFor(const TSharedPtr<FJsonObject>& Hint, const FString& TintOverride = FString(), int32 Variant = 0);
	void ApplyDetailSurface(UMaterialInstanceDynamic* Material, const TSharedPtr<FJsonObject>& Hint, const FString& Detail);
	void ApplyHintParams(UMaterialInstanceDynamic* Material, const TSharedPtr<FJsonObject>& Params);
	UTexture* TextureNamed(const FString& Name);

	// Moving scenery (TrackMotion.cpp).
	void BuildMotion();
	void GatherMotion();
	void TickMotion(double Time);
	static EMotion MotionOf(FName Part);
	FTransform MotionPose(const FMotionPart& Part, double Time) const;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> MaterialCache;

	TArray<FMotionPart> Motion;
	/** Sky recaptures still to do after BeginPlay (the sky light captures once, not every frame). */
	int32 SkyRecapturesLeft = 0;
	int32 SceneryCount = 0;

	/** -KartTrackCam: a fixed view for look checks in screenshots. */
	TWeakObjectPtr<AActor> DebugCamera;

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

	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** The web's gradient sky dome (M_TrackSky), 10 km out; the sky light captures it. */
	UPROPERTY(VisibleAnywhere, Category = "Lighting")
	TObjectPtr<UStaticMeshComponent> SkySphere;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	TSharedPtr<MovecadeKart::FTrack> Track;
	TSharedPtr<FJsonObject> Json;
	FString LoadedId;
	TArray<FVector> ItemBoxBase;
	int32 SectionCount = 0;
};
