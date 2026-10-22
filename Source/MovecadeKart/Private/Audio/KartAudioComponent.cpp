#include "Audio/KartAudioComponent.h"

#include "Audio/KartAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "KartMovementComponent.h"
#include "Sound/SoundWave.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartAudio, Log, All);

using namespace MovecadeKart::Audio;

static TAutoConsoleVariable<int32> CVarKartAudioDebug(
	TEXT("kart.AudioDebug"), 0,
	TEXT("1: log each kart's drivetrain (rpm, gear, load), engine layer gains and loops 4x a second, and every one-shot, and show them on screen."));

namespace
{
	/** A hitch longer than this is smoothed as if it were this long. */
	constexpr double MaxFrameSeconds = 0.1;
	/** EngineVoice.dispose: a short fade, then stop, so a stop never clicks. */
	constexpr float StopFadeSeconds = 0.08f;
	/** What a command may ask of a voice: gain up to 2x, playback rate 0.25-4x, a lowpass between 40 Hz and 20 kHz. */
	constexpr double MaxGain = 2.0;
	constexpr double MinRate = 0.25;
	constexpr double MaxRate = 4.0;
	constexpr double MinLowpassHz = 40.0;
	constexpr double MaxLowpassHz = 20000.0;
	/** kart.AudioDebug logs four times a second. */
	constexpr double DebugLogSeconds = 0.25;

	constexpr int32 NumEngineLayers = NumOnLayers + NumOffLayers;
	const FName EngineCues[NumEngineLayers] = {
		TEXT("kart.engine-on-1000"), TEXT("kart.engine-on-2500"), TEXT("kart.engine-on-4000"), TEXT("kart.engine-on-5500"),
		TEXT("kart.engine-on-7000"), TEXT("kart.engine-off-2500"), TEXT("kart.engine-off-6500"),
	};
	const TCHAR* LoopNames[] = { TEXT("skid"), TEXT("wind"), TEXT("dirt"), TEXT("boost"), TEXT("roulette"), TEXT("aura"), TEXT("rocket") };
	static_assert(UE_ARRAY_COUNT(LoopNames) == static_cast<int32>(ELoopLayer::Count));

	// Each UAudioComponent setter queues a command to the audio thread, even for the value it already has. Engine
	// layers and loops settle to steady values (a held throttle, a cruise), so most frames would resend all of them.
	void SetVolume(UAudioComponent& Voice, float Volume)
	{
		if (Voice.VolumeMultiplier != Volume)
		{
			Voice.SetVolumeMultiplier(Volume);
		}
	}

	void SetPitch(UAudioComponent& Voice, float Pitch)
	{
		if (Voice.PitchMultiplier != Pitch)
		{
			Voice.SetPitchMultiplier(Pitch);
		}
	}

	ERacePhase ToRacePhase(EKartAudioPhase Phase)
	{
		switch (Phase)
		{
		case EKartAudioPhase::Waiting: return ERacePhase::Waiting;
		case EKartAudioPhase::Countdown: return ERacePhase::Countdown;
		case EKartAudioPhase::Results: return ERacePhase::Results;
		default: return ERacePhase::Racing;
		}
	}
}

UKartAudioComponent::UKartAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the movement component has stepped the sim this frame; and on pause, to stop the loops.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	for (double& Rate : LayerRate)
	{
		Rate = 1.0;
	}
}

void UKartAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	LoopVoices.SetNum(static_cast<int32>(ELoopLayer::Count));
	Movement = GetOwner() ? GetOwner()->FindComponentByClass<UKartMovementComponent>() : nullptr;
	if (Movement)
	{
		AddTickPrerequisiteComponent(Movement);
	}
	else
	{
		UE_LOG(LogKartAudio, Warning, TEXT("KartAudio: %s has no UKartMovementComponent; the kart stays silent."), *GetNameSafe(GetOwner()));
	}

	if (UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this))
	{
		int32 Loaded = 0;
		for (const FCueDef& Cue : AllCues())
		{
			Loaded += Mixer->GetSound(Cue) ? 1 : 0;
		}
		UE_LOG(LogKartAudio, Log, TEXT("KartAudio: %s ready, %d/%d cues loaded, output gain %.3f (SFX bus x mono upmix)"),
			*GetNameSafe(GetOwner()), Loaded, AllCues().Num(), Mixer->OutputGain());
	}
}

void UKartAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAll(/*bDestroy*/ true);
	Super::EndPlay(EndPlayReason);
}

void UKartAudioComponent::SetSeat(int32 InSpawnIndex, int32 InLane, int32 InNumKarts)
{
	SpawnIndex = FMath::Max(0, InSpawnIndex);
	Lane = FMath::Max(0, InLane);
	NumKarts = FMath::Max(1, InNumKarts);
}

void UKartAudioComponent::ResetAudio()
{
	StopAll();
	Observer.Reset();
}

void UKartAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this);
	if (!Movement || !Movement->HasSim() || !Mixer)
	{
		return;
	}

	FKartAudioContext Context;
	Context.Phase = ToRacePhase(Phase);
	Context.bPaused = bPaused || (GetWorld() && GetWorld()->IsPaused());
	Context.Lane = Lane;
	Context.NumKarts = NumKarts;
	Context.SpawnIndex = SpawnIndex;
	Context.FrameDt = DeltaTime;
	Commands = Observer.Observe(Context, Movement->SimState(), Movement->GetKartInput(), Status, Movement->GetTrack());

	if (Commands.bStopAll)
	{
		StopAll();
	}
	const double Dt = FMath::Clamp(static_cast<double>(DeltaTime), 0.0, MaxFrameSeconds);
	if (Commands.bEngine && !Mixer->IsMuted())
	{
		ApplyEngine(*Mixer, Dt);
	}
	else
	{
		StopEngine();
	}
	ApplyLoops(*Mixer, Dt);
	for (const FOneShot& Shot : Commands.Plays)
	{
		PlayOneShot(*Mixer, Shot);
	}

	if (CVarKartAudioDebug.GetValueOnGameThread() > 0)
	{
		LogDebug(GetWorld()->GetRealTimeSeconds());
	}
}

UAudioComponent* UKartAudioComponent::MakeVoice(UKartAudioSubsystem& Mixer, FName Cue, float StartTime)
{
	USoundWave* Wave = Mixer.GetSound(Cue);
	if (!Wave || !GetOwner())
	{
		return nullptr;
	}
	UAudioComponent* Voice = NewObject<UAudioComponent>(GetOwner(), NAME_None, RF_Transient);
	Voice->bAutoActivate = false;
	Voice->bAutoDestroy = false;
	Voice->bAllowSpatialization = false;
	Voice->bIsUISound = false;
	Voice->SetSound(Wave);
	Voice->RegisterComponent();
	Voice->SetVolumeMultiplier(0.f);
	Voice->SetPitchMultiplier(1.f);
	Voice->Play(StartTime);
	return Voice;
}

void UKartAudioComponent::ApplyEngine(UKartAudioSubsystem& Mixer, double Dt)
{
	if (EngineVoices.Num() == 0)
	{
		// audioService.ts setEngine: at most 8 engines, and only once every layer has loaded.
		for (const FName& Cue : EngineCues)
		{
			if (!Mixer.GetSound(Cue))
			{
				return;
			}
		}
		if (!Mixer.TryAcquireEngine())
		{
			return;
		}
		bEngineAcquired = true;
		for (const FName& Cue : EngineCues)
		{
			// Karts start at different points in the loop so a full grid never phases.
			const double Duration = Mixer.GetSound(Cue)->Duration;
			const float Start = static_cast<float>(FMath::Fmod(Commands.EngineCharacter * Duration, FMath::Max(0.001, Duration)));
			EngineVoices.Add(MakeVoice(Mixer, Cue, Start));
		}
		// A fresh EngineVoice: layer gains 0, rates 1, buses at the GainNode default 1, output 0.
		for (int32 i = 0; i < NumEngineLayers; ++i)
		{
			LayerGain[i] = 0.0;
			LayerRate[i] = 1.0;
		}
		OnBus = OffBus = 1.0;
		EngineOut = 0.0;
	}

	EngineTarget = EngineTargets(Commands.EngineRpm, Commands.EngineLoad, EngineGain * FMath::Clamp(Commands.EngineMix, 0.0, MaxGain));
	OnBus = Approach(OnBus, EngineTarget.OnBus, EngineBusTau, Dt);
	OffBus = Approach(OffBus, EngineTarget.OffBus, EngineBusTau, Dt);
	EngineOut = Approach(EngineOut, EngineTarget.Out, EngineOutTau, Dt);
	const float Output = Mixer.OutputGain();
	for (int32 i = 0; i < NumEngineLayers; ++i)
	{
		const bool bOn = i < NumOnLayers;
		const double GainTarget = bOn ? EngineTarget.OnGain[i] : EngineTarget.OffGain[i - NumOnLayers];
		const double RateTarget = bOn ? EngineTarget.OnRate[i] : EngineTarget.OffRate[i - NumOnLayers];
		LayerGain[i] = Approach(LayerGain[i], GainTarget, EngineLayerGainTau, Dt);
		LayerRate[i] = Approach(LayerRate[i], RateTarget, EngineLayerRateTau, Dt);
		if (UAudioComponent* Voice = EngineVoices.IsValidIndex(i) ? EngineVoices[i].Get() : nullptr)
		{
			SetVolume(*Voice, static_cast<float>(LayerGain[i] * (bOn ? OnBus : OffBus) * EngineOut * Output));
			SetPitch(*Voice, static_cast<float>(LayerRate[i]));
		}
	}
}

void UKartAudioComponent::StopEngine()
{
	for (const TObjectPtr<UAudioComponent>& Voice : EngineVoices)
	{
		if (IsValid(Voice))
		{
			// EngineVoice.dispose: a short fade, then stop, so a stop never clicks.
			Voice->bAutoDestroy = true;
			Voice->FadeOut(StopFadeSeconds, 0.f);
		}
	}
	EngineVoices.Reset();
	if (bEngineAcquired)
	{
		bEngineAcquired = false;
		if (UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this))
		{
			Mixer->ReleaseEngine();
		}
	}
}

void UKartAudioComponent::ApplyLoops(UKartAudioSubsystem& Mixer, double Dt)
{
	const float Output = Mixer.OutputGain();
	for (int32 i = 0; i < static_cast<int32>(ELoopLayer::Count); ++i)
	{
		const FLoopCommand& Command = Commands.Loops[i];
		const FCueDef* Cue = Command.bActive ? FindCue(Command.Cue) : nullptr;
		if (!Cue || Mixer.IsMuted())
		{
			StopLoop(i);
			continue;
		}
		FLoopVoice& Loop = Loops[i];
		if (!LoopVoices.IsValidIndex(i))
		{
			LoopVoices.SetNum(static_cast<int32>(ELoopLayer::Count));
		}
		if (!LoopVoices[i] || Loop.Cue != Command.Cue)
		{
			StopLoop(i);
			if (!Mixer.TryAcquireLoop())
			{
				continue;
			}
			// Replay this layer's parked component if there is one (same settings as a fresh MakeVoice).
			UAudioComponent* Voice = nullptr;
			if (ParkedLoops.IsValidIndex(i) && IsValid(ParkedLoops[i]))
			{
				if (USoundWave* Wave = Mixer.GetSound(Command.Cue))
				{
					Voice = ParkedLoops[i];
					ParkedLoops[i] = nullptr;
					if (Voice->Sound != Wave)
					{
						Voice->SetSound(Wave);
					}
					Voice->SetLowPassFilterEnabled(false);
					Voice->SetVolumeMultiplier(0.f);
					Voice->SetPitchMultiplier(1.f);
					Voice->Play(0.f);
				}
			}
			else
			{
				Voice = MakeVoice(Mixer, Command.Cue, 0.f);
			}
			if (!Voice)
			{
				Mixer.ReleaseLoop();
				continue;
			}
			LoopVoices[i] = Voice;
			// A new source: gain 0, rate 1, and a filter only when the first frame asks for one.
			Loop = FLoopVoice();
			Loop.Cue = Command.Cue;
			if (Command.Lowpass >= 0.0)
			{
				Loop.Lowpass = FMath::Clamp(Command.Lowpass, MinLowpassHz, MaxLowpassHz);
				Voice->SetLowPassFilterEnabled(true);
				Voice->SetLowPassFilterFrequency(static_cast<float>(Loop.Lowpass));
			}
		}
		Loop.TargetGain = Cue->Gain * FMath::Clamp(Command.Gain, 0.0, MaxGain);
		Loop.Gain = Approach(Loop.Gain, Loop.TargetGain, LoopGainTau, Dt);
		Loop.Rate = Approach(Loop.Rate, FMath::Clamp(Command.Rate, MinRate, MaxRate), LoopRateTau, Dt);
		UAudioComponent* Voice = LoopVoices[i];
		SetVolume(*Voice, static_cast<float>(Loop.Gain * Output));
		SetPitch(*Voice, static_cast<float>(Loop.Rate));
		if (Loop.Lowpass >= 0.0 && Command.Lowpass >= 0.0)
		{
			// The component keeps no copy of its filter frequency; the last one sent is Loop.Lowpass.
			const double Lowpass = Approach(Loop.Lowpass, FMath::Clamp(Command.Lowpass, MinLowpassHz, MaxLowpassHz), LoopLowpassTau, Dt);
			if (static_cast<float>(Lowpass) != static_cast<float>(Loop.Lowpass))
			{
				Voice->SetLowPassFilterFrequency(static_cast<float>(Lowpass));
			}
			Loop.Lowpass = Lowpass;
		}
	}
}

void UKartAudioComponent::StopLoop(int32 Index, bool bDestroy)
{
	if (bDestroy && ParkedLoops.IsValidIndex(Index) && ParkedLoops[Index])
	{
		if (IsValid(ParkedLoops[Index]))
		{
			ParkedLoops[Index]->DestroyComponent();
		}
		ParkedLoops[Index] = nullptr;
	}
	if (!LoopVoices.IsValidIndex(Index) || !LoopVoices[Index])
	{
		return;
	}
	if (IsValid(LoopVoices[Index]))
	{
		LoopVoices[Index]->Stop();
		if (bDestroy)
		{
			LoopVoices[Index]->DestroyComponent();
		}
		else
		{
			// Keep the component for this layer's next start (loops start and stop many times a lap).
			ParkedLoops.SetNum(FMath::Max(ParkedLoops.Num(), static_cast<int32>(ELoopLayer::Count)));
			if (ParkedLoops[Index] && IsValid(ParkedLoops[Index]))
			{
				ParkedLoops[Index]->DestroyComponent();
			}
			ParkedLoops[Index] = LoopVoices[Index];
		}
	}
	LoopVoices[Index] = nullptr;
	Loops[Index] = FLoopVoice();
	if (UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this))
	{
		Mixer->ReleaseLoop();
	}
}

void UKartAudioComponent::StopAll(bool bDestroy)
{
	StopEngine();
	for (int32 i = 0; i < FMath::Max(LoopVoices.Num(), ParkedLoops.Num()); ++i)
	{
		StopLoop(i, bDestroy);
	}
}

void UKartAudioComponent::PlayOneShot(UKartAudioSubsystem& Mixer, const FOneShot& Shot)
{
	const bool bPlayed = Mixer.PlayCue(Shot.Cue, static_cast<float>(Shot.Gain), static_cast<float>(Shot.Rate));
	if (CVarKartAudioDebug.GetValueOnGameThread() > 0)
	{
		const FCueDef* Cue = FindCue(Shot.Cue);
		UE_LOG(LogKartAudio, Display, TEXT("KartAudio[%d] t=%.2f %s %s gain %.3f (x%.2f base) rate %.3f gear %d rpm %.0f"),
			SpawnIndex, Movement ? Movement->SimState().Time : 0.0, bPlayed ? TEXT("play") : TEXT("throttled"), *Shot.Cue.ToString(),
			Shot.Gain, Cue ? Cue->Gain : 0.f, Shot.Rate, Commands.Drivetrain.Gear, Commands.Drivetrain.Rpm);
	}
}

bool UKartAudioComponent::PlayCue(FName CueId, float Intensity)
{
	UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this);
	const bool bPausedNow = bPaused || (GetWorld() && GetWorld()->IsPaused());
	if (!Mixer || Phase != EKartAudioPhase::Racing || bPausedNow || (Movement && Movement->HasSim() && Movement->SimState().bFinished))
	{
		return false;
	}
	FOneShot Shot;
	Shot.Cue = CueId;
	Shot.Gain = Intensity / FMath::Sqrt(static_cast<double>(FMath::Max(1, NumKarts)));
	Shot.Pan = LaneToPan(Lane, NumKarts);
	const bool bPlayed = Mixer->PlayCue(Shot.Cue, static_cast<float>(Shot.Gain), 1.f);
	if (CVarKartAudioDebug.GetValueOnGameThread() > 0)
	{
		UE_LOG(LogKartAudio, Display, TEXT("KartAudio[%d] cue %s %s gain %.3f"), SpawnIndex, bPlayed ? TEXT("play") : TEXT("dropped"), *CueId.ToString(), Shot.Gain);
	}
	return bPlayed;
}

bool UKartAudioComponent::PlayImpact(float Strength)
{
	UKartAudioSubsystem* Mixer = UKartAudioSubsystem::Get(this);
	const bool bPausedNow = bPaused || (GetWorld() && GetWorld()->IsPaused());
	if (!Mixer || Phase != EKartAudioPhase::Racing || bPausedNow || (Movement && Movement->HasSim() && Movement->SimState().bFinished))
	{
		return false;
	}
	double TierGain = 1.0;
	const EImpactTier Tier = ImpactTier(Strength, TierGain);
	const double Gain = TierGain / FMath::Sqrt(static_cast<double>(FMath::Max(1, NumKarts)));
	return Mixer->PlayCue(ImpactCueId(Tier), static_cast<float>(Gain), static_cast<float>(ImpactRate(Strength)));
}

void UKartAudioComponent::LogDebug(double Now)
{
	if (Now < NextDebugLog)
	{
		return;
	}
	NextDebugLog = Now + DebugLogSeconds;
	const FDrivetrainFrame& D = Commands.Drivetrain;
	const MovecadeKart::FKartState& S = Movement->SimState();
	// The Web Audio graph's per-layer output: layer gain x bus x engine output (before the SFX bus).
	double Layer[NumEngineLayers];
	for (int32 i = 0; i < NumEngineLayers; ++i)
	{
		Layer[i] = EngineVoices.Num() > 0 ? LayerGain[i] * (i < NumOnLayers ? OnBus : OffBus) * EngineOut : 0.0;
	}
	FString LoopText;
	for (int32 i = 0; i < static_cast<int32>(ELoopLayer::Count); ++i)
	{
		if (LoopVoices.IsValidIndex(i) && LoopVoices[i])
		{
			LoopText += FString::Printf(TEXT(" %s=%.3f@%.2f"), LoopNames[i], Loops[i].Gain, Loops[i].Rate);
			if (Loops[i].Lowpass >= 0.0)
			{
				LoopText += FString::Printf(TEXT("/lp%.0f"), Loops[i].Lowpass);
			}
		}
	}
	const FString Line = FString::Printf(
		TEXT("KartAudio[%d] t=%.2f v=%.1f gear=%d rpm=%.0f load=%+.2f thr=%d lim=%d | on1000..7000=[%.3f %.3f %.3f %.3f %.3f] off2500/6500=[%.3f %.3f] bus on/off=%.2f/%.2f out=%.3f pitch=[%.2f %.2f %.2f %.2f %.2f|%.2f %.2f] | loops:%s"),
		SpawnIndex, S.Time, S.Speed, D.Gear, D.Rpm, D.Load, D.bThrottle ? 1 : 0, D.bLimiter ? 1 : 0,
		Layer[0], Layer[1], Layer[2], Layer[3], Layer[4], Layer[5], Layer[6], OnBus, OffBus, EngineOut,
		LayerRate[0], LayerRate[1], LayerRate[2], LayerRate[3], LayerRate[4], LayerRate[5], LayerRate[6],
		LoopText.IsEmpty() ? TEXT(" none") : *LoopText);
	UE_LOG(LogKartAudio, Display, TEXT("%s"), *Line);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0x4B41525400ull + SpawnIndex), 0.5f, FColor::Cyan, Line);
	}
}
