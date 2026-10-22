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
	/** Camera moves between shots blend over this long. */
	constexpr double ShotBlendSeconds = 1.2;
	constexpr float MenuFieldOfView = 70.f;
	/** The showroom shot frames the karts tighter. */
	constexpr float ShowroomFieldOfView = 55.f;

	// Shot speeds (radians per second) and framing, as fractions of the track's extent (web units).
	/** Flyover: a low drone circling the circuit. */
	constexpr double FlyoverStartAngle = 0.9;
	constexpr double FlyoverTurnRate = 0.055;
	constexpr double FlyoverRadius = 0.48;
	constexpr double FlyoverHeight = 0.11;
	/** The drone bobs a few metres (web units) and looks across the circuit to a point past its centre. */
	constexpr double FlyoverBob = 6.0;
	constexpr double FlyoverBobRate = 0.3;
	constexpr double FlyoverAimAhead = 0.12;
	constexpr double FlyoverAimDepth = 0.02;
	/** Orbit: high above the whole circuit, turning slowly. */
	constexpr double OrbitTurnRate = 0.035;
	constexpr double OrbitRadius = 0.62;
	constexpr double OrbitHeight = 0.32;
	/** Showroom: in front of the karts on the grid, swaying slowly (web units). */
	constexpr double ShowroomDistance = 9.0;
	constexpr double ShowroomSwayRate = 0.25;
	constexpr double ShowroomSway = 0.35;
	/** More karts: the camera backs off and rises. */
	constexpr double ShowroomSpreadPerKart = 2.0;
	constexpr double ShowroomHeight = 2.4;
	constexpr double ShowroomHeightPerKart = 0.3;
	/** It aims behind and below the karts so they sit in the top half, above the player cards. */
	constexpr double ShowroomAimBehind = 1.5;
	constexpr double ShowroomAimDepth = -0.9;
	/** Without a track: a plain look down from above, and the orbit's fallback size. */
	constexpr double NoTrackPitch = -20.0;
	constexpr double NoTrackHeight = 2000.0;
	constexpr double NoTrackExtent = 200.0;

	/** The backdrop's vignettes, so white type never sits on a bright sky or sea edge. */
	constexpr double BackdropEdge = 0.18;
	constexpr double BackdropBottomTop = 0.82;
	constexpr float BackdropTopAlpha = 0.35f;
	constexpr float BackdropBottomAlpha = 0.4f;
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
	Camera->GetCameraComponent()->SetFieldOfView(MenuFieldOfView);
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
	case EKartMenuEntry::Arcade:
		Screens->PushScreen(KartScreens::MakeMainMenu(this));
		Screens->PushScreen(KartScreens::MakeArcade(this));
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
	P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X, Frame.Size.Y * BackdropEdge), Color::WithAlpha(Color::Night, BackdropTopAlpha), Color::WithAlpha(Color::Night, 0.f));
	P.Gradient(FVector2D(0.0, Frame.Size.Y * BackdropBottomTop), FVector2D(Frame.Size.X, Frame.Size.Y * BackdropEdge), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, BackdropBottomAlpha));
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
		return FTransform(FRotator(NoTrackPitch, 0.0, 0.0), FVector(0.0, 0.0, NoTrackHeight));
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
		const double Angle = FlyoverStartAngle + Time * FlyoverTurnRate;
		const FVector2D Dir(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector Eye = Coords::ToUE(Centre + Dir * Extent * FlyoverRadius, Extent * FlyoverHeight + FlyoverBob * FMath::Sin(Time * FlyoverBobRate));
		return LookAt(Eye, Coords::ToUE(Centre - Dir * Extent * FlyoverAimAhead, -Extent * FlyoverAimDepth));
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
		const double Spread = Count > 1 ? ShowroomSpreadPerKart * Count : 0.0;
		const double Distance = ShowroomDistance + Spread;
		const double Sway = FMath::Sin(Time * ShowroomSwayRate) * ShowroomSway;
		const FVector2D EyeXZ = Centre + (Forward * FMath::Cos(Sway) + Right * FMath::Sin(Sway)) * Distance;
		const FVector Eye = Coords::ToUE(EyeXZ, ShowroomHeight + Count * ShowroomHeightPerKart);
		// Aim below the karts so they sit in the top half, above the player cards.
		const FVector Target = Coords::ToUE(Centre - Forward * ShowroomAimBehind, ShowroomAimDepth);
		return LookAt(Eye, Target);
	}
	// Orbit: high above the whole circuit, turning slowly.
	const FVector2D Centre = T.MinimapBounds.bIsValid ? T.MinimapBounds.GetCenter() : FVector2D::ZeroVector;
	const double Extent = T.MinimapBounds.bIsValid ? FMath::Max(T.MinimapBounds.GetSize().X, T.MinimapBounds.GetSize().Y) : NoTrackExtent;
	const double Angle = Time * OrbitTurnRate;
	const FVector2D EyeXZ = Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Extent * OrbitRadius;
	const FVector Eye = Coords::ToUE(EyeXZ, Extent * OrbitHeight);
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
	Camera->GetCameraComponent()->SetFieldOfView(Shot == EKartFrontEndShot::Showroom ? ShowroomFieldOfView : MenuFieldOfView);
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

void AKartFrontEnd::StartGame(const FString& Map, int32 Locals, int32 Bots)
{
	UKartUISubsystem* UI = UKartUISubsystem::Get(this);
	if (bStarting || !UI)
	{
		return;
	}
	bStarting = true;
	UI->TravelTo(FString::Printf(TEXT("%s?Players=%d?Bots=%d"), *Map, Locals, Bots));
}

void AKartFrontEnd::QuitGame()
{
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
	{
		UI->QuitGame();
	}
}

namespace
{
	/** How a -KartUiShots step opens its screen: the stack it builds, the screen it expects on top, and how long it settles. */
	struct FTourStep
	{
		/** Seconds the entry animations get before the capture. */
		int32 SettleSeconds = 2;
		/** The top screen's id once the step is showing (usually the step's own name). */
		FName ScreenId;
		/** Builds the stack (the main menu is already under it unless bFromTitle). */
		void (*Build)(AKartFrontEnd& FrontEnd, SKartScreenStack& Screens, FName Step) = nullptr;
		bool bFromTitle = false;
	};

	const TMap<FName, FTourStep>& TourSteps()
	{
		using FStack = SKartScreenStack;
		static const TMap<FName, FTourStep> Steps = []()
		{
			TMap<FName, FTourStep> Out;
			auto Add = [&Out](const TCHAR* Step, int32 Settle, void (*Build)(AKartFrontEnd&, FStack&, FName), const TCHAR* ScreenId = nullptr, bool bFromTitle = false)
			{
				Out.Add(FName(Step), FTourStep{ Settle, FName(ScreenId ? ScreenId : Step), Build, bFromTitle });
			};
			auto Racers = [](AKartFrontEnd& FE, FStack& S, FName) { S.PushScreen(KartScreens::MakeRacers(&FE)); };
			auto Rules = [](AKartFrontEnd& FE, FStack& S, FName) { S.PushScreen(KartScreens::MakeRacers(&FE)); S.PushScreen(KartScreens::MakeRules(&FE)); };
			auto Tracks = [](AKartFrontEnd& FE, FStack& S, FName)
			{
				S.PushScreen(KartScreens::MakeRacers(&FE));
				S.PushScreen(KartScreens::MakeRules(&FE));
				S.PushScreen(KartScreens::MakeTracks(&FE));
			};
			auto Store = [](AKartFrontEnd& FE, FStack& S, FName Step) { S.PushScreen(KartScreens::MakeStore(&FE, Step)); };
			Add(TEXT("title"), 3, [](AKartFrontEnd&, FStack&, FName) {}, nullptr, true);
			Add(TEXT("welcome"), 3, [](AKartFrontEnd& FE, FStack& S, FName) { S.PushScreen(KartScreens::MakeWelcome(&FE)); }, nullptr, true);
			Add(TEXT("menu"), 2, [](AKartFrontEnd&, FStack&, FName) {});
			Add(TEXT("racers"), 4, Racers);
			Add(TEXT("rules"), 2, Rules);
			Add(TEXT("tracks"), 3, Tracks);
			Add(TEXT("settings"), 2, [](AKartFrontEnd&, FStack& S, FName) { S.PushScreen(KartScreens::MakeSettings(0)); });
			Add(TEXT("audio"), 2, [](AKartFrontEnd&, FStack& S, FName) { S.PushScreen(KartScreens::MakeSettings(1)); });
			Add(TEXT("controls"), 2, [](AKartFrontEnd&, FStack& S, FName) { S.PushScreen(KartScreens::MakeSettings(2)); });
			Add(TEXT("accessibility"), 2, [](AKartFrontEnd&, FStack& S, FName) { S.PushScreen(KartScreens::MakeSettings(3)); });
			Add(TEXT("arcade"), 2, [](AKartFrontEnd& FE, FStack& S, FName) { S.PushScreen(KartScreens::MakeArcade(&FE)); });
			Add(TEXT("credits"), 2, [](AKartFrontEnd&, FStack& S, FName) { S.PushScreen(KartScreens::MakeCredits()); });
			Add(TEXT("quit"), 2, [](AKartFrontEnd& FE, FStack& S, FName) { S.PushScreen(KartScreens::MakeQuitConfirm(&FE)); }, TEXT("confirm"));
			// The store's own tour steps (KartStoreScreen.cpp).
			Add(TEXT("store"), 2, Store);
			Add(TEXT("storeparty"), 2, Store);
			Add(TEXT("storefree"), 2, Store);
			Add(TEXT("storebuy"), 2, Store, TEXT("store-buy"));
			Add(TEXT("storedone"), 2, Store, TEXT("store-done"));
			return Out;
		}();
		return Steps;
	}
}

bool AKartFrontEnd::TourShow(FName Step, double SecondsInStep)
{
	const FTourStep* Tour = TourSteps().Find(Step);
	UKartUISubsystem* UI = UKartUISubsystem::Get(this);
	if (!Tour || !UI || !Screens.IsValid())
	{
		return false;
	}
	const TSharedPtr<FKartScreen> Top = Screens->Top();
	if (Top.IsValid() && Top->GetId() == Tour->ScreenId)
	{
		if (Clock - TourShownAt >= Tour->SettleSeconds)
		{
			UI->TourShot(Step);
		}
		return true;
	}
	// Give the previous step's capture a moment to be drawn before the stack changes.
	if (SecondsInStep < 0.2)
	{
		return true;
	}
	Screens->Clear();
	TourShownAt = Clock;
	Screens->PushScreen(Tour->bFromTitle ? KartScreens::MakeTitle(this) : KartScreens::MakeMainMenu(this));
	Tour->Build(*this, *Screens, Step);
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
