#include "UI/Screens/KartScreens.h"

#include "UI/KartFrontEnd.h"
#include "UI/KartRoster.h"
#include "UI/KartSessionSettings.h"
#include "UI/Screens/KartScreenCommon.h"

#define LOCTEXT_NAMESPACE "KartFrontEnd"

using namespace KartUI;
using namespace KartScreens::Shared;

namespace
{
	/** The rows, then the "Choose track" button. */
	enum ERulesRow : int32 { BotsRow, SkillRow, LapsRow, ScreensRow, NumOptionRows, NextRow = NumOptionRows, NumRows };
	constexpr int32 NumSkills = 3;
	/** Without a track loaded the grid is assumed to hold eight karts. */
	constexpr int32 DefaultGrid = 8;

	constexpr float PanelWidth = 1040.f;
	constexpr float PanelAlpha = 0.72f;
	constexpr double RowsTop = 200.0;
	constexpr double MaxRowWidth = 880.0;
	/** Room the summary card keeps on the right: narrow canvases (a large UI scale) shrink the rows instead. */
	constexpr double SummaryRoom = 640.0;
	constexpr double RowHeight = 84.0;
	constexpr double RowGap = 18.0;
	constexpr double RowSlide = 200.0;
	constexpr float RowInAt = 0.05f;
	constexpr float RowStagger = 0.05f;
	/** The "Choose track" button under the rows. */
	const FVector2D NextButtonSize(480.0, 92.0);
	constexpr double NextButtonGap = 20.0;
	constexpr float NextButtonInAt = 0.3f;

	/** The summary card: the racer count, laps and skill, and the grid as a row of paint dots. */
	namespace SummaryArt
	{
		const FVector2D Size(600.0, 262.0);
		constexpr float InAt = 0.2f;
		constexpr float Shadow = 10.f;
		constexpr float Rim = 4.f;
		constexpr float CountSize = 56.f;
		const FVector2D CountPos(36.0, 26.0);
		constexpr float LineSize = 26.f;
		const FVector2D LinePos(38.0, 104.0);
		constexpr int32 Dots = 8;
		constexpr double DotsX = 70.0;
		constexpr double DotsY = 210.0;
		constexpr double DotSpacing = 66.0;
		constexpr float DotRadius = 26.f;
		constexpr float DotOutline = 3.f;
		constexpr float EmptyDotAlpha = 0.4f;
		constexpr float PlayerTagSize = 17.f;
		constexpr float CpuTagSize = 13.f;
		/** A bot added with Right pops its dot in. */
		constexpr double NewDotSeconds = 0.3;
		constexpr float NewDotOvershoot = 3.f;
	}

	/** Step 2 of the race setup: opponents, bot skill, laps and the split-screen layout. */
	class FRulesScreen final : public FKartScreen
	{
	public:
		explicit FRulesScreen(AKartFrontEnd* InFrontEnd) : FrontEnd(InFrontEnd)
		{
			Focus.Cursor.Count = NumRows;
			Focus.Cursor.bWrap = true;
		}

		virtual FName GetId() const override { return TEXT("rules"); }

		virtual void OnShown() override
		{
			Focus.FocusAt = NowSeconds();
			if (UKartSessionSettings* S = SessionOf(Host))
			{
				S->MutableSetup().Sanitize(Grid());
			}
			ValuesKey = -1;
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
		}

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::Left, LOCTEXT("Change", "Change") }, { EKartMenuAction::Accept, LOCTEXT("Next", "Next") }, { EKartMenuAction::Back, LOCTEXT("Back", "Back") } };
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Up:
			case EKartMenuAction::Down:
				Focus.Step(Event.Action == EKartMenuAction::Up ? -1 : 1, *Host);
				return true;
			case EKartMenuAction::Left:
			case EKartMenuAction::Right:
				Change(Focus.Cursor.Index, Event.Action == EKartMenuAction::Left ? -1 : 1);
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Next();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override { Focus.HoverTo(KartWidgets::HitBase(HitId), *Host); }

		virtual void OnClick(int32 HitId) override
		{
			const int32 Row = KartWidgets::HitBase(HitId);
			Focus.SetFocus(Row, NowSeconds());
			if (Row == NextRow)
			{
				Next();
			}
			else
			{
				Change(Row, KartWidgets::HitArrow(HitId) < 0 ? -1 : 1);
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const UKartSessionSettings* S = SessionOf(Host);
			if (!S)
			{
				return;
			}
			const FKartRaceSetup& Setup = S->GetSetup();
			LeftPanel(P, Frame, PanelWidth, PanelAlpha);
			KartWidgets::Header(P, Frame, LOCTEXT("RulesTitle", "Race rules"), LOCTEXT("RulesSub", "Set up the grid."), Color::Sky);
			KartWidgets::Steps(P, Frame, RaceSetupSteps(), 1);
			RefreshText(Setup);

			const double Top = Frame.SafeMin.Y + RowsTop;
			PaintRows(P, Frame, Setup, Top);
			PaintSummary(P, Frame, Setup, FVector2D(Frame.SafeMax.X - SummaryArt::Size.X, Top));
		}

	private:
		/** The rows' values and the summary card's text, rebuilt only when the setup changes. */
		void RefreshText(const FKartRaceSetup& Setup)
		{
			static const FText Skill[NumSkills] = { LOCTEXT("Easy", "Easy"), LOCTEXT("Normal", "Normal"), LOCTEXT("Hard", "Hard") };
			const int32 SkillIndex = FMath::Clamp(Setup.BotSkill, 0, NumSkills - 1);
			// Packs every field the text shows into one int (each field's range fits its slot).
			const int32 Key = ((Setup.Bots * 8 + SkillIndex) * 64 + Setup.Laps) * 2 + (Setup.bViewEveryKart ? 1 : 0) + Setup.LocalPlayers * 100000;
			if (Key == ValuesKey)
			{
				return;
			}
			ValuesKey = Key;
			Values[BotsRow] = Setup.Bots == 0 ? LOCTEXT("NoBots", "None") : FText::Format(LOCTEXT("BotsN", "{0} {0}|plural(one=bot,other=bots)"), Setup.Bots);
			Values[SkillRow] = Skill[SkillIndex];
			Values[LapsRow] = FText::AsNumber(Setup.Laps);
			Values[ScreensRow] = Setup.bViewEveryKart ? LOCTEXT("EveryKart", "Every kart (TV)") : LOCTEXT("PerPlayer", "One per player");
			RacersText = FText::Format(LOCTEXT("RacersCount", "{0} racers"), Setup.LocalPlayers + Setup.Bots);
			SummaryText = FText::Format(LOCTEXT("Summary", "{0} {0}|plural(one=lap,other=laps)  ·  {1} bots"), Setup.Laps, Skill[SkillIndex]);
		}

		void PaintRows(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartRaceSetup& Setup, double Top) const
		{
			static const FText Labels[NumOptionRows] = { LOCTEXT("Opponents", "Opponents"), LOCTEXT("BotSkill", "Bot skill"), LOCTEXT("Laps", "Laps"), LOCTEXT("Screens", "Screens") };
			const FVector2D RowSize(FMath::Min(MaxRowWidth, Frame.SafeSize().X - SummaryRoom), RowHeight);
			{
				const FKartPaintGroup Group(P);
				for (int32 Row = 0; Row < NumOptionRows; ++Row)
				{
					Group.Next();
					const float In = KartWidgets::Entrance(Frame, RowInAt + Row * RowStagger);
					const FVector2D Pos(Frame.SafeMin.X + KartWidgets::SlideX(Frame) - (1.0 - In) * RowSlide, Top + Row * (RowSize.Y + RowGap));
					// Bot skill means nothing without bots.
					const bool bEnabled = Row != SkillRow || Setup.Bots > 0;
					KartWidgets::OptionRow(P.Faded(In), Pos, RowSize, Labels[Row], Values[Row], Focus.State(Row, Frame.Now, bEnabled), Row);
				}
			}
			const float In = KartWidgets::Entrance(Frame, NextButtonInAt);
			KartWidgets::Button(P.Faded(In), FVector2D(Frame.SafeMin.X + KartWidgets::SlideX(Frame), Top + static_cast<int32>(NumOptionRows) * (RowSize.Y + RowGap) + NextButtonGap),
				NextButtonSize, LOCTEXT("ChooseTrack", "Choose track"), Focus.State(NextRow, Frame.Now), Color::Hot, NextRow);
		}

		/** The summary card: the racer count, laps and skill, and the grid as dots (the newest bot pops in). */
		void PaintSummary(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartRaceSetup& Setup, const FVector2D& CardPos) const
		{
			using namespace SummaryArt;
			const int32 Racers = Setup.LocalPlayers + Setup.Bots;
			const FKartPainter Card = P.Faded(KartWidgets::Entrance(Frame, InAt));
			Card.Card(CardPos, Size, Color::Paper, Metric::RadiusLarge, Shadow, Color::White, Rim);
			Card.Text(RacersText, Font(EFont::Display, CountSize), CardPos + CountPos, Color::Navy);
			Card.Text(SummaryText, Font(EFont::Body, LineSize), CardPos + LinePos, Color::NavyDim);
			for (int32 i = 0; i < Dots; ++i)
			{
				const FVector2D C(CardPos.X + DotsX + i * DotSpacing, CardPos.Y + DotsY);
				if (i >= Racers)
				{
					Card.Circle(C, DotRadius, FLinearColor::Transparent, Color::WithAlpha(Color::NavyDim, EmptyDotAlpha), DotOutline);
					continue;
				}
				const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - Focus.ChangeAt) / NewDotSeconds), NewDotOvershoot);
				const bool bNew = Focus.ChangedRow == BotsRow && i == Racers - 1 && Focus.ChangeDir > 0;
				const FKartPainter Dot = bNew ? Card.Transformed(C, Pop) : Card;
				const bool bLocal = i < Setup.LocalPlayers;
				const FLinearColor Paint = bLocal ? FKartRoster::PaintColor(Setup.Racers[i].Paint) : Color::NavyDim;
				Dot.Circle(C, DotRadius, Paint, Color::Navy, DotOutline);
				Dot.Text(bLocal ? SeatTag(FMath::Clamp(i, 0, FKartRaceSetup::MaxLocalPlayers - 1)) : CpuTag(), Font(EFont::Label, bLocal ? PlayerTagSize : CpuTagSize),
					C, Color::White, FVector2D(0.5, 0.5));
			}
		}

		int32 Grid() const
		{
			const AKartFrontEnd* FE = FrontEnd.Get();
			return FE && FE->GetTrack() ? FE->GetTrack()->Grid.Num() : DefaultGrid;
		}

		void Change(int32 Row, int32 Dir)
		{
			UKartSessionSettings* S = SessionOf(Host);
			if (!S || Row >= NumOptionRows)
			{
				return;
			}
			FKartRaceSetup& Setup = S->MutableSetup();
			Setup.Sanitize(Grid());
			const FKartRaceSetup Before = Setup;
			switch (Row)
			{
			case BotsRow: Setup.Bots = FMath::Clamp(Setup.Bots + Dir, 0, Grid() - Setup.LocalPlayers); break;
			case SkillRow: Setup.BotSkill = FMath::Clamp(Setup.BotSkill + Dir, 0, NumSkills - 1); break;
			case LapsRow: Setup.Laps = FMath::Clamp(Setup.Laps + Dir, 1, FKartRaceSetup::MaxLaps); break;
			default: Setup.bViewEveryKart = !Setup.bViewEveryKart; break;
			}
			Setup.Sanitize(Grid());
			const bool bChanged = Setup.Bots != Before.Bots || Setup.BotSkill != Before.BotSkill || Setup.Laps != Before.Laps || Setup.bViewEveryKart != Before.bViewEveryKart;
			if (bChanged)
			{
				Focus.Changed(Row, Dir, NowSeconds());
			}
			Host->PlaySound(bChanged ? EKartUISound::Tick : EKartUISound::Error);
		}

		void Next()
		{
			Host->PlaySound(EKartUISound::Accept);
			Host->PushScreen(KartScreens::MakeTracks(FrontEnd.Get()));
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		FListFocus Focus;
		/** Packed setup fields the cached text below was built from (-1: rebuild). */
		int32 ValuesKey = -1;
		FText Values[NumOptionRows];
		FText RacersText;
		FText SummaryText;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeRules(AKartFrontEnd* FrontEnd) { return MakeShared<FRulesScreen>(FrontEnd); }
}

#undef LOCTEXT_NAMESPACE
