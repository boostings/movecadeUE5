#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BowlingAlley.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * The bowling house (movecade-v2 bowling/venue, laneLayout.ts; Docs/Port/06-bowling.md §4), modelled in code like
 * the tracks: five lanes (the player bowls the middle one), the approach and foul line, gutters and capping, the
 * pin deck and pit, masking units and the pinsetter run, walls, a trussed ceiling with linear light troughs, and
 * the light hierarchy (the pin deck brightest, the approach warm). Built in the editor too (OnConstruction).
 *
 * Web world units map 1:1 to metres (UE cm via MovecadeKart::Coords): down the lane is the web's -Z, UE +X.
 * The rack and ball use the web's visual scales (VisualPinScale for the rack, LaneVisualScale for the lane).
 */
UCLASS()
class MOVECADEKART_API ABowlingAlley : public AActor
{
	GENERATED_BODY()

public:
	ABowlingAlley();

	virtual void OnConstruction(const FTransform& Transform) override;

	// Layout (web world units, laneLayout.ts / venueLayout.ts).
	static constexpr double DeckZ = -13.05;
	static constexpr double LaneFrontZ = 1.35;
	static constexpr double PitDepth = 2.25;
	static constexpr double MachineZ = -16.82;
	static constexpr double MaskingZ = -15.02;
	static constexpr double FoulZ = 0.55;
	static constexpr double LaneVisualWidth = 3.15;
	/** Sim metres to lane units across (3.15 / 1.0668). */
	static constexpr double LaneVisualScale = 2.9528;
	/** Sim metres to rack units (visual pin spacing 0.68 / 0.3048). */
	static constexpr double VisualPinScale = 0.68 / 0.3048;
	static constexpr double PinHeight = 0.381 * VisualPinScale;
	static constexpr double PinBelly = 0.0605 * VisualPinScale;
	static constexpr double BallRadius = 0.19;
	static constexpr double DeckY = 0.02;

	/** The played lane's pin spot (web world units) for pin 1..10. */
	static FVector PinSpot(int32 Id, double LaneX = 0.0);
	/** Web world (x, y, z) to UE cm. */
	static FVector ToWorld(const FVector& Web);

	UStaticMesh* GetPinMesh() const { return PinMesh; }
	UStaticMesh* GetBallMesh() const { return BallMesh; }
	UMaterialInterface* GetPinMaterial(int32 Slot) const { return PinMaterials.IsValidIndex(Slot) ? PinMaterials[Slot].Get() : nullptr; }

	/** The house lanes' racks (presentation): one instanced component per lane, ten pins each. */
	UInstancedStaticMeshComponent* GetHouseRack(int32 Lane) const { return HouseRacks.IsValidIndex(Lane) ? HouseRacks[Lane].Get() : nullptr; }

private:
	void Build();
	void Clear();
	void BuildShell();
	void BuildLanes();
	void BuildDeck();
	void BuildCeiling();
	void BuildLights();
	void BuildHouseRacks();
	void BuildMeshes();

	/** A box centred at C (web units) with size S (web x, y, z), optionally yawed about web Y. */
	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad = 0.0);
	/** A cylinder along web Y. */
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height);
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows = true);

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PinMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BallMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> GutterMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> PinMaterials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> HouseRacks;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;
};
