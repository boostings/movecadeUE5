#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StackerRooftop.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Sky Stacker's rooftop (movecade-v2 stacker/{RooftopEnvironment,RooftopProps,StackerCanvas}.tsx, Docs/Port/09 §6),
 * modelled in code: a golden-hour roof deck above the city with a work zone and a coloured plinth per lane, a
 * hazard stripe, the railing, a yellow crane with its hook, cones, a cable spool, a pallet of boxes, an HVAC unit
 * with a turning fan and two flags; three bands of towers with lit windows behind, sinking as the stacks climb;
 * four clouds drifting in the haze above them.
 *
 * Cost: everything that never moves is Static (cached draws); only the city bands, the clouds, the fan and the flags
 * are Movable. Each band's windows are baked into one mesh (lit and dark sections), not thousands of instances.
 */
UCLASS()
class MOVECADEKART_API AStackerRooftop : public AActor
{
	GENERATED_BODY()

public:
	AStackerRooftop();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Builds the venue for this many lanes (once; again only when the count changes). */
	void SetLanes(int32 LaneCount);
	/** The camera rides up with the tallest tower: the city bands sink at their parallax (rows scrolled). */
	void SetScroll(double MaxScroll);

	static FVector ToWorld(const FVector& Web);
	static double LaneX(int32 Lane, int32 LaneCount) { return (Lane - (LaneCount - 1) / 2.0) * 8.0; }
	double DeckWidth() const { return FMath::Max(18.0, Lanes * 8.0 + 8.0); }
	static FLinearColor LaneColor(int32 Lane);
	static constexpr double DeckY = -0.696;

private:
	void Build();
	void Clear();
	void BuildLook();
	void BuildDeck();
	void BuildCity();
	void BuildSky();
	void BuildProps();

	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad = 0.0, bool bShadows = true, USceneComponent* Parent = nullptr);
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows = true);
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent = nullptr);
	/** A built mesh under Parent (Static under the root, Movable under a band). */
	UStaticMeshComponent* Part(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials, bool bShadows, USceneComponent* Parent = nullptr);
	USceneComponent* Pivot(const TCHAR* Name, const FVector& WebAt);
	/** Plain() once per look, kept across rebuilds (a rebuild for a new lane count reuses every material). */
	UMaterialInterface* Mat(const FLinearColor& Color, double Roughness = 0.6, double Metallic = 0.0, double Emissive = 0.0);
	EComponentMobility::Type MobilityUnder(const USceneComponent* Parent) const;

	UPROPERTY(EditAnywhere, Category = "Rooftop")
	int32 Lanes = 1;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkyMaterial;

	/** The three city bands (near, middle, far), each a parent that sinks with its parallax. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Bands;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Clouds;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Fan;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Flags;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;

	UPROPERTY(VisibleAnywhere, Category = "Rooftop")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Rooftop")
	TObjectPtr<UDirectionalLightComponent> Rim;

	UPROPERTY(VisibleAnywhere, Category = "Rooftop")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Rooftop")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY(VisibleAnywhere, Category = "Rooftop")
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY(VisibleAnywhere, Category = "Rooftop")
	TObjectPtr<UPostProcessComponent> Grade;

	double Clock = 0.0;
	/** The bands' last scroll: they move only when it changes. */
	double AppliedScroll = -1.0;
	/** Built since load (a game world builds on SetLanes, or on the first tick if nobody asks). */
	bool bBuilt = false;
};
