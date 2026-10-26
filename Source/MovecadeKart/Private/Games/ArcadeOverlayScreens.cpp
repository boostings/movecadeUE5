#include "Games/ArcadeOverlayScreens.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeGame.h"
#include "Games/ArcadeHud.h"
#include "Games/ArcadeSession.h"
#include "Games/SArcadeOverlay.h"
#include "Net/RoomHostSubsystem.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"
#include "UI/KartUISubsystem.h"
#include "UI/KartWidgets.h"
#include "UI/Screens/KartScreens.h"

#define LOCTEXT_NAMESPACE "ArcadeOverlay"

using namespace KartUI;
using KartWidgets::FItemState;
namespace Arcade = MovecadeKart::Arcade;

namespace MovecadeKart::Arcade::Overlay
{
	const FName PauseScreenId(TEXT("arcade-pause"));
}

namespace
{
	const FName ResultsScreenId(TEXT("arcade-results"));
	/** The front end (and the room's lobby) live on the harbour level. */
	const TCHAR* const FrontEndLevel = TEXT("L_SunsetHarbor");

	/** Party Mode: the results bar plays again by itself after this long untouched. */
	constexpr double RoomAutoSeconds = 25.0;

	// The pause card (canvas units, 1920x1080).
	namespace PauseLayout
	{
		const FVector2D CardSize(1180.0, 760.0);
		constexpr float PopSeconds = 0.32f;
		constexpr float FadeSeconds = 0.25f;
		constexpr float DimAlpha = 0.6f;
		constexpr double TitleX = 300.0;
		const FVector2D ButtonSize(480.0, 74.0);
		constexpr double ButtonGap = 12.0;
		const FVector2D ButtonsAt(60.0, 210.0);
		const FVector2D HelpAt(600.0, 210.0);
		const FVector2D HelpSize(530.0, 490.0);
		constexpr double HelpTextWidth = 466.0;
		constexpr double HelpPulseSeconds = 0.6;

		/** Held upright: one column, the title, the buttons full width, then how to play under them. */
		namespace Portrait
		{
			constexpr double CardWidth = 940.0;
			constexpr double Inset = 50.0;
			constexpr double ButtonsY = 210.0;
			constexpr double HelpGap = 28.0;
			constexpr double HelpHeight = 380.0;
			constexpr double TaglineY = 280.0;
			constexpr double Bottom = 50.0;
		}
		/** The landscape card's tagline, down its help panel. */
		constexpr double TaglineY = 380.0;
	}

	// The results band along the bottom (canvas units).
	namespace ResultsLayout
	{
		constexpr double BandHeight = 330.0;
		constexpr float BandAlpha = 0.86f;
		constexpr int32 MaxChips = 6;
		const FVector2D ChipSize(250.0, 58.0);
		constexpr double ChipGap = 14.0;
		const FVector2D ButtonSize(395.0, 66.0);
		constexpr double ButtonGap = 16.0;
		/** Clear of the hint bar in the bottom-right corner. */
		constexpr double ButtonsAboveSafeBottom = 92.0;
		/** The party chips sit this far above the buttons. */
		constexpr double ChipsAboveButtons = 84.0;

		/** Held upright: the buttons two by two, the chips three to a row, in a taller band. */
		namespace Portrait
		{
			constexpr int32 ButtonColumns = 2;
			constexpr int32 ChipColumns = 3;
			constexpr double BandHeight = 640.0;
			constexpr double RowGap = 14.0;
			/** The buttons widen to share the safe width, up to this. */
			constexpr double MaxButtonWidth = 480.0;
		}
	}

	enum class EPauseItem : int32 { Resume, Restart, HowTo, Settings, ChangeGame, MainMenu, Count };
	enum class EResultsItem : int32 { PlayAgain, NextGame, ChangeGameOrLobby, MainMenuOrLeave, Count };

	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	URoomHostSubsystem* RoomOf(const UWorld* World)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<URoomHostSubsystem>() : nullptr;
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
		if (UArcadeSession* ArcadeSession = UArcadeSession::Get(World); ArcadeSession && Entry == EKartMenuEntry::MainMenu)
		{
			// Leaving the arcade for the main menu ends the evening's party table.
			ArcadeSession->ResetParty();
		}
		Session->ReturnToMenu(Entry);
		UI->TravelTo(FrontEndLevel);
	}

	/** The catalog entry of the overlay's game, or null once the game is gone. */
	const Arcade::FGameInfo* InfoOf(const TWeakPtr<SArcadeOverlay>& Overlay)
	{
		const TSharedPtr<SArcadeOverlay> Pinned = Overlay.Pin();
		const IArcadeGame* Game = Pinned.IsValid() ? Pinned->GetGame() : nullptr;
		return Game ? Arcade::Find(Game->GetArcadeId()) : nullptr;
	}

	/** A row of buttons with keyboard, pad and mouse focus. */
	struct FButtonRow
	{
		FKartMenuCursor Cursor;
		double FocusAt = 0.0;

		bool Move(int32 Delta, IKartScreenHost* Host)
		{
			if (!Cursor.Move(Delta))
			{
				return false;
			}
			FocusAt = NowSeconds();
			Host->PlaySound(EKartUISound::Move);
			return true;
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
		explicit FArcadePauseScreen(const TWeakPtr<SArcadeOverlay>& InOverlay)
			: Overlay(InOverlay)
			, Info(InfoOf(InOverlay))
		{
			Buttons.Cursor.Count = static_cast<int32>(EPauseItem::Count);
		}

		virtual FName GetId() const override { return Arcade::Overlay::PauseScreenId; }
		virtual void OnShown() override { Buttons.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Accept, ARCADE_LABEL("ArcadeOverlay", "Select", "Select") }, { EKartMenuAction::Back, ARCADE_LABEL("ArcadeOverlay", "Resume", "Resume") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				Buttons.Move(Event.Action == EKartMenuAction::Up ? -1 : 1, Host);
				return true;
			case EKartMenuAction::Accept:
				Activate(static_cast<EPauseItem>(Buttons.Cursor.Index));
				return true;
			case EKartMenuAction::Back:
			case EKartMenuAction::Pause:
			case EKartMenuAction::Start:
				Activate(EPauseItem::Resume);
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override { Buttons.Hover(HitId, Host); }
		virtual void OnClick(int32 HitId) override { Activate(static_cast<EPauseItem>(HitId)); }

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			using namespace PauseLayout;
			static const FText Labels[] = { LOCTEXT("ResumeButton", "Resume"), LOCTEXT("Restart", "Restart"), LOCTEXT("HowTo", "How to play"),
				LOCTEXT("Settings", "Settings"), LOCTEXT("ChangeGame", "Change game"), LOCTEXT("QuitMenu", "Main menu") };
			static const FLinearColor Accents[] = { Color::Mint, Color::Sky, Color::Sun, Color::Grape, Color::Sky, Color::Hot };
			static_assert(UE_ARRAY_COUNT(Labels) == static_cast<int32>(EPauseItem::Count), "one label per pause item");

			const FLinearColor Accent = Info ? Info->Accent : Color::Sun;
			const float In = KartWidgets::Entrance(Frame, 0.f, FadeSeconds);
			P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, DimAlpha * In));
			// Buttons beside how to play on a wide screen; one column held upright.
			const int32 Count = static_cast<int32>(EPauseItem::Count);
			const double ButtonsHeight = Count * (ButtonSize.Y + ButtonGap) - ButtonGap;
			const FVector2D Size = Frame.bPortrait
				? FVector2D(Portrait::CardWidth, Portrait::ButtonsY + ButtonsHeight + Portrait::HelpGap + Portrait::HelpHeight + Portrait::Bottom) : CardSize;
			const double TitleAt = Frame.bPortrait ? Size.X * 0.5 : TitleX;
			const FVector2D Pos = Frame.Centre() - Size * 0.5;
			// The card pops in from 80 % with an overshoot; the buttons ride it until it settles.
			const float Pop = Ease::OutBack(static_cast<float>(Frame.Age / PopSeconds), 2.f);
			const FKartPainter Card = P.Transformed(Frame.Centre(), 0.8f + 0.2f * Pop);
			Card.Card(Pos, Size, Color::WithAlpha(Color::Night, 0.94f), Metric::RadiusLarge, 12.f, Accent, 5.f);
			Card.Text(ARCADE_LABEL("ArcadeOverlay", "Paused", "PAUSED"), Font(EFont::Display, 84.f, 5.f), Pos + FVector2D(TitleAt, 92.0), Accent, FVector2D(0.5, 0.5), 5.f);
			if (Info)
			{
				Card.Text(Info->Name, Font(EFont::Title, 30.f), Pos + FVector2D(TitleAt, 160.0), Color::WithAlpha(Color::White, 0.85f), FVector2D(0.5, 0.5));
			}
			const FKartPainter& ButtonPainter = Frame.Age > PopSeconds ? P : Card;
			const FVector2D FirstButton = Frame.bPortrait ? FVector2D(Portrait::Inset, Portrait::ButtonsY) : ButtonsAt;
			const FVector2D ButtonExtent = Frame.bPortrait ? FVector2D(Size.X - 2.0 * Portrait::Inset, ButtonSize.Y) : ButtonSize;
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector2D ButtonPos = Pos + FirstButton + FVector2D(0.0, i * (ButtonSize.Y + ButtonGap));
				KartWidgets::Button(ButtonPainter, ButtonPos, ButtonExtent, Labels[i], Buttons.State(i, Frame.Now), Accents[i], i);
			}
			if (Frame.bPortrait)
			{
				const FVector2D HelpPos = Pos + FVector2D(Portrait::Inset, Portrait::ButtonsY + ButtonsHeight + Portrait::HelpGap);
				PaintHelp(Card, Frame, HelpPos, FVector2D(Size.X - 2.0 * Portrait::Inset, Portrait::HelpHeight), Portrait::TaglineY, Accent);
			}
			else
			{
				PaintHelp(Card, Frame, Pos + HelpAt, HelpSize, TaglineY, Accent);
			}
		}

	private:
		/** The right half: how to play this game, always in view while paused. */
		void PaintHelp(const FKartPainter& Card, const FKartScreenFrame& Frame, const FVector2D& Help, const FVector2D& Size, double TaglineAt,
			const FLinearColor& Accent) const
		{
			using namespace PauseLayout;
			const double TextWidth = Size.X - (HelpSize.X - HelpTextWidth);
			Card.Box(Help, Size, Color::WithAlpha(Color::White, 0.06f), 28.f);
			Card.Text(ARCADE_LABEL("ArcadeOverlay", "HowToTitle", "HOW TO PLAY"), Font(EFont::Label, 22.f, 2.f), Help + FVector2D(32.0, 44.0), Accent, FVector2D(0.0, 0.5));
			if (Info)
			{
				Card.TextWrapped(Info->HowTo, Font(EFont::Body, 26.f), Help + FVector2D(32.0, 84.0), TextWidth, Color::White);
				Card.TextWrapped(Info->Tagline, Font(EFont::Body, 20.f), Help + FVector2D(32.0, TaglineAt), TextWidth, Color::WithAlpha(Color::White, 0.6f));
			}
			if (bHelpPulse)
			{
				const float Pulse = static_cast<float>(FMath::Max(0.0, 1.0 - (Frame.Now - HelpPulseAt) / HelpPulseSeconds));
				Card.Card(Help, Size, FLinearColor::Transparent, 28.f, 0.f, Color::WithAlpha(Accent, Pulse), 5.f);
			}
		}

		void Activate(EPauseItem Item)
		{
			const TSharedPtr<SArcadeOverlay> O = Overlay.Pin();
			if (!O.IsValid())
			{
				return;
			}
			UWorld* World = Host->GetWorld();
			switch (Item)
			{
			case EPauseItem::Resume:
				O->CloseMenus(true);
				break;
			case EPauseItem::Restart:
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
			case EPauseItem::HowTo:
				// The help panel is already on the right; draw the eye to it.
				Host->PlaySound(EKartUISound::Accept);
				bHelpPulse = true;
				HelpPulseAt = NowSeconds();
				break;
			case EPauseItem::Settings:
				Host->PlaySound(EKartUISound::Accept);
				Host->PushScreen(KartScreens::MakeSettings(0));
				break;
			case EPauseItem::ChangeGame:
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
		/** The game never changes under an overlay: looked up once. */
		const Arcade::FGameInfo* Info = nullptr;
		FButtonRow Buttons;
		bool bHelpPulse = false;
		double HelpPulseAt = 0.0;
	};

	// --------------------------------------------------------------------------------------------- Results

	class FArcadeResultsScreen final : public FKartScreen
	{
	public:
		FArcadeResultsScreen(const TWeakPtr<SArcadeOverlay>& InOverlay, bool bInRoom)
			: Overlay(InOverlay)
			, bRoom(bInRoom)
		{
			Buttons.Cursor.Count = static_cast<int32>(EResultsItem::Count);
			Buttons.Cursor.bWrap = false;
			ShownAt = NowSeconds();
			if (const Arcade::FGameInfo* Info = InfoOf(InOverlay))
			{
				Next = &Arcade::NextAfter(Info->Id);
				NextLabel = FText::Format(LOCTEXT("Next", "Next: {0}"), Next->Name);
			}
		}

		virtual FName GetId() const override { return ResultsScreenId; }
		virtual bool CanGoBack() const override { return false; }
		virtual void OnShown() override { Buttons.FocusAt = NowSeconds(); }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, ARCADE_LABEL("ArcadeOverlay", "Choose", "Choose") }, { EKartMenuAction::Accept, ARCADE_LABEL("ArcadeOverlay", "Select", "Select") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			// Any input means someone is at the screen: Party Mode's automatic play-again stands down.
			bTouched = true;
			if (Event.Action == EKartMenuAction::Left || Event.Action == EKartMenuAction::Right)
			{
				Buttons.Move(Event.Action == EKartMenuAction::Left ? -1 : 1, Host);
			}
			else if (Event.Action == EKartMenuAction::Accept || Event.Action == EKartMenuAction::Start)
			{
				Activate(static_cast<EResultsItem>(Buttons.Cursor.Index));
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
				Activate(static_cast<EResultsItem>(HitId));
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			using namespace ResultsLayout;
			const TSharedPtr<SArcadeOverlay> O = Overlay.Pin();
			if (!O.IsValid() || !O->GetGame() || !Next)
			{
				return;
			}
			const float In = KartWidgets::Entrance(Frame, 0.f, 0.45f);
			// A band along the bottom that the game's own standings stay clear of (taller held upright, where the
			// buttons and the chips wrap into rows).
			const double Band = Frame.bPortrait ? Portrait::BandHeight : BandHeight;
			P.Gradient(FVector2D(0.0, Frame.Size.Y - Band), FVector2D(Frame.Size.X, Band), Color::WithAlpha(Color::Night, 0.f),
				Color::WithAlpha(Color::Night, BandAlpha * In), true);
			const int32 ButtonRows = Frame.bPortrait ? FMath::DivideAndRoundUp(static_cast<int32>(EResultsItem::Count), Portrait::ButtonColumns) : 1;
			const double ButtonsY = Frame.SafeMax.Y - ButtonsAboveSafeBottom - ButtonRows * ButtonSize.Y - (ButtonRows - 1) * Portrait::RowGap;
			PaintPartyTable(P, Frame, In, ButtonsY - ChipsAboveButtons);
			PaintButtons(P, Frame, ButtonsY);
		}

	private:
		/** The party table: everyone's running points across the evening. */
		void PaintPartyTable(const FKartPainter& P, const FKartScreenFrame& Frame, float In, double ChipsY)
		{
			using namespace ResultsLayout;
			const UArcadeSession* Session = UArcadeSession::Get(Host->GetWorld());
			if (!Session || Session->GetPartyTable().Num() <= 1)
			{
				return;
			}
			const TArray<FArcadePartyRow>& Table = Session->GetPartyTable();
			CacheTableText(*Session);
			const int32 Shown = FMath::Min(Table.Num(), MaxChips);
			// One row on a wide screen; rows of three held upright, the last row just over the buttons.
			const int32 Columns = FMath::Max(1, Frame.bPortrait ? FMath::Min(Shown, Portrait::ChipColumns) : Shown);
			const int32 Rows = FMath::DivideAndRoundUp(Shown, Columns);
			const double Left = Frame.Centre().X - (Columns * ChipSize.X + (Columns - 1) * ChipGap) * 0.5;
			const double TopY = ChipsY - (Rows - 1) * (ChipSize.Y + Portrait::RowGap);
			P.Faded(In).Text(PartyLabel, Font(EFont::Label, 18.f, 2.f), FVector2D(Frame.Centre().X, TopY - 26.0), Color::Sun, FVector2D(0.5, 0.5));
			for (int32 i = 0; i < Shown; ++i)
			{
				const FArcadePartyRow& Row = Table[i];
				const float ChipIn = KartWidgets::Entrance(Frame, 0.15f + i * 0.06f, 0.35f);
				const FVector2D Pos(Left + (i % Columns) * (ChipSize.X + ChipGap), TopY + (i / Columns) * (ChipSize.Y + Portrait::RowGap) + (1.0 - ChipIn) * 30.0);
				const FKartPainter C = P.Faded(ChipIn);
				const bool bLeader = i == 0;
				C.Card(Pos, ChipSize, bLeader ? Color::Paper : Color::WithAlpha(Color::Night, 0.9f), 29.f, 4.f, Row.Color, 3.f);
				const FLinearColor Ink = bLeader ? Color::Navy : Color::White;
				if (bLeader)
				{
					KartWidgets::Star(C, Pos + FVector2D(30.0, 29.0), 15.f, 7.f, Color::Sun, static_cast<float>(Frame.Now * 30.0));
				}
				else
				{
					C.Circle(Pos + FVector2D(30.0, 29.0), 9.f, Row.Color);
				}
				C.Text(Row.Name, Font(EFont::Title, 22.f), Pos + FVector2D(52.0, 29.0), Ink, FVector2D(0.0, 0.5));
				C.Text(PointsText[i], Font(EFont::Display, 30.f), Pos + FVector2D(ChipSize.X - 20.0, 29.0), Ink, FVector2D(1.0, 0.5));
				// The latest game's gain floats up off the chip.
				const float Rise = static_cast<float>(FMath::Clamp((Frame.Age - 0.6) / 1.2, 0.0, 1.0));
				if (Row.LastGain > 0 && Rise > 0.f && Rise < 1.f)
				{
					C.Faded(1.f - Rise).Text(GainText[i], Font(EFont::Display, 26.f, 3.f, Color::Night),
						Pos + FVector2D(ChipSize.X - 30.0, -6.0 - Rise * 30.0), Color::Mint, FVector2D(1.0, 0.5), 3.f);
				}
			}
		}

		/** The table is fixed while the results show: its labels are formatted once (again if a game is recorded). */
		void CacheTableText(const UArcadeSession& Session)
		{
			if (CachedGames == Session.GetGamesPlayed() && PointsText.Num() == Session.GetPartyTable().Num())
			{
				return;
			}
			CachedGames = Session.GetGamesPlayed();
			PartyLabel = FText::Format(LOCTEXT("PartyPoints", "PARTY POINTS  ·  GAME {0}"), CachedGames);
			PointsText.Reset();
			GainText.Reset();
			for (const FArcadePartyRow& Row : Session.GetPartyTable())
			{
				PointsText.Add(FString::FromInt(Row.Points));
				GainText.Add(FString::Printf(TEXT("+%d"), Row.LastGain));
			}
		}

		void PaintButtons(const FKartPainter& P, const FKartScreenFrame& Frame, double Y)
		{
			using namespace ResultsLayout;
			static const FText PlayAgain = LOCTEXT("PlayAgain", "Play again");
			static const FText ChangeGame = LOCTEXT("ChangeGameButton", "Change game");
			static const FText MainMenu = LOCTEXT("MainMenu", "Main menu");
			static const FText Lobby = LOCTEXT("Lobby", "Back to lobby");
			static const FText Leave = LOCTEXT("Leave", "Leave party");
			const FText* Labels[] = { &PlayAgain, &NextLabel, bRoom ? &Lobby : &ChangeGame, bRoom ? &Leave : &MainMenu };
			const FLinearColor Accents[] = { Color::Hot, Next->Accent, Color::Mint, Color::Sky };
			const int32 Count = static_cast<int32>(EResultsItem::Count);
			// One row on a wide screen; two by two held upright.
			const int32 Columns = Frame.bPortrait ? Portrait::ButtonColumns : Count;
			const FVector2D Size = Frame.bPortrait
				? FVector2D(FMath::Min(Portrait::MaxButtonWidth, (Frame.SafeSize().X - (Columns - 1) * ButtonGap) / Columns), ButtonSize.Y) : ButtonSize;
			const double Left = Frame.Centre().X - (Columns * Size.X + (Columns - 1) * ButtonGap) * 0.5;
			const float ButtonsIn = KartWidgets::Entrance(Frame, 0.25f, 0.4f);
			for (int32 i = 0; i < Count; ++i)
			{
				const FVector2D Pos(Left + (i % Columns) * (Size.X + ButtonGap), Y + (i / Columns) * (Size.Y + Portrait::RowGap) + (1.0 - ButtonsIn) * 80.0);
				KartWidgets::Button(P.Faded(ButtonsIn), Pos, Size, *Labels[i], Buttons.State(i, Frame.Now), Accents[i], i);
			}
			if (bRoom && !bTouched)
			{
				PaintAutoPlayAgain(P, Frame, FVector2D(Left, Y - 18.0));
			}
		}

		/** Party Mode: nobody may be at the keyboard, so the room plays again on its own unless someone takes over. */
		void PaintAutoPlayAgain(const FKartPainter& P, const FKartScreenFrame& Frame, const FVector2D& Bar)
		{
			const double Remaining = RoomAutoSeconds - (Frame.Now - ShownAt);
			if (Remaining <= 0.0)
			{
				Activate(EResultsItem::PlayAgain);
				return;
			}
			const double Width = ResultsLayout::ButtonSize.X;
			P.Box(Bar, FVector2D(Width, 6.0), Color::WithAlpha(Color::White, 0.15f), 3.f);
			P.Box(Bar, FVector2D(Width * (Remaining / RoomAutoSeconds), 6.0), Color::Hot, 3.f);
			const int32 Seconds = FMath::CeilToInt32(Remaining);
			if (Seconds != AutoLabelSeconds)
			{
				AutoLabelSeconds = Seconds;
				AutoLabel = FText::Format(LOCTEXT("AutoAgain", "Playing again in {0}"), Seconds);
			}
			P.Text(AutoLabel, Font(EFont::Label, 16.f), Bar + FVector2D(0.0, -16.0), Color::WithAlpha(Color::White, 0.8f), FVector2D(0.0, 0.5));
		}

		void Activate(EResultsItem Item)
		{
			const TSharedPtr<SArcadeOverlay> O = Overlay.Pin();
			IArcadeGame* G = O.IsValid() ? O->GetGame() : nullptr;
			UWorld* World = Host->GetWorld();
			if (!G || !World || !Next || bActivated)
			{
				return;
			}
			bActivated = true;
			Host->PlaySound(EKartUISound::Accept);
			if (bRoom)
			{
				ActivateInRoom(Item, *G, World);
				return;
			}
			switch (Item)
			{
			case EResultsItem::PlayAgain:
				O->CloseMenus(false);
				G->ArcadeRestart();
				break;
			case EResultsItem::NextGame:
				if (UArcadeSession* Session = UArcadeSession::Get(World))
				{
					Session->LaunchLocal(World, Next->Id);
				}
				break;
			case EResultsItem::ChangeGameOrLobby:
				ReturnToMenu(World, EKartMenuEntry::Arcade);
				break;
			default:
				ReturnToMenu(World, EKartMenuEntry::MainMenu);
				break;
			}
		}

		void ActivateInRoom(EResultsItem Item, IArcadeGame& Game, UWorld* World)
		{
			Game.ArcadeCommitResults();
			URoomHostSubsystem* Room = RoomOf(World);
			UArcadeSession* Session = UArcadeSession::Get(World);
			switch (Item)
			{
			case EResultsItem::PlayAgain:
				// The room restarts through its lobby (the phones re-ready); the lobby starts this game again by itself.
				if (Session)
				{
					Session->QueueRoomGame(Game.GetArcadeId());
				}
				if (Room)
				{
					Room->RestartGame();
				}
				break;
			case EResultsItem::NextGame:
				// The lobby picks the queued game and starts it once the phones are ready again.
				if (Session)
				{
					Session->QueueRoomGame(Next->Id);
				}
				if (Room)
				{
					Room->ReturnToLobby();
				}
				break;
			case EResultsItem::ChangeGameOrLobby:
				if (Room)
				{
					Room->ReturnToLobby();
				}
				break;
			default:
				ReturnToMenu(World, EKartMenuEntry::MainMenu);
				break;
			}
		}

		TWeakPtr<SArcadeOverlay> Overlay;
		/** The catalog's game after this one, and its button label. */
		const Arcade::FGameInfo* Next = nullptr;
		FText NextLabel;
		FButtonRow Buttons;
		bool bRoom = false;
		bool bTouched = false;
		bool bActivated = false;
		double ShownAt = 0.0;
		// Labels formatted on change, not per paint.
		int32 CachedGames = INDEX_NONE;
		FText PartyLabel;
		TArray<FString> PointsText;
		TArray<FString> GainText;
		int32 AutoLabelSeconds = INDEX_NONE;
		FText AutoLabel;
	};
}

namespace MovecadeKart::Arcade::Overlay
{
	TSharedRef<FKartScreen> MakePauseScreen(const TWeakPtr<SArcadeOverlay>& Overlay)
	{
		return MakeShared<FArcadePauseScreen>(Overlay);
	}

	TSharedRef<FKartScreen> MakeResultsScreen(const TWeakPtr<SArcadeOverlay>& Overlay, bool bRoom)
	{
		return MakeShared<FArcadeResultsScreen>(Overlay, bRoom);
	}
}

#undef LOCTEXT_NAMESPACE
