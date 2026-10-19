#pragma once

#include "CoreMinimal.h"
#include "Audio/KartAudioMath.h"
#include "Subsystems/WorldSubsystem.h"
#include "KartAudioSubsystem.generated.h"

class USoundConcurrency;
class USoundWave;

/**
 * The host mixer (web audioService.ts) shared by every kart in a world: the cue table and its SoundWaves,
 * the per-cue throttle, the 24-voice one-shot ring (a StopOldest concurrency), the 8-engine and 32-loop
 * caps, and the master/SFX bus (SFX 0.8 by default, 0 when muted).
 *
 * Everything plays 2D. The web pans each kart's lane by up to ±0.55; the kart recordings are mono and UE
 * has no 2D pan for mono sources, so pan is computed but not applied (it is 0 for a single kart anyway).
 */
UCLASS()
class MOVECADEKART_API UKartAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UKartAudioSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * One-shot at the cue's base gain x clamp(Gain, 0, 2), pitch clamp(Rate, 0.25, 4), subject to the
	 * cue's throttle. For race-level cues ("lobby.tick", "lobby.go", "kart.warning" at 0.6 when a shell
	 * launches); per-kart cues go through UKartAudioComponent::PlayCue. Returns true when it sounded.
	 */
	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	bool PlayCue(FName CueId, float Gain = 1.f, float Rate = 1.f);

	/** sound.ts race countdown: values 5..1 tick (rate 1.0, 1.1 at 2, 1.2 at 1); 0 plays GO. */
	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	void PlayCountdown(int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	void SetMuted(bool bInMuted) { bMuted = bInMuted; }
	UFUNCTION(BlueprintPure, Category = "Kart|Audio")
	bool IsMuted() const { return bMuted; }

	/** The SFX bus, 0..1 (web default 0.8). */
	UFUNCTION(BlueprintCallable, Category = "Kart|Audio")
	void SetSfxVolume(float Volume) { SfxVolume = FMath::Clamp(Volume, 0.f, 1.f); }

	/** master x SFX bus x the mono-upmix correction: what a Web Audio gain of 1 becomes as a UE volume. */
	float OutputGain() const;

	USoundWave* GetSound(const MovecadeKart::Audio::FCueDef& Cue);
	USoundWave* GetSound(FName CueId);

	bool TryAcquireEngine();
	void ReleaseEngine() { EngineCount = FMath::Max(0, EngineCount - 1); }
	bool TryAcquireLoop();
	void ReleaseLoop() { LoopCount = FMath::Max(0, LoopCount - 1); }

private:
	UPROPERTY()
	TMap<FName, TObjectPtr<USoundWave>> Sounds;

	UPROPERTY()
	TObjectPtr<USoundConcurrency> OneShotConcurrency;

	TSet<FName> MissingReported;
	MovecadeKart::Audio::FCueThrottle Throttle;
	bool bMuted = false;
	float SfxVolume = static_cast<float>(MovecadeKart::Audio::DefaultSfxVolume);
	float MonoCompensation = 1.f;
	int32 EngineCount = 0;
	int32 LoopCount = 0;
};
