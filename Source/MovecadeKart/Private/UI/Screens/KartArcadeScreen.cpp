#include "UI/Screens/KartScreens.h"

#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeSession.h"
#include "UI/KartAccessibility.h"
#include "UI/KartFrontEnd.h"
#include "UI/Screens/KartMenuIcons.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using namespace KartScreens::Shared;
using KartWidgets::FItemState;

namespace
{
	/** The game list on the left: the panel, button width, and how the rows tighten as the catalog grows. */
	constexpr float PanelWidth = 760.f;
	constexpr float PanelAlpha = 0.76f;
	constexpr double ListTop = 190.0;
	constexpr double ListRoomBelow = 200.0;
	constexpr double MinRowStep = 64.0;
	constexpr double MaxRowStep = 100.0;
	constexpr double ButtonWidth = 600.0;
	constexpr double ButtonGap = 14.0;
	constexpr double ButtonSlide = 260.0;
	constexpr float ButtonInAt = 0.05f;
	constexpr float ButtonStagger = 0.05f;
	constexpr float ButtonInSeconds = 0.4f;
	/** The SOON chip on a game that isn't ported yet. */
	const FVector2D SoonChipSize(116.0, 40.0);
	constexpr double SoonChipInset = 24.0;
	constexpr float SoonChipAlpha = 0.9f;
	constexpr float SoonChipRound = 20.f;
	constexpr float SoonChipFont = 19.f;
	/** The two option rows under the list (players, house players). */
	const FVector2D OptionRowSize(600.0, 76.0);
	constexpr double OptionGap = 14.0;
	constexpr double OptionsDrop = 20.0;
	constexpr double OptionSlide = 200.0;
	constexpr float OptionInAt = 0.25f;
	constexpr float OptionStagger = 0.05f;

	/** The picked game's card on the right and its line-up of seat dots. */
	namespace CardArt
	{
		const FVector2D Size(620.0, 560.0);
		constexpr float InAt = 0.2f;
		constexpr double PopSeconds = 0.35;
		constexpr float PopOvershoot = 2.f;
		/** A new pick pops the card from 92 % with a small tilt. */
		constexpr float PopFrom = 0.92f;
		constexpr float PopGrowth = 0.08f;
		constexpr float PopTilt = -3.f;
		constexpr float Shadow = 10.f;
		constexpr float Rim = 5.f;
		constexpr double ArtInset = 18.0;
		constexpr double ArtHeight = 250.0;
		constexpr double PhoneArtTrim = 80.0;
		/** The art's gradient darkens towards night by this much. */
		constexpr float ArtShade = 0.35f;
		constexpr double IconY = 143.0;
		constexpr float IconSize = 100.f;
		constexpr float NameSize = 54.f;
		const FVector2D NamePos(36.0, 290.0);
		constexpr float TaglineSize = 22.f;
		const FVector2D TaglinePos(38.0, 372.0);
		/** The line-up: a dot per seat along the bottom. */
		constexpr double SeatsX = 70.0;
		constexpr double SeatsLift = 66.0;
		constexpr double SeatSpacing = 76.0;
		constexpr float SeatRadius = 28.f;
		constexpr float SeatOutline = 3.f;
		constexpr float EmptySeatAlpha = 0.4f;
		constexpr float PlayerTagSize = 18.f;
		constexpr float CpuTagSize = 14.f;
		constexpr float PlayInAt = 0.35f;
		constexpr double PlayButtonDrop = 70.0;
	}

	constexpr int32 PlayHit = 99;

	/** The picker's view of one catalog game (Games/ArcadeCatalog is the one list). */
	struct FArcadeGame
	{
		FName Id;
		FText Name;
		FText Tagline;
		/** The level; empty until the game is ported (the row then shows SOON and won't start). */
		FString Map;
		FLinearColor Accent;
		int32 MaxPlayers;
		/** Players on this screen: turn-based games share one set of controls; simultaneous ones need a pair of hands each. */
		int32 MaxLocal;
		KartMenuIcons::FPaintIcon Icon;

		bool IsPlayable() const { return !Map.IsEmpty(); }
	};

	const TArray<FArcadeGame>& ArcadeGames()
	{
		static const TArray<FArcadeGame> Games = []()
		{
			TArray<FArcadeGame> Out;
			for (const MovecadeKart::Arcade::FGameInfo& G : MovecadeKart::Arcade::Games())
			{
				const FName Id(*G.Id);
				Out.Add({ Id, G.Name, G.Tagline, G.Map, G.Accent, G.MaxPlayers, G.MaxLocal, KartMenuIcons::ForArcadeGame(Id) });
			}
			return Out;
		}();
		return Games;
	}

	/** The picker's choices. A deliberate session cache: coming back to the picker lands where you left it. */
	struct FArcadePick
	{
		int32 Game = 0;
		int32 Players = 1;
		int32 House = 1;
	};

	FArcadePick& ArcadePick()
	{
		static FArcadePick Pick;
		return Pick;
	}

	/** The Arcade picker: the party games, then how many play here and how many house players fill in. */
	class FArcadeScreen final : public FKartScreen
	{
	public:
		explicit FArcadeScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = HouseRow + 1;
		}

		virtual FName GetId() const override { return TEXT("arcade"); }

		virtual void OnShown() override
		{
			// Back from a game: land on the one just played (next game may have moved on from the pick).
			if (const UArcadeSession* Session = UArcadeSession::Get(FrontEnd.Get()); Session && !Session->GetGameId().IsEmpty())
			{
				const FName PlayedId(*Session->GetGameId());
				const int32 Played = ArcadeGames().IndexOfByPredicate([PlayedId](const FArcadeGame& G) { return G.Id == PlayedId; });
				if (Played != INDEX_NONE)
				{
					ArcadePick().Game = Played;
				}
			}
			Focus.Cursor.Set(ArcadePick().Game);
			Focus.FocusAt = NowSeconds();
			bStarting = false;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
		}

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Change", "Change") }, { EKartMenuAction::Accept, LOCTEXT("Play", "Play") }, { EKartMenuAction::Back, LOCTEXT("Back", "Back") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			if (bStarting)
			{
				return true;
			}
			switch (Event.Action)
			{
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				MoveTo(Focus.Cursor.Index + (Event.Action == EKartMenuAction::Up ? -1 : 1));
				return true;
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Change(Focus.Cursor.Index, Event.Action == EKartMenuAction::Left ? -1 : 1);
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Play();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId != PlayHit)
			{
				MoveTo(KartWidgets::HitBase(HitId));
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId == PlayHit)
			{
				Play();
				return;
			}
			const int32 Row = KartWidgets::HitBase(HitId);
			MoveTo(Row);
			if (Row < PlayersRow)
			{
				Play();
			}
			else
			{
				Change(Row, KartWidgets::HitArrow(HitId) < 0 ? -1 : 1);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			LeftPanel(P, Frame, PanelWidth, PanelAlpha);
			KartWidgets::Header(P, Frame, LOCTEXT("ArcadeTitle", "Arcade"), LOCTEXT("ArcadeSub", "Pick a game, then who's playing."), Color::Sun);
			const double Top = Frame.SafeMin.Y + ListTop;
			// The game list tightens as the catalog grows, so the two option rows under it stay on screen.
			const double Step = FMath::Clamp((Frame.SafeMax.Y - Top - ListRoomBelow) / FMath::Max(1, ArcadeGames().Num()), MinRowStep, MaxRowStep);
			PaintGameList(P, Frame, Top, Step);
			PaintOptionRows(P, Frame, Top + ArcadeGames().Num() * Step + OptionsDrop);
			PaintGameCard(P, Frame, FVector2D(Frame.SafeMax.X - CardArt::Size.X, Top));
		}

	private:
		/** Rows: the games, then Players and House players. */
		const int32 PlayersRow = ArcadeGames().Num();
		const int32 HouseRow = PlayersRow + 1;

		void PaintGameList(const FKartPainter& P, const FKartScreenFrame& Frame, double Top, double Step) const
		{
			const TArray<FArcadeGame>& Games = ArcadeGames();
			const FVector2D ButtonSize(ButtonWidth, Step - ButtonGap);
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < Games.Num(); ++i)
			{
				Group.Next();
				const FArcadeGame& Game = Games[i];
				const float In = KartWidgets::Entrance(Frame, ButtonInAt + i * ButtonStagger, ButtonInSeconds);
				const FKartPainter Row = P.Faded(In);
				const FVector2D Pos(Frame.SafeMin.X - (1.0 - In) * ButtonSlide + KartWidgets::SlideX(Frame), Top + i * Step);
				KartWidgets::Button(Row, Pos, ButtonSize, Game.Name, Focus.State(i, Frame.Now, Game.IsPlayable()), Game.Accent, i);
				if (!Game.IsPlayable())
				{
					// Not ported yet: a chip at the right end rather than a second line.
					const FVector2D ChipPos(Pos.X + ButtonSize.X - SoonChipSize.X - SoonChipInset, Pos.Y + (ButtonSize.Y - SoonChipSize.Y) * 0.5);
					Row.Box(ChipPos, SoonChipSize, Color::WithAlpha(Game.Accent, SoonChipAlpha), SoonChipRound);
					Row.Text(LOCTEXT("Soon", "SOON"), Font(EFont::Label, SoonChipFont), ChipPos + SoonChipSize * 0.5, Color::TextOn(Game.Accent), FVector2D(0.5, 0.5));
				}
			}
		}

		void PaintOptionRows(const FKartPainter& P, const FKartScreenFrame& Frame, double OptionsTop)
		{
			static const FText Labels[] = { LOCTEXT("ArcadePlayers", "Players"), LOCTEXT("ArcadeHouse", "House players") };
			const FArcadePick& Pick = ArcadePick();
			if (Pick.Players != ValuesPlayers || Pick.House != ValuesHouse)
			{
				ValuesPlayers = Pick.Players;
				ValuesHouse = Pick.House;
				Values[0] = FText::Format(LOCTEXT("PlayersN", "{0} on this screen"), Pick.Players);
				Values[1] = Pick.House == 0 ? LOCTEXT("NoHouse", "None") : FText::Format(LOCTEXT("HouseN", "{0} {0}|plural(one=bot,other=bots)"), Pick.House);
			}
			const FKartPaintGroup Group(P);
			for (int32 i = 0; i < UE_ARRAY_COUNT(Labels); ++i)
			{
				Group.Next();
				const int32 Row = PlayersRow + i;
				const float In = KartWidgets::Entrance(Frame, OptionInAt + i * OptionStagger);
				const FVector2D Pos(Frame.SafeMin.X + KartWidgets::SlideX(Frame) - (1.0 - In) * OptionSlide, OptionsTop + i * (OptionRowSize.Y + OptionGap));
				KartWidgets::OptionRow(P.Faded(In), Pos, OptionRowSize, Labels[i], Values[i], Focus.State(Row, Frame.Now), Row);
			}
		}

		/** The picked game's card: its icon, pitch and the line-up as dots, with the Play button under it. */
		void PaintGameCard(const FKartPainter& P, const FKartScreenFrame& Frame, const FVector2D& CardPos) const
		{
			using namespace CardArt;
			const FArcadePick& Pick = ArcadePick();
			// A phone's bigger tagline takes room from the art, so it ends above the line-up.
			const double Trim = Frame.bPhone ? PhoneArtTrim : 0.0;
			const FArcadeGame& Game = ArcadeGames()[Pick.Game];
			const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - PickedAt) / PopSeconds), PopOvershoot);
			const FKartPainter Card = P.Transformed(CardPos + Size * 0.5, PopFrom + PopGrowth * Pop, (1.f - Pop) * PopTilt * FKartAccessibility::MotionScale())
				.Faded(KartWidgets::Entrance(Frame, InAt));
			Card.Card(CardPos, Size, Color::Paper, Metric::RadiusLarge, Shadow, Color::White, Rim);
			Card.Gradient(CardPos + FVector2D(ArtInset, ArtInset), FVector2D(Size.X - 2.0 * ArtInset, ArtHeight - Trim), Game.Accent, FMath::Lerp(Game.Accent, Color::Night, ArtShade), true,
				Metric::Radius);
			Game.Icon(Card, CardPos + FVector2D(Size.X * 0.5, IconY - Trim * 0.5), IconSize, Frame.Now);
			Card.Text(Game.Name, Font(EFont::Display, NameSize), CardPos + NamePos - FVector2D(0.0, Trim), Color::Navy);
			Card.TextWrapped(Game.Tagline, Font(EFont::Body, TaglineSize), CardPos + TaglinePos - FVector2D(0.0, Trim), Size.X - 2.0 * TaglinePos.X, Color::NavyDim);

			const int32 Seats = Pick.Players + Pick.House;
			for (int32 i = 0; i < Game.MaxPlayers; ++i)
			{
				const FVector2D C(CardPos.X + SeatsX + i * SeatSpacing, CardPos.Y + Size.Y - SeatsLift);
				if (i >= Seats)
				{
					Card.Circle(C, SeatRadius, FLinearColor::Transparent, Color::WithAlpha(Color::NavyDim, EmptySeatAlpha), SeatOutline);
					continue;
				}
				const bool bLocal = i < Pick.Players;
				const FLinearColor Fill = bLocal ? Game.Accent : Color::NavyDim;
				Card.Circle(C, SeatRadius, Fill, Color::Navy, SeatOutline);
				Card.Text(bLocal ? SeatTag(i) : CpuTag(), Font(EFont::Label, bLocal ? PlayerTagSize : CpuTagSize), C, Color::TextOn(Fill), FVector2D(0.5, 0.5));
			}

			FItemState Start;
			Start.bFocused = true;
			Start.bEnabled = Game.IsPlayable();
			Start.FocusAge = Frame.Now - Focus.FocusAt;
			KartWidgets::PrimaryButton(P.Faded(KartWidgets::Entrance(Frame, PlayInAt)), FVector2D(CardPos.X + Size.X * 0.5, CardPos.Y + Size.Y + PlayButtonDrop),
				Game.IsPlayable() ? LOCTEXT("PlayButton", "PLAY") : LOCTEXT("SoonButton", "COMING SOON"), EKartMenuAction::Accept, Frame.Device, Start, PlayHit);
		}

		void MoveTo(int32 Row)
		{
			if (!Focus.SetFocus(FMath::Clamp(Row, 0, Focus.Cursor.Count - 1), NowSeconds()))
			{
				return;
			}
			Host->PlaySound(EKartUISound::Move);
			FArcadePick& Pick = ArcadePick();
			if (Focus.Cursor.Index < PlayersRow && Pick.Game != Focus.Cursor.Index)
			{
				Pick.Game = Focus.Cursor.Index;
				// A simultaneous game seats one player here; the rest of the line-up becomes the house.
				const FArcadeGame& G = ArcadeGames()[Pick.Game];
				Pick.Players = FMath::Min(Pick.Players, G.MaxLocal);
				Pick.House = FMath::Min(Pick.House, G.MaxPlayers - Pick.Players);
				PickedAt = NowSeconds();
			}
		}

		void Change(int32 Row, int32 Dir)
		{
			if (Row < PlayersRow)
			{
				return;
			}
			FArcadePick& Pick = ArcadePick();
			const FArcadePick Before = Pick;
			const FArcadeGame& Game = ArcadeGames()[Pick.Game];
			if (Row == PlayersRow)
			{
				Pick.Players = FMath::Clamp(Pick.Players + Dir, 1, Game.MaxLocal);
				Pick.House = FMath::Min(Pick.House, Game.MaxPlayers - Pick.Players);
			}
			else
			{
				Pick.House = FMath::Clamp(Pick.House + Dir, 0, Game.MaxPlayers - Pick.Players);
			}
			const bool bChanged = Pick.Players != Before.Players || Pick.House != Before.House;
			if (bChanged)
			{
				Focus.Changed(Row, Dir, NowSeconds());
			}
			Host->PlaySound(bChanged ? EKartUISound::Tick : EKartUISound::Error);
		}

		void Play()
		{
			const FArcadePick& Pick = ArcadePick();
			const FArcadeGame& Game = ArcadeGames()[Pick.Game];
			AKartFrontEnd* FE = FrontEnd.Get();
			if (bStarting || !Game.IsPlayable() || !FE)
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			bStarting = true;
			Host->PlaySound(EKartUISound::Go);
			if (UArcadeSession* Session = UArcadeSession::Get(FE))
			{
				Session->NoteLaunch(Game.Id.ToString(), Pick.Players, Pick.House);
			}
			FE->StartGame(Game.Map, Pick.Players, Pick.House);
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FListFocus Focus;
		double PickedAt = -10.0;
		bool bStarting = false;
		/** The option rows' values, rebuilt when the pick changes. */
		int32 ValuesPlayers = -1;
		int32 ValuesHouse = -1;
		FText Values[2];
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeArcade(AKartFrontEnd* FrontEnd) { return MakeShared<FArcadeScreen>(FrontEnd); }
}

#undef LOCTEXT_NAMESPACE
