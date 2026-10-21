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
	/** A life: a small block. */
	void LifeBlock(const FKartPainter& P, const FVector2D& C, float S, const FLinearColor& Fill)
	{
		P.Box(C - FVector2D(14.f, 7.f) * S, FVector2D(28.f, 14.f) * S, Fill, 4.f * S);
	}
}

void SStackerHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
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
	switch (G->GetStage())
	{
	case EStackerStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case EStackerStage::Finished:
		PaintStandings(P, *G, S);
		break;
	default:
		PaintClock(P, *G, S);
		PaintLanes(P, *G, S);
		PaintCallouts(P, *G, S);
		PaintControls(P, *G, S);
		PaintCountdown(P, *G, S);
		break;
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
	P.Transformed(Top + FVector2D(0.f, 46.f * S), Pulse).Text(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), KartUI::Font(EFont::Display, 56.f * S, 3.f * S, KC::Night),
		Top + FVector2D(0.f, 46.f * S), bLate ? KC::Hot : KC::White, FVector2D(0.5, 0.5), 4.f * S);
	// The wind, the same for every tower: an arrow and its strength at the tallest tower's height.
	int32 Tallest = 0;
	for (const FTower& T : M.Towers)
	{
		Tallest = FMath::Max(Tallest, T.Height());
	}
	const FVector2D Wind = WindAt(M.ElapsedMs, Tallest);
	const float Strength = FMath::Clamp(static_cast<float>(FMath::Abs(Wind.X) / 7.0), 0.f, 1.f);
	const FVector2D C = Top + FVector2D(0.f, 136.f * S);
	const float Dir = Wind.X >= 0.0 ? 1.f : -1.f;
	const float Len = (14.f + 34.f * Strength) * S;
	const FLinearColor Ink = FMath::Lerp(KC::Mint, KC::Hot, Strength);
	P.Box(C - FVector2D(96.f, 17.f) * S, FVector2D(192.f, 34.f) * S, KC::WithAlpha(KC::Night, 0.72f), 17.f * S);
	P.Lines({ C - FVector2D(Len * Dir, 0.f), C + FVector2D(Len * Dir, 0.f) }, Ink, 4.f * S);
	P.Polygon({ C + FVector2D((Len + 10.f * S) * Dir, 0.f), C + FVector2D(Len * Dir, -8.f * S), C + FVector2D(Len * Dir, 8.f * S) }, Ink);
	P.Text(NSLOCTEXT("Stacker", "Wind", "WIND"), KartUI::Font(EFont::Label, 14.f * S), C + FVector2D(-(Len + 22.f * S) * Dir, 0.f), KC::WithAlpha(KC::White, 0.7f),
		FVector2D(Dir > 0.f ? 1.0 : 0.0, 0.5));
}

void SStackerHud::PaintLanes(const FKartPainter& P, const AStackerGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 Span = G.LaneSpan();
	const float Width = FMath::Min(460.f * S, (P.Size().X * 0.92f) / FMath::Max(1, M.Towers.Num()) - 24.f * S);
	const float Height = 150.f * S;
	for (const FTower& Tower : M.Towers)
	{
		const FStackerSeat* Seat = G.SeatOf(Tower.PlayerId);
		const FLinearColor Color = AStackerRooftop::LaneColor(Tower.LaneIndex);
		FVector2D At01(0.5, 0.9);
		if (G.ScreenOf(AStackerGame::W(FVector(AStackerRooftop::LaneX(Tower.LaneIndex, Span), -0.7, 2.5)), At01))
		{
			At01.X = FMath::Clamp(At01.X, 0.13, 0.87);
		}
		const FVector2D Pos(At01.X * P.Size().X - Width / 2.f, P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height);
		const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
		const double Ratio = FMath::Sqrt(Tower.Balance.LeanX * Tower.Balance.LeanX + Tower.Balance.LeanY * Tower.Balance.LeanY) / Limit;
		const float Danger = static_cast<float>(Tower.Balance.DangerMs / BalanceFailureMs);
		const bool bWarn = Ratio > 1.0 || Danger > 0.f;
		const float Flash = bWarn ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(G.GetClock()) * 14.f) : 0.f;
		P.Card(Pos, FVector2D(Width, Height), KC::WithAlpha(KC::Night, Tower.bEliminated ? 0.5f : 0.84f), 22.f * S, 6.f * S,
			bWarn ? FMath::Lerp(Color, KC::Hot, Flash) : Color, (bWarn ? 4.f : 2.f) * S);
		P.Circle(Pos + FVector2D(30.f * S, 32.f * S), 11.f * S, Color, KC::Night, 2.f * S);
		P.Text(Tower.Name, KartUI::Font(EFont::Title, 26.f * S), Pos + FVector2D(52.f * S, 32.f * S), KC::White, FVector2D(0.0, 0.5));
		P.Text(FString::FromInt(Tower.Score), KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(Width - 22.f * S, 38.f * S), KC::White, FVector2D(1.0, 0.5), 3.f * S);
		for (int32 i = 0; i < StartingLives; ++i)
		{
			LifeBlock(P, Pos + FVector2D((38.f + i * 36.f) * S, 76.f * S), S, i < Tower.Lives ? Color : KC::WithAlpha(KC::White, 0.16f));
		}
		P.Text(FString::Printf(TEXT("ROW %d"), Tower.Height()), KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(150.f * S, 76.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.0, 0.5));
		if (Tower.Combo >= 2)
		{
			P.Text(FString::Printf(TEXT("PERFECT x%d"), Tower.Combo), KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(Width - 22.f * S, 76.f * S), Tower.Combo >= 5 ? KC::Sun : KC::Mint,
				FVector2D(1.0, 0.5));
		}
		// The balance gauge: the lean across (the dot) against the steady band and the limit, the danger under it.
		const FVector2D GaugePos = Pos + FVector2D(24.f * S, 102.f * S);
		const float GaugeW = Width - 48.f * S;
		const float Mid = GaugePos.X + GaugeW / 2.f;
		P.Box(GaugePos, FVector2D(GaugeW, 16.f * S), KC::WithAlpha(KC::White, 0.12f), 8.f * S);
		const float Steady = GaugeW / 2.f * static_cast<float>(SteadyBalanceRatio / 1.5);
		P.Box(FVector2D(Mid - Steady, GaugePos.Y), FVector2D(Steady * 2.f, 16.f * S), KC::WithAlpha(KC::Mint, 0.35f), 8.f * S);
		for (const float Side : { -1.f, 1.f })
		{
			const float X = Mid + Side * GaugeW / 2.f / 1.5f;
			P.Box(FVector2D(X - 1.5f * S, GaugePos.Y - 4.f * S), FVector2D(3.f * S, 24.f * S), KC::WithAlpha(KC::Hot, 0.85f));
		}
		const float Lean = FMath::Clamp(static_cast<float>(Tower.Balance.LeanX / Limit), -1.5f, 1.5f);
		const FLinearColor DotColor = Ratio > 1.0 ? KC::Hot : (Ratio <= SteadyBalanceRatio ? KC::Mint : KC::Sun);
		P.Circle(FVector2D(Mid + Lean / 1.5f * GaugeW / 2.f, GaugePos.Y + 8.f * S), 11.f * S, DotColor, KC::Night, 2.f * S);
		if (Danger > 0.f)
		{
			P.Box(GaugePos + FVector2D(0.f, 24.f * S), FVector2D(GaugeW * FMath::Min(1.f, Danger), 6.f * S), KC::Hot, 3.f * S);
		}
		const bool bPhoneQuiet = Seat && Seat->Kind == EBuilderKind::Phone && BalanceInputMissing(Tower.Balance, M.ElapsedMs) && M.Phase == EMatchPhase::Playing;
		if (Tower.bEliminated || Tower.bCompleted || bPhoneQuiet || Tower.Balance.RecoveryMs > 0.0)
		{
			const FText Banner = Tower.bCompleted ? NSLOCTEXT("Stacker", "Done", "COMPLETE!") : Tower.bEliminated ? NSLOCTEXT("Stacker", "Out", "OUT")
				: bPhoneQuiet ? NSLOCTEXT("Stacker", "Quiet", "HOLD YOUR PHONE FLAT") : NSLOCTEXT("Stacker", "Steady", "STEADY...");
			const FLinearColor Ink = Tower.bCompleted ? KC::Sun : (Tower.bEliminated ? KC::Hot : KC::White);
			P.Text(Banner, KartUI::Font(EFont::Display, (bPhoneQuiet ? 26.f : 40.f) * S, 3.f * S, KC::Night), Pos + FVector2D(Width / 2.f, -26.f * S), Ink, FVector2D(0.5, 0.5), 4.f * S);
		}
	}
}

void SStackerHud::PaintCallouts(const FKartPainter& P, const AStackerGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 Span = G.LaneSpan();
	for (const FEffect& E : M.Effects)
	{
		const FTower* Tower = M.FindTower(E.PlayerId);
		if (!Tower || E.Label.IsEmpty())
		{
			continue;
		}
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		const FVector Web(AStackerRooftop::LaneX(E.LaneIndex, Span) + Tower->Top().Center, G.TopY(*Tower) + 0.9, 0.0);
		FVector2D At01;
		if (!G.ScreenOf(AStackerGame::W(Web), At01))
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
	if (!G.HasLocal() || G.GetMatch().ElapsedMs > 10000.0)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((10000.0 - G.GetMatch().ElapsedMs) / 600.0), 0.f, 1.f);
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
	F.Text(NSLOCTEXT("Stacker", "Title", "SKY STACKER"), KartUI::Font(EFont::Display, 140.f * S, 7.f * S), Centre - FVector2D(0.f, 60.f * S), KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	FString Names;
	for (const FStackerSeat& Seat : G.GetSeats())
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
	}
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(NSLOCTEXT("Stacker", "Rules", "Drop each block on the one below. Overhang is cut away. Lean against the wind."), KartUI::Font(EFont::Body, 24.f * S),
		Centre + FVector2D(0.f, 100.f * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5), 3.f * S);
}

void SStackerHud::PaintStandings(const FKartPainter& P, const AStackerGame& G, float S) const
{
	const TArray<const FTower*> Order = G.Standings();
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.12f);
	F.Text(NSLOCTEXT("Stacker", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 84.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	// The rows sit low and narrow: the showcase of the towers is the picture.
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FTower& Tower = *Order[i];
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.6 - i * 0.18) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X - 760.f * S, P.Size().Y * 0.22f + i * 96.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(340.f, 40.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = G.GetMatch().WinnerIds.Contains(Tower.PlayerId);
		Row.Card(Pos, FVector2D(680.f, 80.f) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), 22.f * S, 6.f * S, AStackerRooftop::LaneColor(Tower.LaneIndex), 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(44.f * S, 40.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Tower.Name, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(92.f * S, 27.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(Tower.bCompleted ? FString::Printf(TEXT("tower complete  ·  %d perfect"), Tower.Perfects)
			: FString::Printf(TEXT("%d rows  ·  %d perfect  ·  best x%d"), Tower.Height(), Tower.Perfects, Tower.BestCombo),
			KartUI::Font(EFont::Body, 17.f * S), Pos + FVector2D(92.f * S, 58.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Tower.Score), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(655.f * S, 40.f * S), Ink, FVector2D(1.0, 0.5));
	}
	if (Age > 2.5)
	{
		const float Pulse = 0.75f + 0.25f * FMath::Sin(static_cast<float>(G.GetClock()) * 3.f);
		F.Faded(Pulse).Text(NSLOCTEXT("Stacker", "Again", "ENTER / A  play again     ESC / B  menu"), KartUI::Font(EFont::Label, 24.f * S),
			FVector2D(P.Size().X / 2.f, P.Size().Y * 0.92f), KC::White, FVector2D(0.5, 0.5), 3.f * S);
	}
}
