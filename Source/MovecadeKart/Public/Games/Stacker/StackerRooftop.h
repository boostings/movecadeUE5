#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterialCache.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeVenueKit.h"
#include "StackerRooftop.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** The rooftop's golden-hour light, haze and grade (StackerCanvas.tsx), tunable on the placed actor. */
USTRUCT()
struct FStackerRooftopLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Light")
	float SunIntensity = 2.6f;

	UPROPERTY(EditAnywhere, Category = "Light")
	float RimIntensity = 1.35f;

	UPROPERTY(EditAnywhere, Category = "Light")
	float SkyLightIntensity = 1.4f;

	UPROPERTY(EditAnywhere, Category = "Fog")
	float FogStartDistance = 2600.f;

	UPROPERTY(EditAnywhere, Category = "Fog")
	float FogDensity = 0.07f;

	UPROPERTY(EditAnywhere, Category = "Fog")
	float FogHeightFalloff = 0.02f;

	UPROPERTY(EditAnywhere, Category = "Fog")
	float FogMaxOpacity = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Grade")
	float Vignette = 0.4f;

	/** StackerCanvas.tsx: bloom .8 for the sun, the windows and the sparks. */
	UPROPERTY(EditAnywhere, Category = "Grade")
	float Bloom = 0.8f;
};

/**
 * Sky Stacker's rooftop (movecade-v2 stacker/{RooftopEnvironment,RooftopProps,StackerCanvas}.tsx, Docs/Port/09 §6),
 * placed in L_Stacker (or spawned by AStackerGameMode) and modelled in code: a golden-hour roof deck above the city
 * with a work zone and a coloured plinth per lane, a hazard stripe, the railing, a yellow crane with its hook, cones,
 * a cable spool, a pallet of boxes, an HVAC unit with a turning fan and two flags; three bands of towers with lit
 * windows behind, sinking as the stacks climb; four clouds drifting in the haze above them.
 *
 * AStackerGame sets the lane count once (SetLanes) and the scroll each frame (SetScroll). In the editor the venue
 * previews itself on construction.
 *
 * Cost: everything that never moves is Static (cached draws) and batched per look in instance groups; only the city
 * bands, the clouds, the fan and the flags are Movable. Each band's windows are baked into one mesh (lit and dark
 * sections), not thousands of instances.
 */
UCLASS()
class MOVECADEKART_API AStackerRooftop : public AActor
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxLanes = 4;
	/** Lanes sit this far apart (web units). */
	static constexpr double LaneSpacing = 8.0;
	/** The deck's surface (web y): the plinths rise from it to the foundation block at 0. */
	static constexpr double DeckY = -0.696;

	AStackerRooftop();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Builds the venue for this many lanes (once; again only when the count changes). */
	void SetLanes(int32 LaneCount);
	/** The camera rides up with the tallest tower: the city bands sink at their parallax (rows scrolled). */
	void SetScroll(double MaxScroll);

	static FVector ToWorld(const FVector& Web);
	/** A lane's centre across the deck (web x) when LaneCount lanes share it, centred on 0. */
	static double LaneX(int32 Lane, int32 LaneCount) { return (Lane - (LaneCount - 1) / 2.0) * LaneSpacing; }
	static FLinearColor LaneColor(int32 Lane);
	/** The deck's width (web units): the lanes plus a margin, never narrower than the props need. */
	double DeckWidth() const { return FMath::Max(18.0, Lanes * LaneSpacing + 8.0); }

private:
	void Build();
	void Clear();
	void BuildLook();
	void BuildSkyMaterial();
	void BuildDeck();
	void BuildLaneZone(int32 Lane);
	void BuildCity();
	void BuildSky();
	void BuildProps();
	void BuildCrane(double DeckWidth);
	void BuildRailing(double DeckWidth);
	void BuildDeckProps(double DeckWidth);
	void BuildHvac(double SideX, UMaterialInterface* Body, UMaterialInterface* Steel);
	void BuildFlags(double DeckWidth, UMaterialInterface* PoleMaterial);

	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad = 0.0, bool bShadows = true, USceneComponent* Parent = nullptr);
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows = true);
	/** The instance group for a look: Static under the root, Movable under a moving parent (a city band). */
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent = nullptr);
	/** A built mesh under Parent (Static under the root, Movable under a band). */
	UStaticMeshComponent* Part(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials, bool bShadows, USceneComponent* Parent = nullptr);
	USceneComponent* Pivot(const TCHAR* Name, const FVector& WebAt);
	EComponentMobility::Type MobilityUnder(const USceneComponent* Parent) const;

	UPROPERTY(EditAnywhere, Category = "Rooftop")
	int32 Lanes = 1;

	UPROPERTY(EditAnywhere, Category = "Rooftop")
	FStackerRooftopLook Look;

	/** Instance groups of the parts that never move, and of the city bands' (which sink with the scroll). */
	UPROPERTY(Transient)
	FArcadeInstanceGroups StaticGroups;

	UPROPERTY(Transient)
	FArcadeInstanceGroups MovingGroups;

	/** One material per look, made once per build (and reset with the venue). */
	UPROPERTY(Transient)
	FKartMaterialCache Materials;

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

	/** Seconds since play began: drives the fan, the flags and the clouds. */
	double Clock = 0.0;
	/** The bands' last scroll: they move only when it changes. */
	double AppliedScroll = -1.0;
	/** Built since load (a game world builds on SetLanes, or on the first tick if nobody asks). */
	bool bBuilt = false;
};
