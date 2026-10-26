#include "Platform/Mobile/MobileLifecycleSubsystem.h"

#include "Async/Async.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Games/ArcadeGame.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/SArcadeOverlay.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "KartGameMode.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "MoviePlayer.h"
#include "Net/RoomHostSubsystem.h"
#include "Platform/Mobile/MobileDebugFlags.h"
#include "Platform/Mobile/MobileHaptics.h"
#include "Platform/Mobile/MobilePrefs.h"
#include "Platform/Mobile/SMobileBootSplash.h"
#include "Platform/Mobile/SMobileStatusOverlay.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"
#include "Render/KartGameUserSettings.h"
#include "ShaderPipelineCache.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Store/KartStoreSubsystem.h"
#include "UI/KartPlayerPrefs.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobileLifecycle, Log, All);

using namespace MovecadeKart::Mobile;

namespace
{
	/** The world, room and policy poll; the OS events themselves are handled as they come. */
	constexpr double PollSeconds = 0.25;
	/** iOS asks SCNetworkReachability synchronously: once a second is plenty to catch a switch. */
	constexpr double NetworkPollSeconds = 1.0;
	/** Above every screen and the arcade/race overlays. */
	constexpr int32 OverlayZOrder = 1000;
	/** The race pause reason while the app is away in Party Mode (the race HUD shows its room-pause card). */
	const TCHAR* const AppPauseReason = TEXT("app-background");
	const TCHAR* const LocalPauseReason = TEXT("local");
	const TCHAR* const MusicClassPath = TEXT("/Game/UI/Audio/SC_Music.SC_Music");
	constexpr float MusicDuckFadeSeconds = 0.3f;
	/** Mobile.Sim warmup=N: pipelines "compiled" per second. */
	constexpr double SimulatedWarmupRate = 60.0;

	const FName ThrowCues[] = { TEXT("kart.projectile-launch"), TEXT("kart.hazard-drop") };

	/** The local player's controller in RaceSeat, or null for a bot, a phone or a bad seat. */
	APlayerController* LocalViewerOf(const AKartRaceManager& Race, int32 RaceSeat)
	{
		const TArray<FKartRacer>& Racers = Race.GetRacers();
		return Racers.IsValidIndex(RaceSeat) && Racers[RaceSeat].Kind == EKartSeatKind::Local ? Racers[RaceSeat].Viewer.Get() : nullptr;
	}

	bool IsRacing(const AKartRaceManager& Race)
	{
		return Race.GetPhase() == EKartRacePhase::Countdown || Race.GetPhase() == EKartRacePhase::Racing;
	}

	bool IsRoomPaused(const UGameInstance* GameInstance)
	{
		const URoomHostSubsystem* Room = GameInstance ? GameInstance->GetSubsystem<URoomHostSubsystem>() : nullptr;
		return Room && Room->GetRoom().Status == EMcRoomStatus::Paused;
	}

#if !UE_BUILD_SHIPPING
	void RunSimConsoleCommand(const TArray<FString>& Args, UWorld* World)
	{
		UMobileLifecycleSubsystem* Mobile = UMobileLifecycleSubsystem::Get(World);
		const FString Command = FString::Join(Args, TEXT(" "));
		if (!Mobile || !Mobile->RunSimCommand(Command))
		{
			UE_LOG(LogMobileLifecycle, Warning, TEXT("Mobile.Sim %s: not a command (background, foreground, deactivate, reactivate, interrupt, interrupt-end, focus-lost, focus, music, music-end, unplug, plug, network=none|wifi|cell|airplane)"), *Command);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GMobileSimCommand(TEXT("Mobile.Sim"),
		TEXT("Simulates a phone OS event: background, foreground, deactivate, reactivate, interrupt, interrupt-end, focus-lost, focus, music, music-end, unplug, plug, network=none|wifi|cell|airplane, warmup=N"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSimConsoleCommand));

	FAutoConsoleCommand GMobileHapticsCommand(TEXT("Mobile.Haptics"), TEXT("Mobile.Haptics 0|1: the player's haptics switch (saved)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() > 0)
			{
				FHaptics::SetEnabled(Args[0].ToBool());
			}
			UE_LOG(LogMobileLifecycle, Display, TEXT("Haptics %s"), FHaptics::IsEnabled() ? TEXT("on") : TEXT("off"));
		}));
#endif
}

UMobileLifecycleSubsystem* UMobileLifecycleSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UMobileLifecycleSubsystem>() : nullptr;
}

bool UMobileLifecycleSubsystem::IsFirstLaunch()
{
	return !GetDefault<UMobilePrefs>()->bLaunchedBefore;
}

void UMobileLifecycleSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StartTime = FPlatformTime::Seconds();
	bPhoneBehaviour = IsPhoneBehaviourOn();
	if (bPhoneBehaviour)
	{
		BindPlatformDelegates();
		Recovery.NoteNetwork(FPlatformMisc::GetNetworkConnectionType(), Now());
		if (!GIsEditor && IsMoviePlayerEnabled() && GetMoviePlayer())
		{
			PrepareLoadingScreenHandle = GetMoviePlayer()->OnPrepareLoadingScreen().AddUObject(this, &UMobileLifecycleSubsystem::PrepareLoadingScreen);
		}
		UE_LOG(LogMobileLifecycle, Log, TEXT("Phone behaviour on%s (network %s)"), FMobileDebugFlags::Get().bSimulate ? TEXT(" (-MobileSim)") : TEXT(""),
			LexToString(Recovery.GetNetwork()));
	}
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMobileLifecycleSubsystem::Tick));
}

void UMobileLifecycleSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	UnbindPlatformDelegates();
	if (PrepareLoadingScreenHandle.IsValid() && GetMoviePlayer())
	{
		GetMoviePlayer()->OnPrepareLoadingScreen().Remove(PrepareLoadingScreenHandle);
	}
	if (AKartRaceManager* Race = HapticRace.Get())
	{
		Race->OnHaptic.Remove(RaceHapticHandle);
		Race->OnCue.Remove(RaceCueHandle);
	}
	RemoveOverlay();
	if (bMutedAll && GEngine)
	{
		if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
		{
			Device->SetTransientPrimaryVolume(1.f);
		}
	}
	Super::Deinitialize();
}

// Binding to the OS ----------------------------------------------------------------------------------------

void UMobileLifecycleSubsystem::BindPlatformDelegates()
{
	if (bDelegatesBound)
	{
		return;
	}
	bDelegatesBound = true;
	auto BindVoid = [this](TMulticastDelegate<void()>& Delegate, EAppEvent Event)
	{
		VoidHandles.Emplace(&Delegate, Delegate.AddWeakLambda(this, [this, Event]() { QueueAppEvent(Event); }));
	};
	BindVoid(FCoreDelegates::ApplicationWillDeactivateDelegate, EAppEvent::WillDeactivate);
	BindVoid(FCoreDelegates::ApplicationHasReactivatedDelegate, EAppEvent::HasReactivated);
	BindVoid(FCoreDelegates::ApplicationWillEnterBackgroundDelegate, EAppEvent::WillEnterBackground);
	BindVoid(FCoreDelegates::ApplicationHasEnteredForegroundDelegate, EAppEvent::HasEnteredForeground);
	// Terminate saves where it is called: a task queued for later may never run.
	TerminateHandle = FCoreDelegates::GetApplicationWillTerminateDelegate().AddWeakLambda(this, [this]()
	{
		if (IsInGameThread())
		{
			HandleAppEvent(EAppEvent::WillTerminate);
		}
	});
	AudioInterruptHandle = FCoreDelegates::AudioInterruptionDelegate.AddWeakLambda(this, [this](bool bBegan)
	{
		QueueAppEvent(bBegan ? EAppEvent::AudioInterruptBegan : EAppEvent::AudioInterruptEnded);
	});
	AudioFocusHandle = FCoreDelegates::AudioFocusChangedDelegate.AddWeakLambda(this, [this](bool bHasFocus)
	{
		QueueAppEvent(bHasFocus ? EAppEvent::AudioFocusGained : EAppEvent::AudioFocusLost);
	});
	// iOS: true when the OS takes the music over (another app plays), false when it hands it back.
	MusicInterruptHandle = FCoreDelegates::UserMusicInterruptDelegate.AddWeakLambda(this, [this](bool bOtherAudio)
	{
		QueueAppEvent(bOtherAudio ? EAppEvent::OtherAudioStarted : EAppEvent::OtherAudioStopped);
	});
	// iOS: true for a new device (headphones in), false when the old one went away (headphones out).
	AudioRouteHandle = FCoreDelegates::AudioRouteChangedDelegate.AddWeakLambda(this, [this](bool bNewDevice)
	{
		QueueAppEvent(bNewDevice ? EAppEvent::HeadphonesPlugged : EAppEvent::HeadphonesUnplugged);
	});
	NetworkHandle = FCoreDelegates::OnNetworkConnectionChanged.AddWeakLambda(this, [this](ENetworkConnectionType Type)
	{
		if (IsInGameThread())
		{
			HandleNetworkChanged(Type);
			return;
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<UMobileLifecycleSubsystem>(this), Type]()
		{
			if (UMobileLifecycleSubsystem* Self = WeakThis.Get())
			{
				Self->HandleNetworkChanged(Type);
			}
		});
	});
}

void UMobileLifecycleSubsystem::UnbindPlatformDelegates()
{
	if (!bDelegatesBound)
	{
		return;
	}
	bDelegatesBound = false;
	for (const TPair<TMulticastDelegate<void()>*, FDelegateHandle>& Bound : VoidHandles)
	{
		Bound.Key->Remove(Bound.Value);
	}
	VoidHandles.Reset();
	FCoreDelegates::GetApplicationWillTerminateDelegate().Remove(TerminateHandle);
	FCoreDelegates::AudioInterruptionDelegate.Remove(AudioInterruptHandle);
	FCoreDelegates::AudioFocusChangedDelegate.Remove(AudioFocusHandle);
	FCoreDelegates::UserMusicInterruptDelegate.Remove(MusicInterruptHandle);
	FCoreDelegates::AudioRouteChangedDelegate.Remove(AudioRouteHandle);
	FCoreDelegates::OnNetworkConnectionChanged.Remove(NetworkHandle);
}

void UMobileLifecycleSubsystem::QueueAppEvent(EAppEvent Event)
{
	// UE broadcasts the lifecycle on the game thread (FAppleAsyncTask, the Android event queue), but not every audio callback.
	if (IsInGameThread())
	{
		HandleAppEvent(Event);
		return;
	}
	AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<UMobileLifecycleSubsystem>(this), Event]()
	{
		if (UMobileLifecycleSubsystem* Self = WeakThis.Get())
		{
			Self->HandleAppEvent(Event);
		}
	});
}

void UMobileLifecycleSubsystem::HandleAppEvent(EAppEvent Event)
{
	const FLifecycleActions Actions = Lifecycle.Handle(Event, Now());
	UE_LOG(LogMobileLifecycle, Log, TEXT("%s: state %d%s%s%s%s"), LexToString(Event), static_cast<int32>(Lifecycle.GetState()),
		Actions.bPauseGame ? TEXT(", pause") : TEXT(""), Actions.bResumeRoomGame ? TEXT(", resume room game") : TEXT(""),
		Actions.bSave ? TEXT(", save") : TEXT(""),
		Actions.bReturnedToForeground ? *FString::Printf(TEXT(", back after %.1f s"), Actions.AwaySeconds) : TEXT(""));
	if (Actions.bPauseGame)
	{
		PauseCurrentGame(Actions.bPauseLocalOnly);
	}
	if (Actions.bResumeRoomGame)
	{
		ResumeRoomGame();
	}
	if (Actions.bSave)
	{
		SaveEverything(Actions.bSaveBlocking);
	}
	if (Actions.bReturnedToForeground)
	{
		Recovery.NoteForeground(Actions.AwaySeconds, Now());
	}
	ApplyAudio();
}

void UMobileLifecycleSubsystem::HandleNetworkChanged(ENetworkConnectionType Type)
{
	if (SimulatedNetwork.IsSet() && Type != *SimulatedNetwork)
	{
		return;
	}
	const ENetworkConnectionType Before = Recovery.GetNetwork();
	Recovery.NoteNetwork(Type, Now());
	if (Before != Recovery.GetNetwork())
	{
		UE_LOG(LogMobileLifecycle, Log, TEXT("Network %s -> %s"), LexToString(Before), LexToString(Type));
	}
}

void UMobileLifecycleSubsystem::SimulateNetwork(ENetworkConnectionType Type)
{
	SimulatedNetwork = Type;
	HandleNetworkChanged(Type);
}

bool UMobileLifecycleSubsystem::RunSimCommand(const FString& Command)
{
	const FSimCommand Parsed = ParseSimCommand(Command);
	if (Parsed.Warmup.IsSet())
	{
		SimulatedWarmup = static_cast<double>(*Parsed.Warmup);
		return true;
	}
	if (Parsed.Banner.IsSet())
	{
		SimulatedBanner = *Parsed.Banner;
		return true;
	}
	if (Parsed.bBannerOff)
	{
		SimulatedBanner.Reset();
		return true;
	}
	if (Parsed.Network.IsSet())
	{
		SimulateNetwork(*Parsed.Network);
		return true;
	}
	if (Parsed.Event.IsSet())
	{
		HandleAppEvent(*Parsed.Event);
		return true;
	}
	return false;
}

// Carrying out the lifecycle's actions -----------------------------------------------------------------------

void UMobileLifecycleSubsystem::PauseCurrentGame(bool bLocalOnly)
{
	UWorld* World = GetGameWorld();
	AGameModeBase* Mode = World ? World->GetAuthGameMode() : nullptr;
	if (AArcadeGameModeBase* Arcade = Cast<AArcadeGameModeBase>(Mode))
	{
		Arcade->PauseForAppInterruption(bLocalOnly);
		return;
	}
	const AKartGameMode* KartMode = Cast<AKartGameMode>(Mode);
	AKartRaceManager* Race = KartMode ? KartMode->GetRaceManager() : nullptr;
	if (!Race || Race->IsPaused() || !IsRacing(*Race))
	{
		return;
	}
	// Room races send "race again" through the room; local ones have the pause menu.
	const bool bRoomRace = static_cast<bool>(Race->OnRestartRequested);
	if (!bRoomRace)
	{
		if (const TSharedPtr<SKartRaceOverlay> RaceOverlay = SKartRaceOverlay::GetActive())
		{
			RaceOverlay->OpenPauseMenu();
		}
		if (!Race->IsPaused())
		{
			Race->SetPaused(true, LocalPauseReason);
		}
		return;
	}
	if (!bLocalOnly)
	{
		Race->SetPaused(true, AppPauseReason);
		RacePausedForApp = Race;
	}
}

void UMobileLifecycleSubsystem::ResumeRoomGame()
{
	UWorld* World = GetGameWorld();
	if (AArcadeGameModeBase* Arcade = World ? Cast<AArcadeGameModeBase>(World->GetAuthGameMode()) : nullptr)
	{
		Arcade->ResumeAfterAppInterruption();
	}
	AKartRaceManager* Race = RacePausedForApp.Get();
	RacePausedForApp.Reset();
	if (Race && Race->IsPaused() && Race->GetPauseReason() == AppPauseReason && !IsRoomPaused(GetGameInstance()))
	{
		Race->SetPaused(false);
	}
}

void UMobileLifecycleSubsystem::SaveEverything(bool bBlocking) const
{
	// Everything here writes synchronously, so bBlocking needs nothing extra today; the saved room (URoomHostSubsystem)
	// writes its own file whenever it changes.
	GetMutableDefault<UKartPlayerPrefs>()->Save();
	GetMutableDefault<UMobilePrefs>()->Save();
	if (UKartGameUserSettings* Settings = UKartGameUserSettings::Get(); Settings && !Settings->IsDirty())
	{
		// Only what was applied: a settings screen mid-edit keeps its pending changes pending.
		Settings->SaveSettings();
	}
	if (const MovecadeKart::Store::FKartStore* Store = UKartStoreSubsystem::Active())
	{
		Store->Save();
	}
	if (GConfig)
	{
		GConfig->Flush(false);
	}
	UE_LOG(LogMobileLifecycle, Log, TEXT("Saved settings, store and profile%s"), bBlocking ? TEXT(" (terminating)") : TEXT(""));
}

void UMobileLifecycleSubsystem::ApplyAudio()
{
	const bool bMute = Lifecycle.ShouldMuteAll();
	if (bMute != bMutedAll && GEngine)
	{
		bMutedAll = bMute;
		if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
		{
			Device->SetTransientPrimaryVolume(bMute ? 0.f : 1.f);
		}
	}

	const bool bDuck = Lifecycle.ShouldMuteMusic();
	UWorld* World = GetGameWorld();
	if (bDuck == bMusicDucked || !World)
	{
		return;
	}
	if (!MusicClass)
	{
		MusicClass = LoadObject<USoundClass>(nullptr, MusicClassPath);
	}
	if (!DuckMix)
	{
		DuckMix = NewObject<USoundMix>(this, MakeUniqueObjectName(this, USoundMix::StaticClass(), TEXT("MobileMusicDuck")));
	}
	if (!MusicClass)
	{
		return;
	}
	bMusicDucked = bDuck;
	if (bDuck)
	{
		UGameplayStatics::SetSoundMixClassOverride(World, DuckMix, MusicClass, 0.f, 1.f, MusicDuckFadeSeconds, true);
		UGameplayStatics::PushSoundMixModifier(World, DuckMix);
	}
	else
	{
		UGameplayStatics::PopSoundMixModifier(World, DuckMix);
	}
}

// The poll -------------------------------------------------------------------------------------------------

bool UMobileLifecycleSubsystem::Tick(float DeltaSeconds)
{
	const double T = Now();
	RunSimScript(T);
	TickAccumulator += DeltaSeconds;
	NetworkPollAccumulator += DeltaSeconds;
	if (TickAccumulator < PollSeconds)
	{
		return true;
	}
	TickAccumulator = 0.0;

	UWorld* World = GetGameWorld();
	PlayState = World ? ReadPlayState(*World) : EPlayState::Menu;
	if (PlayState == EPlayState::Playing && !Lifecycle.IsAttentive())
	{
		// A game that started while the player was away (the level finished loading behind a call): pause it now.
		PauseCurrentGame(false);
		PlayState = World ? ReadPlayState(*World) : EPlayState::Menu;
	}
	if (bPhoneBehaviour && NetworkPollAccumulator >= NetworkPollSeconds && !SimulatedNetwork.IsSet())
	{
#if PLATFORM_IOS || PLATFORM_ANDROID
		// Desktop sends OnNetworkConnectionChanged itself; phones may not, so they are asked.
		NetworkPollAccumulator = 0.0;
		HandleNetworkChanged(FPlatformMisc::GetNetworkConnectionType());
#endif
	}
	UpdateRoom(T);
	if (SimulatedBanner.IsSet())
	{
		Banner = *SimulatedBanner; // -MobileSimScript banner=...: the banner without a room server
	}
	if (!bPhoneBehaviour || !World)
	{
		return true;
	}
	BindRaceHaptics(*World);
	UpdatePolicy();
	UpdateWarmup();
	UpdateOnboarding();
	if (Banner != EConnectionBanner::None || bWarmupShown || Overlay.IsValid())
	{
		EnsureOverlay(*World);
	}
	if (Overlay.IsValid())
	{
		Overlay->SetBanner(Banner);
	}
	return true;
}

void UMobileLifecycleSubsystem::UpdateRoom(double T)
{
	URoomHostSubsystem* Room = GetGameInstance() ? GetGameInstance()->GetSubsystem<URoomHostSubsystem>() : nullptr;
	if (!Room)
	{
		Banner = EConnectionBanner::None;
		return;
	}
	EMcConnection Connection = Room->GetConnectionState();
	if (Connection == EMcConnection::Idle)
	{
		bRoomWasOpen = false;
		bRoomWentDown = false;
		RoomRestoredAt = -1.0;
	}
	else if (Connection == EMcConnection::Open)
	{
		if (bRoomWentDown)
		{
			RoomRestoredAt = T;
			bRoomWentDown = false;
			UE_LOG(LogMobileLifecycle, Log, TEXT("Room connection restored"));
		}
		bRoomWasOpen = true;
	}
	else if (bRoomWasOpen)
	{
		bRoomWentDown = true;
	}

	if (Recovery.ConsumeReconnect(T, Connection))
	{
		// The room's own reconnect path, minus the backoff: same code, same host token, phones stay seated.
		UE_LOG(LogMobileLifecycle, Log, TEXT("Reconnecting the room now (network %s, was %d)"), LexToString(Recovery.GetNetwork()), static_cast<int32>(Connection));
		Room->CloseRoom(false);
		Room->OpenRoom();
		Connection = Room->GetConnectionState();
		bRoomWentDown = bRoomWasOpen;
	}
	LastRoomConnection = Connection;

	FConnectionView View;
	View.Room = Connection;
	View.bWasOpen = bRoomWasOpen;
	View.bDeviceOffline = Recovery.IsDeviceOffline();
	View.SinceRestored = RoomRestoredAt >= 0.0 ? T - RoomRestoredAt : -1.0;
	View.bInGame = PlayState != EPlayState::Menu;
	Banner = ChooseBanner(View);
}

void UMobileLifecycleSubsystem::UpdatePolicy()
{
	// Away: nothing to apply until the player is back.
	if (Lifecycle.GetState() != EAppState::Active || !GEngine)
	{
		return;
	}
	const FDevicePolicy Policy = ChoosePolicy(Lifecycle.GetState(), PlayState, LastRoomConnection != EMcConnection::Idle);
	if (!bPolicyApplied || Policy.bKeepScreenAwake != bScreenKeptAwake)
	{
		bScreenKeptAwake = Policy.bKeepScreenAwake;
		GEngine->EnableScreenSaver(!Policy.bKeepScreenAwake);
		UE_LOG(LogMobileLifecycle, Log, TEXT("Screen %s"), Policy.bKeepScreenAwake ? TEXT("kept awake") : TEXT("may sleep"));
	}
	if (!bPolicyApplied || Policy.FrameCap != AppliedFrameCap)
	{
		if (IConsoleVariable* MaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
		{
			if (AppliedFrameCap <= 0.f)
			{
				FrameCapBefore = MaxFPS->GetFloat();
			}
			// A lower limit someone already set (a device profile, the player) wins over ours.
			const bool bKeepLower = FrameCapBefore > 0.f && FrameCapBefore < Policy.FrameCap;
			const float Target = Policy.FrameCap > 0.f && !bKeepLower ? Policy.FrameCap : FrameCapBefore;
			if (MaxFPS->GetFloat() != Target)
			{
				MaxFPS->Set(Target, ECVF_SetByCode);
				UE_LOG(LogMobileLifecycle, Log, TEXT("Frame cap %.0f (%s)"), Target, PlayState == EPlayState::Playing ? TEXT("playing") : TEXT("menu or paused"));
			}
		}
		AppliedFrameCap = Policy.FrameCap;
	}
	bPolicyApplied = true;
}

void UMobileLifecycleSubsystem::UpdateWarmup()
{
	uint32 Remaining = FShaderPipelineCache::IsPrecompiling() ? FShaderPipelineCache::NumPrecompilesRemaining() : 0;
	if (SimulatedWarmup.IsSet())
	{
		SimulatedWarmup = FMath::Max(0.0, *SimulatedWarmup - SimulatedWarmupRate * PollSeconds);
		Remaining = static_cast<uint32>(FMath::CeilToDouble(*SimulatedWarmup));
	}
	MaxPrecompilesSeen = FMath::Max(MaxPrecompilesSeen, Remaining);
	// Big batches while nobody is playing; background-sized ones during play keep the frame time steady.
	const bool bFast = Remaining > 0 && PlayState != EPlayState::Playing;
	if (bFast != bWarmupFast)
	{
		bWarmupFast = bFast;
		FShaderPipelineCache::SetBatchMode(bFast ? FShaderPipelineCache::BatchMode::Precompile : FShaderPipelineCache::BatchMode::Background);
	}
	const bool bShow = Remaining > 0 && MaxPrecompilesSeen >= MinPrecompilesWorthShowing && PlayState == EPlayState::Menu;
	if (bShow != bWarmupShown)
	{
		UE_LOG(LogMobileLifecycle, Log, TEXT("Shader warm-up %s (%u to go)"), bShow ? TEXT("shown") : TEXT("done"), Remaining);
	}
	bWarmupShown = bShow;
	if (Overlay.IsValid())
	{
		Overlay->SetWarmup(bShow, PrecompileProgress(MaxPrecompilesSeen, Remaining));
	}
}

void UMobileLifecycleSubsystem::UpdateOnboarding()
{
	if (bOnboardingSent || PlayState != EPlayState::Menu || bWarmupShown || !Lifecycle.IsAttentive())
	{
		return;
	}
	if (IsMoviePlayerEnabled() && GetMoviePlayer() && GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		return;
	}
	// The front end or the Party Mode lobby: the kart level with no race on it (a race's own wait is no menu).
	const UWorld* World = GetGameWorld();
	const AKartGameMode* KartMode = World ? Cast<AKartGameMode>(World->GetAuthGameMode()) : nullptr;
	if (!KartMode || KartMode->GetRaceManager())
	{
		return;
	}
	bOnboardingSent = true;
	UMobilePrefs* Prefs = GetMutableDefault<UMobilePrefs>();
	bFirstLaunchThisRun = !Prefs->bLaunchedBefore;
	if (bFirstLaunchThisRun)
	{
		Prefs->bLaunchedBefore = true;
		Prefs->Save();
	}
	UE_LOG(LogMobileLifecycle, Log, TEXT("Ready for onboarding (first launch: %s)"), bFirstLaunchThisRun ? TEXT("yes") : TEXT("no"));
	OnReadyForOnboarding.Broadcast(bFirstLaunchThisRun);
}

void UMobileLifecycleSubsystem::BindRaceHaptics(UWorld& World)
{
	const AKartGameMode* KartMode = Cast<AKartGameMode>(World.GetAuthGameMode());
	AKartRaceManager* Race = KartMode ? KartMode->GetRaceManager() : nullptr;
	if (Race == HapticRace.Get())
	{
		return;
	}
	if (AKartRaceManager* Old = HapticRace.Get())
	{
		Old->OnHaptic.Remove(RaceHapticHandle);
		Old->OnCue.Remove(RaceCueHandle);
	}
	HapticRace = Race;
	if (!Race)
	{
		return;
	}
	// The race already sends phone seats their haptics through the room; these are the local players' own.
	RaceHapticHandle = Race->OnHaptic.AddWeakLambda(this, [this](int32 Seat, float Intensity, FName Type, bool bSustained)
	{
		const AKartRaceManager* Live = HapticRace.Get();
		// Sustained ones (a wall scrape, dirt) would buzz for seconds on a phone: the first contact is enough.
		if (Live && !bSustained)
		{
			FHaptics::Play(LocalViewerOf(*Live, Seat), EHaptic::Hit, Intensity);
		}
	});
	RaceCueHandle = Race->OnCue.AddWeakLambda(this, [this](int32 Seat, FName Cue)
	{
		const AKartRaceManager* Live = HapticRace.Get();
		if (Live && (Cue == ThrowCues[0] || Cue == ThrowCues[1]))
		{
			FHaptics::Play(LocalViewerOf(*Live, Seat), EHaptic::Throw);
		}
	});
}

void UMobileLifecycleSubsystem::RunSimScript(double T)
{
	const TArray<FSimStep>& Script = FMobileDebugFlags::Get().Script;
	while (Script.IsValidIndex(NextSimStep) && T - StartTime >= Script[NextSimStep].At)
	{
		UE_LOG(LogMobileLifecycle, Display, TEXT("-MobileSimScript %.1f s: %s"), Script[NextSimStep].At, *Script[NextSimStep].Command);
		RunSimCommand(Script[NextSimStep].Command);
		++NextSimStep;
	}
}

// The splash and the overlay ---------------------------------------------------------------------------------

void UMobileLifecycleSubsystem::PrepareLoadingScreen()
{
	FLoadingScreenAttributes Splash;
	// Gone the moment the level is in: the splash is there to hide a black screen, not to make anyone wait.
	Splash.bAutoCompleteWhenLoadingCompletes = true;
	Splash.MinimumLoadingScreenDisplayTime = 0.f;
	Splash.bMoviesAreSkippable = false;
	Splash.WidgetLoadingScreen = SNew(SMobileBootSplash);
	GetMoviePlayer()->SetupLoadingScreen(Splash);
	UE_LOG(LogMobileLifecycle, Log, TEXT("Loading splash up"));
}

void UMobileLifecycleSubsystem::EnsureOverlay(UWorld& World)
{
	if (Overlay.IsValid() && OverlayWorld.Get() == &World)
	{
		return;
	}
	RemoveOverlay();
	UGameViewportClient* Viewport = World.GetGameViewport();
	if (!Viewport)
	{
		return;
	}
	Overlay = SNew(SMobileStatusOverlay);
	Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), OverlayZOrder);
	OverlayWorld = &World;
	Overlay->SetBanner(Banner);
}

void UMobileLifecycleSubsystem::RemoveOverlay()
{
	if (Overlay.IsValid())
	{
		if (const UWorld* World = OverlayWorld.Get(); World && World->GetGameViewport())
		{
			World->GetGameViewport()->RemoveViewportWidgetContent(Overlay.ToSharedRef());
		}
	}
	Overlay.Reset();
	OverlayWorld.Reset();
}

// Helpers --------------------------------------------------------------------------------------------------

UWorld* UMobileLifecycleSubsystem::GetGameWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetWorld() : nullptr;
}

EPlayState UMobileLifecycleSubsystem::ReadPlayState(const UWorld& World) const
{
	AGameModeBase* Mode = World.GetAuthGameMode();
	if (const AArcadeGameModeBase* Arcade = Cast<AArcadeGameModeBase>(Mode))
	{
		const IArcadeGame* Game = Arcade->GetArcadeGame();
		if (!Game || Game->GetArcadeFinishedAge() >= 0.0)
		{
			return EPlayState::Menu;
		}
		return Game->IsArcadePaused() || SArcadeOverlay::IsMenuOpen(&World) ? EPlayState::Paused : EPlayState::Playing;
	}
	const AKartGameMode* KartMode = Cast<AKartGameMode>(Mode);
	const AKartRaceManager* Race = KartMode ? KartMode->GetRaceManager() : nullptr;
	if (!Race || Race->GetPhase() == EKartRacePhase::Waiting || Race->GetPhase() == EKartRacePhase::Finished)
	{
		return EPlayState::Menu;
	}
	return Race->IsPaused() ? EPlayState::Paused : EPlayState::Playing;
}

double UMobileLifecycleSubsystem::Now() const
{
	return FPlatformTime::Seconds();
}
