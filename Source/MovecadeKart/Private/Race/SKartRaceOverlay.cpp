#include "Race/SKartRaceOverlay.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Race/KartRaceManager.h"
#include "UI/Hud/SKartRaceHud.h"
#include "UI/KartScreen.h"
#include "UI/KartUISubsystem.h"
#include "UI/Screens/KartScreens.h"
#include "Widgets/SOverlay.h"

namespace
{
	/** The race in progress's overlay: one race runs at a time, and the menus reach it through GetActive. */
	TWeakPtr<SKartRaceOverlay> GActiveOverlay;

	/** How long the "race over" banner plays before the results board slides in. */
	constexpr double ResultsDelaySeconds = 1.6;

	const FName PauseScreenId(TEXT("pause"));
	const FName ResultsScreenId(TEXT("results"));
	/** The pause reason of the local pause menu (a room pause from a phone has its own). */
	const TCHAR* LocalPauseReason = TEXT("local");
}

SKartRaceOverlay::~SKartRaceOverlay()
{
	if (AKartRaceManager* Race = Manager.Get())
	{
		Race->bUiOwnsHostKeys = false;
	}
}

void SKartRaceOverlay::Construct(const FArguments& InArgs)
{
	Manager = InArgs._Manager;
	GActiveOverlay = StaticCastSharedRef<SKartRaceOverlay>(AsShared());
	SetVisibility(EVisibility::SelfHitTestInvisible);
	SetCanTick(true);

	AKartRaceManager* Race = Manager.Get();
	UWorld* World = Race ? Race->GetWorld() : nullptr;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(Hud, SKartRaceHud).Manager(Manager)
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Menus, SKartScreenStack).World(World).ShowHints(true)
		]
	];
	Menus->SetVisibility(EVisibility::Collapsed);

	if (UKartUISubsystem* UI = UKartUISubsystem::Get(World))
	{
		// Pause, skip and results now come through the menu input router.
		UI->PushInputTarget(StaticCastSharedRef<IKartMenuInputTarget>(StaticCastSharedRef<SKartRaceOverlay>(AsShared())));
		Race->bUiOwnsHostKeys = true;
		bRegistered = true;
	}
}

TSharedPtr<SKartRaceOverlay> SKartRaceOverlay::GetActive()
{
	return GActiveOverlay.Pin();
}

bool SKartRaceOverlay::AreMenusOpen() const
{
	return Menus.IsValid() && Menus->Depth() > 0;
}

void SKartRaceOverlay::OpenPauseMenu()
{
	AKartRaceManager* Race = Manager.Get();
	if (!Race || AreMenusOpen())
	{
		return;
	}
	Race->SetPaused(true, LocalPauseReason);
	Menus->Clear();
	Menus->PushScreen(KartScreens::MakePause(Race));
	Menus->SetVisibility(EVisibility::Visible);
	Menus->PlaySound(EKartUISound::Pause);
	UpdateCursor();
}

void SKartRaceOverlay::ClosePauseMenu(bool bResume)
{
	AKartRaceManager* Race = Manager.Get();
	CloseMenus();
	if (Race && bResume && Race->IsPaused() && Race->GetPauseReason() == LocalPauseReason)
	{
		Race->SetPaused(false);
		Menus->PlaySound(EKartUISound::Resume);
	}
	else if (Race && !bResume && Race->GetPauseReason() == LocalPauseReason)
	{
		Race->SetPaused(false);
	}
}

void SKartRaceOverlay::ShowResults(AKartRaceManager& Race)
{
	Menus->PushScreen(KartScreens::MakeResults(&Race));
	Menus->SetVisibility(EVisibility::Visible);
	UpdateCursor();
}

void SKartRaceOverlay::CloseMenus()
{
	if (Menus.IsValid())
	{
		Menus->Clear();
		Menus->SetVisibility(EVisibility::Collapsed);
	}
	UpdateCursor();
}

void SKartRaceOverlay::UpdateCursor()
{
	AKartRaceManager* Race = Manager.Get();
	APlayerController* PC = Race && Race->GetWorld() ? Race->GetWorld()->GetFirstPlayerController() : nullptr;
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

void SKartRaceOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	AKartRaceManager* Race = Manager.Get();
	if (!Race || !bRegistered)
	{
		return;
	}
	const EKartRacePhase Phase = Race->GetPhase();
	const TSharedPtr<FKartScreen> Top = Menus->Top();
	if (Phase == EKartRacePhase::Finished)
	{
		// The results board follows the "race over" banner.
		if (!AreMenusOpen() && Race->GetPhaseSeconds() >= ResultsDelaySeconds && Race->GetResults().Num() > 0)
		{
			ShowResults(*Race);
		}
	}
	else if (Top.IsValid() && Top->GetId() == ResultsScreenId)
	{
		CloseMenus(); // restarted
	}
	// A room pause from a phone while the local menu is up, or an unpause from elsewhere.
	if (Top.IsValid() && Top->GetId() == PauseScreenId && !Race->IsPaused())
	{
		CloseMenus();
	}
}

bool SKartRaceOverlay::HandleMenuEvent(const FKartMenuEvent& Event)
{
	AKartRaceManager* Race = Manager.Get();
	if (!Race)
	{
		return false;
	}
	if (AreMenusOpen())
	{
		return Menus->HandleMenuEvent(Event);
	}
	const EKartRacePhase Phase = Race->GetPhase();
	// The intro skips on Enter or Start (as before); A and Space are gas and drift.
	const bool bSkipKey = Event.Key == EKeys::Enter || Event.Key == EKeys::Gamepad_Special_Right;
	if ((Phase == EKartRacePhase::Intro || Phase == EKartRacePhase::Waiting) && bSkipKey && !Event.bRepeat)
	{
		Race->SkipIntro();
		return true;
	}
	// The local pause menu; in room mode the room owns pauses (a phone pauses from its own menu).
	const bool bRoom = static_cast<bool>(Race->OnRestartRequested);
	if (!Race->IsPaused() && !bRoom && !Event.bRepeat && FKartMenuInputProcessor::IsPauseKey(Event.Key)
		&& (Phase == EKartRacePhase::Countdown || Phase == EKartRacePhase::Racing))
	{
		OpenPauseMenu();
		return true;
	}
	if (Phase == EKartRacePhase::Finished && bSkipKey && !Event.bRepeat && Race->GetResults().Num() > 0)
	{
		ShowResults(*Race);
		return true;
	}
	return false;
}

FString SKartRaceOverlay::FormatRaceTime(double Ms, bool bMillis)
{
	const int64 Total = FMath::Max<int64>(0, FMath::RoundToInt64(Ms));
	const int64 Minutes = Total / 60000;
	const int64 Seconds = (Total / 1000) % 60;
	return bMillis
		? FString::Printf(TEXT("%lld:%02lld.%03lld"), Minutes, Seconds, Total % 1000)
		: FString::Printf(TEXT("%lld:%02lld"), Minutes, Seconds);
}

FString SKartRaceOverlay::OrdinalSuffix(int32 Place)
{
	return (Place % 100 >= 11 && Place % 100 <= 13) ? TEXT("th")
		: Place % 10 == 1 ? TEXT("st") : Place % 10 == 2 ? TEXT("nd") : Place % 10 == 3 ? TEXT("rd") : TEXT("th");
}

FString SKartRaceOverlay::Ordinal(int32 Place)
{
	return FString::Printf(TEXT("%d%s"), Place, *OrdinalSuffix(Place));
}
