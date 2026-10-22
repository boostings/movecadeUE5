#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FX/KartFx.h"
#include "KartFxComponent.generated.h"

class AKartPawn;
class AKartRaceManager;
class UKartFxSubsystem;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;

namespace MovecadeKart
{
	struct FKartState;
	struct FKartItemSlot;
}

/**
 * Cosmetic item states forced by -KartFxShowcase (or anything else that wants to preview an effect) without
 * touching the item world. Times are world seconds.
 */
struct FKartFxOverrides
{
	double PrismUntil = -1.0;
	double RocketUntil = -1.0;
	double InkStart = -1.0;
	double InkUntil = -1.0;
	double FlashStart = -1.0;
};

/**
 * One kart's continuous race VFX and its view's screen effects, driven every frame from its sim state and its item
 * slot (KartFxLayer.tsx useFrame, Kart.jsx FX section, kartTrails.ts), plus the AAA pass's additions:
 * - boost flames and sparks from the exhausts (riding the sprung chassis) and a gout on every mini-turbo,
 * - drift smoke, tier-coloured drift sparks and wheel glows (blue, orange, purple) with a flick on each level-up,
 * - tyre marks (instanced quads on the road) while drifting or cornering hard, off-road dust, cruise exhaust puffs,
 * - landing puffs after a hop, a chevron flash and burst on boost pads,
 * - wall-scrape and kart-bump sparks, once per sim event (FKartState event counters), with camera shake,
 * - the prism's rainbow shimmer shell and twinkles, the rocket's shell, afterburner and smoke,
 * - speed lines on this kart's own camera at high speed,
 * - the per-view post-process: prism rainbow, hit flash, lightning flash, explosion flash and ink.
 * Anchors (exhausts, rear wheels) are measured from whatever vehicle the kart wears, so every body works.
 * AKartPawn creates it; nothing here feeds back into the sim.
 *
 * Source files: KartFxComponent.cpp (setup, anchors, the frame), KartFxDrive.cpp (boost, exhaust, drift, dust,
 * skids, speed lines), KartFxEvents.cpp (impacts, respawn, landing, boost pads) and KartFxItems.cpp (prism, rocket
 * and the screen post-process).
 */
UCLASS(ClassGroup = (Movecade), meta = (BlueprintSpawnableComponent))
class MOVECADEKART_API UKartFxComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKartFxComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** A one-off screen flash on this kart's view (kartFxBus flash: peak * (1 - t)^2 over 0.28 s). */
	void Flash(double Peak, const FLinearColor& Color);
	/** The lightning flash on this kart's view (two quick strobes over 0.45 s). */
	void Zap();

	FKartFxOverrides& Overrides() { return Showcase; }

protected:
	virtual void BeginPlay() override;

private:
	struct FAnchors
	{
		/** In ChassisPivot space. */
		FVector Exhaust[2] = { FVector::ZeroVector, FVector::ZeroVector };
		/** Rear wheel contact patches in VehicleRoot space. */
		FVector Wheel[2] = { FVector::ZeroVector, FVector::ZeroVector };
		double WheelRadius = 25.0;
		bool bValid = false;
	};

	// Setup (KartFxComponent.cpp).
	void Build();
	void BuildRocket(UKartFxSubsystem& Fx);
	void BuildGlows();
	void BuildSkidMarks();
	UStaticMeshComponent* MakeFxMesh(USceneComponent* Parent, FName Name, UStaticMesh* Mesh);
	void RefreshAnchors();
	void PlaceStreams();
	bool IsViewed() const;
	FVector Velocity() const { return LastVelocity; }
	/** The road under the middle of the rear axle, in world space. */
	FVector RearGround() const;

	// Driving (KartFxDrive.cpp).
	void UpdateDriveFx(const MovecadeKart::FKartState& S, double Time, bool bBoosting, bool bRocket);
	void UpdateBoostStreams(const MovecadeKart::FKartState& S, const FVector& Back, bool bBoosting, bool bRocket);
	void UpdateExhaustPuffs(double Speed, const FVector& Back, bool bBoosting, bool bRocket);
	/** Drift smoke, sparks and wheel glows; returns the spark recipe the drift flourishes build on. */
	MovecadeKart::Fx::FKartFxEmit UpdateDrift(const MovecadeKart::FKartState& S, double Time, const FVector& Back, const MovecadeKart::Fx::FDriftLevel& Level);
	void EmitDriftFlourishes(const MovecadeKart::FKartState& S, const FVector& Back, const MovecadeKart::Fx::FDriftLevel& Level, const MovecadeKart::Fx::FKartFxEmit& Spark);
	void UpdateDust(const MovecadeKart::FKartState& S, const FVector& Back);
	void UpdateSkids(const MovecadeKart::FKartState& S, double Time);
	void UpdateSpeedLines(const MovecadeKart::FKartState& S, bool bRocket);

	// One-off events from the sim's counters (KartFxEvents.cpp).
	void UpdateEvents(const MovecadeKart::FKartState& S, double Time);
	void EmitImpacts(const MovecadeKart::FKartState& S, const FVector& Ground, TArray<MovecadeKart::Fx::FKartFxEmit>& Out);
	void EmitPadBoosts(const MovecadeKart::FKartState& S, double Time, const FVector& Ground, TArray<MovecadeKart::Fx::FKartFxEmit>& Out);
	UStaticMeshComponent* PadFlash(int32 Pad, int32 PadCount);
	void UpdatePadFlashes(double Time);

	// Items and the screen (KartFxItems.cpp).
	void UpdateItemFx(double Time, bool bPrism, bool bRocket);
	void UpdateScreen(double Time, const MovecadeKart::FKartItemSlot* Slot, double ItemNow, bool bPrism);

	TWeakObjectPtr<AKartRaceManager> Race;

	UPROPERTY()
	TObjectPtr<USceneComponent> ChassisPivot;

	UPROPERTY()
	TObjectPtr<USceneComponent> VehicleRoot;

	UPROPERTY()
	TObjectPtr<USceneComponent> SlipPivot;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> BoostFire;   // left, right exhaust

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> BoostCore;   // hot core inside each flame

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> BoostSparks;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> ExhaustPuff;

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> WheelSmoke;  // left, right rear wheel: drift smoke

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> WheelDust;   // left, right rear wheel: off-road dust

	UPROPERTY()
	TArray<TObjectPtr<UNiagaraComponent>> WheelSparks; // left, right rear wheel

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> AuraFlares;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> RocketFire;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> RocketCore;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> RocketSmoke;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> SpeedLines;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> WheelGlow;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> AuraShell;

	/** The prism shell's size round the kart (a 1 m sphere, scaled), before it breathes. */
	FVector AuraShellScale = FVector(3.0, 2.3, 1.9);

	/** Tyre marks laid on the road (world space quads, reused oldest first). */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> SkidMarks;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SkidMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Rocket;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GlowMaterials;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> AuraMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;

	/** Pad activation flashes: a chevron plane per pad (by pad index), faded out over half a second. */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PadFlashes;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PadMaterials;

	TArray<double> PadFlashStart;
	TArray<double> LastPadBoost;

	FVector LastMark[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bMarking[2] = { false, false };
	int32 NextSkid = 0;

	FAnchors Anchors;
	TWeakObjectPtr<UStaticMesh> AnchoredBody;
	TWeakObjectPtr<UStaticMeshComponent> BodyPart;
	bool bBuilt = false;
	FVector LastLocation = FVector::ZeroVector;
	FVector LastVelocity = FVector::ZeroVector;
	bool bSeen = false;
	uint32 LastDriftBoostCount = 0;
	uint32 SeenBarrierEvents = 0;
	uint32 SeenBumpEvents = 0;
	uint32 SeenTeleports = 0;
	bool bWasHopping = false;
	double HopPeakSpeed = 0.0;
	MovecadeKart::Fx::FDriftLevel LastDriftLevel;
	double FlashStart = -1000.0;
	double FlashPeak = 0.0;
	FLinearColor FlashTint = FLinearColor::White;
	double ZapStart = -1000.0;
	FKartFxOverrides Showcase;
};
