#include "Games/SArcadeOverlay.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeGame.h"
#include "Games/ArcadeSession.h"
#include "Kismet/GameplayStatics.h"
#include "Net/RoomHostSubsystem.h"
#include "UI/KartScreen.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartUISubsystem.h"
#include "UI/KartWidgets.h"
#include "UI/Screens/KartScreens.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "ArcadeOverlay"

using namespace KartUI;
using KartWidgets::FItemState;
namespace Arcade = MovecadeKart::Arcade;

namespace
{
	/** Overlays by world: the games ask whether a menu is up. */
	TMap<TWeakObjectPtr<const UWorld>, TWeakPtr<SArcadeOverlay>> GOverlays;

	/** Party Mode: the results bar plays again by itself after this long untouched. */
	constexpr double RoomAutoSeconds = 25.0;

	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	URoomHostSubsystem* RoomOf(const UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoomHostSubsystem>() : nullptr;
	}

	/** Back to the front end at Entry (leaving the room, in Party Mode), with the curtain. */
	void ReturnToMenu(UWorld* World, EKartMenuEntry Entry)
	{
		UKartSessionSettings* Session = UKartSessionSettings::Get(World);
		UKartUISubsystem* UI = UKartUISubsystem::Get(World);
		if (!World || !Session || !UI)
		{
			return;
		}
		if (URoomHostSubsystem* Room = RoomOf(World); Room && Session->GetMode() == EKartSessionMode::Party)
		{
			Room->CloseRoom(true);
		}
		if (UArcadeSession* Arcade = UArcadeSession::Get(World); Arcade && Entry == EKartMenuEntry::MainMenu)
		{
			// Leaving the arcade for the main menu ends the evening's party table.
			Arcade->ResetParty();
		}
		Session->ReturnToMenu(Entry);
		// The front end lives on the harbour level.
		UI->TravelTo(TEXT("L_SunsetHarbor"));
	}

	/** A row of buttons with keyboard, pad and mouse focus. */
	struct FButtonRow
	{
		FKartMenuCursor Cursor;
		double FocusAt = 0.0;

		bool Move(int32 Delta, IKartScreenHost* Host)
		{
			if (Cursor.Move(Delta))
			{
				FocusAt = NowSeconds();
				Host->PlaySound(EKartUISound::Move);
				return true;
			}
			return false;
		}

		void Hover(int32 Id, IKartScreenHost* Host)
		{
			if (Id >= 0 && Id < Cursor.Count && Cursor.Set(Id))
			{
				FocusAt = NowSeconds();
				Host->PlaySound(EKartUISound::Move);
			}
		}

		FItemState State(int32 Row, double Now) const
		{
			FItemState S;
			S.bFocused = Row == Cursor.Index;
			S.FocusAge = Now - FocusAt;
			return S;
		}
	};

	// ----------------------------------------------------------------------------------------------- Pause

	class FArcadePauseScreen final : public FKartScreen
	{
	public:
		explicit FArcadePauseScreen(TWeakPtr<SArcadeOverlay> InOverlay) : Overlay(InOverlay)
		{
			Buttons.Cursor.Count = 6;
		}

		virtual FName GetId() const override { return TEXT("arcade-pause"); }
		virtual void OnShown() override { Buttons.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Accept, LOCTEXT("Select", "Select") }, { EKartMenuAction::Back, LOCTEXT("Resume", "Resume") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (Event.Action == EKartMenuAction::Up || Event.Action == EKartMenuAction::Down)
			{
				Buttons.Move(Event.Action == EKartMenuAction::Up ? -1 : 1, Host);
				return true;
			}
			if (Event.Action == EKartMenuAction::Accept)
			{
				Activate(Buttons.Cursor.Index);
				return true;
			}
			if (Event.Action == EKartMenuAction::Back || Event.Action == EKartMenuAction::Pause || Event.Action == EKartMenuAction::Start)
			{
				Activate(0);
				return true;
			}
			return false;
		}

		virtual void OnHover(int32 HitId) override { Buttons.Hover(HitId, Host); }
		virtual void OnClick(int32 HitId) override { Activate(HitId); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const TSharedPtr<SArcadeOverlay> O = Overlay.Pin();
			const IArcadeGame* G = O.IsValid() ? O->GetGame() : nullptr;
			const Arcade::FGameInfo* Info = G ? Arcade::Find(G->GetArcadeId()) : nullptr;
			const FLinearColor Accent = Info ? Info->Accent : Color::Sun;
			const float In = KartWidgets::Entrance(Frame, 0.f, 0.25f);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.6f * In));
			const FVector2D Size(1180.0, 760.0);
			const FVector2D Pos = Frame.Centre() - Size * 0.5;
			const float Pop = Ease::OutBack(static_cast<float>(Frame.Age / 0.32), 2.f);
			const FKartPainter Card = P.Transformed(Frame.Centre(), 0.8f + 0.2f * Pop);
			Card.Card(Pos, Size, Color::WithAlpha(Color::Night, 0.94f), Metric::RadiusLarge, 12.f, Accent, 5.f);
			Card.Text(LOCTEXT("Paused", "PAUSED"), Font(EFont::Display, 84.f, 5.f), Pos + FVector2D(300.0, 92.0), Accent, FVector2D(0.5, 0.5), 5.f);
			if (Info)
			{
				Card.Text(Info->Name, Font(EFont::Title, 30.f), Pos + FVector2D(300.0, 160.0), Color::WithAlpha(Color::White, 0.85f), FVector2D(0.5, 0.5));
			}
			const FText Names[] = { LOCTEXT("ResumeButton", "Resume"), LOCTEXT("Restart", "Restart"), LOCTEXT("HowTo", "How to play"),
				LOCTEXT("Settings", "Settings"), LOCTEXT("ChangeGame", "Change game"), LOCTEXT("QuitMenu", "Main menu") };
			const FLinearColor Accents[] = { Color::Mint, Color::Sky, Color::Sun, Color::Grape, Color::Sky, Color::Hot };
			const FVector2D ButtonSize(480.0, 74.0);
			for (int32 i = 0; i < 6; ++i)
			{
				const FVector2D BPos(Pos.X + 60.0, Pos.Y + 210.0 + i * (ButtonSize.Y + 12.0));
				KartWidgets::Button(Frame.Age > 0.32 ? P : Card, BPos, ButtonSize, Names[i], Buttons.State(i, Frame.Now), Accents[i], i);
			}
			// The right half: how to play this game, always in view while paused.
			const FVector2D Help(Pos.X + 600.0, Pos.Y + 210.0);
			Card.Box(Help, FVector2D(530.0, 490.0), Color::WithAlpha(Color::White, 0.06f), 28.f);
			Card.Text(LOCTEXT("HowToTitle", "HOW TO PLAY"), Font(EFont::Label, 22.f, 2.f), Help + FVector2D(32.0, 44.0), Accent, FVector2D(0.0, 0.5));
			if (Info)
			{
				Card.TextWrapped(Info->HowTo, Font(EFont::Body, 26.f), Help + FVector2D(32.0, 84.0), 466.0, Color::White);
				Card.TextWrapped(Info->Tagline, Font(EFont::Body, 20.f), Help + FVector2D(32.0, 380.0), 466.0, Color::WithAlpha(Color::White, 0.6f));
			}
			if (bHelpPulse)
			{
				const float Pulse = static_cast<float>(FMath::Max(0.0, 1.0 - (Frame.Now - HelpAt) / 0.6));
				Card.Card(Help, FVector2D(530.0, 490.0), FLinearColor::Transparent, 28.f, 0.f, Color::WithAlpha(Accent, Pulse), 5.f);
			}
		}

	private:
		void Activate(int32 Item)
		{
			const TSharedPtr<SArcadeOverlay> O = Overlay.Pin();
			if (!O.IsValid())
			{
				return;
			}
			UWorld* World = Host->GetWorld();
			switch (Item)
			{
			case 0:
				O->CloseMenus(true);
				break;
			case 1:
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("RestartTitle", "Restart the game?"), LOCTEXT("RestartBody", "Scores go back to zero."),
					LOCTEXT("RestartYes", "Restart"), LOCTEXT("RestartNo", "Cancel"), [Overlay = Overlay]()
					{
						if (const TSharedPtr<SArcadeOverlay> Pinned = Overlay.Pin())
						{
							Pinned->CloseMenus(true);
							if (IArcadeGame* G = Pinned->GetGame())
							{
								G->ArcadeRestart();
							}
						}
					}));
				break;
			case 2:
				// The help panel is already on the right; draw the eye to it.
				Host->PlaySound(EKartUISound::Accept);
				bHelpPulse = true;
				HelpAt = NowSeconds();
				break;
			case 3:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeSettings(0));
				break;
			case 4:
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("ChangeTitle", "Pick another game?"), LOCTEXT("ChangeBody", "This game won't count."),
					LOCTEXT("ChangeYes", "Change game"), LOCTEXT("ChangeNo", "Keep playing"), [World]() { ReturnToMenu(World, EKartMenuEntry::Arcade); }));
				break;
			default:
				Host->PushScreen(KartScreens::MakeConfirm(LOCTEXT("QuitTitle", "Quit to the menu?"), LOCTEXT("QuitBody", "This game won't count."),
					LOCTEXT("QuitYes", "Quit"), LOCTEXT("QuitNo", "Keep playing"), [World]() { ReturnToMenu(World, EKartMenuEntry::MainMenu); }));
				break;
			}
		}

		TWeakPtr<SArcadeOverlay> Overlay;
		FButtonRow Buttons;
		bool bHelpPulse = false;
		double HelpAt = 0.0;
	};

	// --------------------------------------------------------------------------------------------- Results

	class FArcadeResultsScreen final : public FKartScreen
	{
	public:
		FArcadeResultsScreen(TWeakPtr<SArcadeOverlay> InOverlay, bool bInRoom) : Overlay(InOverlay), bRoom(bInRoom)
		{
			Buttons.Cursor.Count = 4;
			Buttons.Cursor.bWrap = false;
			ShownAt = NowSeconds();
		}

		virtual FName GetId() const override { return TEXT("arcade-results"); }
		virtual bool CanGoBack() const override { return false; }
		virtual void OnShown() override { Buttons.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Choose", "Choose") }, { EKartMenuAction::Accept, LOCTEXT("Select", "Select") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			bTouched = true;
			if (Event.Action == EKartMenuAction::Left || Event.Action == EKartMenuAction::Right)
			{
				Buttons.Move(Event.Action == EKartMenuAction::Left ? -1 : 1, Host);
				return true;
			}
			if (Event.Action == EKartMenuAction::Accept || Event.Action == EKartMenuAction::Start)
			{
				Activate(Buttons.Cursor.Index);
				return true;
			}
			return true;
		}

		virtual void OnHover(int32 HitId) override
		{
			bTouched = true;
			Buttons.Hover(HitId, Host);
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId >= 0 && HitId < Buttons.Cursor.Count)
			{
				Activate(HitId);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const TSharedPtr<SArcadeOverlay> O = Overlay.Pin();
			const IArcadeGame* G = O.IsValid() ? O->GetGame() : nullptr;
			if (!G)
			{
				return;
			}
			const FString Id = G->GetArcadeId();
			const Arcade::FGameInfo& Next = Arcade::NextAfter(Id);
			const float In = KartWidgets::Entrance(Frame, 0.f, 0.45f);
			// A band along the bottom that the game's own standings stay clear of.
			P.Gradient(FVector2D(0.0, Frame.Size.Y - 330.0), FVector2D(Frame.Size.X, 330.0), Color::WithAlpha(Color::Night, 0.f), Color::WithAlpha(Color::Night, 0.86f * In), true);

			// The party table: everyone's running points across the evening.
			const UArcadeSession* Session = UArcadeSession::Get(Host->GetWorld());
			const double ChipsY = Frame.SafeMax.Y - 66.0 - 92.0 - 84.0;
			if (Session && Session->GetPartyTable().Num() > 1)
			{
				const TArray<FArcadePartyRow>& Table = Session->GetPartyTable();
				const FText Label = FText::Format(LOCTEXT("PartyPoints", "PARTY POINTS  ·  GAME {0}"), Session->GetGamesPlayed());
				P.Faded(In).Text(Label, Font(EFont::Label, 18.f, 2.f), FVector2D(Frame.Centre().X, ChipsY - 26.0), Color::Sun, FVector2D(0.5, 0.5));
				const int32 Shown = FMath::Min(Table.Num(), 6);
				const double ChipW = 250.0, ChipGap = 14.0;
				const double Left = Frame.Centre().X - (Shown * ChipW + (Shown - 1) * ChipGap) * 0.5;
				for (int32 i = 0; i < Shown; ++i)
				{
					const FArcadePartyRow& Row = Table[i];
					const float ChipIn = KartWidgets::Entrance(Frame, 0.15f + i * 0.06f, 0.35f);
					const FVector2D Pos(Left + i * (ChipW + ChipGap), ChipsY + (1.0 - ChipIn) * 30.0);
					const FKartPainter C = P.Faded(ChipIn);
					C.Card(Pos, FVector2D(ChipW, 58.0), i == 0 ? Color::Paper : Color::WithAlpha(Color::Night, 0.9f), 29.f, 4.f, Row.Color, 3.f);
					const FLinearColor Ink = i == 0 ? Color::Navy : Color::White;
					if (i == 0)
					{
						KartWidgets::Star(C, Pos + FVector2D(30.0, 29.0), 15.f, 7.f, Color::Sun, static_cast<float>(Frame.Now * 30.0));
					}
					else
					{
						C.Circle(Pos + FVector2D(30.0, 29.0), 9.f, Row.Color);
					}
					C.Text(Row.Name, Font(EFont::Title, 22.f), Pos + FVector2D(52.0, 29.0), Ink, FVector2D(0.0, 0.5));
					C.Text(FString::FromInt(Row.Points), Font(EFont::Display, 30.f), Pos + FVector2D(ChipW - 20.0, 29.0), Ink, FVector2D(1.0, 0.5));
					if (Row.LastGain > 0)
					{
						const float Rise = static_cast<float>(FMath::Clamp((Frame.Age - 0.6) / 1.2, 0.0, 1.0));
						if (Rise > 0.f && Rise < 1.f)
						{
							C.Faded(1.f - Rise).Text(FString::Printf(TEXT("+%d"), Row.LastGain), Font(EFont::Display, 26.f, 3.f, Color::Night),
								Pos + FVector2D(ChipW - 30.0, -6.0 - Rise * 30.0), Color::Mint, FVector2D(1.0, 0.5), 3.f);
						}
					}
				}
			}

			// The buttons.
			const FText NextLabel = FText::Format(LOCTEXT("Next", "Next: {0}"), Next.Name);
			const FText LocalNames[] = { LOCTEXT("PlayAgain", "Play again"), NextLabel, LOCTEXT("ChangeGameButton", "Change game"), LOCTEXT("MainMenu", "Main menu") };
			const FText RoomNames[] = { LOCTEXT("PlayAgain", "Play again"), NextLabel, LOCTEXT("Lobby", "Back to lobby"), LOCTEXT("Leave", "Leave party") };
			const FLinearColor Accents[] = { Color::Hot, Next.Accent, Color::Mint, Color::Sky };
			const FVector2D ButtonSize(395.0, 66.0);
			const double Gap = 16.0;
			const double Left = Frame.Centre().X - (4 * ButtonSize.X + 3 * Gap) * 0.5;
			// Clear of the hint bar in the bottom-right corner.
			const double Y = Frame.SafeMax.Y - ButtonSize.Y - 92.0;
			const float ButtonsIn = KartWidgets::Entrance(Frame, 0.25f, 0.4f);
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector2D Pos(Left + i * (ButtonSize.X + Gap), Y + (1.0 - ButtonsIn) * 80.0);
				KartWidgets::Button(P.Faded(ButtonsIn), Pos, ButtonSize, bRoom ? RoomNames[i] : LocalNames[i], Buttons.State(i, Frame.Now), Accents[i], i);
			}
			// Party Mode: nobody may be at the keyboard, so the room plays again on its own unless someone takes over.
			if (bRoom && !bTouched)
			{
				const double Left2 = RoomAutoSeconds - (Frame.Now - ShownAt);
				if (Left2 <= 0.0)
				{
					Activate(0);
					return;
				}
				const FVector2D Bar(Left, Y - 18.0);
				P.Box(Bar, FVector2D(ButtonSize.X, 6.0), Color::WithAlpha(Color::White, 0.15f), 3.f);
				P.Box(Bar, FVector2D(ButtonSize.X * (Left2 / RoomAutoSeconds), 6.0), Color::Hot, 3.f);
				P.Text(FText::Format(LOCTEXT("AutoAgain", "Playing again in {0}"), FMath::CeilToInt32(Left2)), Font(EFont::Label, 16.f), Bar + FVector2D(0.0, -16.0),
					Color::WithAlpha(Color::White, 0.8f), FVector2D(0.0, 0.5));
			}
		}

	private:
		void Activate(int32 Item)
		{
			const TSharedPtr<SArcadeOverlay> O = Overlay.Pin();
			IArcadeGame* G = O.IsValid() ? O->GetGame() : nullptr;
			UWorld* World = Host->GetWorld();
			if (!G || !World || bActivated)
			{
				return;
			}
			bActivated = true;
			Host->PlaySound(EKartUISound::Accept);
			const FString Next = Arcade::NextAfter(G->GetArcadeId()).Id;
			if (bRoom)
			{
				G->ArcadeCommitResults();
				URoomHostSubsystem* Room = RoomOf(World);
				switch (Item)
				{
				case 0:
					// The room restarts through its lobby (the phones re-ready); the lobby starts this game again by itself.
					if (UArcadeSession* Session = UArcadeSession::Get(World)) Session->QueueRoomGame(G->GetArcadeId());
					if (Room) Room->RestartGame();
					break;
				case 1:
					// The lobby picks the queued game and starts it once the phones are ready again.
					if (UArcadeSession* Session = UArcadeSession::Get(World)) Session->QueueRoomGame(Next);
					if (Room) Room->ReturnToLobby();
					break;
				case 2:
					if (Room) Room->ReturnToLobby();
					break;
				default:
					ReturnToMenu(World, EKartMenuEntry::MainMenu);
					break;
				}
				return;
			}
			switch (Item)
			{
			case 0:
				O->CloseMenus(false);
				G->ArcadeRestart();
				break;
			case 1:
				if (UArcadeSession* Session = UArcadeSession::Get(World)) Session->LaunchLocal(World, Next);
				break;
			case 2:
				ReturnToMenu(World, EKartMenuEntry::Arcade);
				break;
			default:
				ReturnToMenu(World, EKartMenuEntry::MainMenu);
				break;
			}
		}

		TWeakPtr<SArcadeOverlay> Overlay;
		FButtonRow Buttons;
		bool bRoom = false;
		bool bTouched = false;
		bool bActivated = false;
		double ShownAt = 0.0;
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
	// Over the game's HUD (10), under the curtain.
	Viewport->AddViewportWidgetContent(Overlay, 30);
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
	Menus->PushScreen(MakeShared<FArcadePauseScreen>(StaticCastSharedRef<SArcadeOverlay>(AsShared())));
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
	Menus->PushScreen(MakeShared<FArcadeResultsScreen>(StaticCastSharedRef<SArcadeOverlay>(AsShared()), bRoom));
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
	const double FinishedAge = G->GetArcadeFinishedAge();
	if (FinishedAge >= 0.0)
	{
		if (!bRecorded)
		{
			// The party table counts every finished game once.
			bRecorded = true;
			if (UArcadeSession* Session = UArcadeSession::Get(World.Get()))
			{
				TArray<FArcadeStanding> Rows;
				G->GetArcadeStandings(Rows);
				Session->RecordResult(FString::Printf(TEXT("%s-%p-%.3f"), *G->GetArcadeId(), Game.Get(), InCurrentTime), Rows);
			}
		}
		const Arcade::FGameInfo* Info = Arcade::Find(G->GetArcadeId());
		if (!bResultsShown && FinishedAge >= (Info ? Info->ResultsDelay : 2.0))
		{
			ShowResults();
		}
	}
	else
	{
		// Restarted (here or by the room): the next finish counts again.
		bRecorded = false;
		const TSharedPtr<FKartScreen>& Top = Menus->Top();
		if (bResultsShown || (Top.IsValid() && Top->GetId() == TEXT("arcade-pause") && !G->IsArcadePaused()))
		{
			CloseMenus(false);
		}
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

#undef LOCTEXT_NAMESPACE
