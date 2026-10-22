#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SumoArena.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Sumo Bumpers' venue, modelled in code: a neon dojo ring floating over a night sea. The ring is seven 1 m bands of
 * lacquered planks round a clay core, each band a ring of segments with a glowing edge; under it a rock keel tapers
 * to a point with crystals in it. When the rules warn a band it flashes red, then it breaks up segment by segment
 * and drops into the sea; a new round lifts every band back up. Around it: four tasselled posts on floating rocks
 * (one per player colour), a vermilion torii standing in the water, paper lanterns drifting on the swell, islands
 * on the horizon, a moon and a field of stars, all mirrored in the water.
 *
 * Plane units match the rules (Games/Sumo/SumoRules.h): metres, X away from the camera, Y to its right, the ring's
 * top at Z = 0.
 */
UCLASS()
class MOVECADEKART_API ASumoArena : public AActor
{
	GENERATED_BODY()

public:
	ASumoArena();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The ring from the rules: bands fallen so far, whether the next one is warned and for how long (s). */
	void SetRing(int32 ShrinksDone, bool bPending, double WarnAge);
	/** The players: the posts' tassels light in their colours. */
	void SetPlayers(int32 Count);

	static FLinearColor SlotColor(int32 Slot);
	/** The sea's surface, cm. */
	static constexpr double WaterZ = -800.0;
	/** Bands from the outside in; the last is the core. */
	static constexpr int32 BandCount = 8;

private:
	enum class EBandState : uint8 { Intact, Falling, Gone, Rising };

	struct FSegmentFall
	{
		double Delay = 0.0;
		FVector Axis = FVector::UpVector;
		double Spin = 1.0;
		double Drift = 0.0;
		bool bSplashed = false;
	};

	struct FBand
	{
		EBandState State = EBandState::Intact;
		double Since = 0.0;
		double Inner = 0.0;
		double Outer = 0.0;
		int32 Segments = 0;
		TArray<FTransform> Rest;
		TArray<FSegmentFall> Falls;
	};

	void Build();
	void Clear();
	void BuildLook();
	void BuildRing();
	void BuildKeel();
	void BuildSea();
	void BuildSky();
	void BuildProps();
	void AnimateBands();
	void AnimateProps(float Dt);

	UStaticMesh* SectorMesh(double Inner, double Outer, double Span, const TCHAR* Name);
	UStaticMeshComponent* Mesh(UStaticMesh* InMesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows = true, USceneComponent* Parent = nullptr);
	UInstancedStaticMeshComponent* Group(UStaticMesh* InMesh, UMaterialInterface* Material, bool bShadows);
	UPointLightComponent* Light(const FVector& At, const FLinearColor& Color, float Lumens, float RadiusCm);
	void Splash(const FVector& At, double Scale);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BandMeshes;

	/** Per band: the plank top (it flushes red when warned), the edge strip, the warning wash over it. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BandTops;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BandTrims;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BandWarnings;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BandWarningMats;

	/** The core's markings (start lines, the ring rope), hidden when the core goes. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CoreDecor;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Lanterns;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> TasselMats;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> TasselLights;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Floaters;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> RimLights;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Sphere;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<UDirectionalLightComponent> Moon;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<UDirectionalLightComponent> Rim;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<UPostProcessComponent> Grade;

	TArray<FBand> Bands;
	TArray<FVector> LanternRest;
	TArray<FVector> FloaterRest;
	int32 Players = 4;
	int32 WarnBand = INDEX_NONE;
	double WarnAge = 0.0;
	double Clock = 0.0;
};
