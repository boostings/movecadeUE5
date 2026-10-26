#include "Games/SArcadeOverlay.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeGame.h"
#include "Games/ArcadeOverlayScreens.h"
#include "Games/ArcadeSession.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/KartScreen.h"
#include "UI/KartUISubsystem.h"
#include "UI/Screens/KartScreens.h"

namespace
{
	/**
	 * The live overlay of each world, so a game can ask IsMenuOpen without holding the overlay. A deliberate registry:
	 * entries are added in Construct, removed in Detach, and dead ones are swept when an overlay is destroyed.
	 */
	TMap<TWeakObjectPtr<const UWorld>, TWeakPtr<SArcadeOverlay>> GOverlays;

	/** Over the game's HUD (10), under the curtain. */
	constexpr int32 OverlayZOrder = 30;
	/** When the catalog has no results delay for a game. */
	constexpr double DefaultResultsDelay = 2.0;

	/**
	 * Screenshot hooks for the overlay's layouts (not in Shipping): -ArcadePauseAt=N opens the pause menu and
	 * -ArcadeResultsAt=N the results bar N seconds after the overlay appears, whatever the game is doing.
	 */
	struct FOverlayShotFlags
	{
		double PauseAt = -1.0;
		double ResultsAt = -1.0;
		/** -ArcadeSettingsAt=N[-ArcadeSettingsTab=T]: the pause menu's settings, on tab T, N seconds in. */
		double SettingsAt = -1.0;
		int32 SettingsTab = 0;

		static const FOverlayShotFlags& Get()
		{
			static const FOverlayShotFlags Flags = []()
			{
				FOverlayShotFlags Out;
#if !UE_BUILD_SHIPPING
				FParse::Value(FCommandLine::Get(), TEXT("ArcadePauseAt="), Out.PauseAt);
				FParse::Value(FCommandLine::Get(), TEXT("ArcadeResultsAt="), Out.ResultsAt);
				FParse::Value(FCommandLine::Get(), TEXT("ArcadeSettingsAt="), Out.SettingsAt);
				FParse::Value(FCommandLine::Get(), TEXT("ArcadeSettingsTab="), Out.SettingsTab);
#endif
				return Out;
			}();
			return Flags;
		}
	};
}

SArcadeOverlay::~SArcadeOverlay()
{
	for (auto It = GOverlays.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void SArcadeOverlay::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	Interface = InArgs._Interface;
	bRoom = InArgs._Room;
	World = Game.IsValid() ? Game->GetWorld() : nullptr;
	if (Interface)
	{
		const MovecadeKart::Arcade::FGameInfo* Info = MovecadeKart::Arcade::Find(Interface->GetArcadeId());
		ResultsDelay = Info ? Info->ResultsDelay : DefaultResultsDelay;
	}
	SetVisibility(EVisibility::SelfHitTestInvisible);
	SetCanTick(true);
	ChildSlot
	[
		SAssignNew(Menus, SKartScreenStack).World(World).ShowHints(true)
	];
	Menus->SetVisibility(EVisibility::Collapsed);
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(World.Get()))
	{
		UI->PushInputTarget(StaticCastSharedRef<IKartMenuInputTarget>(StaticCastSharedRef<SArcadeOverlay>(AsShared())));
		bRegistered = true;
	}
	GOverlays.Add(World, StaticCastSharedRef<SArcadeOverlay>(AsShared()));
}

TSharedPtr<SArcadeOverlay> SArcadeOverlay::AttachTo(AActor* InGame, IArcadeGame* InInterface, bool bInRoom)
{
	UWorld* W = InGame ? InGame->GetWorld() : nullptr;
	UGameViewportClient* Viewport = W ? W->GetGameViewport() : nullptr;
	if (!Viewport || !InInterface)
	{
		return nullptr;
	}
	TSharedRef<SArcadeOverlay> Overlay = SNew(SArcadeOverlay).Game(InGame).Interface(InInterface).Room(bInRoom);
	Viewport->AddViewportWidgetContent(Overlay, OverlayZOrder);
	return Overlay;
}

void SArcadeOverlay::Detach()
{
	if (bRegistered)
	{
		if (UKartUISubsystem* UI = UKartUISubsystem::Get(World.Get()))
		{
			UI->PopInputTarget(StaticCastSharedRef<IKartMenuInputTarget>(StaticCastSharedRef<SArcadeOverlay>(AsShared())));
		}
		bRegistered = false;
	}
	if (UWorld* W = World.Get())
	{
		if (UGameViewportClient* Viewport = W->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(AsShared());
		}
	}
	GOverlays.Remove(World);
}

bool SArcadeOverlay::IsMenuOpen(const UWorld* InWorld)
{
	const TWeakPtr<SArcadeOverlay>* Found = GOverlays.Find(InWorld);
	const TSharedPtr<SArcadeOverlay> Overlay = Found ? Found->Pin() : nullptr;
	return Overlay.IsValid() && Overlay->AreMenusOpen();
}

IArcadeGame* SArcadeOverlay::GetGame() const
{
	return Game.IsValid() ? Interface : nullptr;
}

bool SArcadeOverlay::AreMenusOpen() const
{
	return Menus.IsValid() && Menus->Depth() > 0;
}

void SArcadeOverlay::OpenPause()
{
	IArcadeGame* G = GetGame();
	if (!G || AreMenusOpen() || bRoom)
	{
		return;
	}
	G->SetArcadePaused(true);
	Menus->Clear();
	Menus->PushScreen(MovecadeKart::Arcade::Overlay::MakePauseScreen(StaticCastSharedRef<SArcadeOverlay>(AsShared())));
	Menus->SetVisibility(EVisibility::Visible);
	Menus->PlaySound(EKartUISound::Pause);
	UpdateCursor();
}

void SArcadeOverlay::CloseMenus(bool bResume)
{
	if (Menus.IsValid())
	{
		Menus->Clear();
		Menus->SetVisibility(EVisibility::Collapsed);
	}
	if (IArcadeGame* G = GetGame(); G && G->IsArcadePaused() && !bRoom)
	{
		G->SetArcadePaused(false);
		if (bResume)
		{
			Menus->PlaySound(EKartUISound::Resume);
		}
	}
	bResultsShown = false;
	UpdateCursor();
}

void SArcadeOverlay::ShowResults()
{
	Menus->Clear();
	Menus->PushScreen(MovecadeKart::Arcade::Overlay::MakeResultsScreen(StaticCastSharedRef<SArcadeOverlay>(AsShared()), bRoom));
	Menus->SetVisibility(EVisibility::Visible);
	bResultsShown = true;
	UpdateCursor();
}

void SArcadeOverlay::UpdateCursor()
{
	APlayerController* PC = World.IsValid() ? World->GetFirstPlayerController() : nullptr;
	const bool bShow = AreMenusOpen();
	if (!PC || bShow == bCursorShown)
	{
		return;
	}
	bCursorShown = bShow;
	PC->SetShowMouseCursor(bShow);
	if (bShow)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
	}
}

void SArcadeOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	IArcadeGame* G = GetGame();
	if (!G)
	{
		return;
	}
	if (AttachedAt < 0.0)
	{
		AttachedAt = InCurrentTime;
	}
	if (!bShotHookFired)
	{
		const FOverlayShotFlags& Shot = FOverlayShotFlags::Get();
		const double Age = InCurrentTime - AttachedAt;
		if (Shot.PauseAt >= 0.0 && Age >= Shot.PauseAt)
		{
			bShotHookFired = true;
			OpenPause();
		}
		else if (Shot.SettingsAt >= 0.0 && Age >= Shot.SettingsAt)
		{
			bShotHookFired = true;
			OpenPause();
			if (AreMenusOpen())
			{
				Menus->PushScreen(KartScreens::MakeSettings(Shot.SettingsTab));
			}
		}
		else if (Shot.ResultsAt >= 0.0 && Age >= Shot.ResultsAt)
		{
			bShotHookFired = true;
			ShowResults();
			return;
		}
	}
	if (bShotHookFired && bResultsShown && G->GetArcadeFinishedAge() < 0.0)
	{
		return; // the forced results bar stays up over the running game
	}
	const double FinishedAge = G->GetArcadeFinishedAge();
	if (FinishedAge < 0.0)
	{
		// Restarted (here or by the room): the next finish counts again.
		bRecorded = false;
		const TSharedPtr<FKartScreen>& Top = Menus->Top();
		const bool bStalePause = Top.IsValid() && Top->GetId() == MovecadeKart::Arcade::Overlay::PauseScreenId && !G->IsArcadePaused();
		if (bResultsShown || bStalePause)
		{
			CloseMenus(false);
		}
		return;
	}
	if (!bRecorded)
	{
		RecordFinishedGame(*G, InCurrentTime);
	}
	if (!bResultsShown && FinishedAge >= ResultsDelay)
	{
		ShowResults();
	}
}

void SArcadeOverlay::RecordFinishedGame(const IArcadeGame& G, double Now)
{
	// The party table counts every finished game once: the key is unique to this game actor and finish.
	bRecorded = true;
	if (UArcadeSession* Session = UArcadeSession::Get(World.Get()))
	{
		TArray<FArcadeStanding> Rows;
		G.GetArcadeStandings(Rows);
		Session->RecordResult(FString::Printf(TEXT("%s-%p-%.3f"), *G.GetArcadeId(), Game.Get(), Now), Rows);
	}
}

bool SArcadeOverlay::HandleMenuEvent(const FKartMenuEvent& Event)
{
	if (AreMenusOpen())
	{
		Menus->HandleMenuEvent(Event);
		// Nothing reaches the game while a menu is up.
		return true;
	}
	IArcadeGame* G = GetGame();
	if (G && !bRoom && !Event.bRepeat && FKartMenuInputProcessor::IsPauseKey(Event.Key) && G->GetArcadeFinishedAge() < 0.0)
	{
		OpenPause();
		return true;
	}
	return false;
}
