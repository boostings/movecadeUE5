#include "Audio/KartAudioSubsystem.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWave.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartAudio, Log, All);

using namespace MovecadeKart::Audio;

UKartAudioSubsystem* UKartAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UKartAudioSubsystem>() : nullptr;
}

void UKartAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The web's one-shot ring: 24 voices, reusing the oldest.
	OneShotConcurrency = NewObject<USoundConcurrency>(this, TEXT("KartOneShots"));
	OneShotConcurrency->Concurrency.MaxCount = VoiceCount;
	OneShotConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
	OneShotConcurrency->Concurrency.bLimitToOwner = false;

	// A mono source on a Web Audio StereoPanner at pan 0 reaches each speaker at cos(pi/4) = 0.707. UE splits a
	// 2D mono source by the project's upmix method, so scale to land on the same per-channel level.
	switch (GetDefault<UAudioSettings>()->MonoChannelUpmixMethod)
	{
	case EMonoChannelUpmixMethod::Linear: MonoCompensation = 0.70710678f / 0.5f; break;
	case EMonoChannelUpmixMethod::FullVolume: MonoCompensation = 0.70710678f; break;
	default: MonoCompensation = 1.f; break;
	}
}

float UKartAudioSubsystem::OutputGain() const
{
	return bMuted ? 0.f : SfxVolume * MonoCompensation;
}

USoundWave* UKartAudioSubsystem::GetSound(const FCueDef& Cue)
{
	const FName Id(Cue.Id);
	if (const TObjectPtr<USoundWave>* Found = Sounds.Find(Id))
	{
		return *Found;
	}
	if (MissingReported.Contains(Id))
	{
		return nullptr; // looked for once already: don't hit the asset registry every tick
	}
	const FString Path = FString::Printf(TEXT("/Game/Audio/Kart/%s.%s"), Cue.Asset, Cue.Asset);
	USoundWave* Wave = LoadObject<USoundWave>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Wave)
	{
		MissingReported.Add(Id);
		UE_LOG(LogKartAudio, Error, TEXT("KartAudio: missing SoundWave %s for cue %s"), *Path, Cue.Id);
		return nullptr;
	}
	Sounds.Add(Id, Wave);
	return Wave;
}

USoundWave* UKartAudioSubsystem::GetSound(FName CueId)
{
	const FCueDef* Cue = FindCue(CueId);
	return Cue ? GetSound(*Cue) : nullptr;
}

bool UKartAudioSubsystem::PlayCue(FName CueId, float Gain, float Rate)
{
	const FCueDef* Cue = FindCue(CueId);
	if (!Cue)
	{
		if (!MissingReported.Contains(CueId))
		{
			MissingReported.Add(CueId);
			UE_LOG(LogKartAudio, Warning, TEXT("KartAudio: unknown cue %s"), *CueId.ToString());
		}
		return false;
	}
	if (bMuted)
	{
		return false;
	}
	USoundWave* Wave = GetSound(*Cue);
	if (!Wave || !Throttle.TryPlay(*Cue, FPlatformTime::Seconds()))
	{
		return false;
	}
	const float Volume = Cue->Gain * FMath::Clamp(Gain, 0.f, 2.f) * OutputGain();
	const float Pitch = FMath::Clamp(Rate, 0.25f, 4.f);
	UGameplayStatics::PlaySound2D(this, Wave, Volume, Pitch, 0.f, OneShotConcurrency, nullptr, /*bIsUISound*/ false);
	return true;
}

void UKartAudioSubsystem::PlayCountdown(int32 Value)
{
	if (Value > 0)
	{
		PlayCue(TEXT("lobby.tick"), 1.f, static_cast<float>(CountdownRate(Value)));
	}
	else
	{
		PlayCue(TEXT("lobby.go"));
	}
}

bool UKartAudioSubsystem::TryAcquireEngine()
{
	if (EngineCount >= MaxEngines)
	{
		return false;
	}
	++EngineCount;
	return true;
}

bool UKartAudioSubsystem::TryAcquireLoop()
{
	// Four players, eight possible sustained layers each; refuse runaway callers.
	if (LoopCount >= MaxLoops)
	{
		return false;
	}
	++LoopCount;
	return true;
}
