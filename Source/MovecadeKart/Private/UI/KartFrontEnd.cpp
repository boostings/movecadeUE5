#include "UI/KartFrontEnd.h"

#include "Audio/KartAudioComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "UI/KartScreen.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartUISubsystem.h"
#include "UI/Screens/KartScreens.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartFrontEnd, Log, All);

using namespace MovecadeKart;

namespace
{
	constexpr double ShotBlendSeconds = 1.2;
}

AKartFrontEnd::AKartFrontEnd()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

AKartFrontEnd* AKartFrontEnd::Open(UWorld* World, TSharedPtr<const FTrack> InTrack)
{
	if (!World)
	{
		return nullptr;
	}
	AKartFrontEnd* FrontEnd = World->SpawnActorDeferred<AKartFrontEnd>(AKartFrontEnd::StaticClass(), FTransform::Identity);
	FrontEnd->Track = InTrack;
	FrontEnd->FinishSpawning(FTransform::Identity);
	return FrontEnd;
}

AKartFrontEnd* AKartFrontEnd::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	if (TActorIterator<AKartFrontEnd> It{const_cast<UWorld*>(World)})
	{
		return *It;
	}
	return nullptr;
}

APlayerController* AKartFrontEnd::GetPlayer() const
{
	return GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
}

void AKartFrontEnd::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();

	// One screen in the menus: drop the extra local players a split-screen race left behind.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		const TArray<ULocalPlayer*> Locals = GameInstance->GetLocalPlayers();
		for (int32 i = Locals.Num() - 1; i >= 1; --i)
		{
			GameInstance->RemoveLocalPlayer(Locals[i]);
		}
	}
	if (UGameViewportClient* Viewport = World->GetGameViewport())
	{
		Viewport->SetForceDisableSplitscreen(true);
	}

	Camera = World->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetFieldOfView(70.f);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	if (APlayerController* PC = GetPlayer())
	{
		PC->SetViewTarget(Camera);
	}
	UpdateCamera(0.f);

	UKartSessionSettings* Session = UKartSessionSettings::Get(this);
	TWeakObjectPtr<AKartFrontEnd> WeakThis(this);
	Screens = SNew(SKartScreenStack)
		.World(World)
		.ShowHints(true)
		.Background([WeakThis](const FKartPainter& P, const FKartScreenFrame& Frame)
		{
			if (const AKartFrontEnd* Self = WeakThis.Get())
			{
				Self->PaintBackdrop(P, Frame);
			}
		});
	Screens->OnBackAtRoot = [WeakThis]()
	{
		AKartFrontEnd* Self = WeakThis.Get();
		if (Self && Self->Screens.IsValid() && Self->Screens->Top().IsValid() && Self->Screens->Top()->GetId() == TEXT("menu"))
		{
			Self->Screens->PlaySound(EKartUISound::Back);
			Self->Screens->ReplaceScreen(KartScreens::MakeTitle(Self));
		}
	};
	BuildScreens(Session ? Session->ConsumeMenuEntry() : EKartMenuEntry::Title);

	if (UGameViewportClient* Viewport = World->GetGameViewport())
	{
		Viewport->AddViewportWidgetContent(Screens.ToSharedRef(), 20);
	}
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
	{
		UI->PushInputTarget(Screens.ToSharedRef());
		UI->ApplyVolumes();
		UI->SetMenuMusic(true);
	}
	if (APlayerController* PC = GetPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(Screens);
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
	UE_LOG(LogKartFrontEnd, Display, TEXT("Front end open on %s"), Track.IsValid() ? *Track->Id : TEXT("(no track)"));
}

void AKartFrontEnd::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Screens.IsValid())
	{
		if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
		{
			UI->PopInputTarget(Screens.ToSharedRef());
		}
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Screens.ToSharedRef());
			Viewport->SetForceDisableSplitscreen(false);
		}
		Screens->Clear();
	}
	Screens.Reset();
	for (AKartPawn* Kart : ShowroomKarts)
	{
		if (Kart)
		{
			Kart->Destroy();
		}
	}
	ShowroomKarts.Reset();
	Super::EndPlay(Reason);
}

void AKartFrontEnd::BuildScreens(EKartMenuEntry Entry)
{
	Screens->Clear();
	switch (Entry)
	{
	case EKartMenuEntry::MainMenu:
		Screens->PushScreen(KartScreens::MakeMainMenu(this));
		break;
	case EKartMenuEntry::TrackSelect:
		Screens->PushScreen(KartScreens::MakeMainMenu(this));
		Screens->PushScreen(KartScreens::MakeTracks(this));
		break;
	default:
		Screens->PushScreen(KartScreens::MakeTitle(this));
		break;
	}
}

void AKartFrontEnd::PaintBackdrop(const FKartPainter& P, const FKartScreenFrame& Frame) const
{
	// A soft vignette so white type never sits on a bright sky edge.
	using namespace KartUI;
	P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, Frame.Size.Y * 0.18), Color::WithAlpha(Color::Night, 0.35f), Color::WithAlpha(Color::Night, 0.f));
	P.Gradient(FVector2D(0.0, Frame.Size.Y * 0.82), FVector2D(Frame.Size.X, Frame.Size.Y * 0.18), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.4f));
}

void AKartFrontEnd::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	UpdateCamera(DeltaSeconds);
	// Console-style cursor: hidden while a gamepad drives the menus.
	if (APlayerController* PC = GetPlayer())
	{
		const UKartUISubsystem* UI = UKartUISubsystem::Get(this);
		const bool bShow = !UI || !UI->IsUsingGamepad();
		if (PC->ShouldShowMouseCursor() != bShow)
		{
			PC->SetShowMouseCursor(bShow);
		}
	}
}

void AKartFrontEnd::SetShot(EKartFrontEndShot NewShot)
{
	if (NewShot == Shot)
	{
		return;
	}
	PreviousShot = Shot;
	Shot = NewShot;
	ShotChangedAt = Clock;
}

FTransform AKartFrontEnd::ShotTransform(EKartFrontEndShot InShot, double Time) const
{
	if (!Track.IsValid())
	{
		return FTransform(FRotator(-20.0, 0.0, 0.0), FVector(0.0, 0.0, 2000.0));
	}
	auto LookAt = [](const FVector& Eye, const FVector& Target)
	{
		return FTransform((Target - Eye).Rotation(), Eye);
	};
	const FTrack& T = *Track;

	if (InShot == EKartFrontEndShot::Flyover && T.MinimapBounds.bIsValid)
	{
		// The attract shot: a low drone circling the circuit, looking across it, a little faster than the menus' orbit.
		const FVector2D Centre = T.MinimapBounds.GetCenter();
		const double Extent = FMath::Max(T.MinimapBounds.GetSize().X, T.MinimapBounds.GetSize().Y);
		const double Angle = 0.9 + Time * 0.055;
		const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector Eye = Coords::ToUE(Centre + Dir * Extent * 0.48, Extent * 0.11 + 6.0 * FMath::Sin(Time * 0.3));
		return LookAt(Eye, Coords::ToUE(Centre - Dir * Extent * 0.12, -Extent * 0.02));
	}
	if (InShot == EKartFrontEndShot::Showroom && T.Grid.Num() > 0)
	{
		// In front of the karts on the grid, low, swaying slowly; they sit in the upper half of the frame.
		const int32 Count = FMath::Clamp(ShowroomKarts.Num(), 1, T.Grid.Num());
		FVector2D Centre = FVector2D::ZeroVector;
		for (int32 i = 0; i < Count; ++i)
		{
			Centre += T.Grid[i].Position;
		}
		Centre /= Count;
		const double Yaw = T.Grid[0].Yaw;
		const FVector2D Forward(-FMath::Sin(Yaw), -FMath::Cos(Yaw)); // three.js yaw 0 faces -Z
		const FVector2D Right(-Forward.Y, Forward.X);
		const double Spread = Count > 1 ? 2.0 * Count : 0.0;
		const double Distance = 9.0 + Spread;
		const double Sway = FMath::Sin(Time * 0.25) * 0.35;
		const FVector2D EyeXZ = Centre + (Forward * FMath::Cos(Sway) + Right * FMath::Sin(Sway)) * Distance;
		const FVector Eye = Coords::ToUE(EyeXZ, 2.4 + Count * 0.3);
		// Aim below the karts so they sit in the top half, above the player cards.
		const FVector Target = Coords::ToUE(Centre - Forward * 1.5, -0.9);
		return LookAt(Eye, Target);
	}
	// Orbit: high above the whole circuit, turning slowly.
	const FVector2D Centre = T.MinimapBounds.bIsValid ? T.MinimapBounds.GetCenter() : FVector2D::ZeroVector;
	const double Extent = T.MinimapBounds.bIsValid ? FMath::Max(T.MinimapBounds.GetSize().X, T.MinimapBounds.GetSize().Y) : 200.0;
	const double Angle = Time * 0.035;
	const FVector2D EyeXZ = Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Extent * 0.62;
	const FVector Eye = Coords::ToUE(EyeXZ, Extent * 0.32);
	return LookAt(Eye, Coords::ToUE(Centre, 0.0));
}

void AKartFrontEnd::UpdateCamera(float DeltaSeconds)
{
	if (!Camera)
	{
		return;
	}
	FTransform Target = ShotTransform(Shot, Clock);
	const float Blend = KartUI::Ease::InOutSine(static_cast<float>((Clock - ShotChangedAt) / ShotBlendSeconds));
	if (Blend < 1.f)
	{
		const FTransform From = ShotTransform(PreviousShot, Clock);
		const FTransform To = Target;
		Target.Blend(From, To, Blend);
	}
	Camera->SetActorLocationAndRotation(Target.GetLocation(), Target.GetRotation());
	Camera->GetCameraComponent()->SetFieldOfView(Shot == EKartFrontEndShot::Showroom ? 55.f : 70.f);
}

void AKartFrontEnd::SetShowroom(const TArray<FKartLook>& Looks)
{
	if (!Track.IsValid())
	{
		return;
	}
	const int32 Count = FMath::Min(Looks.Num(), Track->Grid.Num());
	while (ShowroomKarts.Num() > Count)
	{
		if (AKartPawn* Kart = ShowroomKarts.Pop())
		{
			Kart->Destroy();
		}
	}
	for (int32 i = 0; i < Count; ++i)
	{
		if (!ShowroomKarts.IsValidIndex(i))
		{
			const FSpawnSlot& Slot = Track->Grid[i];
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AKartPawn* Kart = GetWorld()->SpawnActor<AKartPawn>(AKartPawn::StaticClass(),
				FTransform(FRotator(0.0, Coords::YawToUE(Slot.Yaw), 0.0), Coords::ToUE(Slot.Position)), Params);
			Kart->GetKartMovement()->InitSim(Track, i);
			if (UKartAudioComponent* Audio = Kart->GetKartAudio())
			{
				Audio->SetPaused(true); // the music has the stage
			}
			ShowroomKarts.Add(Kart);
		}
		ShowroomKarts[i]->SetLook(Looks[i]);
	}
}

void AKartFrontEnd::StartRace()
{
	UKartSessionSettings* Session = UKartSessionSettings::Get(this);
	UKartUISubsystem* UI = UKartUISubsystem::Get(this);
	if (bStarting || !Session || !UI)
	{
		return;
	}
	bStarting = true;
	Session->StartLocalRace();
	const FString Level = FKartTrackCatalog::LevelFor(Session->GetSetup().TrackId);
	UI->SetMenuMusic(false);
	UI->TravelTo(Level.IsEmpty() ? UGameplayStatics::GetCurrentLevelName(this) : Level);
}

void AKartFrontEnd::StartParty()
{
	UKartSessionSettings* Session = UKartSessionSettings::Get(this);
	UKartUISubsystem* UI = UKartUISubsystem::Get(this);
	if (bStarting || !Session || !UI)
	{
		return;
	}
	bStarting = true;
	Session->StartParty();
	// The lobby opens on this track's level; the room's track pick travels on start, as with -KartRoom.
	UI->TravelTo(UGameplayStatics::GetCurrentLevelName(this));
}

void AKartFrontEnd::QuitGame()
{
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
	{
		UI->QuitGame();
	}
}

bool AKartFrontEnd::TourShow(FName Step, double SecondsInStep)
{
	static const TMap<FName, int32> Settle = {
		{ TEXT("title"), 3 }, { TEXT("menu"), 2 }, { TEXT("racers"), 4 }, { TEXT("rules"), 2 }, { TEXT("tracks"), 3 },
		{ TEXT("settings"), 2 }, { TEXT("audio"), 2 }, { TEXT("controls"), 2 }, { TEXT("credits"), 2 }, { TEXT("quit"), 2 },
	};
	const int32* Seconds = Settle.Find(Step);
	UKartUISubsystem* UI = UKartUISubsystem::Get(this);
	if (!Seconds || !UI || !Screens.IsValid())
	{
		return false;
	}
	const TSharedPtr<FKartScreen>& Top = Screens->Top();
	const FName Expected = Step == TEXT("quit") ? FName(TEXT("confirm")) : Step;
	if (!Top.IsValid() || Top->GetId() != Expected)
	{
		if (SecondsInStep < 0.2)
		{
			return true;
		}
		Screens->Clear();
		TourShownAt = Clock;
		if (Step == TEXT("title"))
		{
			Screens->PushScreen(KartScreens::MakeTitle(this));
			return true;
		}
		Screens->PushScreen(KartScreens::MakeMainMenu(this));
		if (Step == TEXT("racers") || Step == TEXT("rules") || Step == TEXT("tracks"))
		{
			Screens->PushScreen(KartScreens::MakeRacers(this));
			if (Step != TEXT("racers"))
			{
				Screens->PushScreen(KartScreens::MakeRules(this));
			}
			if (Step == TEXT("tracks"))
			{
				Screens->PushScreen(KartScreens::MakeTracks(this));
			}
		}
		else if (Step == TEXT("settings") || Step == TEXT("audio") || Step == TEXT("controls"))
		{
			Screens->PushScreen(KartScreens::MakeSettings(Step == TEXT("settings") ? 0 : Step == TEXT("audio") ? 1 : 2));
		}
		else if (Step == TEXT("credits"))
		{
			Screens->PushScreen(KartScreens::MakeCredits());
		}
		else if (Step == TEXT("quit"))
		{
			Screens->PushScreen(KartScreens::MakeConfirm(FText::FromString(TEXT("Quit the game?")), FText::FromString(TEXT("See you on the track soon.")),
				FText::FromString(TEXT("Quit")), FText::FromString(TEXT("Stay")), nullptr));
		}
		return true;
	}
	if (Clock - TourShownAt >= *Seconds)
	{
		UI->TourShot(Step);
	}
	return true;
}

void AKartFrontEnd::TourStartRace()
{
	if (!bTourRaceStarted)
	{
		bTourRaceStarted = true;
		StartRace();
	}
}
