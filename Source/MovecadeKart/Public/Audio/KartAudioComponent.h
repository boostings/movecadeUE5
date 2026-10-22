#pragma once

#include "CoreMinimal.h"
#include "Audio/KartSoundObserver.h"
#include "Components/ActorComponent.h"
#include "KartAudioComponent.generated.h"

class UAudioComponent;
class UKartAudioSubsystem;
class UKartMovementComponent;

UENUM(BlueprintType)
enum class EKartAudioPhase : uint8
{
	/** Lobby, or the grid before the countdown: silent. */
	Waiting,
	/** Engine in neutral: idles, and revs to the limiter on the gas. */
	Countdown,
	Racing,
	/** Results: silent. */
	Results,
};

/**
 * The kart's sound, ported from the web build (games/kart/sound.ts, soundMix.ts, drivetrain.ts,
 * audio/engineVoice.ts, audioService.ts; spec Docs/Port/04 §9).
 *
 * Each tick it reads UKartMovementComponent::SimState() and the kart's input and drives:
 * - the engine: the fake 6-speed drivetrain (rpm, shifts, limiter, lift-off backfires) and the 5 on-throttle +
 *   2 off-throttle recorded layers, equal-power crossfaded by rpm and by load, each pitched by rpm / layer rpm;
 * - the loops: tire skid (drift or grip slide), wind (with its lowpass), off-road, boost, and the item-driven
 *   roulette, invincible aura and rocket (from SetStatus);
 * - one-shots from state changes: shift, backfire, drift-boost whoosh tier, boost, drift charge, wrong way,
 *   lap / final lap / finish, hop / land, fall / respawn, wall impacts (crash tier by impact strength), and the
 *   item edges (pickup, ready, invincible start, lightning, ink).
 *
 * Cue ids are the web's (kartCues.ts) names; PlayCue takes any of them:
 *   kart.shift  kart.backfire-a  kart.backfire-b  kart.drift-charge  kart.crash-light  kart.crash-medium
 *   kart.crash-heavy  kart.drift-boost-1  kart.drift-boost-2  kart.drift-boost-3  kart.boost  kart.collision
 *   kart.hop  kart.land  kart.fall  kart.respawn  kart.item-pickup  kart.item-ready  kart.projectile-launch
 *   kart.projectile-bounce  kart.projectile-hit  kart.hazard-drop  kart.spinout  kart.explosion  kart.lightning
 *   kart.ink  kart.invincible-start  kart.warning  kart.lap  kart.final-lap  kart.finish
 *   (loops, driven from state, not PlayCue: kart.engine-on-1000/2500/4000/5500/7000, kart.engine-off-2500/6500,
 *    kart.tire-skid, kart.offroad, kart.wind, kart.boost-loop, kart.item-roulette, kart.invincible-loop,
 *    kart.rocket-loop)
 *   Race-level cues without a kart: UKartAudioSubsystem::PlayCue / PlayCountdown (lobby.tick, lobby.go).
 *
 * Already derived here from the sim, so other systems must not also trigger them: shift, backfire, boost
 * and drift-boost tiers, drift charge, hop, land, fall, respawn, wall impacts, wrong way, lap, final lap, finish.
 * Expected from other systems via PlayCue: projectile-launch / hazard-drop / projectile-bounce / projectile-hit /
 * spinout / explosion (items), kart-to-kart bumps via PlayImpact (race, once kart contact exists).
 *
 * Debug: `kart.AudioDebug 1` logs rpm, gear, load, layer gains and loops 4x a second, and every one-shot.
 */
UCLASS(ClassGroup = (Movecade), meta = (BlueprintSpawnableComponent))
class MOVECADEKART_API UKartAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKartAudioComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * A one-shot for this kart (web playKartEvent): only while racing, not paused and not finished, at the
	 * cue's gain x Intensity / sqrt(karts). Returns true when it sounded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	bool PlayCue(FName CueId, float Intensity = 1.f);

	/** A crash or bump voiced by its strength (0..1, soundMix.ts impactStrength): crash-light/medium/heavy. */
	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	bool PlayImpact(float Strength);

	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	void SetRacePhase(EKartAudioPhase InPhase) { Phase = InPhase; }

	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	/** Seat order: spawn index (drivetrain seed, engine phase), lane among the karts sorted by spawn index, and their count. */
	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	void SetSeat(int32 InSpawnIndex, int32 InLane, int32 InNumKarts);

	/** Item and status flags from the items layer. */
	void SetStatus(const MovecadeKart::Audio::FKartAudioStatus& InStatus) { Status = InStatus; }

	/** Stop everything and start over, as for a new race. */
	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	void ResetAudio();

	/** Defaults to Racing until the race layer drives it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kart|Audio")
	EKartAudioPhase Phase = EKartAudioPhase::Racing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kart|Audio")
	bool bPaused = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kart|Audio")
	int32 SpawnIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kart|Audio")
	int32 Lane = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Kart|Audio")
	int32 NumKarts = 1;

	const MovecadeKart::Audio::FKartAudioCommands& LastCommands() const { return Commands; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FLoopVoice
	{
		FName Cue;
		double Gain = 0.0;
		double Rate = 1.0;
		double Lowpass = -1.0;
		double TargetGain = 0.0;
		/** The values last pushed to the UAudioComponent (pushed only on change). */
		float SentVolume = -1.f;
		float SentPitch = -1.f;
		double SentLowpass = -1.0;
	};

	void ApplyEngine(UKartAudioSubsystem& Mixer, double Dt);
	void StopEngine();
	void ApplyLoops(UKartAudioSubsystem& Mixer, double Dt);
	/** Stops a loop; its component is parked for that layer's next start (bDestroy: freed instead). */
	void StopLoop(int32 Index, bool bDestroy = false);
	void StopAll(bool bDestroy = false);
	UAudioComponent* MakeVoice(UKartAudioSubsystem& Mixer, FName Cue, float StartTime);
	void PlayOneShot(UKartAudioSubsystem& Mixer, const MovecadeKart::Audio::FOneShot& Shot);
	void LogDebug(double Now);

	UPROPERTY(Transient)
	TObjectPtr<UKartMovementComponent> Movement;

	/** 5 on-throttle then 2 off-throttle layers, while the engine runs. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> EngineVoices;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> LoopVoices;

	/** Stopped loop components kept per layer and replayed on its next start, instead of a new component each time. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> ParkedLoops;

	/** The engine layers' last pushed volume and pitch (pushed only on change). */
	float SentLayerVolume[MovecadeKart::Audio::NumOnLayers + MovecadeKart::Audio::NumOffLayers] = {};
	float SentLayerPitch[MovecadeKart::Audio::NumOnLayers + MovecadeKart::Audio::NumOffLayers] = {};

	MovecadeKart::Audio::FKartSoundObserver Observer;
	MovecadeKart::Audio::FKartAudioStatus Status;
	MovecadeKart::Audio::FKartAudioCommands Commands;
	MovecadeKart::Audio::FEngineTargets EngineTarget;

	/** The smoothed Web Audio graph values (setTargetAtTime), multiplied into each layer's UE volume. */
	double LayerGain[MovecadeKart::Audio::NumOnLayers + MovecadeKart::Audio::NumOffLayers] = {};
	double LayerRate[MovecadeKart::Audio::NumOnLayers + MovecadeKart::Audio::NumOffLayers] = {};
	double OnBus = 1.0;
	double OffBus = 1.0;
	double EngineOut = 0.0;
	bool bEngineAcquired = false;

	FLoopVoice Loops[static_cast<int32>(MovecadeKart::Audio::ELoopLayer::Count)];
	double NextDebugLog = 0.0;
};
