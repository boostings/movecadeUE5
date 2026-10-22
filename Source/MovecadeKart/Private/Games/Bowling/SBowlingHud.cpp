#include "Games/Bowling/SBowlingHud.h"

#include "Games/Bowling/BowlingGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"
#include "UI/KartWidgets.h"

using namespace MovecadeKart::Bowling;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	/** The meter's sweet band (the web speed dial's 7.6-9 m/s) starts at this power and runs to the end. */
	constexpr float SweetPower = 0.6f;
	constexpr float SweetBandWidth = 0.4f;
	/** The swing read-out shows for this long after the release. */
	constexpr double SwingReadSeconds = 3.2;
	/** The coach card shows for a local bowler's first throws, the second time dimmer and briefly. */
	constexpr int32 CoachThrows = 2;
	constexpr double CoachSecondTurnSeconds = 5.0;
	/** A long name shrinks to fit its column down to this size, then loses letters. */
	constexpr float NameSize = 26.f;
	constexpr float MinNameSize = 15.f;
	constexpr float NameRoom = 166.f;

	// Fixed labels are ARCADE_LABELs (made once); labels with values are formatted only when the value changes.

	/** The keycaps' labels. */
	struct FKeyLabels
	{
		FString A = TEXT("A");
		FString D = TEXT("D");
		FString Q = TEXT("Q");
		FString E = TEXT("E");
		FString Space = TEXT("SPACE");
	};

	const FKeyLabels& Keys()
	{
		static const FKeyLabels Labels;
		return Labels;
	}

	/** "1".."4" for the results' places (a game seats at most four). */
	const FString& PlaceLabel(int32 Place)
	{
		static const FString Labels[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") };
		ensure(Place >= 1 && Place <= UE_ARRAY_COUNT(Labels));
		return Labels[FMath::Clamp(Place, 1, static_cast<int32>(UE_ARRAY_COUNT(Labels))) - 1];
	}

	/** The sheet's columns, scaled. */
	struct FSheetLayout
	{
		float RowH, Gap, NameW, FrameW, TenthW, TotalW, Width;

		static FSheetLayout For(int32 Rows, float S)
		{
			FSheetLayout L;
			L.RowH = (Rows > 2 ? 64.f : 72.f) * S;
			L.Gap = 8.f * S;
			L.NameW = 222.f * S;
			L.FrameW = 64.f * S;
			L.TenthW = 92.f * S;
			L.TotalW = 108.f * S;
			L.Width = L.NameW + L.FrameW * 9.f + L.TenthW + 10.f * S + L.TotalW;
			return L;
		}
	};

	/**
	 * One sheet row's ten frames: the ball marks in cells along the top right of each box (two, three in the 10th; a
	 * strike in the last cell), over the running total. CurrentFrame (1..10, or -1) is outlined in the seat's colour.
	 */
	void PaintFrames(const FKartPainter& P, const FSheetLayout& L, const TArray<TArray<FString>>& Marks, const TArray<FString>& Totals,
		const FVector2D& Pos, bool bUp, int32 CurrentFrame, const FLinearColor& SeatColor, float S)
	{
		const FLinearColor Ink = bUp ? KC::Navy : KC::White;
		const FLinearColor StrikeInk = bUp ? KC::Hot : KC::Sun;
		const FLinearColor SpareInk = bUp ? KC::SkyDeep : KC::Mint;
		const float Cell = 24.f * S;
		for (int32 f = 0; f < Frames; ++f)
		{
			const bool bTenth = f == Frames - 1;
			const FVector2D BoxPos = Pos + FVector2D(L.NameW + f * L.FrameW, 5.f * S);
			const FVector2D BoxSize((bTenth ? L.TenthW : L.FrameW) - 3.f * S, L.RowH - 10.f * S);
			const bool bCurrent = CurrentFrame == f + 1;
			P.Box(BoxPos, BoxSize, bCurrent ? KC::WithAlpha(SeatColor, 0.32f) : KC::WithAlpha(Ink, bUp ? 0.06f : 0.08f), 7.f * S,
				bCurrent ? SeatColor : FLinearColor::Transparent, bCurrent ? 2.f * S : 0.f);
			const int32 Cells = bTenth ? 3 : 2;
			const TArray<FString>& CellMarks = Marks[f];
			for (int32 k = 0; k < Cells; ++k)
			{
				const FVector2D CellPos = BoxPos + FVector2D(BoxSize.X - (Cells - k) * Cell - 3.f * S, 3.f * S);
				P.Box(CellPos, FVector2D(Cell - 2.f * S, Cell - 2.f * S), KC::WithAlpha(Ink, 0.07f), 4.f * S);
				const int32 MarkIndex = !bTenth && CellMarks.Num() == 1 && CellMarks[0] == TEXT("X") ? (k == 1 ? 0 : -1) : k;
				if (CellMarks.IsValidIndex(MarkIndex))
				{
					const FString& M = CellMarks[MarkIndex];
					const FLinearColor MarkInk = M == TEXT("X") ? StrikeInk : (M == TEXT("/") ? SpareInk : Ink);
					P.Text(M, KartUI::Font(EFont::Display, 20.f * S), CellPos + FVector2D(Cell / 2.f - 1.f * S, Cell / 2.f), MarkInk, FVector2D(0.5, 0.5));
				}
			}
			if (!Totals[f].IsEmpty())
			{
				P.Text(Totals[f], KartUI::Font(EFont::Title, 22.f * S), BoxPos + FVector2D(BoxSize.X / 2.f, BoxSize.Y - 4.f * S), Ink, FVector2D(0.5, 1.0));
			}
		}
	}

	FText BeatText(EBowlingBeat Beat, int32 Felled)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey: return ARCADE_LABEL("Bowling", "Turkey", "TURKEY!");
		case EBowlingBeat::Strike: return ARCADE_LABEL("Bowling", "Strike", "STRIKE!");
		case EBowlingBeat::Spare: return ARCADE_LABEL("Bowling", "Spare", "SPARE!");
		case EBowlingBeat::SplitConvert: return ARCADE_LABEL("Bowling", "SplitConvert", "SPLIT CONVERTED!");
		case EBowlingBeat::Split: return ARCADE_LABEL("Bowling", "Split", "SPLIT");
		case EBowlingBeat::Gutter: return ARCADE_LABEL("Bowling", "Gutter", "GUTTER");
		default:
			return Felled == 0 ? ARCADE_LABEL("Bowling", "Miss", "MISS") : FText::Format(ARCADE_LABEL("Bowling", "Pins", "{0} {0}|plural(one=PIN,other=PINS)"), Felled);
		}
	}

	/** The line under the banner word: the streak, the strike, the pick-up; empty for the rest. */
	FText BeatSubText(EBowlingBeat Beat, int32 Streak)
	{
		if (Beat == EBowlingBeat::Turkey || (Beat == EBowlingBeat::Strike && Streak >= 2))
		{
			return Streak >= 3 ? FText::Format(ARCADE_LABEL("Bowling", "InARow", "{0} STRIKES IN A ROW"), Streak)
				: ARCADE_LABEL("Bowling", "Double", "DOUBLE! ONE MORE FOR A TURKEY");
		}
		if (Beat == EBowlingBeat::Strike)
		{
			return ARCADE_LABEL("Bowling", "AllTen", "ALL TEN DOWN");
		}
		if (Beat == EBowlingBeat::SplitConvert)
		{
			return ARCADE_LABEL("Bowling", "Converted", "WHAT A PICK-UP");
		}
		return FText::GetEmpty();
	}

	FLinearColor BeatColor(EBowlingBeat Beat)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey:
		case EBowlingBeat::Strike: return KC::Sun;
		case EBowlingBeat::Spare:
		case EBowlingBeat::SplitConvert: return KC::Mint;
		case EBowlingBeat::Split: return KC::Grape;
		case EBowlingBeat::Gutter: return KC::Hot;
		default: return KC::Sky;
		}
	}

	/** The banner's beats (spares too, unlike the house's celebrations), and the two that land hardest. */
	bool IsBannerBeat(EBowlingBeat Beat)
	{
		return Beat == EBowlingBeat::Strike || Beat == EBowlingBeat::Turkey || Beat == EBowlingBeat::Spare || Beat == EBowlingBeat::SplitConvert;
	}

	bool IsHugeBeat(EBowlingBeat Beat)
	{
		return Beat == EBowlingBeat::Strike || Beat == EBowlingBeat::Turkey;
	}

	float BeatWordSize(EBowlingBeat Beat)
	{
		return Beat == EBowlingBeat::Turkey ? 168.f : (IsHugeBeat(Beat) ? 150.f : (IsBannerBeat(Beat) ? 124.f : 84.f));
	}

	const FString& LineText(EBowlingLine Line)
	{
		const FText& Pocket = ARCADE_LABEL("Bowling", "LinePocket", "IN THE POCKET");
		const FText& OnTarget = ARCADE_LABEL("Bowling", "LineOnTarget", "ON TARGET");
		const FText& Brooklyn = ARCADE_LABEL("Bowling", "LineBrooklyn", "BROOKLYN SIDE");
		const FText& HeadOn = ARCADE_LABEL("Bowling", "LineHeadOn", "HEAD ON");
		const FText& Wide = ARCADE_LABEL("Bowling", "LineWide", "WIDE");
		const FText& Gutter = ARCADE_LABEL("Bowling", "LineGutter", "GUTTER LINE");
		const FText& Light = ARCADE_LABEL("Bowling", "LineLight", "OFF THE POCKET");
		switch (Line)
		{
		case EBowlingLine::Pocket: return Pocket.ToString();
		case EBowlingLine::OnTarget: return OnTarget.ToString();
		case EBowlingLine::Brooklyn: return Brooklyn.ToString();
		case EBowlingLine::HeadOn: return HeadOn.ToString();
		case EBowlingLine::Wide: return Wide.ToString();
		case EBowlingLine::Gutter: return Gutter.ToString();
		default: return Light.ToString();
		}
	}

	FLinearColor LineColor(EBowlingLine Line)
	{
		switch (Line)
		{
		case EBowlingLine::Pocket:
		case EBowlingLine::OnTarget: return KC::Mint;
		case EBowlingLine::Brooklyn: return KC::Sky;
		case EBowlingLine::HeadOn: return KC::Sun;
		case EBowlingLine::Gutter: return KC::Hot;
		default: return KC::WithAlpha(KC::White, 0.85f);
		}
	}

	/** A keyboard key or pad button drawn as a cap from its left edge's middle (LeftMid); returns its width. */
	float Keycap(const FKartPainter& P, const FString& Label, const FVector2D& LeftMid, float S, const FLinearColor& Accent = KC::White)
	{
		const FSlateFontInfo Font = KartUI::Font(EFont::Label, 18.f * S);
		const float W = FMath::Max(40.f * S, static_cast<float>(FKartPainter::Measure(Label, Font).X) + 22.f * S);
		const FVector2D Size(W, 40.f * S);
		const FVector2D C = LeftMid + FVector2D(W / 2.f, 0.f);
		P.Box(C - Size / 2.f + FVector2D(0.f, 4.f * S), Size, KC::WithAlpha(KC::Night, 0.9f), 9.f * S);
		P.Box(C - Size / 2.f, Size, KC::Paper, 9.f * S, Accent, 2.f * S);
		P.Text(Label, Font, C, KC::Navy, FVector2D(0.5, 0.5));
		return W;
	}

	/** A rounded pill of text in Color, centred at C. */
	void Chip(const FKartPainter& P, const FString& Text, const FVector2D& C, float Size, const FLinearColor& Color, float S, const FLinearColor& Ink = KC::Night)
	{
		const FSlateFontInfo Font = KartUI::Font(EFont::Label, Size * S);
		const FVector2D T = FKartPainter::Measure(Text, Font);
		const FVector2D Box(T.X + 34.f * S, T.Y + 14.f * S);
		P.Card(C - Box / 2.f, Box, Color, Box.Y / 2.f, 4.f * S);
		P.Text(Text, Font, C, Ink, FVector2D(0.5, 0.5));
	}
}

void SBowlingHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SBowlingHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const ABowlingGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	PaintFlash(P, *G);
	if (G->GetPhase() == EBowlingPhase::Intro)
	{
		PaintIntro(P, *G, S);
		return Layer;
	}
	if (G->GetPhase() == EBowlingPhase::Finished)
	{
		PaintStandings(P, *G, S);
		return Layer;
	}
	PaintSheet(P, *G, S);
	PaintUp(P, *G, S);
	PaintCoach(P, *G, S);
	PaintControls(P, *G, S);
	PaintSwing(P, *G, S);
	PaintBeat(P, *G, S);
	return Layer;
}

// ---- Caches -----------------------------------------------------------------------------------------------------

const SBowlingHud::FRowCache& SBowlingHud::Row(const ABowlingGame& G, int32 SeatIndex, float S) const
{
	if (Rows.Num() != G.GetSeats().Num())
	{
		Rows.Reset();
		Rows.SetNum(G.GetSeats().Num());
	}
	FRowCache& R = Rows[SeatIndex];
	const FBowlerSeat& Seat = G.GetSeats()[SeatIndex];
	const FCard* Card = G.GetMatch().CardOf(Seat.Id);
	const int32 Rolls = Card ? Card->Rolls.Num() : 0;
	if (R.Id != Seat.Id || R.Rolls != Rolls)
	{
		RebuildRow(R, Seat, Card);
	}
	if (R.Name != Seat.Name || R.FittedScale != S)
	{
		FitName(R, Seat.Name, S);
	}
	return R;
}

void SBowlingHud::RebuildRow(FRowCache& R, const FBowlerSeat& Seat, const FCard* Card)
{
	// The card changed (a roll, a new game): rebuild its strings once.
	R.Id = Seat.Id;
	R.Rolls = Card ? Card->Rolls.Num() : 0;
	R.Marks.Reset();
	R.Totals.Reset();
	R.Marks.SetNum(Frames);
	R.Totals.SetNum(Frames);
	R.Strikes = 0;
	R.Spares = 0;
	for (int32 f = 0; Card && f < Frames && Card->FrameList.IsValidIndex(f); ++f)
	{
		const FFrameScore& Frame = Card->FrameList[f];
		R.Marks[f] = FrameMarks(Frame);
		if (Frame.Cumulative.IsSet())
		{
			R.Totals[f] = FString::FromInt(Frame.Cumulative.GetValue());
		}
		for (const FString& M : R.Marks[f])
		{
			R.Strikes += M == TEXT("X") ? 1 : 0;
			R.Spares += M == TEXT("/") ? 1 : 0;
		}
	}
	R.Score = FString::FromInt(Card ? Card->Score : 0);
	R.Frame = Card ? FrameNumberFor(*Card) : -1;
	R.Tally = FText::Format(ARCADE_LABEL("Bowling", "Tally", "{0} {0}|plural(one=STRIKE,other=STRIKES)  ·  {1} {1}|plural(one=SPARE,other=SPARES)"),
		R.Strikes, R.Spares).ToString();
}

void SBowlingHud::FitName(FRowCache& R, const FString& Name, float S)
{
	// Long names shrink to the column, then lose letters, so they never run into frame 1.
	R.Name = Name;
	R.FittedScale = S;
	const float Room = NameRoom * S;
	R.FittedSize = NameSize * S;
	R.FittedName = Name;
	const double NaturalW = FKartPainter::Measure(R.FittedName, KartUI::Font(EFont::Title, R.FittedSize)).X;
	if (NaturalW <= Room)
	{
		return;
	}
	R.FittedSize = FMath::Max(MinNameSize * S, R.FittedSize * Room / static_cast<float>(NaturalW));
	while (R.FittedName.Len() > 2 && FKartPainter::Measure(R.FittedName, KartUI::Font(EFont::Title, R.FittedSize)).X > Room)
	{
		R.FittedName = R.FittedName.LeftChop(2) + TEXT(".");
	}
}

const SBowlingHud::FBeatCache& SBowlingHud::Beat(const ABowlingGame& G, float S) const
{
	FBeatCache& B = BeatWords;
	if (B.At != G.GetLastBeatAt())
	{
		B.At = G.GetLastBeatAt();
		B.Word = BeatText(G.GetLastBeat(), G.GetLastFelled()).ToString();
		B.Sub = BeatSubText(G.GetLastBeat(), G.GetLastStreak()).ToString();
		B.FittedScale = -1.f;
	}
	if (B.FittedScale != S)
	{
		// The turkey's stars are measured against the word as a turkey (its banner is the only one with stars).
		B.FittedScale = S;
		const float Size = BeatWordSize(G.GetLastBeat());
		B.HalfWidth = static_cast<float>(FKartPainter::Measure(BeatText(G.GetLastBeat(), 0).ToString(), KartUI::Font(EFont::Display, Size * S, 7.f * S)).X) / 2.f;
	}
	return B;
}

// ---- During play ------------------------------------------------------------------------------------------------

void SBowlingHud::PaintFlash(const FKartPainter& P, const ABowlingGame& G) const
{
	FLinearColor Color;
	const float A = G.GetFlash(Color);
	if (A > 0.003f)
	{
		P.Box(FVector2D::ZeroVector, P.Size(), KC::WithAlpha(Color, A));
	}
}

void SBowlingHud::PaintSheet(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	// One row per bowler: colour tab, name, ten frames (ball marks in cells over the running total), the score.
	const FVector2D Origin = P.Size() * KartUI::Metric::SafeFraction;
	const int32 Count = G.GetSeats().Num();
	const FSheetLayout L = FSheetLayout::For(Count, S);
	const int32 Up = G.GetBowlerSeat();
	for (int32 r = 0; r < Count; ++r)
	{
		const FBowlerSeat& Seat = G.GetSeats()[r];
		const FRowCache& C = Row(G, r, S);
		const bool bUp = r == Up;
		const FVector2D Pos = Origin + FVector2D(0.f, r * (L.RowH + L.Gap));
		P.Card(Pos, FVector2D(L.Width, L.RowH), bUp ? KC::Paper : KC::WithAlpha(KC::Night, 0.84f), 14.f * S, 5.f * S,
			bUp ? Seat.Color : FLinearColor::Transparent, bUp ? 3.f * S : 0.f);
		const FLinearColor Ink = bUp ? KC::Navy : KC::White;
		// The seat's colour tab down the left edge.
		P.Box(Pos + FVector2D(10.f * S, 10.f * S), FVector2D(8.f * S, L.RowH - 20.f * S), Seat.Color, 4.f * S);
		P.Text(C.FittedName, KartUI::Font(EFont::Title, C.FittedSize), Pos + FVector2D(32.f * S, L.RowH / 2.f), Ink, FVector2D(0.0, 0.5));
		PaintFrames(P, L, C.Marks, C.Totals, Pos, bUp, bUp ? C.Frame : -1, Seat.Color, S);
		P.Text(C.Score, KartUI::Font(EFont::Display, 38.f * S), Pos + FVector2D(L.Width - 18.f * S, L.RowH / 2.f), Ink, FVector2D(1.0, 0.5));
		// A strike streak burns on the row.
		if (Seat.Streak >= 2)
		{
			const FString& Streak = C.StreakText.Get(Seat.Streak, [](int32 Value) { return FText::Format(ARCADE_LABEL("Bowling", "StreakChip", "{0}X"), Value).ToString(); });
			Chip(P, Streak, Pos + FVector2D(L.Width + 40.f * S, L.RowH / 2.f), 18.f, Seat.Streak >= 3 ? KC::Sun : KC::Hot, S, KC::Night);
		}
	}
}

void SBowlingHud::PaintUp(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const FBowlerSeat* Bowler = G.GetBowler();
	if (!Bowler || !G.GetMatch().CardOf(Bowler->Id))
	{
		return;
	}
	const FRowCache& C = Row(G, G.GetBowlerSeat(), S);
	const FVector2D TopRight(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction);
	const FString& Frame = FrameText.Get(C.Frame, [](int32 Number) { return FString::Printf(TEXT("FRAME %d"), Number); });
	P.Text(Frame, KartUI::Font(EFont::Display, 50.f * S, 3.f * S), TopRight, KC::White, FVector2D(1.0, 0.0), 4.f * S);
	const int32 Ball = G.GetMatch().RollInFrame, Standing = G.GetStanding();
	const FString& BallLine = BallText.Get(Ball * 100 + Standing, [Ball, Standing](int32)
	{
		return FString::Printf(TEXT("BALL %d  ·  %d %s STANDING"), Ball, Standing, Standing == 1 ? TEXT("PIN") : TEXT("PINS"));
	});
	P.Text(BallLine, KartUI::Font(EFont::Label, 20.f * S, 2.f * S), TopRight + FVector2D(0.f, 66.f * S), KC::Sun, FVector2D(1.0, 0.0), 2.f * S);
}

void SBowlingHud::PaintControls(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const FBowlerSeat* Bowler = G.GetBowler();
	if (!Bowler || G.GetPhase() != EBowlingPhase::Aiming)
	{
		return;
	}
	const float In = KartUI::Ease::OutCubic(FMath::Clamp(static_cast<float>(G.GetPhaseAge() / 0.3), 0.f, 1.f));
	const FKartPainter F = P.Faded(In);
	// Lower right, clear of the bowler standing in the middle of the shot.
	const FVector2D Size(760.f * S, 168.f * S);
	const FVector2D Pos(P.Size().X * (1.f - KartUI::Metric::SafeFraction) - Size.X, P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Size.Y + (1.f - In) * 30.f * S);
	const FString& Up = UpText.Get(G.GetBowlerSeat(), [Bowler](int32)
	{
		return FText::Format(ARCADE_LABEL("Bowling", "Up", "{0} is up"), FText::FromString(Bowler->Name)).ToString();
	});
	EBowlingLine Line;
	if (Bowler->Kind != EBowlerKind::Local)
	{
		// Phones and bots: who is up and what they're doing, with the phone's live line.
		const FText& PhoneHint = ARCADE_LABEL("Bowling", "PhoneHint", "Swing your phone and let go to bowl");
		const FText& BotHint = ARCADE_LABEL("Bowling", "BotHint", "Lining up...");
		const FVector2D Small(Size.X, 96.f * S);
		const FVector2D At = Pos + FVector2D(0.f, Size.Y - Small.Y);
		F.Card(At, Small, KC::WithAlpha(KC::Night, 0.88f), 22.f * S, 6.f * S, Bowler->Color, 3.f * S);
		F.Circle(At + FVector2D(44.f * S, Small.Y / 2.f), 14.f * S, Bowler->Color, KC::White, 2.f * S);
		F.Text(Up, KartUI::Font(EFont::Title, 32.f * S), At + FVector2D(74.f * S, 32.f * S), KC::White, FVector2D(0.0, 0.5));
		F.Text(Bowler->Kind == EBowlerKind::Phone ? PhoneHint : BotHint, KartUI::Font(EFont::Body, 21.f * S), At + FVector2D(74.f * S, 66.f * S),
			KC::WithAlpha(KC::White, 0.85f), FVector2D(0.0, 0.5));
		if (Bowler->Kind == EBowlerKind::Phone && G.GetLiveLine(Line))
		{
			Chip(F, LineText(Line), At + FVector2D(Small.X - 130.f * S, Small.Y / 2.f), 17.f, LineColor(Line), S);
		}
		return;
	}
	F.Card(Pos, Size, KC::WithAlpha(KC::Night, 0.88f), 24.f * S, 6.f * S, Bowler->Color, 3.f * S);
	F.Circle(Pos + FVector2D(40.f * S, 38.f * S), 13.f * S, Bowler->Color, KC::White, 2.f * S);
	F.Text(Up, KartUI::Font(EFont::Title, 30.f * S), Pos + FVector2D(66.f * S, 38.f * S), KC::White, FVector2D(0.0, 0.5));
	// The live line's read, so you know before you throw.
	if (G.GetLiveLine(Line))
	{
		Chip(F, LineText(Line), Pos + FVector2D(Size.X - 140.f * S, 38.f * S), 17.f, LineColor(Line), S);
	}
	const FLocalBowl& L = G.GetLocal();
	const float Power = static_cast<float>(L.bGripped ? L.Power : 0.0);
	PaintMeter(F, Pos + FVector2D(30.f * S, 74.f * S), FVector2D(Size.X - 200.f * S, 26.f * S), Power, S);
	// The keys, or what to do now.
	const float RowY = Pos.Y + 136.f * S;
	if (!L.bGripped)
	{
		PaintKeys(F, G, FVector2D(Pos.X + 30.f * S, RowY), S);
		return;
	}
	const FText& Flick = ARCADE_LABEL("Bowling", "Flick", "Flick up and let go to bowl");
	const FText& ReleaseNow = ARCADE_LABEL("Bowling", "ReleaseNow", "LET GO NOW!");
	const FText& ReleaseGreen = ARCADE_LABEL("Bowling", "ReleaseGreen", "Let go in the green for a fast ball");
	const FText& Hint = L.bMouseFlick ? Flick : (Power >= SweetPower ? ReleaseNow : ReleaseGreen);
	F.Text(Hint, KartUI::Font(EFont::Title, 24.f * S), FVector2D(Pos.X + Size.X / 2.f, RowY), Power >= SweetPower ? KC::Mint : KC::White, FVector2D(0.5, 0.5));
}

void SBowlingHud::PaintMeter(const FKartPainter& F, const FVector2D& BarPos, const FVector2D& BarSize, float Power, float S) const
{
	// The 7.6-9 m/s sweet band of the web's speed dial, marked in mint.
	F.Box(BarPos, BarSize, KC::WithAlpha(KC::White, 0.12f), 13.f * S);
	F.Box(BarPos + FVector2D(BarSize.X * SweetPower, 0.f), FVector2D(BarSize.X * SweetBandWidth, BarSize.Y), KC::WithAlpha(KC::Mint, 0.3f), 13.f * S,
		KC::WithAlpha(KC::Mint, 0.8f), 1.5f * S);
	if (Power > 0.f)
	{
		F.Gradient(BarPos, FVector2D(FMath::Max(BarSize.Y, BarSize.X * Power), BarSize.Y), KC::Sky, Power >= SweetPower ? KC::Mint : KC::Sun, false, 13.f * S);
		F.Box(BarPos + FVector2D(BarSize.X * Power - 3.f * S, -6.f * S), FVector2D(6.f * S, BarSize.Y + 12.f * S), KC::White, 3.f * S);
	}
	const FString& Speed = SpeedText.Get(Power, [](float Value) { return FString::Printf(TEXT("%.1f m/s"), SpeedForPower(FMath::Max(0.04f, Value))); });
	F.Text(Speed, KartUI::Font(EFont::Label, 20.f * S), BarPos + FVector2D(BarSize.X + 18.f * S, BarSize.Y / 2.f), Power >= SweetPower ? KC::Mint : KC::White, FVector2D(0.0, 0.5));
}

void SBowlingHud::PaintKeys(const FKartPainter& F, const ABowlingGame& G, const FVector2D& Pos, float S) const
{
	const FText& Line = ARCADE_LABEL("Bowling", "KeyLine", "line");
	const FText& HookNone = ARCADE_LABEL("Bowling", "KeyHookNone", "hook");
	const FText& HookLeft = ARCADE_LABEL("Bowling", "KeyHookLeft", "hook left");
	const FText& HookRight = ARCADE_LABEL("Bowling", "KeyHookRight", "hook right");
	const FText& Grip = ARCADE_LABEL("Bowling", "KeyGrip", "hold to swing");
	const double Curve = G.GetLocal().Curve;
	float X = Pos.X;
	auto Pair = [&](const FString& A, const FString* B, const FText& What)
	{
		X += Keycap(F, A, FVector2D(X, Pos.Y), S) + 6.f * S;
		if (B)
		{
			X += Keycap(F, *B, FVector2D(X, Pos.Y), S);
		}
		X += 12.f * S;
		X += static_cast<float>(F.Text(What, KartUI::Font(EFont::Body, 20.f * S), FVector2D(X, Pos.Y), KC::WithAlpha(KC::White, 0.9f), FVector2D(0.0, 0.5)).X) + 30.f * S;
	};
	Pair(Keys().A, &Keys().D, Line);
	Pair(Keys().Q, &Keys().E, FMath::Abs(Curve) < 0.04 ? HookNone : (Curve < 0.0 ? HookLeft : HookRight));
	Pair(Keys().Space, nullptr, Grip);
}

void SBowlingHud::PaintCoach(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	// First turns: a step-by-step card beside the lane that ticks off as you go, then fades for good.
	const FBowlerSeat* Bowler = G.GetBowler();
	const int32 Thrown = G.GetLocalThrows();
	if (!Bowler || Bowler->Kind != EBowlerKind::Local || G.GetPhase() != EBowlingPhase::Aiming || Thrown >= CoachThrows)
	{
		return;
	}
	const double Age = G.GetPhaseAge();
	const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.15) / 0.4), 0.f, 1.f));
	// The second turn shows it again briefly, dimmer, then lets go.
	const float Keep = Thrown == 0 ? 1.f : 0.75f * FMath::Clamp(static_cast<float>((CoachSecondTurnSeconds - Age) / 1.0), 0.f, 1.f);
	if (Keep <= 0.f || In <= 0.f)
	{
		return;
	}
	const FText& Title = ARCADE_LABEL("Bowling", "CoachTitle", "HOW TO BOWL");
	const FText& LineStep = ARCADE_LABEL("Bowling", "CoachLine", "Slide your line. Mint dots = the pocket.");
	const FText& HookStep = ARCADE_LABEL("Bowling", "CoachHook", "Add hook: the ball curves into the pins.");
	const FText& SwingStep = ARCADE_LABEL("Bowling", "CoachSwing", "Hold to swing, let go in the green.");
	const FText& PadHelp = ARCADE_LABEL("Bowling", "CoachPad", "Pad: sticks to aim and hook, hold RT");
	const FText& MouseHelp = ARCADE_LABEL("Bowling", "CoachMouse", "Mouse: move to aim, click and flick up");
	const FVector2D Size(500.f * S, 350.f * S);
	const FVector2D Pos(P.Size().X * KartUI::Metric::SafeFraction, P.Size().Y * 0.37f);
	const FKartPainter F = P.Transformed(Pos + FVector2D(0.f, Size.Y / 2.f), FMath::Max(0.01f, In)).Faded(Keep);
	F.Card(Pos, Size, KC::WithAlpha(KC::Navy, 0.93f), 24.f * S, 8.f * S, KC::Sun, 3.f * S);
	F.Text(Title, KartUI::Font(EFont::Display, 30.f * S, 2.f * S), Pos + FVector2D(28.f * S, 34.f * S), KC::Sun, FVector2D(0.0, 0.5), 2.f * S);
	const bool bGripped = G.GetLocal().bGripped;
	struct FStep { const FString& A; const FString* B; const FText& What; bool bDone; bool bNow; };
	const FStep Steps[] = {
		{ Keys().A, &Keys().D, LineStep, G.HasLocalAimed(), !G.HasLocalAimed() },
		{ Keys().Q, &Keys().E, HookStep, G.HasLocalHooked(), G.HasLocalAimed() && !G.HasLocalHooked() && !bGripped },
		{ Keys().Space, nullptr, SwingStep, false, bGripped || (G.HasLocalAimed() && G.HasLocalHooked()) },
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Steps); ++i)
	{
		const FStep& Step = Steps[i];
		const float Y = Pos.Y + (92.f + i * 70.f) * S;
		if (Step.bNow)
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(G.GetClock()) * 6.f);
			F.Box(FVector2D(Pos.X + 12.f * S, Y - 30.f * S), FVector2D(Size.X - 24.f * S, 60.f * S), KC::WithAlpha(KC::Sun, 0.12f + 0.1f * Pulse), 14.f * S, KC::WithAlpha(KC::Sun, 0.7f), 2.f * S);
		}
		float X = Pos.X + 28.f * S;
		X += Keycap(F, Step.A, FVector2D(X, Y), S, Step.bNow ? KC::Sun : KC::White) + 6.f * S;
		if (Step.B)
		{
			Keycap(F, *Step.B, FVector2D(X, Y), S, Step.bNow ? KC::Sun : KC::White);
		}
		F.TextWrapped(Step.What, KartUI::Font(EFont::Body, 19.f * S), FVector2D(Pos.X + 170.f * S, Y - 24.f * S), 270.f * S,
			Step.bDone ? KC::WithAlpha(KC::White, 0.55f) : KC::White);
		// The tick.
		const FVector2D Tick(Pos.X + Size.X - 30.f * S, Y);
		F.Circle(Tick, 13.f * S, Step.bDone ? KC::Mint : KC::WithAlpha(KC::White, 0.1f), Step.bDone ? KC::Mint : KC::WithAlpha(KC::White, 0.5f), 2.f * S);
		if (Step.bDone)
		{
			F.Lines({ Tick + FVector2D(-6.f, 0.f) * S, Tick + FVector2D(-1.f, 5.f) * S, Tick + FVector2D(7.f, -5.f) * S }, KC::Night, 3.f * S);
		}
	}
	// The other inputs, in a footer of their own below a rule, one line each, well inside the card.
	F.Box(FVector2D(Pos.X + 24.f * S, Pos.Y + Size.Y - 66.f * S), FVector2D(Size.X - 48.f * S, 2.f * S), KC::WithAlpha(KC::White, 0.15f));
	F.Text(PadHelp, KartUI::Font(EFont::Body, 16.f * S), Pos + FVector2D(Size.X / 2.f, Size.Y - 46.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.5, 0.5));
	F.Text(MouseHelp, KartUI::Font(EFont::Body, 16.f * S), Pos + FVector2D(Size.X / 2.f, Size.Y - 22.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.5, 0.5));
}

void SBowlingHud::PaintSwing(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	// After the release: three read-outs pop in under the lane: the speed, the hook and the line.
	const EBowlingPhase Phase = G.GetPhase();
	if (Phase != EBowlingPhase::Delivery && Phase != EBowlingPhase::Rolling && Phase != EBowlingPhase::Settling)
	{
		return;
	}
	const FBowlingSwing& Sw = G.GetSwing();
	const double Age = G.GetClock() - Sw.At;
	if (Age < 0.0 || Age > SwingReadSeconds)
	{
		return;
	}
	const float Out = FMath::Clamp(static_cast<float>((SwingReadSeconds - Age) / 0.35), 0.f, 1.f);
	const FString& SpeedRead = SwingSpeedText.Get(Sw.At, [&Sw](double)
	{
		const FText Word = Sw.Power >= SweetPower ? ARCADE_LABEL("Bowling", "SpeedGreat", "GREAT SPEED")
			: (Sw.Power >= 0.3 ? ARCADE_LABEL("Bowling", "SpeedGood", "GOOD SPEED") : ARCADE_LABEL("Bowling", "SpeedSoft", "SOFT"));
		return FText::Format(ARCADE_LABEL("Bowling", "SpeedRead", "{0}  {1} m/s"), Word, FText::AsNumber(FMath::RoundToDouble(Sw.Speed * 10.0) / 10.0)).ToString();
	});
	const FString& HookRead = SwingHookText.Get(Sw.At, [&Sw](double)
	{
		return (FMath::Abs(Sw.Curve) < 0.06 ? ARCADE_LABEL("Bowling", "HookStraight", "STRAIGHT")
			: FText::Format(ARCADE_LABEL("Bowling", "HookAmount", "{0} HOOK"), FText::FromString(FMath::Abs(Sw.Curve) > 0.25 ? TEXT("BIG") : TEXT("SOFT")))).ToString();
	});
	const FLinearColor SpeedColor = Sw.Power >= SweetPower ? KC::Mint : (Sw.Power >= 0.3 ? KC::Sky : KC::Sun);
	const struct { const FString& Text; FLinearColor Color; } Items[] = {
		{ SpeedRead, SpeedColor },
		{ HookRead, KC::WithAlpha(KC::White, 0.9f) },
		{ LineText(Sw.Line), LineColor(Sw.Line) },
	};
	const float Y = P.Size().Y * 0.86f;
	for (int32 i = 0; i < UE_ARRAY_COUNT(Items); ++i)
	{
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.08 * i) / 0.3), 0.f, 1.f));
		if (In <= 0.f)
		{
			continue;
		}
		const FVector2D C(P.Size().X / 2.f + (i - 1) * 330.f * S, Y);
		Chip(P.Transformed(C, FMath::Max(0.01f, In)).Faded(Out), Items[i].Text, C, 21.f, Items[i].Color, S);
	}
}

void SBowlingHud::PaintBeat(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const EBowlingBeat BeatKind = G.GetLastBeat();
	const bool bBig = IsBannerBeat(BeatKind);
	const bool bHuge = IsHugeBeat(BeatKind);
	const double Hold = bHuge ? 2.1 : 1.8;
	const double Age = G.GetClock() - G.GetLastBeatAt();
	if (Age < 0.0 || Age > Hold || BeatKind == EBowlingBeat::None)
	{
		return;
	}
	const FBeatCache& Words = Beat(G, S);
	const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.35), 0.f, 1.f), bHuge ? 2.6f : 1.7f);
	const float Out = FMath::Clamp(static_cast<float>((Hold - Age) / 0.3), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.4f);
	const FLinearColor Color = BeatColor(BeatKind);
	if (bBig)
	{
		// A burst of rays behind the word, turning slowly.
		const FKartPainter Rays = P.Faded(Out * 0.55f * FMath::Clamp(static_cast<float>(Age / 0.2), 0.f, 1.f));
		const int32 Count = BeatKind == EBowlingBeat::Turkey ? 18 : 12;
		const float Spin = static_cast<float>(Age) * 0.35f;
		const float Reach = (bHuge ? 900.f : 620.f) * S * (0.6f + 0.4f * In);
		for (int32 i = 0; i < Count; ++i)
		{
			const float A0 = Spin + 2.f * UE_PI * i / Count, A1 = A0 + UE_PI / Count;
			Rays.Polygon({ Centre, Centre + FVector2D(FMath::Cos(A0), FMath::Sin(A0) * 0.55f) * Reach, Centre + FVector2D(FMath::Cos(A1), FMath::Sin(A1) * 0.55f) * Reach },
				KC::WithAlpha(Color, 0.1f));
		}
	}
	// Big words shudder for a beat as they land.
	const float Jolt = bHuge ? static_cast<float>(FMath::Max(0.0, 0.35 - Age) * 30.0 * FMath::Sin(Age * 80.0)) * S : 0.f;
	const FKartPainter Pop = P.Transformed(Centre, FMath::Max(0.01f, In), -4.f * (1.f - In), FVector2D(Jolt, 0.f)).Faded(Out);
	const float Size = BeatWordSize(BeatKind);
	Pop.Text(Words.Word, KartUI::Font(EFont::Display, Size * S, 7.f * S, KC::Night), Centre, Color, FVector2D(0.5, 0.5), 9.f * S);
	if (BeatKind == EBowlingBeat::Turkey)
	{
		// Stars bob either side of the word, clear of its letters and of the line under it.
		for (int32 i = 0; i < 4; ++i)
		{
			const float Side = i < 2 ? -1.f : 1.f;
			const float Phase = static_cast<float>(Age) * 3.f + i * 1.7f;
			const FVector2D At(Side * (Words.HalfWidth + (60.f + 24.f * (i % 2) + 10.f * FMath::Sin(Phase)) * S), ((i % 2 ? 46.f : -52.f) + 14.f * FMath::Cos(Phase)) * S);
			KartWidgets::Star(Pop, Centre + At, 26.f * S, 11.f * S, KC::Sun, Phase * 40.f);
		}
	}
	if (!Words.Sub.IsEmpty())
	{
		// The line under it.
		const float SubIn = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.25) / 0.3), 0.f, 1.f));
		const FVector2D SubAt = Centre + FVector2D(0.f, (Size * 0.62f + 10.f) * S);
		if (SubIn > 0.f)
		{
			Chip(P.Transformed(SubAt, FMath::Max(0.01f, SubIn)).Faded(Out), Words.Sub, SubAt, 26.f, Color, S);
		}
	}
}

// ---- Before and after -------------------------------------------------------------------------------------------

void SBowlingHud::PaintIntro(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const FText& Title = ARCADE_LABEL("Bowling", "Title", "BOWLING");
	const FText& Tagline = ARCADE_LABEL("Bowling", "Tagline", "MOVECADE LANES  ·  TEN FRAMES  ·  CHASE THE TURKEY");
	const double Age = G.GetPhaseAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.3), 0.f, 1.f) * FMath::Clamp(static_cast<float>((2.4 - Age) / 0.35), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre = P.Size() / 2.f;
	// A dark band across the lane so the card reads over the bright deck.
	F.Gradient(FVector2D(0.f, Centre.Y - 260.f * S), FVector2D(P.Size().X, 260.f * S), KC::WithAlpha(KC::Night, 0.f), KC::WithAlpha(KC::Night, 0.82f), true);
	F.Gradient(FVector2D(0.f, Centre.Y), FVector2D(P.Size().X, 260.f * S), KC::WithAlpha(KC::Night, 0.82f), KC::WithAlpha(KC::Night, 0.f), true);
	// The title slams in, with a sweep of light across it.
	const float Slam = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.45), 0.f, 1.f), 2.2f);
	const FVector2D TitleAt = Centre - FVector2D(0.f, 70.f * S);
	const FKartPainter T = F.Transformed(TitleAt, FMath::Max(0.01f, Slam * (1.f + 0.03f * static_cast<float>(Age))));
	T.Text(Title, KartUI::Font(EFont::Display, 160.f * S, 8.f * S), TitleAt, KC::Sun, FVector2D(0.5, 0.5), 10.f * S);
	const float Sweep = FMath::Clamp(static_cast<float>((Age - 0.35) / 0.6), 0.f, 1.f);
	if (Sweep > 0.f && Sweep < 1.f)
	{
		const float X = FMath::Lerp(-500.f, 500.f, Sweep) * S;
		F.Faded(0.5f * KartUI::Ease::Pulse(Sweep)).Polygon({ TitleAt + FVector2D(X - 40.f * S, -90.f * S), TitleAt + FVector2D(X + 30.f * S, -90.f * S),
			TitleAt + FVector2D(X - 10.f * S, 90.f * S), TitleAt + FVector2D(X - 80.f * S, 90.f * S) }, KC::WithAlpha(KC::White, 0.8f));
	}
	F.Text(Tagline, KartUI::Font(EFont::Label, 22.f * S, 2.f * S), Centre + FVector2D(0.f, 30.f * S), KC::WithAlpha(KC::White, 0.9f), FVector2D(0.5, 0.5), 2.f * S);
	// The bowlers, in order, as chips in their colours.
	const TArray<FBowlerSeat>& Seats = G.GetSeats();
	const float ChipW = 250.f * S, ChipGap = 16.f * S;
	const float Left = Centre.X - (Seats.Num() * ChipW + (Seats.Num() - 1) * ChipGap) / 2.f;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float ChipIn = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.3 - 0.08 * i) / 0.35), 0.f, 1.f));
		if (ChipIn <= 0.f)
		{
			continue;
		}
		const FVector2D Pos(Left + i * (ChipW + ChipGap), Centre.Y + 80.f * S);
		const FKartPainter C = F.Transformed(Pos + FVector2D(ChipW / 2.f, 30.f * S), FMath::Max(0.01f, ChipIn));
		C.Card(Pos, FVector2D(ChipW, 60.f * S), i == 0 ? KC::Paper : KC::WithAlpha(KC::Night, 0.9f), 30.f * S, 5.f * S, Seats[i].Color, 3.f * S);
		C.Circle(Pos + FVector2D(32.f * S, 30.f * S), 12.f * S, Seats[i].Color, KC::White, 2.f * S);
		C.Text(Seats[i].Name, KartUI::Font(EFont::Title, 24.f * S), Pos + FVector2D(54.f * S, 30.f * S), i == 0 ? KC::Navy : KC::White, FVector2D(0.0, 0.5));
	}
	if (Seats.Num() > 0)
	{
		const FString& First = FirstText.Get(static_cast<int32>(GetTypeHash(Seats[0].Name)), [&Seats](int32)
		{
			return FText::Format(ARCADE_LABEL("Bowling", "First", "{0} bowls first"), FText::FromString(Seats[0].Name)).ToString();
		});
		F.Text(First, KartUI::Font(EFont::Body, 22.f * S), Centre + FVector2D(0.f, 176.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5), 2.f * S);
	}
}

void SBowlingHud::PaintStandings(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const FText& Final = ARCADE_LABEL("Bowling", "Final", "FINAL SCORES");
	const TArray<FBowlerSeat>& Seats = G.GetSeats();
	// Highest first, ties in turn order (the seats are the turn order); the row caches hold each card's score.
	const TArray<int32>& Order = FinalRanks.Update(Seats.Num(), [&](int32 Seat)
	{
		const FCard* Card = G.GetMatch().CardOf(Seats[Seat].Id);
		return static_cast<int64>(Card ? Card->Score : 0);
	});
	const float A = FMath::Clamp(static_cast<float>(G.GetFinishedAge() / 0.5), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.15f);
	F.Text(Final, KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FBowlerSeat& Seat = Seats[Order[i]];
		const FRowCache& C = Row(G, Order[i], S);
		const float Delay = 0.3f + i * 0.18f;
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((G.GetFinishedAge() - Delay) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X / 2.f - 380.f * S, P.Size().Y * 0.25f + i * 100.f * S);
		const FKartPainter Card = F.Transformed(Pos + FVector2D(380.f, 44.f) * S, FMath::Max(0.01f, In));
		Card.Card(Pos, FVector2D(760.f, 88.f) * S, i == 0 ? KC::Paper : KC::WithAlpha(KC::Night, 0.88f), 22.f * S, 6.f * S, Seat.Color, 3.f * S);
		const FLinearColor Ink = i == 0 ? KC::Navy : KC::White;
		if (i == 0)
		{
			KartWidgets::Star(Card, Pos + FVector2D(48.f * S, 44.f * S), 42.f * S, 22.f * S, KC::Sun, static_cast<float>(G.GetClock()) * 30.f);
		}
		Card.Text(PlaceLabel(i + 1), KartUI::Font(EFont::Display, 44.f * S, 2.f * S), Pos + FVector2D(48.f * S, 44.f * S), i == 0 ? KC::Navy : KC::ForPlace(i + 1), FVector2D(0.5, 0.5));
		Card.Text(Seat.Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 34.f * S), Ink, FVector2D(0.0, 0.5));
		Card.Text(C.Tally, KartUI::Font(EFont::Label, 16.f * S), Pos + FVector2D(100.f * S, 66.f * S), i == 0 ? KC::SkyDeep : KC::WithAlpha(KC::White, 0.7f), FVector2D(0.0, 0.5));
		Card.Text(C.Score, KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(730.f * S, 44.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
