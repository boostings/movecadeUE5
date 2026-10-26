#include "Games/Stacker/SStackerHud.h"

#include "Games/Stacker/StackerGame.h"
#include "Games/Stacker/StackerRooftop.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"
#include "SStackerHudLayout.h"

using namespace MovecadeKart::Stacker;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	using L = MovecadeKart::Stacker::Hud::FHudLayout;

	/** A life: a small block. */
	void LifeBlock(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Fill)
	{
		P.Box(C - FVector2D(L::FPanel::LifeWidth / 2.f, L::FPanel::LifeHeight / 2.f) * S, FVector2D(L::FPanel::LifeWidth, L::FPanel::LifeHeight) * S, Fill,
			L::FPanel::LifeRadius * S);
	}

	/** A chevron (an open arrowhead) at C pointing along Dir (+1 right, -1 left). */
	void Chevron(const FKartPainter& P, const FVector2D& C, float Dir, float Back, float Tip, float HalfHeight, float Thickness,
		const FLinearColor& Ink, float S)
	{
		P.Lines({ C + FVector2D(-Back * Dir, -HalfHeight) * S, C + FVector2D(Tip * Dir, 0.f) * S, C + FVector2D(-Back * Dir, HalfHeight) * S }, Ink, Thickness * S);
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
		using C = L::FCoach;
		const FVector2D Pos(Anchor.X - Size.X / 2.f, Anchor.Y - Size.Y - C::PointerGap * S);
		P.Card(Pos, Size, Fill, C::BubbleRadius * S, C::BubbleShadow * S, Edge, C::BubbleOutline * S);
		P.Polygon({ Anchor, Anchor + FVector2D(-C::PointerHalfWidth, -C::PointerHeight) * S, Anchor + FVector2D(C::PointerHalfWidth, -C::PointerHeight) * S }, Edge);
	}

	/** The lean against the tower's limit at its height and width: how close it is to toppling (1 = at the limit). */
	double LeanRatio(const FTower& Tower)
	{
		const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
		return FMath::Sqrt(Tower.Balance.LeanX * Tower.Balance.LeanX + Tower.Balance.LeanY * Tower.Balance.LeanY) / Limit;
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
	Safe = KartSafeArea::FKartSafeFrame::ForHud(AllottedGeometry);
	const float S = Safe.Scale;
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
	case EStackerStage::Match:
	{
		// One projection for every point this paint (the lanes, the callouts, the coach).
		const FArcadeHudView View = FArcadeHudView::Capture(G->GetViewer());
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
	using C = L::FClock;
	const FMatch& M = G.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, Safe.SafeMin.Y);
	const int32 Seconds = FMath::CeilToInt32(M.TimeRemainingMs / 1000.0);
	const bool bLate = Seconds <= C::LateSeconds && M.Phase == EMatchPhase::Playing;
	const float Pulse = bLate ? 1.f + C::Pulse * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * C::PulseRate)) : 1.f;
	P.Card(Top - FVector2D(C::Width / 2.f, 0.f) * S, FVector2D(C::Width, C::Height) * S, KC::WithAlpha(KC::Night, 0.8f), C::Radius * S, C::Shadow * S,
		bLate ? KC::Hot : KC::Sun, C::Outline * S);
	const FString& Text = ClockText.Get(Seconds, [](int32 Value) { return FString::Printf(TEXT("%d:%02d"), Value / 60, Value % 60); });
	const FVector2D TextAt = Top + FVector2D(0.f, C::TextY * S);
	P.Transformed(TextAt, Pulse).Text(Text, KartUI::Font(EFont::Display, C::FontSize * S, C::FontOutline * S, KC::Night), TextAt, bLate ? KC::Hot : KC::White,
		FVector2D(0.5, 0.5), C::TextShadow * S);
	PaintWind(P, G, Top, S);
}

void SStackerHud::PaintWind(const FKartPainter& P, const AStackerGame& G, const FVector2D& Top, float S) const
{
	// The wind, the same for every tower: which way it pushes and how hard, at the tallest tower's height.
	using Wd = L::FWind;
	const FMatch& M = G.GetMatch();
	int32 Tallest = 0;
	for (const FTower& T : M.Towers)
	{
		Tallest = FMath::Max(Tallest, T.Height());
	}
	const FVector2D Wind = WindAt(M.ElapsedMs, Tallest);
	const float Strength = FMath::Clamp(static_cast<float>(FMath::Abs(Wind.X) / Wd::FullWind), 0.f, 1.f);
	const FVector2D C = Top + FVector2D(0.f, Wd::CentreY * S);
	const float Dir = Wind.X >= 0.0 ? 1.f : -1.f;
	const FLinearColor Ink = FMath::Lerp(KC::Mint, KC::Hot, Strength);
	P.Card(C - FVector2D(Wd::Width / 2.f, Wd::Height / 2.f) * S, FVector2D(Wd::Width, Wd::Height) * S, KC::WithAlpha(KC::Night, 0.78f), Wd::Radius * S,
		Wd::Shadow * S, KC::WithAlpha(Ink, 0.7f), Wd::Outline * S);
	P.Text(ARCADE_LABEL("Stacker", "Wind", "WIND"), KartUI::Font(EFont::Label, Wd::LabelSize * S), C + FVector2D(Wd::LabelX * S, 0.f), KC::WithAlpha(KC::White, 0.8f), FVector2D(0.0, 0.5));
	// Three chevrons streaming the way it blows, lit by strength, drifting so the eye reads the direction.
	const float Drift = FMath::Frac(static_cast<float>(G.GetClock()) * (Wd::DriftRate + Wd::DriftRatePerStrength * Strength));
	for (int32 k = 0; k < Wd::Chevrons; ++k)
	{
		const float Lit = Strength * Wd::Chevrons - k;
		const float X = (Wd::ChevronX + k * Wd::ChevronStep + Drift * Wd::ChevronDrift) * Dir * S;
		const FVector2D At = C + FVector2D(X + Wd::ChevronOffset * S, 0.f);
		const FLinearColor Col = Lit > 0.f ? KC::WithAlpha(Ink, FMath::Clamp(0.35f + Lit, 0.35f, 1.f)) : KC::WithAlpha(KC::White, 0.18f);
		Chevron(P, At, Dir, Wd::ChevronBack, Wd::ChevronTip, Wd::ChevronHalfHeight, Wd::ChevronThickness, Col, S);
	}
	for (int32 k = 0; k < Wd::Bars; ++k)
	{
		const float H = (Wd::BarBaseHeight + k * Wd::BarHeightStep) * S;
		const bool bOn = Strength * Wd::Bars > k + Wd::BarLitAt;
		P.Box(C + FVector2D((Wd::BarX + k * Wd::BarStep) * S, Wd::BarBottom * S - H), FVector2D(Wd::BarWidth * S, H), bOn ? Ink : KC::WithAlpha(KC::White, 0.16f),
			Wd::BarRadius * S);
	}
}

void SStackerHud::PaintLanes(const FKartPainter& P, const AStackerGame& G, const FArcadeHudView& View, float S) const
{
	using Pn = L::FPanel;
	const FMatch& M = G.GetMatch();
	const int32 Span = G.LaneSpan();
	LaneText.SetNum(M.Towers.Num());
	PanelAt.SetNum(M.Towers.Num());
	// Each panel sits under its lane; as wide as the space to its neighbours allows (never overlapping them).
	TArray<float, TInlineAllocator<AStackerRooftop::MaxLanes>> Centres;
	for (const FTower& Tower : M.Towers)
	{
		FVector2D At01(0.5, 0.9);
		View.Project(AStackerGame::W(FVector(AStackerRooftop::LaneX(Tower.LaneIndex, Span), Pn::AnchorY, Pn::AnchorZ)), At01);
		Centres.Add(static_cast<float>(At01.X * P.Size().X));
	}
	float Width = FMath::Min(Pn::MaxWidth * S, (P.Size().X * Pn::ScreenShare) / FMath::Max(1, M.Towers.Num()) - Pn::Gap * S);
	for (int32 a = 0; a < Centres.Num(); ++a)
	{
		for (int32 b = a + 1; b < Centres.Num(); ++b)
		{
			Width = FMath::Min(Width, FMath::Abs(Centres[a] - Centres[b]) - Pn::NeighbourGap * S);
		}
	}
	Width = FMath::Max(Width, Pn::MinWidth * S);
	const float Height = Pn::Height * S;
	if (Safe.HasTouchZones())
	{
		// A landscape phone: the bottom corners are the touch controls', so the panels line the top, either side of
		// the clock and the wind.
		const float Gap = Pn::Gap * S, CentreGap = Pn::PhoneClockRoom * S;
		const KartSafeArea::FKartSafeFrame::FTopPanels Panels = Safe.TopPanels(M.Towers.Num(), Pn::MaxWidth * S, 0.f, Height, Gap, CentreGap);
		Width = Panels.Width;
		for (int32 t = 0; t < M.Towers.Num(); ++t)
		{
			const FVector2D Pos = Panels.Pos(t);
			// The lean coach hangs above PanelAt: set it low enough that the coach sits under the panel instead.
			using Co = L::FCoach;
			PanelAt[t] = Pos + FVector2D(Width / 2.f, Height + (Co::LeanAbovePanel + Co::PointerGap + Co::LeanHeight + Pn::Gap) * S);
			PaintLanePanel(P, G, t, Pos, Width, S);
		}
		return;
	}
	for (int32 t = 0; t < M.Towers.Num(); ++t)
	{
		const float Margin = Pn::ScreenMargin * S + Width / 2.f;
		const float Centre = FMath::Clamp(Centres[t], Margin, static_cast<float>(P.Size().X) - Margin);
		const FVector2D Pos(Centre - Width / 2.f, Safe.SafeMax.Y - Height);
		PanelAt[t] = Pos + FVector2D(Width / 2.f, 0.f);
		PaintLanePanel(P, G, t, Pos, Width, S);
	}
}

void SStackerHud::PaintLanePanel(const FKartPainter& P, const AStackerGame& G, int32 TowerIndex, const FVector2D& Pos, float Width, float S) const
{
	using Pn = L::FPanel;
	const FTower& Tower = G.GetMatch().Towers[TowerIndex];
	FLaneText& Text = LaneText[TowerIndex];
	const FStackerSeat* Seat = G.SeatOf(Tower.PlayerId);
	const bool bHuman = Seat && Seat->Kind != EBuilderKind::Bot;
	const FLinearColor Color = AStackerRooftop::LaneColor(Tower.LaneIndex);
	const float Danger = static_cast<float>(Tower.Balance.DangerMs / BalanceFailureMs);
	const bool bWarn = LeanRatio(Tower) > 1.0 || Danger > 0.f;
	const float Flash = bWarn ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(G.GetClock()) * (Pn::WarnFlashRate + Pn::WarnFlashRate * Danger)) : 0.f;
	P.Card(Pos, FVector2D(Width, Pn::Height * S), KC::WithAlpha(KC::Night, Tower.bEliminated ? Pn::EliminatedAlpha : Pn::Alpha), Pn::Radius * S, Pn::Shadow * S,
		bWarn ? FMath::Lerp(Color, KC::Hot, Flash) : Color, (bWarn ? Pn::WarnOutline : Pn::Outline) * S);
	// A strip of the lane's colour along the top, so the panel ties to its tower at a glance.
	P.Box(Pos + FVector2D(Pn::StripInset * S, 0.f), FVector2D(Width - 2.f * Pn::StripInset * S, Pn::StripHeight * S), Color, Pn::StripRadius * S);
	P.Circle(Pos + FVector2D(Pn::DotX * S, Pn::DotY * S), Pn::DotRadius * S, Color, KC::Night, Pn::DotOutline * S);
	P.Text(Tower.Name, KartUI::Font(EFont::Title, Pn::NameSize * S), Pos + FVector2D(Pn::NameX * S, Pn::NameY * S), KC::White, FVector2D(0.0, 0.5));
	const FString& ScoreText = Text.Score.Get(Tower.Score, [](int32 Value) { return FString::FromInt(Value); });
	P.Text(ScoreText, KartUI::Font(EFont::Display, Pn::ScoreSize * S), Pos + FVector2D(Width - Pn::ScoreRight * S, Pn::ScoreY * S), KC::White, FVector2D(1.0, 0.5),
		Pn::ScoreShadow * S);
	for (int32 i = 0; i < StartingLives; ++i)
	{
		LifeBlock(P, Pos + FVector2D((Pn::LifeX + i * Pn::LifeStep) * S, Pn::StatY * S), S, i < Tower.Lives ? Color : KC::WithAlpha(KC::White, 0.16f));
	}
	const FString& RowText = Text.Row.Get(Tower.Height(), [](int32 Value) { return FString::Printf(TEXT("ROW %d"), Value); });
	P.Text(RowText, KartUI::Font(EFont::Label, Pn::RowSize * S), Pos + FVector2D(Pn::RowX * S, Pn::StatY * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.0, 0.5));
	if (Tower.Combo >= 2)
	{
		// A combo pill on the right, clear of the row count.
		const FString& ComboText = Text.Combo.Get(Tower.Combo, [](int32 Value) { return FString::Printf(TEXT("x%d PERFECT"), Value); });
		const FSlateFontInfo ComboFont = KartUI::Font(EFont::Label, Pn::ComboSize * S);
		const FVector2D Size = FKartPainter::Measure(ComboText, ComboFont);
		const FLinearColor Ink = Tower.Combo >= Pn::ComboGold ? KC::Sun : KC::Mint;
		const FVector2D PillPos(Pos.X + Width - Pn::ScoreRight * S - Size.X - Pn::ComboPadding * S, Pos.Y + Pn::StatY * S - Pn::ComboHeight / 2.f * S);
		P.Box(PillPos, FVector2D(Size.X + Pn::ComboPadding * S, Pn::ComboHeight * S), KC::WithAlpha(Ink, 0.18f), Pn::ComboHeight / 2.f * S, Ink, Pn::ComboOutline * S);
		P.Text(ComboText, ComboFont, PillPos + FVector2D(Pn::ComboPadding / 2.f * S, Pn::ComboHeight / 2.f * S), Ink, FVector2D(0.0, 0.5));
	}
	PaintGauge(P, G, Tower, bHuman, Pos, Width, S);
	PaintBanner(P, G, Tower, Seat && Seat->Kind == EBuilderKind::Phone, Pos, Width, S);
}

void SStackerHud::PaintGauge(const FKartPainter& P, const AStackerGame& G, const FTower& Tower, bool bHuman, const FVector2D& Pos, float Width, float S) const
{
	// The balance gauge: the lean across (the dot) against the steady band and the limit, red beyond it; the danger
	// toward a topple filling under it.
	using Gg = L::FGauge;
	const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
	const double Ratio = LeanRatio(Tower);
	const float Danger = static_cast<float>(Tower.Balance.DangerMs / BalanceFailureMs);
	const float GaugeH = Gg::Height * S;
	const FVector2D GaugePos = Pos + FVector2D(Gg::Inset * S, Gg::Top * S);
	const float GaugeW = Width - 2.f * Gg::Inset * S;
	const float Mid = GaugePos.X + GaugeW / 2.f;
	const float LimitX = GaugeW / 2.f / Gg::Range;
	P.Box(GaugePos, FVector2D(GaugeW, GaugeH), KC::WithAlpha(KC::White, 0.1f), GaugeH / 2.f);
	P.Box(GaugePos, FVector2D(GaugeW / 2.f - LimitX, GaugeH), KC::WithAlpha(KC::Hot, 0.28f), GaugeH / 2.f);
	P.Box(FVector2D(Mid + LimitX, GaugePos.Y), FVector2D(GaugeW / 2.f - LimitX, GaugeH), KC::WithAlpha(KC::Hot, 0.28f), GaugeH / 2.f);
	const float Steady = LimitX * static_cast<float>(SteadyBalanceRatio);
	P.Box(FVector2D(Mid - Steady, GaugePos.Y), FVector2D(Steady * 2.f, GaugeH), KC::WithAlpha(KC::Mint, 0.38f), GaugeH / 2.f);
	P.Box(FVector2D(Mid - Gg::MidMarkWidth / 2.f * S, GaugePos.Y + Gg::MidMarkInset * S), FVector2D(Gg::MidMarkWidth * S, GaugeH - 2.f * Gg::MidMarkInset * S),
		KC::WithAlpha(KC::White, 0.5f));
	for (const float Side : { -1.f, 1.f })
	{
		const float X = Mid + Side * LimitX;
		P.Box(FVector2D(X - Gg::LimitMarkWidth / 2.f * S, GaugePos.Y - Gg::LimitMarkOverhang * S), FVector2D(Gg::LimitMarkWidth * S, GaugeH + 2.f * Gg::LimitMarkOverhang * S),
			KC::WithAlpha(KC::Hot, 0.9f));
	}
	// The side marks are letters, not words: not localized.
	static const FString GaugeLeft(TEXT("L")), GaugeRight(TEXT("R"));
	const FSlateFontInfo SideFont = KartUI::Font(EFont::Label, Gg::SideLabelSize * S);
	P.Text(GaugeLeft, SideFont, FVector2D(GaugePos.X - Gg::SideLabelGap * S, GaugePos.Y + GaugeH / 2.f), KC::WithAlpha(KC::White, 0.6f), FVector2D(0.5, 0.5));
	P.Text(GaugeRight, SideFont, FVector2D(GaugePos.X + GaugeW + Gg::SideLabelGap * S, GaugePos.Y + GaugeH / 2.f), KC::WithAlpha(KC::White, 0.6f), FVector2D(0.5, 0.5));
	const float Lean = FMath::Clamp(static_cast<float>(Tower.Balance.LeanX / Limit), -Gg::Range, Gg::Range);
	const FLinearColor DotColor = Ratio > 1.0 ? KC::Hot : (Ratio <= SteadyBalanceRatio ? KC::Mint : KC::Sun);
	const FVector2D Dot(Mid + Lean / Gg::Range * GaugeW / 2.f, GaugePos.Y + GaugeH / 2.f);
	P.Circle(Dot, Gg::DotHalo * S, KC::WithAlpha(DotColor, 0.25f));
	P.Circle(Dot, Gg::DotRadius * S, DotColor, KC::Night, Gg::DotOutline * S);
	if (bHuman && Ratio > SteadyBalanceRatio && !Tower.IsDone())
	{
		// Chevrons back toward the middle: which way to tip the palm.
		const float Back = -FMath::Sign(Lean);
		for (int32 k = 0; k < Gg::Chevrons; ++k)
		{
			const FVector2D C = Dot + FVector2D(Back * (Gg::ChevronX + k * Gg::ChevronStep) * S, 0.f);
			Chevron(P, C, Back, Gg::ChevronBack, Gg::ChevronTip, Gg::ChevronHalfHeight, Gg::ChevronThickness, KC::WithAlpha(KC::White, 0.9f - k * 0.35f), S);
		}
	}
	if (Danger > 0.f)
	{
		const FVector2D Bar = GaugePos + FVector2D(0.f, GaugeH + Gg::DangerGap * S);
		P.Box(Bar, FVector2D(GaugeW, Gg::DangerHeight * S), KC::WithAlpha(KC::Hot, 0.2f), Gg::DangerRadius * S);
		P.Box(Bar, FVector2D(GaugeW * FMath::Min(1.f, Danger), Gg::DangerHeight * S), FMath::Lerp(KC::Sun, KC::Hot, FMath::Min(1.f, Danger)), Gg::DangerRadius * S);
	}
}

void SStackerHud::PaintBanner(const FKartPainter& P, const AStackerGame& G, const FTower& Tower, bool bPhone, const FVector2D& Pos, float Width, float S) const
{
	// Over the panel: how the tower ended, a phone gone quiet, a topple coming or the recovery after one.
	using Pn = L::FPanel;
	const FMatch& M = G.GetMatch();
	const float Danger = static_cast<float>(Tower.Balance.DangerMs / BalanceFailureMs);
	const bool bPhoneQuiet = bPhone && BalanceInputMissing(Tower.Balance, M.ElapsedMs) && M.Phase == EMatchPhase::Playing;
	const bool bRecovering = Tower.Balance.RecoveryMs > 0.0;
	if (!Tower.bEliminated && !Tower.bCompleted && !bPhoneQuiet && !bRecovering && Danger <= Pn::BannerDanger)
	{
		return;
	}
	const bool bTipping = !Tower.bEliminated && !Tower.bCompleted && !bPhoneQuiet && !bRecovering;
	const FText& Banner = Tower.bCompleted ? ARCADE_LABEL("Stacker", "Done", "COMPLETE!") : Tower.bEliminated ? ARCADE_LABEL("Stacker", "Out", "OUT") : bPhoneQuiet ? ARCADE_LABEL("Stacker", "Quiet", "HOLD YOUR PHONE FLAT") : bTipping ? ARCADE_LABEL("Stacker", "Tipping", "IT'S TIPPING!") : ARCADE_LABEL("Stacker", "Steady", "STEADY...");
	const FLinearColor Ink = Tower.bCompleted ? KC::Sun : (Tower.bEliminated || bTipping ? KC::Hot : KC::White);
	const float Wobble = bTipping ? Danger * Pn::BannerWobbleDeg * FMath::Sin(static_cast<float>(G.GetClock()) * Pn::BannerWobbleRate) : 0.f;
	const FVector2D BannerAt = Pos + FVector2D(Width / 2.f, Pn::BannerY * S);
	P.Transformed(BannerAt, 1.f, Wobble).Text(Banner, KartUI::Font(EFont::Display, (bPhoneQuiet ? Pn::QuietBannerSize : Pn::BannerSize) * S, Pn::BannerOutline * S, KC::Night),
		BannerAt, Ink, FVector2D(0.5, 0.5), Pn::BannerShadow * S);
}

void SStackerHud::PaintCallouts(const FKartPainter& P, const AStackerGame& G, const FArcadeHudView& View, float S) const
{
	using Co = L::FCallout;
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
		const FVector Web(AStackerRooftop::LaneX(E.LaneIndex, Span) + Tower.Top().Center, G.TopY(t) + Co::AboveTop, 0.0);
		FVector2D At01;
		if (!View.Project(AStackerGame::W(Web), At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * Co::RiseRate) * S);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / Co::InSeconds), 0.f, 1.f), Co::InOvershoot);
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / Co::OutSeconds), 0.f, 1.f);
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
		P.Transformed(At, FMath::Max(0.01f, In) * (bBig ? Co::BigScale : Co::SmallScale), bBig ? Co::BigTiltDeg : 0.f).Faded(Out)
			.Text(E.Label, KartUI::Font(EFont::Display, Co::FontSize * S, Co::FontOutline * S, KC::Night), At, Color, FVector2D(0.5, 0.5), Co::Shadow * S);
	}
}

void SStackerHud::PaintCoach(const FKartPainter& P, const AStackerGame& G, const FArcadeHudView& View, float S) const
{
	const FStackerCoach& Coach = G.GetCoach();
	if (!G.GetMatch().Towers.IsValidIndex(Coach.Tower))
	{
		return;
	}
	if (Coach.Drop > L::FCoach::MinAlpha)
	{
		PaintDropCoach(P, G, View, S);
	}
	if (Coach.Lean > L::FCoach::MinAlpha && PanelAt.IsValidIndex(Coach.Tower))
	{
		PaintLeanCoach(P, G, S);
	}
}

void SStackerHud::PaintDropCoach(const FKartPainter& P, const AStackerGame& G, const FArcadeHudView& View, float S) const
{
	// Over the sweeping block: wait for the guide to line up, then drop.
	using C = L::FCoach;
	const FStackerCoach& Coach = G.GetCoach();
	const FTower& Tower = G.GetMatch().Towers[Coach.Tower];
	const FVector Web(AStackerRooftop::LaneX(Tower.LaneIndex, G.LaneSpan()) + Tower.CursorCenter, G.TopY(Coach.Tower) + C::DropAboveTop, 0.0);
	FVector2D At01;
	if (!View.Project(AStackerGame::W(Web), At01))
	{
		return;
	}
	const float Clock = static_cast<float>(G.GetClock());
	const FVector2D Anchor = At01 * P.Size() - FVector2D(0.f, C::DropLift * S);
	const float Pop = Coach.bAligned ? C::AlignedPop + C::AlignedPulse * FMath::Sin(Clock * C::AlignedPulseRate) : 1.f;
	const FKartPainter F = P.Faded(Coach.Drop).Transformed(Anchor, Pop);
	const FVector2D Size(Coach.bAligned ? C::AlignedWidth * S : C::DropWidth * S, C::DropHeight * S);
	Bubble(F, Anchor, Size, Coach.bAligned ? KC::Mint : KC::WithAlpha(KC::Night, 0.9f), Coach.bAligned ? KC::White : KC::Sun, S);
	const FVector2D Mid = Anchor - FVector2D(0.f, C::PointerGap * S + Size.Y / 2.f);
	F.Text(Coach.bAligned ? ARCADE_LABEL("Stacker", "DropNow", "DROP NOW!") : ARCADE_LABEL("Stacker", "DropAligned", "DROP WHEN ALIGNED"), KartUI::Font(EFont::Display, C::DropTitleSize * S, Coach.bAligned ? 0.f : C::DropTitleOutline * S, KC::Night),
		Mid + FVector2D(0.f, C::DropTitleY * S), Coach.bAligned ? KC::Night : KC::Sun, FVector2D(0.5, 0.5));
	F.Text(ARCADE_LABEL("Stacker", "DropKeys", "Space  ·  A  ·  Click"), KartUI::Font(EFont::Label, C::KeysSize * S), Mid + FVector2D(0.f, C::DropKeysY * S), Coach.bAligned ? KC::Night : KC::WithAlpha(KC::White, 0.8f),
		FVector2D(0.5, 0.5));
}

void SStackerHud::PaintLeanCoach(const FKartPainter& P, const AStackerGame& G, float S) const
{
	// Over the builder's panel: an arrow the way to tip the palm.
	using C = L::FCoach;
	const FStackerCoach& Coach = G.GetCoach();
	const float Clock = static_cast<float>(G.GetClock());
	const FVector2D Anchor = PanelAt[Coach.Tower] - FVector2D(0.f, C::LeanAbovePanel * S);
	const FKartPainter F = P.Faded(Coach.Lean);
	const FVector2D Size(C::LeanWidth * S, C::LeanHeight * S);
	Bubble(F, Anchor, Size, KC::WithAlpha(KC::Night, 0.92f), KC::Sky, S);
	const FVector2D Mid = Anchor - FVector2D(0.f, C::PointerGap * S + Size.Y / 2.f);
	F.Text(ARCADE_LABEL("Stacker", "LeanWind", "LEAN AGAINST THE WIND"), KartUI::Font(EFont::Display, C::LeanTitleSize * S, C::LeanTitleOutline * S, KC::Night), Mid + FVector2D(0.f, C::LeanTitleY * S), KC::Sky,
		FVector2D(0.5, 0.5));
	const float Side = Coach.LeanSide;
	const float Nudge = C::ArrowNudge * S * FMath::Sin(Clock * C::ArrowNudgeRate);
	Arrow(F, Mid + FVector2D(Side * (C::ArrowX * S + Nudge), C::ArrowY * S), Side, C::ArrowLength * S, C::ArrowHead * S, C::ArrowThickness * S, KC::Sun);
	F.Text(Side > 0.f ? ARCADE_LABEL("Stacker", "LeanKeysR", "D  ·  stick  ·  mouse right") : ARCADE_LABEL("Stacker", "LeanKeysL", "A  ·  stick  ·  mouse left"), KartUI::Font(EFont::Label, C::KeysSize * S), Mid + FVector2D(-Side * C::LeanKeysAway * S, C::LeanKeysY * S),
		KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5));
}

void SStackerHud::PaintCountdown(const FKartPainter& P, const AStackerGame& G, float S) const
{
	using Cd = L::FCountdown;
	const FMatch& M = G.GetMatch();
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Cd::CentreY);
	if (M.Phase == EMatchPhase::Countdown)
	{
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 N = FMath::CeilToInt32(Remaining);
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(1.0 - (N - Remaining)) / Cd::PopSeconds, 0.f, 1.f));
		P.Transformed(Centre, FMath::Max(0.01f, Pop)).Text(FString::FromInt(FMath::Max(1, N)), KartUI::Font(EFont::Display, Cd::DigitSize * S, Cd::DigitOutline * S, KC::Night),
			Centre, KC::Sun, FVector2D(0.5, 0.5), Cd::DigitShadow * S);
		return;
	}
	if (M.Phase == EMatchPhase::Playing && M.ElapsedMs < Cd::GoMs)
	{
		const float A = FMath::Clamp(static_cast<float>((Cd::GoMs - M.ElapsedMs) / Cd::GoFadeMs), 0.f, 1.f);
		P.Faded(A).Text(ARCADE_LABEL("Stacker", "Go", "STACK!"), KartUI::Font(EFont::Display, Cd::GoSize * S, Cd::GoOutline * S, KC::Night), Centre, KC::Sun, FVector2D(0.5, 0.5), Cd::GoShadow * S);
	}
}

void SStackerHud::PaintControls(const FKartPainter& P, const AStackerGame& G, float S) const
{
	// The full controls while the countdown runs; the coach marks take over once blocks are moving.
	using Ct = L::FControls;
	const FMatch& M = G.GetMatch();
	if (!G.HasLocal() || M.Phase != EMatchPhase::Countdown)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>(M.CountdownRemainingMs / Ct::FadeMs), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Ct::CentreY);
	const FKartPainter F = P.Faded(A);
	F.Card(Centre - FVector2D(Ct::Width / 2.f, Ct::Height / 2.f) * S, FVector2D(Ct::Width, Ct::Height) * S, KC::WithAlpha(KC::Night, 0.78f), Ct::Radius * S, Ct::Shadow * S,
		KC::Sun, Ct::Outline * S);
	F.Text(ARCADE_LABEL("Stacker", "Controls", "Stick, A/D or mouse: lean against the wind  ·  A, Space or click: drop"), KartUI::Font(EFont::Body, Ct::FontSize * S), Centre, KC::White, FVector2D(0.5, 0.5));
}

void SStackerHud::PaintIntro(const FKartPainter& P, const AStackerGame& G, float S) const
{
	using In = L::FIntro;
	const double Age = G.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / In::FadeInSeconds), 0.f, 1.f)
		* FMath::Clamp(static_cast<float>((AStackerGame::IntroSeconds - Age) / In::FadeOutSeconds), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * In::CentreY);
	const float Rise = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / In::RiseSeconds), 0.f, 1.f));
	const FVector2D TitleAt = Centre + FVector2D(0.f, In::TitleY * S);
	F.Transformed(TitleAt, FMath::Max(0.01f, Rise)).Text(ARCADE_LABEL("Stacker", "Title", "SKY STACKER"), KartUI::Font(EFont::Display, In::TitleSize * S, In::TitleOutline * S), TitleAt, KC::Sun,
		FVector2D(0.5, 0.5), In::TitleShadow * S);
	F.Text(Names, KartUI::Font(EFont::Title, In::NamesSize * S, In::NamesOutline * S), Centre + FVector2D(0.f, In::NamesY * S), KC::White, FVector2D(0.5, 0.5),
		In::NamesShadow * S);
	F.Text(ARCADE_LABEL("Stacker", "Rules", "Drop each block on the one below. Overhang is cut away. Lean against the wind."), KartUI::Font(EFont::Body, In::RulesSize * S), Centre + FVector2D(0.f, In::RulesY * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5),
		In::RulesShadow * S);
}

void SStackerHud::CacheStandingLines(const AStackerGame& G) const
{
	bStandingsCached = true;
	StandingLines.Reset();
	for (const int32 t : G.Standings())
	{
		const FTower& Tower = G.GetMatch().Towers[t];
		StandingLines.Add(Tower.bCompleted ? FString::Printf(TEXT("tower complete  ·  %d perfect"), Tower.Perfects)
			: FString::Printf(TEXT("%d rows  ·  %d perfect  ·  best x%d"), Tower.Height(), Tower.Perfects, Tower.BestCombo));
	}
}

void SStackerHud::PaintStandings(const FKartPainter& P, const AStackerGame& G, float S) const
{
	using St = L::FStandings;
	if (!bStandingsCached)
	{
		CacheStandingLines(G);
	}
	const TArray<int32>& Order = G.Standings();
	const FMatch& M = G.GetMatch();
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / St::FadeSeconds), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * St::TitleY);
	F.Text(ARCADE_LABEL("Stacker", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, St::TitleSize * S, St::TitleOutline * S), Centre, KC::Sun, FVector2D(0.5, 0.5), St::TitleShadow * S);
	for (int32 i = 0; i < Order.Num() && i < StandingLines.Num(); ++i)
	{
		const FTower& Tower = M.Towers[Order[i]];
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - St::RowDelay - i * St::RowStagger) / St::RowPopSeconds), 0.f, 1.f));
		const FVector2D Pos(P.Size().X - St::FromRight * S, P.Size().Y * St::Top + i * St::RowStep * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(St::Width / 2.f, St::Height / 2.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = M.WinnerIds.Contains(Tower.PlayerId);
		Row.Card(Pos, FVector2D(St::Width, St::Height) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), St::Radius * S, St::Shadow * S,
			AStackerRooftop::LaneColor(Tower.LaneIndex), St::Outline * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, St::PlaceSize * S), Pos + FVector2D(St::PlaceX * S, St::Height / 2.f * S), KC::ForPlace(i + 1),
			FVector2D(0.5, 0.5), St::PlaceShadow * S);
		Row.Text(Tower.Name, KartUI::Font(EFont::Title, St::NameSize * S), Pos + FVector2D(St::NameX * S, St::NameY * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(StandingLines[i], KartUI::Font(EFont::Body, St::DetailSize * S), Pos + FVector2D(St::NameX * S, St::DetailY * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Tower.Score), KartUI::Font(EFont::Display, St::ScoreSize * S), Pos + FVector2D(St::ScoreX * S, St::Height / 2.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
