#include "Games/Samurai/SSamuraiHud.h"

#include "Algo/StableSort.h"

#include "Art/KartMaterials.h"
#include "Games/Samurai/SamuraiGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Samurai;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	void Heart(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill)
	{
		P.Circle(C + FVector2D(-R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		P.Circle(C + FVector2D(R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		P.Polygon({ C + FVector2D(-R * 1.05f, -R * 0.05f), C + FVector2D(R * 1.05f, -R * 0.05f), C + FVector2D(0.f, R * 1.05f) }, Fill);
	}
}

void SSamuraiHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SSamuraiHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const ASamuraiGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	switch (G->GetStage())
	{
	case ESamuraiStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case ESamuraiStage::Finished:
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

void SSamuraiHud::PaintClock(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * KartUI::Metric::SafeFraction);
	const int32 Seconds = FMath::CeilToInt32(M.TimeRemainingMs / 1000.0);
	const bool bLate = Seconds <= 10 && M.Phase == EMatchPhase::Playing;
	const float Pulse = bLate ? 1.f + 0.06f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 6.f)) : 1.f;
	P.Card(Top - FVector2D(110.f, 0.f) * S, FVector2D(220.f, 112.f) * S, KC::WithAlpha(KC::Night, 0.8f), 26.f * S, 6.f * S, bLate ? KC::Hot : KC::Sun, 3.f * S);
	P.Transformed(Top + FVector2D(0.f, 46.f * S), Pulse).Text(FString::FromInt(Seconds), KartUI::Font(EFont::Display, 64.f * S, 3.f * S, KC::Night), Top + FVector2D(0.f, 46.f * S),
		bLate ? KC::Hot : KC::White, FVector2D(0.5, 0.5), 4.f * S);
	P.Text(FString::Printf(TEXT("WAVE %d / %d"), WaveNumber(M.ElapsedMs), WaveCount), KartUI::Font(EFont::Label, 18.f * S), Top + FVector2D(0.f, 94.f * S), KC::Sun, FVector2D(0.5, 0.5));
}

void SSamuraiHud::PaintLanes(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 N = M.Players.Num();
	const float Width = FMath::Min(520.f * S, (P.Size().X * 0.9f) / FMath::Max(1, N) - 24.f * S);
	for (const FPlayer& Player : M.Players)
	{
		const FSamuraiSeat* Seat = G.SeatOf(Player.Id);
		const FLinearColor Color = Seat ? Seat->Color : KC::Sky;
		// Under each lane: where the swordsman stands, projected; the panels keep to the bottom of the frame.
		FVector2D At01(0.5, 0.9);
		if (G.ScreenOf(ASamuraiGame::W(FVector(LaneX(Player.LaneIndex, N), 0.0, 1.15)), At01))
		{
			At01.X = FMath::Clamp(At01.X, 0.12, 0.88);
		}
		const FVector2D Pos(At01.X * P.Size().X - Width / 2.f, P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - 118.f * S);
		const bool bFocus = Player.FocusRemainingMs > 0.0;
		P.Card(Pos, FVector2D(Width, 118.f * S), KC::WithAlpha(KC::Night, Player.bEliminated ? 0.5f : 0.84f), 22.f * S, 6.f * S, bFocus ? KC::Sun : Color, (bFocus ? 4.f : 2.f) * S);
		P.Circle(Pos + FVector2D(30.f * S, 32.f * S), 11.f * S, Color, KC::Night, 2.f * S);
		P.Text(Player.Name, KartUI::Font(EFont::Title, 26.f * S), Pos + FVector2D(52.f * S, 32.f * S), KC::White, FVector2D(0.0, 0.5));
		P.Text(FString::FromInt(Player.Score), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(Width - 22.f * S, 40.f * S), KC::White, FVector2D(1.0, 0.5), 3.f * S);
		// Lives.
		for (int32 i = 0; i < StartingLives; ++i)
		{
			Heart(P, Pos + FVector2D((32.f + i * 34.f) * S, 82.f * S), 11.f * S, i < Player.Lives ? KC::Hot : KC::WithAlpha(KC::White, 0.18f));
		}
		// Combo.
		if (Player.Combo >= 2)
		{
			P.Text(FString::Printf(TEXT("%dx"), Player.Combo), KartUI::Font(EFont::Display, 34.f * S), Pos + FVector2D(150.f * S, 82.f * S), Player.Combo >= 6 ? KC::Sun : KC::Mint,
				FVector2D(0.0, 0.5), 3.f * S);
		}
		// Focus: three pips charge it; while it burns, a gold bar counts its six seconds down.
		const FVector2D FocusPos = Pos + FVector2D(Width - 196.f * S, 88.f * S);
		if (bFocus)
		{
			const float Left = static_cast<float>(Player.FocusRemainingMs / FocusDurationMs);
			P.Box(FocusPos, FVector2D(174.f, 14.f) * S, KC::WithAlpha(KC::White, 0.15f), 7.f * S);
			P.Box(FocusPos, FVector2D(174.f * Left, 14.f) * S, KC::Sun, 7.f * S);
			P.Text(NSLOCTEXT("Samurai", "Focus", "FOCUS x2"), KartUI::Font(EFont::Label, 15.f * S), FocusPos + FVector2D(-12.f * S, 7.f * S), KC::Sun, FVector2D(1.0, 0.5));
		}
		else
		{
			P.Text(NSLOCTEXT("Samurai", "FocusCharge", "FOCUS"), KartUI::Font(EFont::Label, 14.f * S), FocusPos + FVector2D(0.f, 7.f * S), KC::WithAlpha(KC::White, 0.6f), FVector2D(0.0, 0.5));
			for (int32 i = 0; i < FocusCuts; ++i)
			{
				P.Circle(FocusPos + FVector2D((96.f + i * 28.f) * S, 7.f * S), 9.f * S, i < Player.FocusCharge ? KC::Sun : FLinearColor::Transparent, KC::WithAlpha(KC::Sun, 0.7f), 2.f * S);
			}
		}
		if (Player.bEliminated)
		{
			P.Text(NSLOCTEXT("Samurai", "Out", "OUT"), KartUI::Font(EFont::Display, 44.f * S, 3.f * S, KC::Night), Pos + FVector2D(Width / 2.f, 59.f * S), KC::Hot, FVector2D(0.5, 0.5), 4.f * S);
		}
	}
}

void SSamuraiHud::PaintCallouts(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 N = M.Players.Num();
	for (const FEffect& E : M.Effects)
	{
		if (E.Kind == EEffectKind::Slash || E.Label.IsEmpty())
		{
			continue;
		}
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		FVector Web(LaneX(E.LaneIndex, N), 2.7, 0.55);
		if (E.Struck.Num() > 0)
		{
			Web = TargetCentre(E.Struck[0], N) + FVector(0.0, 0.35, 0.0);
		}
		FVector2D At01;
		if (!G.ScreenOf(ASamuraiGame::W(Web), At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * 90.0) * S);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.2), 0.f, 1.f), 2.4f);
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / 0.25), 0.f, 1.f);
		const bool bBig = E.Kind == EEffectKind::Combo || E.Kind == EEffectKind::Bomb;
		const FLinearColor Color = E.Kind == EEffectKind::Bomb ? KC::Hot : (E.Kind == EEffectKind::Miss ? KC::WithAlpha(KC::White, 0.85f) : MovecadeKart::Art::Hex(E.Color));
		P.Transformed(At, FMath::Max(0.01f, In) * (bBig ? 1.25f : 1.f), bBig ? -5.f : 0.f).Faded(Out).Text(E.Label, KartUI::Font(EFont::Display, 50.f * S, 4.f * S, KC::Night), At,
			Color, FVector2D(0.5, 0.5), 6.f * S);
	}
}

void SSamuraiHud::PaintCountdown(const FKartPainter& P, const ASamuraiGame& G, float S) const
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
	if (M.Phase == EMatchPhase::Playing && M.ElapsedMs < 800.0)
	{
		const float A = FMath::Clamp(static_cast<float>((800.0 - M.ElapsedMs) / 300.0), 0.f, 1.f);
		P.Faded(A).Text(NSLOCTEXT("Samurai", "Go", "SLICE!"), KartUI::Font(EFont::Display, 150.f * S, 8.f * S, KC::Night), Centre, KC::Hot, FVector2D(0.5, 0.5), 9.f * S);
	}
}

void SSamuraiHud::PaintControls(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	if (!G.HasLocal() || G.GetMatch().ElapsedMs > 9000.0)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((9000.0 - G.GetMatch().ElapsedMs) / 600.0), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.7f);
	const FKartPainter F = P.Faded(A);
	F.Card(Centre - FVector2D(420.f, 40.f) * S, FVector2D(840.f, 80.f) * S, KC::WithAlpha(KC::Night, 0.78f), 22.f * S, 6.f * S, KC::Sun, 2.f * S);
	F.Text(NSLOCTEXT("Samurai", "Controls", "Mouse or stick: sweep the katana through the fruit  ·  never the bombs"), KartUI::Font(EFont::Body, 22.f * S), Centre,
		KC::White, FVector2D(0.5, 0.5));
}

void SSamuraiHud::PaintIntro(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const double Age = G.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>((3.2 - Age) / 0.4), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.44f);
	F.Text(NSLOCTEXT("Samurai", "Title", "SAMURAI SLICE"), KartUI::Font(EFont::Display, 140.f * S, 7.f * S), Centre - FVector2D(0.f, 60.f * S), KC::Hot, FVector2D(0.5, 0.5), 9.f * S);
	FString Names;
	for (const FSamuraiSeat& Seat : G.GetSeats())
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
	}
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(NSLOCTEXT("Samurai", "Rules", "Cut the fruit. Never the bombs. Three precise cuts: Focus, double points."), KartUI::Font(EFont::Body, 24.f * S),
		Centre + FVector2D(0.f, 100.f * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5), 3.f * S);
}

void SSamuraiHud::PaintStandings(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	TArray<const FPlayer*> Order;
	for (const FPlayer& Player : G.GetMatch().Players)
	{
		Order.Add(&Player);
	}
	Algo::StableSortBy(Order, [](const FPlayer* X) { return -X->Score; });
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	F.Text(NSLOCTEXT("Samurai", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FPlayer& Player = *Order[i];
		const FSamuraiSeat* Seat = G.SeatOf(Player.Id);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.3 - i * 0.18) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X / 2.f - 400.f * S, P.Size().Y * 0.3f + i * 104.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(400.f, 44.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = G.GetMatch().WinnerIds.Contains(Player.Id);
		Row.Card(Pos, FVector2D(800.f, 88.f) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::Sky, 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 52.f * S), Pos + FVector2D(48.f * S, 44.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Player.Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(FString::Printf(TEXT("best combo %d  ·  %d cuts  ·  %d bombs"), Player.BestCombo, Player.Hits, Player.Bombs), KartUI::Font(EFont::Body, 18.f * S),
			Pos + FVector2D(100.f * S, 64.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Player.Score), KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(770.f * S, 44.f * S), Ink, FVector2D(1.0, 0.5));
	}
	if (Age > 2.0)
	{
		const float Pulse = 0.75f + 0.25f * FMath::Sin(static_cast<float>(G.GetClock()) * 3.f);
		F.Faded(Pulse).Text(NSLOCTEXT("Samurai", "Again", "ENTER / A  play again     ESC / B  menu"), KartUI::Font(EFont::Label, 24.f * S),
			FVector2D(P.Size().X / 2.f, P.Size().Y * 0.9f), KC::White, FVector2D(0.5, 0.5), 3.f * S);
	}
}
