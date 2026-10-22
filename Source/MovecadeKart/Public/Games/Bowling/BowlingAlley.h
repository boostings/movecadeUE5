#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BowlingAlley.generated.h"

class UInstancedStaticMeshComponent;
class ULightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;
class USpotLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * The bowling house (movecade-v2 bowling/venue, laneLayout.ts; Docs/Port/06-bowling.md §4), modelled in code like
 * the tracks: five lanes (the player bowls the middle one), the approach and foul line, gutters and capping, the
 * pin deck and pit with neon-trimmed kickbacks, masking units and the pinsetter run, walls with neon runs, a trussed
 * ceiling with linear light troughs, the light hierarchy (the pin deck brightest, the approach warm), a grade that
 * holds the exposure, and a light show the game fires on strikes. Built in the editor too (OnConstruction).
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

	/**
	 * The house light show, driven by the game every frame: Energy 0 is the resting room; up to 1 the show spots
	 * sweep the deck in Tint and the neon pulses (a strike), past 1 a faster strobe (a turkey). Time is the show's
	 * age in seconds. Cheap when nothing changes: the resting state is applied once.
	 */
	void SetShow(double Energy, const FLinearColor& Tint, double Time);

	/** An opaque unlit emissive colour (Color above 1 blooms): neon tubes, lenses, lightboxes. */
	static UMaterialInstanceDynamic* OpaqueGlow(UObject* Outer, const FLinearColor& Color);

private:
	void Build();
	void Clear();
	void BuildShell();
	void BuildLanes();
	void BuildDeck();
	void BuildCeiling();
	void BuildNeon();
	void BuildLights();
	void BuildGrade();
	void BuildHouseRacks();
	void BuildMeshes();

	/** A box centred at C (web units) with size S (web x, y, z), optionally yawed about web Y. */
	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad = 0.0, bool bShadows = true);
	/** A box rolled in the plane of the back wall (about the lane axis), for neon drawn on it. */
	void WallBox(UMaterialInterface* Material, const FVector& C, const FVector& S, double RollRad);
	/** A cylinder along web Y. */
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height);
	UInstancedStaticMeshComponent* Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows = true);
	/** One material per key per Build(), so equal looks share a MID (and an instanced group). */
	UMaterialInterface* Cached(const TCHAR* Key, TFunctionRef<UMaterialInterface*()> Make);
	/** A neon colour whose glow the light show pulses. */
	UMaterialInterface* Neon(const TCHAR* Key, const FLinearColor& Color);

	/** Keyed by mesh and material pointers (equal names on different objects stay distinct). */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Groups;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> Materials;

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

	/** The neon materials and their resting colours, which the show pulses. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> NeonMids;
	TArray<FLinearColor> NeonColors;

	/** The show spots (off at rest) and their resting intensity when lit. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> ShowSpots;

	/** The deck key, which the show flashes. */
	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> DeckKey;

	double ShownEnergy = -1.0;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;
};
