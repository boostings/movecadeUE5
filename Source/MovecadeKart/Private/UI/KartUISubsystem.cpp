#include "UI/KartUISubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "UI/KartAccessibility.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartPlayerPrefs.h"
#include "UI/KartUiDebugFlags.h"
#include "UI/Widgets/SKartCurtain.h"
#include "UnrealClient.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartUI, Log, All);

namespace
{
	const TCHAR* MenuMusicPath = TEXT("/Game/UI/Sounds/music_menu_loop.music_menu_loop");
	/** The menu music's fades (seconds). */
	constexpr float MusicFadeIn = 1.2f;
	constexpr float MusicFadeOut = 0.8f;
	/** The tour gives up on a step after this long, and lets the last capture write before quitting. */
	constexpr double TourStepTimeout = 150.0;
	constexpr double TourQuitDelay = 1.5;

	const TCHAR* SoundPath(EKartUISound Sound)
	{
		switch (Sound)
		{
		case EKartUISound::Move: return TEXT("/Game/UI/Sounds/ui_move.ui_move");
		case EKartUISound::Tick: return TEXT("/Game/UI/Sounds/ui_tick.ui_tick");
		case EKartUISound::Accept: return TEXT("/Game/UI/Sounds/ui_accept.ui_accept");
		case EKartUISound::Back: return TEXT("/Game/UI/Sounds/ui_back.ui_back");
		case EKartUISound::Error: return TEXT("/Game/UI/Sounds/ui_error.ui_error");
		case EKartUISound::Start: return TEXT("/Game/UI/Sounds/ui_start.ui_start");
		case EKartUISound::Go: return TEXT("/Game/UI/Sounds/ui_go.ui_go");
		case EKartUISound::Pause: return TEXT("/Game/UI/Sounds/ui_pause.ui_pause");
		case EKartUISound::Resume: return TEXT("/Game/UI/Sounds/ui_resume.ui_resume");
		default: return TEXT("/Game/UI/Sounds/ui_whoosh.ui_whoosh");
		}
	}
}

UKartUISubsystem* UKartUISubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UKartUISubsystem>() : nullptr;
}

void UKartUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (FSlateApplication::IsInitialized())
	{
		Processor = MakeShared<FKartMenuInputProcessor>();
		Processor->OnEvent = [WeakThis = TWeakObjectPtr<UKartUISubsystem>(this)](const FKartMenuEvent& Event)
		{
			UKartUISubsystem* Self = WeakThis.Get();
			return Self && Self->HandleEvent(Event);
		};
		FSlateApplication::Get().RegisterInputPreProcessor(Processor, 0);
	}
	MapLoadedHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UKartUISubsystem::HandleMapLoaded);
	// The player's UI scale (Settings > Accessibility) multiplies the viewport's DPI curve.
	FKartAccessibility::ApplyUIScale();
	StartTourFromCommandLine();
}

void UKartUISubsystem::StartTourFromCommandLine()
{
	// The flags read as empty in Shipping, so no tour can start there.
	const FKartUiDebugFlags Flags = FKartUiDebugFlags::Parse(FCommandLine::Get());
	if (Processor.IsValid() && Flags.bForcePad)
	{
		Processor->ForceDevice(EKartInputDevice::Gamepad);
	}
	if (Flags.bTour)
	{
		TourSteps = Flags.TourSteps;
		TourIndex = 0;
		TourStepStarted = FPlatformTime::Seconds();
		TourTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UKartUISubsystem::TickTour));
		UE_LOG(LogKartUI, Display, TEXT("KartUiShots: %d steps"), TourSteps.Num());
	}
}

void UKartUISubsystem::Deinitialize()
{
	if (Processor.IsValid())
	{
		Processor->OnEvent = nullptr; // Slate may still hold the processor after this subsystem is gone
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(Processor);
		}
	}
	Processor.Reset();
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(TourTicker);
	if (Music)
	{
		Music->Stop();
	}
	Super::Deinitialize();
}

UWorld* UKartUISubsystem::GetGameWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}

// Input -----------------------------------------------------------------------------------------------

void UKartUISubsystem::PushInputTarget(const TSharedRef<IKartMenuInputTarget>& Target)
{
	Targets.Remove(Target);
	Targets.Add(Target);
}

void UKartUISubsystem::PopInputTarget(const TSharedRef<IKartMenuInputTarget>& Target)
{
	Targets.Remove(Target);
}

bool UKartUISubsystem::HandleEvent(const FKartMenuEvent& Event)
{
	Targets.RemoveAll([](const TWeakPtr<IKartMenuInputTarget>& Target) { return !Target.IsValid(); });
	if (Targets.Num() == 0 || bTravelling)
	{
		return bTravelling && Event.Action != EKartMenuAction::None;
	}
	// In the editor, keys typed into other panels while PIE runs are not the game's.
	if (GIsEditor && Event.UserIndex == 0)
	{
		const UGameViewportClient* Viewport = GetGameWorld() ? GetGameWorld()->GetGameViewport() : nullptr;
		const TSharedPtr<SViewport> Widget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
		if (!Widget.IsValid() || (!Widget->HasFocusedDescendants() && !Widget->HasKeyboardFocus()))
		{
			return false;
		}
	}
	const TSharedPtr<IKartMenuInputTarget> Top = Targets.Last().Pin();
	return Top.IsValid() && Top->HandleMenuEvent(Event);
}

// Sound -----------------------------------------------------------------------------------------------

USoundWave* UKartUISubsystem::LoadSound(EKartUISound Sound)
{
	const uint8 Key = static_cast<uint8>(Sound);
	if (const TObjectPtr<USoundWave>* Found = Sounds.Find(Key))
	{
		return *Found;
	}
	USoundWave* Wave = LoadObject<USoundWave>(nullptr, SoundPath(Sound));
	if (!Wave)
	{
		// Cached as missing, so this warns once per sound; the menus stay silent rather than fail.
		UE_LOG(LogKartUI, Warning, TEXT("Missing UI sound %s (run Tools/ui/make_ui_sounds.py)."), SoundPath(Sound));
	}
	Sounds.Add(Key, Wave);
	return Wave;
}

void UKartUISubsystem::PlaySound(EKartUISound Sound)
{
	UWorld* World = GetGameWorld();
	USoundWave* Wave = LoadSound(Sound);
	if (World && Wave && GEngine && GEngine->UseSound())
	{
		UGameplayStatics::PlaySound2D(World, Wave, 1.f, 1.f, 0.f, nullptr, nullptr, /*bIsUISound*/ true);
	}
}

void UKartUISubsystem::SetMenuMusic(bool bPlaying)
{
	bWantMusic = bPlaying;
	UWorld* World = GetGameWorld();
	if (!World || !GEngine || !GEngine->UseSound())
	{
		return;
	}
	if (bPlaying)
	{
		if (Music && Music->IsPlaying())
		{
			return;
		}
		if (!MusicWave)
		{
			MusicWave = LoadObject<USoundWave>(nullptr, MenuMusicPath);
			UE_CLOG(!MusicWave, LogKartUI, Warning, TEXT("Missing menu music %s; the menus play without it."), MenuMusicPath);
		}
		if (MusicWave)
		{
			Music = UGameplayStatics::CreateSound2D(World, MusicWave, 1.f, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition*/ true, /*bAutoDestroy*/ false);
			if (Music)
			{
				Music->bIsUISound = true;
				Music->FadeIn(MusicFadeIn);
			}
		}
	}
	else if (Music && Music->IsPlaying())
	{
		Music->FadeOut(MusicFadeOut, 0.f);
	}
}

void UKartUISubsystem::ApplyVolumes()
{
	UWorld* World = GetGameWorld();
	if (!World || !GEngine || !GEngine->UseSound())
	{
		return;
	}
	if (!VolumeMix)
	{
		VolumeMix = LoadObject<USoundMix>(nullptr, TEXT("/Game/UI/Audio/SM_KartVolumes.SM_KartVolumes"));
		MusicClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/UI/Audio/SC_Music.SC_Music"));
		UIClass = LoadObject<USoundClass>(nullptr, TEXT("/Game/UI/Audio/SC_UI.SC_UI"));
	}
	if (!VolumeMix)
	{
		return;
	}
	const UKartPlayerPrefs* Prefs = GetDefault<UKartPlayerPrefs>();
	const float Master = FMath::Clamp(Prefs->MasterVolume, 0.f, 1.f);
	const float Sfx = Master * FMath::Clamp(Prefs->SfxVolume, 0.f, 1.f);
	// Race sounds play on the project's default sound class; the menu sounds and music have their own.
	if (USoundClass* Default = GetDefault<UAudioSettings>()->GetDefaultSoundClass())
	{
		UGameplayStatics::SetSoundMixClassOverride(World, VolumeMix, Default, Sfx, 1.f, 0.f, true);
	}
	if (UIClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(World, VolumeMix, UIClass, Sfx, 1.f, 0.f, true);
	}
	if (MusicClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(World, VolumeMix, MusicClass, Master * FMath::Clamp(Prefs->MusicVolume, 0.f, 1.f), 1.f, 0.f, true);
	}
	UGameplayStatics::PushSoundMixModifier(World, VolumeMix);
}

// Flow ------------------------------------------------------------------------------------------------

void UKartUISubsystem::ShowCurtain(bool bClosing, TFunction<void()> OnClosed)
{
	UWorld* World = GetGameWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		if (OnClosed)
		{
			OnClosed();
		}
		return;
	}
	if (!Curtain.IsValid())
	{
		Curtain = SNew(SKartCurtain).Closing(bClosing);
	}
	Viewport->RemoveViewportWidgetContent(Curtain.ToSharedRef());
	Viewport->AddViewportWidgetContent(Curtain.ToSharedRef(), 1000);
	if (bClosing)
	{
		Curtain->Close(MoveTemp(OnClosed));
	}
	else
	{
		Curtain->Open();
	}
}

void UKartUISubsystem::TravelTo(const FString& Level)
{
	if (bTravelling)
	{
		return;
	}
	bTravelling = true;
	PlaySound(EKartUISound::Whoosh);
	TWeakObjectPtr<UKartUISubsystem> WeakThis(this);
	ShowCurtain(true, [WeakThis, Level]()
	{
		UKartUISubsystem* Self = WeakThis.Get();
		if (UWorld* World = Self ? Self->GetGameWorld() : nullptr)
		{
			Self->bCurtainOnLoad = true;
			UGameplayStatics::OpenLevel(World, FName(*Level));
		}
	});
}

void UKartUISubsystem::QuitGame()
{
	bTravelling = true;
	TWeakObjectPtr<UKartUISubsystem> WeakThis(this);
	ShowCurtain(true, [WeakThis]()
	{
		UKartUISubsystem* Self = WeakThis.Get();
		if (UWorld* World = Self ? Self->GetGameWorld() : nullptr)
		{
			UKismetSystemLibrary::QuitGame(World, World->GetFirstPlayerController(), EQuitPreference::Quit, false);
		}
	});
}

void UKartUISubsystem::HandleMapLoaded(UWorld* World)
{
	if (!World || World != GetGameWorld())
	{
		return;
	}
	bTravelling = false;
	if (IsTourActive())
	{
		TourStepStarted = FPlatformTime::Seconds(); // a step that travelled starts timing on arrival
	}
	ApplyVolumes();
	if (bWantMusic && (!Music || !Music->IsPlaying()))
	{
		SetMenuMusic(true);
	}
	if (bCurtainOnLoad)
	{
		// The closed curtain survived the load (or LoadMap dropped it); either way, open it on the new level.
		bCurtainOnLoad = false;
		ShowCurtain(false);
	}
	else if (Curtain.IsValid())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(Curtain.ToSharedRef());
		}
		Curtain.Reset();
	}
	if (IsTourActive() && GEngine)
	{
		GEngine->Exec(World, TEXT("DisableAllScreenMessages"));
	}
}

// Screenshot tour -------------------------------------------------------------------------------------

FName UKartUISubsystem::GetTourStep() const
{
	return TourSteps.IsValidIndex(TourIndex) ? TourSteps[TourIndex] : NAME_None;
}

void UKartUISubsystem::TourShot(FName Name)
{
	FIntPoint Size(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Size = GEngine->GameViewport->Viewport->GetSizeXY();
	}
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(TEXT("KartUi_%s_%dx%d.png"), *Name.ToString(), Size.X, Size.Y));
	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	UE_LOG(LogKartUI, Display, TEXT("KartUiShots: %s -> %s"), *Name.ToString(), *Path);
	++TourIndex;
	TourStepStarted = FPlatformTime::Seconds();
}

bool UKartUISubsystem::TickTour(float DeltaTime)
{
	UWorld* World = GetGameWorld();
	if (!World || bTravelling)
	{
		return true;
	}
	const double InStep = FPlatformTime::Seconds() - TourStepStarted;
	if (!TourSteps.IsValidIndex(TourIndex))
	{
		// Let the last capture write, then leave.
		if (InStep > TourQuitDelay)
		{
			UE_LOG(LogKartUI, Display, TEXT("KartUiShots: done"));
			FTSTicker::GetCoreTicker().RemoveTicker(TourTicker);
			TourIndex = -1;
			TourSteps.Reset();
			UKismetSystemLibrary::QuitGame(World, World->GetFirstPlayerController(), EQuitPreference::Quit, false);
			return false;
		}
		return true;
	}
	const FName Step = TourSteps[TourIndex];
	if (InStep > TourStepTimeout)
	{
		UE_LOG(LogKartUI, Warning, TEXT("KartUiShots: step %s timed out; skipping it."), *Step.ToString());
		++TourIndex;
		TourStepStarted = FPlatformTime::Seconds();
		return true;
	}

	if (Step == TEXT("lobby"))
	{
		// Party Mode from the menu: the level reloads into the phone lobby (no room server needed for the shot).
		if (AKartFrontEnd* FrontEnd = AKartFrontEnd::Find(World))
		{
			FrontEnd->StartParty();
		}
		else if (InStep > 6.0)
		{
			TourShot(Step);
		}
		return true;
	}
	if (AKartFrontEnd* FrontEnd = AKartFrontEnd::Find(World))
	{
		// Menu screens: open it, let the entry animation settle, capture.
		if (FrontEnd->TourShow(Step, InStep))
		{
			return true;
		}
		if (InStep > 0.5)
		{
			// race / pause / results need a race: start one with the current setup.
			FrontEnd->TourStartRace();
		}
		return true;
	}

	AKartRaceManager* Race = AKartRaceManager::Get(World);
	const TSharedPtr<SKartRaceOverlay> Overlay = SKartRaceOverlay::GetActive();
	if (!Race || !Overlay.IsValid())
	{
		return true;
	}
	if (Step == TEXT("race") || Step == TEXT("countdown"))
	{
		const bool bReady = Step == TEXT("race")
			? Race->GetPhase() == EKartRacePhase::Racing && Race->GetRaceMs() > 6000.0
			: Race->GetPhase() == EKartRacePhase::Countdown && Race->GetPhaseSeconds() > 2.1;
		if (Race->GetPhase() == EKartRacePhase::Intro || Race->GetPhase() == EKartRacePhase::Waiting)
		{
			if (InStep > 2.5)
			{
				Race->SkipIntro();
			}
		}
		if (bReady)
		{
			TourShot(Step);
		}
	}
	else if (Step == TEXT("pause"))
	{
		if (Race->GetPhase() == EKartRacePhase::Racing && !Race->IsPaused())
		{
			Overlay->OpenPauseMenu();
			TourStepStarted = FPlatformTime::Seconds();
		}
		else if (Race->IsPaused() && InStep > 1.2)
		{
			TourShot(Step); // the menu closes on the next step, once the capture has been drawn
		}
	}
	else if (Step == TEXT("results"))
	{
		if (Race->IsPaused() && Race->GetPauseReason() == TEXT("local") && InStep > 0.5)
		{
			Overlay->ClosePauseMenu();
		}
		if (Race->GetPhase() == EKartRacePhase::Finished && InStep > 4.0 && Race->GetPhaseSeconds() > 4.0)
		{
			TourShot(Step);
		}
	}
	else
	{
		UE_LOG(LogKartUI, Warning, TEXT("KartUiShots: no front end for step %s; skipping it."), *Step.ToString());
		++TourIndex;
		TourStepStarted = FPlatformTime::Seconds();
	}
	return true;
}
