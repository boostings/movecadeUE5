#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TrackLife.generated.h"

class ATrackBuilder;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UTextureRenderTarget2D;
namespace MovecadeKart { struct FTrack; }

/**
 * The living track: everything around the circuit that reacts to the race, without ever touching it. It reads
 * the karts' poses and the race's cues, and writes only to its own meshes and materials (Tools/ue/
 * make_track_life.py builds them), so the sim stays deterministic and -KartNoLife turns it all off.
 *
 * - Lawn grass: geometry tufts and flowers scattered over the sim's dirt cells (Violet's lawn verges, where karts
 *   cut corners). The wind rolls gusts across them, and every kart presses a trail into a world-space trample
 *   field (two ping-ponged render targets: each frame the field decays and each kart stamps a disc); the grass
 *   bends away and lies flat along the kart's path, reading as a lighter mown trail until it springs back.
 * - Crowds: the grandstand prefab rebuilt without its block people, and a spectator on every seat place. They
 *   watch the nearest kart, and jump and wave when one rushes past or on a race cue (GO, laps, the finish).
 * - Butterflies over the lawn flowers that scatter when a kart comes close, and drift home again.
 * - Fireworks over the start gate at GO, on a final lap and at every finish.
 * - Wind: the shared MPC_TrackLife wind also sways the instanced trees, blossoms, lavender and palms
 *   (M_TrackFoliageWind).
 *
 * ATrackBuilder spawns one at BeginPlay in game worlds. Test hook: -KartLifeDemo with -KartTrackCam sends a phantom
 * kart round the camera's look-at point, so a fixed screenshot shows the grass and butterflies reacting.
 */
UCLASS(NotPlaceable, Transient)
class MOVECADEKART_API ATrackLife : public AActor
{
	GENERATED_BODY()

public:
	ATrackLife();

	/** Builds everything for the builder's track. */
	void Init(ATrackBuilder* Builder);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	int32 NumGrassTufts() const { return GrassTufts; }
	int32 NumSpectators() const { return Spectators; }
	int32 NumButterflies() const { return Butterflies.Num(); }

	/** -KartNoLife turns the whole layer off (performance comparisons). */
	static bool IsEnabled();

	/** A firework shell over Cm: launched now, bursting Delay seconds later in Color. */
	void LaunchFirework(const FVector& Cm, const FLinearColor& Color, double Delay = 0.0);

private:
	void BuildGrass(ATrackBuilder* Builder, const MovecadeKart::FTrack& Track);
	void BuildCrowds(ATrackBuilder* Builder);
	void BuildButterflies(const MovecadeKart::FTrack& Track);
	void BindRace();

	struct FKartView
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		double Speed = 0.0;
		bool bOnDirt = false;
	};
	void GatherKarts(float DeltaSeconds);
	void UpdateShared(float DeltaSeconds);
	void UpdateTrample(float DeltaSeconds);
	void UpdateButterflies(float DeltaSeconds);
	void UpdateFireworks(float DeltaSeconds);
	void OnRaceCue(int32 Seat, FName Cue);
	void FireworkShow(int32 Shells, double Over);

	UPROPERTY()
	TObjectPtr<UMaterialParameterCollection> Collection;

	UPROPERTY()
	TArray<TObjectPtr<UTextureRenderTarget2D>> TrampleTargets;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DecayMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> StampMaterials;

	/** Materials that sample the trample field (the grass), repointed at the live target every frame. */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FieldReaders;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Grass;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Crowd;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ButterflyMesh;

	TArray<FKartView> Karts;
	/** The trample field's world rectangle (UE cm): min corner and edge length. */
	FVector2D FieldMin = FVector2D::ZeroVector;
	double FieldSize = 0.0;
	int32 LiveTarget = 0;
	bool bHasField = false;

	int32 GrassTufts = 0;
	int32 Spectators = 0;
	double Cheer = 0.0;
	double CheerHold = 0.0;

	struct FButterfly
	{
		FVector Home = FVector::ZeroVector;
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		double Scared = 0.0;
		double Phase = 0.0;
		float Rate = 14.f;
	};
	TArray<FButterfly> Butterflies;

	struct FShell
	{
		FVector Launch = FVector::ZeroVector;
		FVector Burst = FVector::ZeroVector;
		FLinearColor Color = FLinearColor::White;
		double Start = 0.0;
		double Rise = 1.0;
		bool bBurst = false;
		bool bCrackled = false;
		/** The rising shell's trail: an index into TrailStreams while it climbs. */
		int32 Trail = INDEX_NONE;
	};
	TArray<FShell> Shells;

	/** Spark streams for rising shells, reused: one stream follows each shell instead of a burst per frame. */
	UPROPERTY()
	TArray<TObjectPtr<class UNiagaraComponent>> TrailStreams;
	TArray<bool> TrailBusy;

	/** Trample time not yet applied: the render target updates at 30 Hz with the whole elapsed time. */
	float TrampleDt = 0.f;
	TArray<FTransform> ButterflyXforms;
	/** Where the shows go up: over the finish line, either side of it (UE cm, ground level). */
	FVector FinishCm = FVector::ZeroVector;
	FVector FinishRight = FVector::RightVector;
	bool bFinishFirework = false;
	bool bFinishShowDone = false;
	/** -KartLifeDemo (with -KartTrackCam): a phantom kart circles the camera's look-at point, for screenshots. */
	bool bDemo = false;
	FVector DemoCentre = FVector::ZeroVector;
	FDelegateHandle CueHandle;
	TWeakObjectPtr<class AKartRaceManager> Race;
	FRandomStream Rng;
};
