#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FishingLake.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Fishing Frenzy's venue, modelled in code: a cosy timber dock on a lake at golden hour. The sun sits low over the
 * far shore and lays a glitter path across single layer water (the harbour track's M_TrackWater) over a lake bed
 * that shelves from the shallows into the deep, so the water reads turquoise by the dock and dark far out. Pines
 * and rounded hills on the shores go to silhouette against an orange sky (M_TrackSky); reeds and lily pads in the
 * shallows, a moored rowboat, lanterns and string lights along the dock, a bucket and tackle box per angler, and a
 * row of buoys marking each distance band (shallows, reeds, deep, far).
 *
 * UE space, cm: the dock's fishing edge runs along Y at X = EdgeX, the lake is +X, the water's surface is Z = 0.
 */
UCLASS()
class MOVECADEKART_API AFishingLake : public AActor
{
	GENERATED_BODY()

public:
	AFishingLake();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	void SetLanes(int32 LaneCount);
	int32 GetLanes() const { return Lanes; }

	static constexpr double EdgeX = 150.0;
	static constexpr double DeckZ = 92.0;
	/** Centimetres of scene per metre of cast (the far water is compressed so a 30 m cast stays readable). */
	static constexpr double VisualCmPerM = 82.0;
	static constexpr double LaneSpacing = 250.0;

	static double LaneY(int32 Lane, int32 LaneCount) { return (Lane - (LaneCount - 1) / 2.0) * LaneSpacing; }
	/** Where an angler stands (feet on the deck). */
	static FVector AnglerSpot(int32 Lane, int32 LaneCount);
	/** A point on the water DistanceM out along a lane (Lateral -1..1 swims to either side). */
	static FVector WaterPoint(int32 Lane, int32 LaneCount, double DistanceM, double Lateral = 0.0);
	static FVector BucketSpot(int32 Lane, int32 LaneCount);
	static FLinearColor LaneColor(int32 Lane);
	double DeckHalfWidth() const { return FMath::Max(420.0, Lanes * LaneSpacing / 2.0 + 220.0); }
	/** The sun's direction (toward it), for the camera and the glints. */
	static FVector SunDirection();

private:
	void Build();
	void Clear();
	void BuildLook();
	void BuildWater();
	void BuildDock();
	void BuildShores();
	void BuildShallows();
	void BuildProps();

	void Box(UMaterialInterface* Material, const FVector& Centre, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, bool bShadows = true);
	void Instance(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows = true);
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows);
	UStaticMeshComponent* Part(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows = true);

	UPROPERTY(EditAnywhere, Category = "Lake")
	int32 Lanes = 4;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	/** Bobbing things: the rowboat and the band buoys (with their rest transforms). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Floaters;
	TArray<FTransform> FloaterRest;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Lamps;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Sphere;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Plane;

	UPROPERTY(VisibleAnywhere, Category = "Lake")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Lake")
	TObjectPtr<UDirectionalLightComponent> Fill;

	UPROPERTY(VisibleAnywhere, Category = "Lake")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Lake")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY(VisibleAnywhere, Category = "Lake")
	TObjectPtr<UStaticMeshComponent> Water;

	UPROPERTY(VisibleAnywhere, Category = "Lake")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere, Category = "Lake")
	TObjectPtr<UPostProcessComponent> Grade;

	double Clock = 0.0;
};
