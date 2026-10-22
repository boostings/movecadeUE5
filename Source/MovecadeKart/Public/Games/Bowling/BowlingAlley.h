#pragma once

#include "Art/KartMaterialCache.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Games/ArcadeVenueKit.h"
#include "Games/Bowling/BowlingPins.h"
#include "BowlingAlley.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class USpotLightComponent;
class UStaticMesh;

/**
 * The bowling house (movecade-v2 bowling/venue, laneLayout.ts; Docs/Port/06-bowling.md §4), modelled in code like
 * the tracks: five lanes (the player bowls the middle one), the approach and foul line, gutters and capping, the
 * pin deck and pit with neon-trimmed kickbacks, masking units and the pinsetter run, walls with neon runs, a trussed
 * ceiling with linear light troughs, the light hierarchy (the pin deck brightest, the approach warm), a grade that
 * holds the exposure, and a light show the game fires on strikes. Built in the editor too (OnConstruction).
 *
 * L_Bowling places one; ABowlingGameMode spawns one if the level has none. ABowlingGame borrows its pin and ball
 * meshes and drives SetShow every frame.
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
	/** The head pin's spot, down the lane. */
	static constexpr double DeckZ = -13.05;
	static constexpr double LaneVisualWidth = 3.15;
	/** Sim metres to lane units across (3.15 / 1.0668). */
	static constexpr double LaneVisualScale = 2.9528;
	/** Sim metres to rack units (visual pin spacing 0.68 / 0.3048). */
	static constexpr double VisualPinScale = MovecadeKart::Bowling::VisualUnitsPerMetre;

	/** Web world (x, y, z) to UE cm. */
	static FVector ToWorld(const FVector& Web);

	UStaticMesh* GetPinMesh() const { return PinMesh; }
	UStaticMesh* GetBallMesh() const { return BallMesh; }
	UMaterialInterface* GetPinMaterial(int32 Slot) const { return PinMaterials.IsValidIndex(Slot) ? PinMaterials[Slot].Get() : nullptr; }

	/**
	 * The house light show, driven by the game every frame: Energy 0 is the resting room; up to 1 the show spots
	 * sweep the deck in Tint and the neon pulses (a strike), past 1 a faster strobe (a turkey). Time is the show's
	 * age in seconds. Cheap when nothing changes: the resting state is applied once.
	 */
	void SetShow(double Energy, const FLinearColor& Tint, double Time);

private:
	void Build();
	void Clear();
	void BuildMeshes();
	void BuildPinMesh();
	void BuildBallMesh();
	void BuildGutterMesh();
	void BuildShell();
	void BuildLanes();
	void BuildLaneMarkings(double LaneX);
	void BuildGutters();
	void BuildDeck();
	void BuildMasking(double LaneX, UMaterialInterface* PanelArt);
	void BuildCeiling();
	void BuildNeon();
	void BuildHouseSign(double BackZ);
	void BuildLights();
	void BuildFillLights();
	void BuildTroughLights();
	void BuildShowSpots();
	void BuildGrade();
	void BuildHouseRacks();

	/** A box centred at C (web units) with size S (web x, y, z), optionally yawed about web Y. */
	void Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad = 0.0, bool bShadows = true);
	/** A box rolled in the plane of the back wall (about the lane axis), for neon drawn on it. */
	void WallBox(UMaterialInterface* Material, const FVector& C, const FVector& S, double RollRad);
	/** A cylinder along web Y. */
	void Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height);
	/** A neon colour whose glow the light show pulses: its own material, never shared with a steady look. */
	UMaterialInterface* Neon(const TCHAR* Key, const FLinearColor& Color);
	/** A movable light (or other part) under the root, tagged so Clear() finds it, named uniquely from Prefix. */
	template <class TComponent>
	TComponent* NewPart(const TCHAR* Prefix);
	void RegisterPart(USceneComponent* Part);

	void PulseNeon(double Energy, double Time, double Beat, bool bOn);
	void SweepShowSpots(double Energy, const FLinearColor& Tint, double Time, double Beat, bool bOn);

	/** One instanced component per (mesh, material, shadow): the house's few thousand boxes in a few dozen batches. */
	UPROPERTY(Transient)
	FArcadeInstanceGroups Groups;

	/** One material per look per Build(), so equal looks share a MID (and an instanced group). */
	UPROPERTY(Transient)
	FKartMaterialCache Materials;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PinMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BallMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> GutterMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> PinMaterials;

	/** The neon materials and their resting colours, which the show pulses. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> NeonMids;
	TArray<FLinearColor> NeonColors;

	/** The show spots (off at rest). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> ShowSpots;

	/** The deck key, which the show flashes. */
	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> DeckKey;

	/** The show energy last applied; -1 forces the next SetShow to apply. */
	double ShownEnergy = -1.0;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY()
	TObjectPtr<UStaticMesh> Cylinder;
};
