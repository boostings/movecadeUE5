#include "Games/Stacker/SStackerHud.h"

#include "Games/Stacker/StackerGame.h"
#include "Games/Stacker/StackerRooftop.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Stacker;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	constexpr float PanelHeight = 168.f;

	/** A life: a small block. */
	void LifeBlock(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Fill)
	{
		P.Box(C - FVector2D(14.f, 7.f) * S, FVector2D(28.f, 14.f) * S, Fill, 4.f * S);
	}

	/** An arrow pointing along Dir (+1 right, -1 left), its tip at Tip. */
	void Arrow(const FKartPainter& P, const FVector2D& Tip, float Dir, float Len, float Head, float Thick, const FLinearColor& Ink)
	{
		const FVector2D Back = Tip - FVector2D(Dir * Head, 0.f);
		P.Box(FVector2D(FMath::Min(Back.X, Back.X - Dir * (Len - Head)), Tip.Y - Thick / 2.f), FVector2D(Len - Head + 1.f, Thick), Ink, Thick / 2.f);
		P.Polygon({ Tip, Back + FVector2D(0.f, -Head * 0.8f), Back + FVector2D(0.f, Head * 0.8f) }, Ink);
	}

	/** A speech-bubble card centred over Anchor with a pointer down to it. */
	void Bubble(const FKartPainter& P, const FVector2D& Anchor, const FVector2D& Size, const FLinearColor& Fill, const FLinearColor& Edge, float S)
	{
		const FVector2D Pos(Anchor.X - Size.X / 2.f, Anchor.Y - Size.Y - 16.f * S);
		P.Card(Pos, Size, Fill, 18.f * S, 6.f * S, Edge, 3.f * S);
		P.Polygon({ Anchor, Anchor + FVector2D(-14.f, -18.f) * S, Anchor + FVector2D(14.f, -18.f) * S }, Edge);
	}

	const FString& Cached(int32 Value, int32 Shown, FString& Text, TFunctionRef<FString()> Make, int32& Store)
	{
		if (Store != Value)
		{
			Store = Value;
			Text = Make();
		}
		(void)Shown;
		return Text;
	}
}

void SStackerHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
	if (const AStackerGame* G = Game.Get())
	{
		for (const FStackerSeat& Seat : G->GetSeats())
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
		}
	}
}

int32 SStackerHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const AStackerGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	if (G->GetStage() != EStackerStage::Finished)
	{
		bStandingsCached = false;
	}
	switch (G->GetStage())
	{
	case EStackerStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case EStackerStage::Finished:
		PaintStandings(P, *G, S);
		break;
	default:
	{
		// One projection for every point this paint (the lanes, the callouts, the coach).
		const FStackerView View = G->GetView();
		PaintClock(P, *G, S);
		PaintLanes(P, *G, View, S);
		PaintCallouts(P, *G, View, S);
		PaintCoach(P, *G, View, S);
		PaintControls(P, *G, S);
		PaintCountdown(P, *G, S);
		break;
	}
	}
	return Layer;
}

void SStackerHud::PaintClock(const FKartPainter& P, const AStackerGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * KartUI::Metric::SafeFraction);
	const int32 Seconds = FMath::CeilToInt32(M.TimeRemainingMs / 1000.0);
	const bool bLate = Seconds <= 10 && M.Phase == EMatchPhase::Playing;
	const float Pulse = bLate ? 1.f + 0.06f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 6.f)) : 1.f;
	P.Card(Top - FVector2D(110.f, 0.f) * S, FVector2D(220.f, 112.f) * S, KC::WithAlpha(KC::Night, 0.8f), 26.f * S, 6.f * S, bLate ? KC::Hot : KC::Sun, 3.f * S);
	if (ClockText.Value != Seconds)
	{
		ClockText.Value = Seconds;
		ClockText.Text = FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
	}
	P.Transformed(Top + FVector2D(0.f, 46.f * S), Pulse).Text(ClockText.Text, KartUI::Font(EFont::Display, 56.f * S, 3.f * S, KC::Night),
		Top + FVector2D(0.f, 46.f * S), bLate ? KC::Hot : KC::White, FVector2D(0.5, 0.5), 4.f * S);
	// The wind, the same for every tower: which way it pushes and how hard, at the tallest tower's height.
	int32 Tallest = 0;
	for (const FTower& T : M.Towers)
	{
		Tallest = FMath::Max(Tallest, T.Height());
	}
	const FVector2D Wind = WindAt(M.ElapsedMs, Tallest);
	const float Strength = FMath::Clamp(static_cast<float>(FMath::Abs(Wind.X) / 7.0), 0.f, 1.f);
	const FVector2D C = Top + FVector2D(0.f, 140.f * S);
	const float Dir = Wind.X >= 0.0 ? 1.f : -1.f;
	const FLinearColor Ink = FMath::Lerp(KC::Mint, KC::Hot, Strength);
	P.Card(C - FVector2D(130.f, 22.f) * S, FVector2D(260.f, 44.f) * S, KC::WithAlpha(KC::Night, 0.78f), 22.f * S, 4.f * S, KC::WithAlpha(Ink, 0.7f), 2.f * S);
	P.Text(NSLOCTEXT("Stacker", "Wind", "WIND"), KartUI::Font(EFont::Label, 15.f * S), C + FVector2D(-112.f * S, 0.f), KC::WithAlpha(KC::White, 0.8f), FVector2D(0.0, 0.5));
	// Three chevrons streaming the way it blows, lit by strength, drifting so the eye reads the direction.
	const float Drift = FMath::Frac(static_cast<float>(G.GetClock()) * (0.6f + 1.6f * Strength));
	for (int32 k = 0; k < 3; ++k)
	{
		const float Lit = Strength * 3.f - k;
		const float X = (-18.f + k * 22.f + Drift * 8.f) * Dir * S;
		const FVector2D At = C + FVector2D(X + 10.f * S, 0.f);
		const FLinearColor Col = Lit > 0.f ? KC::WithAlpha(Ink, FMath::Clamp(0.35f + Lit, 0.35f, 1.f)) : KC::WithAlpha(KC::White, 0.18f);
		P.Lines({ At + FVector2D(-7.f * Dir, -10.f) * S, At + FVector2D(5.f * Dir, 0.f) * S, At + FVector2D(-7.f * Dir, 10.f) * S }, Col, 4.f * S);
	}
	// Strength bars on the right.
	for (int32 k = 0; k < 5; ++k)
	{
		const float H = (8.f + k * 4.f) * S;
		const bool bOn = Strength * 5.f > k + 0.25f;
		P.Box(C + FVector2D((62.f + k * 11.f) * S, 12.f * S - H), FVector2D(7.f * S, H), bOn ? Ink : KC::WithAlpha(KC::White, 0.16f), 2.f * S);
	}
}

void SStackerHud::PaintLanes(const FKartPainter& P, const AStackerGame& G, const FStackerView& View, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 Span = G.LaneSpan();
	const float Width = FMath::Min(460.f * S, (P.Size().X * 0.92f) / FMath::Max(1, M.Towers.Num()) - 24.f * S);
	const float Height = PanelHeight * S;
	LaneText.SetNum(M.Towers.Num());
	PanelAt.SetNum(M.Towers.Num());
	for (int32 t = 0; t < M.Towers.Num(); ++t)
	{
		const FTower& Tower = M.Towers[t];
		FLaneText& Text = LaneText[t];
		const FStackerSeat* Seat = G.SeatOf(Tower.PlayerId);
		const bool bHuman = Seat && Seat->Kind != EBuilderKind::Bot;
		const FLinearColor Color = AStackerRooftop::LaneColor(Tower.LaneIndex);
		FVector2D At01(0.5, 0.9);
		if (View.Project(AStackerGame::W(FVector(AStackerRooftop::LaneX(Tower.LaneIndex, Span), -0.7, 2.5)), At01))
		{
			At01.X = FMath::Clamp(At01.X, 0.13, 0.87);
		}
		const FVector2D Pos(At01.X * P.Size().X - Width / 2.f, P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height);
		PanelAt[t] = Pos + FVector2D(Width / 2.f, 0.f);
		const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
		const double Ratio = FMath::Sqrt(Tower.Balance.LeanX * Tower.Balance.LeanX + Tower.Balance.LeanY * Tower.Balance.LeanY) / Limit;
		const float Danger = static_cast<float>(Tower.Balance.DangerMs / BalanceFailureMs);
		const bool bWarn = Ratio > 1.0 || Danger > 0.f;
		const float Flash = bWarn ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(G.GetClock()) * (10.f + 10.f * Danger)) : 0.f;
		P.Card(Pos, FVector2D(Width, Height), KC::WithAlpha(KC::Night, Tower.bEliminated ? 0.5f : 0.86f), 22.f * S, 6.f * S,
			bWarn ? FMath::Lerp(Color, KC::Hot, Flash) : Color, (bWarn ? 4.f : 2.f) * S);
		// A strip of the lane's colour along the top, so the panel ties to its tower at a glance.
		P.Box(Pos + FVector2D(18.f * S, 0.f), FVector2D(Width - 36.f * S, 5.f * S), Color, 2.5f * S);
		P.Circle(Pos + FVector2D(30.f * S, 34.f * S), 11.f * S, Color, KC::Night, 2.f * S);
		P.Text(Tower.Name, KartUI::Font(EFont::Title, 26.f * S), Pos + FVector2D(52.f * S, 34.f * S), KC::White, FVector2D(0.0, 0.5));
		const FString& ScoreText = Cached(Tower.Score, 0, Text.Score.Text, [&Tower]() { return FString::FromInt(Tower.Score); }, Text.Score.Value);
		P.Text(ScoreText, KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(Width - 22.f * S, 40.f * S), KC::White, FVector2D(1.0, 0.5), 3.f * S);
		for (int32 i = 0; i < StartingLives; ++i)
		{
			LifeBlock(P, Pos + FVector2D((38.f + i * 36.f) * S, 78.f * S), S, i < Tower.Lives ? Color : KC::WithAlpha(KC::White, 0.16f));
		}
		const FString& RowText = Cached(Tower.Height(), 0, Text.Row.Text, [&Tower]() { return FString::Printf(TEXT("ROW %d"), Tower.Height()); }, Text.Row.Value);
		P.Text(RowText, KartUI::Font(EFont::Label, 17.f * S), Pos + FVector2D(140.f * S, 78.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.0, 0.5));
		if (Tower.Combo >= 2)
		{
			// A combo pill on the right, clear of the row count.
			const FString& ComboText = Cached(Tower.Combo, 0, Text.Combo.Text, [&Tower]() { return FString::Printf(TEXT("x%d PERFECT"), Tower.Combo); }, Text.Combo.Value);
			const FSlateFontInfo ComboFont = KartUI::Font(EFont::Label, 15.f * S);
			const FVector2D Sz = FKartPainter::Measure(ComboText, ComboFont);
			const FLinearColor Ink = Tower.Combo >= 5 ? KC::Sun : KC::Mint;
			const FVector2D PillPos(Pos.X + Width - 22.f * S - Sz.X - 20.f * S, Pos.Y + 78.f * S - 13.f * S);
			P.Box(PillPos, FVector2D(Sz.X + 20.f * S, 26.f * S), KC::WithAlpha(Ink, 0.18f), 13.f * S, Ink, 1.5f * S);
			P.Text(ComboText, ComboFont, PillPos + FVector2D(10.f * S, 13.f * S), Ink, FVector2D(0.0, 0.5));
		}
		// The balance gauge: the lean across (the dot) against the steady band and the limit, red beyond it; the
		// wind's push as a small arrow over the middle; the danger toward a topple filling under it.
		const float GaugeH = 22.f * S;
		const FVector2D GaugePos = Pos + FVector2D(30.f * S, 106.f * S);
		const float GaugeW = Width - 60.f * S;
		const float Mid = GaugePos.X + GaugeW / 2.f;
		const float LimitX = GaugeW / 2.f / 1.5f;
		P.Box(GaugePos, FVector2D(GaugeW, GaugeH), KC::WithAlpha(KC::White, 0.1f), GaugeH / 2.f);
		P.Box(GaugePos, FVector2D(GaugeW / 2.f - LimitX, GaugeH), KC::WithAlpha(KC::Hot, 0.28f), GaugeH / 2.f);
		P.Box(FVector2D(Mid + LimitX, GaugePos.Y), FVector2D(GaugeW / 2.f - LimitX, GaugeH), KC::WithAlpha(KC::Hot, 0.28f), GaugeH / 2.f);
		const float Steady = LimitX * static_cast<float>(SteadyBalanceRatio);
		P.Box(FVector2D(Mid - Steady, GaugePos.Y), FVector2D(Steady * 2.f, GaugeH), KC::WithAlpha(KC::Mint, 0.38f), GaugeH / 2.f);
		P.Box(FVector2D(Mid - 1.f * S, GaugePos.Y + 4.f * S), FVector2D(2.f * S, GaugeH - 8.f * S), KC::WithAlpha(KC::White, 0.5f));
		for (const float Side : { -1.f, 1.f })
		{
			const float X = Mid + Side * LimitX;
			P.Box(FVector2D(X - 1.5f * S, GaugePos.Y - 4.f * S), FVector2D(3.f * S, GaugeH + 8.f * S), KC::WithAlpha(KC::Hot, 0.9f));
		}
		P.Text(FString(TEXT("L")), KartUI::Font(EFont::Label, 14.f * S), FVector2D(GaugePos.X - 14.f * S, GaugePos.Y + GaugeH / 2.f), KC::WithAlpha(KC::White, 0.6f), FVector2D(0.5, 0.5));
		P.Text(FString(TEXT("R")), KartUI::Font(EFont::Label, 14.f * S), FVector2D(GaugePos.X + GaugeW + 14.f * S, GaugePos.Y + GaugeH / 2.f), KC::WithAlpha(KC::White, 0.6f), FVector2D(0.5, 0.5));
		const float Lean = FMath::Clamp(static_cast<float>(Tower.Balance.LeanX / Limit), -1.5f, 1.5f);
		const FLinearColor DotColor = Ratio > 1.0 ? KC::Hot : (Ratio <= SteadyBalanceRatio ? KC::Mint : KC::Sun);
		const FVector2D Dot(Mid + Lean / 1.5f * GaugeW / 2.f, GaugePos.Y + GaugeH / 2.f);
		P.Circle(Dot, 17.f * S, KC::WithAlpha(DotColor, 0.25f));
		P.Circle(Dot, 12.f * S, DotColor, KC::Night, 2.5f * S);
		if (bHuman && Ratio > SteadyBalanceRatio && !Tower.IsDone())
		{
			// Chevrons back toward the middle: which way to tip the palm.
			const float Back = -FMath::Sign(Lean);
			for (int32 k = 0; k < 2; ++k)
			{
				const FVector2D C = Dot + FVector2D(Back * (26.f + k * 12.f) * S, 0.f);
				P.Lines({ C + FVector2D(-5.f * Back, -7.f) * S, C + FVector2D(4.f * Back, 0.f) * S, C + FVector2D(-5.f * Back, 7.f) * S }, KC::WithAlpha(KC::White, 0.9f - k * 0.35f), 3.f * S);
			}
		}
		if (Danger > 0.f)
		{
			const FVector2D Bar = GaugePos + FVector2D(0.f, GaugeH + 10.f * S);
			P.Box(Bar, FVector2D(GaugeW, 8.f * S), KC::WithAlpha(KC::Hot, 0.2f), 4.f * S);
			P.Box(Bar, FVector2D(GaugeW * FMath::Min(1.f, Danger), 8.f * S), FMath::Lerp(KC::Sun, KC::Hot, FMath::Min(1.f, Danger)), 4.f * S);
		}
		const bool bPhoneQuiet = Seat && Seat->Kind == EBuilderKind::Phone && BalanceInputMissing(Tower.Balance, M.ElapsedMs) && M.Phase == EMatchPhase::Playing;
		if (Tower.bEliminated || Tower.bCompleted || bPhoneQuiet || Tower.Balance.RecoveryMs > 0.0 || Danger > 0.35f)
		{
			const bool bTipping = !Tower.bEliminated && !Tower.bCompleted && !bPhoneQuiet && Tower.Balance.RecoveryMs <= 0.0;
			const FText Banner = Tower.bCompleted ? NSLOCTEXT("Stacker", "Done", "COMPLETE!") : Tower.bEliminated ? NSLOCTEXT("Stacker", "Out", "OUT")
				: bPhoneQuiet ? NSLOCTEXT("Stacker", "Quiet", "HOLD YOUR PHONE FLAT") : bTipping ? NSLOCTEXT("Stacker", "Tipping", "IT'S TIPPING!") : NSLOCTEXT("Stacker", "Steady", "STEADY...");
			const FLinearColor Ink = Tower.bCompleted ? KC::Sun : (Tower.bEliminated || bTipping ? KC::Hot : KC::White);
			const float Wobble = bTipping ? Danger * 4.f * FMath::Sin(static_cast<float>(G.GetClock()) * 22.f) : 0.f;
			const FVector2D BannerAt = Pos + FVector2D(Width / 2.f, -26.f * S);
			P.Transformed(BannerAt, 1.f, Wobble).Text(Banner, KartUI::Font(EFont::Display, (bPhoneQuiet ? 26.f : 40.f) * S, 3.f * S, KC::Night), BannerAt, Ink, FVector2D(0.5, 0.5), 4.f * S);
		}
	}
}

void SStackerHud::PaintCallouts(const FKartPainter& P, const AStackerGame& G, const FStackerView& View, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 Span = G.LaneSpan();
	for (const FEffect& E : M.Effects)
	{
		const int32 t = G.TowerOfLane(E.LaneIndex);
		if (t == INDEX_NONE || E.Label.IsEmpty())
		{
			continue;
		}
		const FTower& Tower = M.Towers[t];
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		const FVector Web(AStackerRooftop::LaneX(E.LaneIndex, Span) + Tower.Top().Center, G.TopY(t) + 0.9, 0.0);
		FVector2D At01;
		if (!View.Project(AStackerGame::W(Web), At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * 80.0) * S);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.2), 0.f, 1.f), 2.4f);
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / 0.25), 0.f, 1.f);
		const bool bBig = E.Kind == EEffectKind::Perfect || E.Kind == EEffectKind::Complete || E.Kind == EEffectKind::Topout;
		FLinearColor Color = KC::White;
		switch (E.Kind)
		{
		case EEffectKind::Perfect: Color = KC::Sun; break;
		case EEffectKind::Complete: Color = KC::Gold; break;
		case EEffectKind::Miss:
		case EEffectKind::Topout: Color = KC::Hot; break;
		default: Color = FMath::Lerp(AStackerRooftop::LaneColor(E.LaneIndex), KC::White, 0.4f); break;
		}
		P.Transformed(At, FMath::Max(0.01f, In) * (bBig ? 1.2f : 0.9f), bBig ? -4.f : 0.f).Faded(Out).Text(E.Label, KartUI::Font(EFont::Display, 38.f * S, 4.f * S, KC::Night), At,
			Color, FVector2D(0.5, 0.5), 6.f * S);
	}
}

void SStackerHud::PaintCoach(const FKartPainter& P, const AStackerGame& G, const FStackerView& View, float S) const
{
	const FStackerCoach& Coach = G.GetCoach();
	const FMatch& M = G.GetMatch();
	if (!M.Towers.IsValidIndex(Coach.Tower))
	{
		return;
	}
	const FTower& Tower = M.Towers[Coach.Tower];
	const float Clock = static_cast<float>(G.GetClock());
	if (Coach.Drop > 0.02f)
	{
		// Over the sweeping block: wait for the guide to line up, then drop.
		const FVector Web(AStackerRooftop::LaneX(Tower.LaneIndex, G.LaneSpan()) + Tower.CursorCenter, G.TopY(Coach.Tower) + 1.0, 0.0);
		FVector2D At01;
		if (View.Project(AStackerGame::W(Web), At01))
		{
			const FVector2D Anchor = At01 * P.Size() - FVector2D(0.f, 12.f * S);
			const float Pop = Coach.bAligned ? 1.06f + 0.04f * FMath::Sin(Clock * 16.f) : 1.f;
			const FKartPainter F = P.Faded(Coach.Drop).Transformed(Anchor, Pop);
			const FVector2D Size(Coach.bAligned ? 300.f * S : 340.f * S, 78.f * S);
			Bubble(F, Anchor, Size, Coach.bAligned ? KC::Mint : KC::WithAlpha(KC::Night, 0.9f), Coach.bAligned ? KC::White : KC::Sun, S);
			const FVector2D Mid = Anchor - FVector2D(0.f, 16.f * S + Size.Y / 2.f);
			F.Text(Coach.bAligned ? NSLOCTEXT("Stacker", "DropNow", "DROP NOW!") : NSLOCTEXT("Stacker", "DropAligned", "DROP WHEN ALIGNED"),
				KartUI::Font(EFont::Display, 28.f * S, 2.f * S, KC::Night), Mid - FVector2D(0.f, 11.f * S), Coach.bAligned ? KC::Night : KC::Sun, FVector2D(0.5, 0.5));
			F.Text(NSLOCTEXT("Stacker", "DropKeys", "Space  ·  A  ·  Click"), KartUI::Font(EFont::Label, 16.f * S), Mid + FVector2D(0.f, 20.f * S),
				Coach.bAligned ? KC::Night : KC::WithAlpha(KC::White, 0.8f), FVector2D(0.5, 0.5));
		}
	}
	if (Coach.Lean > 0.02f && PanelAt.IsValidIndex(Coach.Tower))
	{
		// Over the builder's panel: an arrow the way to tip the palm.
		const FVector2D Anchor = PanelAt[Coach.Tower] - FVector2D(0.f, 64.f * S);
		const FKartPainter F = P.Faded(Coach.Lean);
		const FVector2D Size(420.f * S, 92.f * S);
		Bubble(F, Anchor, Size, KC::WithAlpha(KC::Night, 0.92f), KC::Sky, S);
		const FVector2D Mid = Anchor - FVector2D(0.f, 16.f * S + Size.Y / 2.f);
		F.Text(NSLOCTEXT("Stacker", "LeanWind", "LEAN AGAINST THE WIND"), KartUI::Font(EFont::Display, 26.f * S, 2.f * S, KC::Night), Mid - FVector2D(0.f, 16.f * S), KC::Sky,
			FVector2D(0.5, 0.5));
		const float Side = Coach.LeanSide;
		const float Nudge = 6.f * S * FMath::Sin(Clock * 9.f);
		Arrow(F, Mid + FVector2D(Side * (150.f * S + Nudge), 18.f * S), Side, 70.f * S, 16.f * S, 8.f * S, KC::Sun);
		F.Text(Side > 0.f ? NSLOCTEXT("Stacker", "LeanKeysR", "D  ·  stick  ·  mouse right") : NSLOCTEXT("Stacker", "LeanKeysL", "A  ·  stick  ·  mouse left"),
			KartUI::Font(EFont::Label, 16.f * S), Mid + FVector2D(-Side * 30.f * S, 18.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5));
	}
}

void SStackerHud::PaintCountdown(const FKartPainter& P, const AStackerGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.36f);
	if (M.Phase == EMatchPhase::Countdown)
	{
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 N = FMath::CeilToInt32(Remaining);
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(1.0 - (N - Remaining)) / 0.35f, 0.f, 1.f));
		P.Transformed(Centre, FMath::Max(0.01f, Pop)).Text(FString::FromInt(FMath::Max(1, N)), KartUI::Font(EFont::Display, 200.f * S, 9.f * S, KC::Night), Centre, KC::Sun,
			FVector2D(0.5, 0.5), 10.f * S);
		return;
	}
	// runtime.ts: "go" while the first 700 ms play.
	if (M.Phase == EMatchPhase::Playing && M.ElapsedMs < 700.0)
	{
		const float A = FMath::Clamp(static_cast<float>((700.0 - M.ElapsedMs) / 300.0), 0.f, 1.f);
		P.Faded(A).Text(NSLOCTEXT("Stacker", "Go", "STACK!"), KartUI::Font(EFont::Display, 150.f * S, 8.f * S, KC::Night), Centre, KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	}
}

void SStackerHud::PaintControls(const FKartPainter& P, const AStackerGame& G, float S) const
{
	// The full controls while the countdown runs; the coach marks take over once blocks are moving.
	const FMatch& M = G.GetMatch();
	if (!G.HasLocal() || M.Phase != EMatchPhase::Countdown)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>(M.CountdownRemainingMs / 400.0), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.62f);
	const FKartPainter F = P.Faded(A);
	F.Card(Centre - FVector2D(470.f, 40.f) * S, FVector2D(940.f, 80.f) * S, KC::WithAlpha(KC::Night, 0.78f), 22.f * S, 6.f * S, KC::Sun, 2.f * S);
	F.Text(NSLOCTEXT("Stacker", "Controls", "Stick, A/D or mouse: lean against the wind  ·  A, Space or click: drop"), KartUI::Font(EFont::Body, 22.f * S), Centre,
		KC::White, FVector2D(0.5, 0.5));
}

void SStackerHud::PaintIntro(const FKartPainter& P, const AStackerGame& G, float S) const
{
	const double Age = G.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>((3.4 - Age) / 0.4), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.44f);
	const float Rise = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.6), 0.f, 1.f));
	F.Transformed(Centre - FVector2D(0.f, 60.f * S), FMath::Max(0.01f, Rise)).Text(NSLOCTEXT("Stacker", "Title", "SKY STACKER"), KartUI::Font(EFont::Display, 140.f * S, 7.f * S),
		Centre - FVector2D(0.f, 60.f * S), KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(NSLOCTEXT("Stacker", "Rules", "Drop each block on the one below. Overhang is cut away. Lean against the wind."), KartUI::Font(EFont::Body, 24.f * S),
		Centre + FVector2D(0.f, 100.f * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5), 3.f * S);
}

void SStackerHud::PaintStandings(const FKartPainter& P, const AStackerGame& G, float S) const
{
	const TArray<int32>& Order = G.Standings();
	const FMatch& M = G.GetMatch();
	if (!bStandingsCached)
	{
		bStandingsCached = true;
		StandingLines.Reset();
		for (const int32 t : Order)
		{
			const FTower& Tower = M.Towers[t];
			StandingLines.Add(Tower.bCompleted ? FString::Printf(TEXT("tower complete  ·  %d perfect"), Tower.Perfects)
				: FString::Printf(TEXT("%d rows  ·  %d perfect  ·  best x%d"), Tower.Height(), Tower.Perfects, Tower.BestCombo));
		}
	}
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.12f);
	F.Text(NSLOCTEXT("Stacker", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 84.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	// The rows sit low and narrow: the showcase of the towers is the picture.
	for (int32 i = 0; i < Order.Num() && i < StandingLines.Num(); ++i)
	{
		const FTower& Tower = M.Towers[Order[i]];
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.6 - i * 0.18) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X - 760.f * S, P.Size().Y * 0.22f + i * 96.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(340.f, 40.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = M.WinnerIds.Contains(Tower.PlayerId);
		Row.Card(Pos, FVector2D(680.f, 80.f) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), 22.f * S, 6.f * S, AStackerRooftop::LaneColor(Tower.LaneIndex), 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(44.f * S, 40.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Tower.Name, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(92.f * S, 27.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(StandingLines[i], KartUI::Font(EFont::Body, 17.f * S), Pos + FVector2D(92.f * S, 58.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Tower.Score), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(655.f * S, 40.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
