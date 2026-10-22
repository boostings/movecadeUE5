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
	const FLinearColor Vermilion = MovecadeKart::Art::Hex(TEXT("#d8343c"));
	const FLinearColor Gilt = MovecadeKart::Art::Hex(TEXT("#ffd477"));
	const FLinearColor Jade = MovecadeKart::Art::Hex(TEXT("#7cf0c4"));

	void Heart(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill, TArray<FVector2D>& Scratch)
	{
		P.Circle(C + FVector2D(-R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		P.Circle(C + FVector2D(R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		Scratch.Reset();
		Scratch.Add(C + FVector2D(-R * 1.05f, -R * 0.05f));
		Scratch.Add(C + FVector2D(R * 1.05f, -R * 0.05f));
		Scratch.Add(C + FVector2D(0.f, R * 1.05f));
		P.Polygon(Scratch, Fill);
	}

	float Clamp01(double X)
	{
		return FMath::Clamp(static_cast<float>(X), 0.f, 1.f);
	}
}

const FString& SSamuraiHud::FCachedNumber::Get(int32 V, const TCHAR* Suffix)
{
	if (V != Value)
	{
		Value = V;
		Text = Suffix ? FString::Printf(TEXT("%d%s"), V, Suffix) : FString::FromInt(V);
	}
	return Text;
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
	View = G->MakeView();
	switch (G->GetStage())
	{
	case ESamuraiStage::Intro:
		PaintNotes(P, *G, S);
		PaintIntro(P, *G, S);
		PaintPractice(P, *G, S);
		break;
	case ESamuraiStage::Finished:
		PaintStandings(P, *G, S);
		break;
	default:
		PaintFocusFrame(P, *G, S);
		PaintClock(P, *G, S);
		PaintLanes(P, *G, S);
		PaintCallouts(P, *G, S);
		PaintNotes(P, *G, S);
		PaintBanner(P, *G, S);
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
	P.Card(Top - FVector2D(110.f, 0.f) * S, FVector2D(220.f, 112.f) * S, KC::WithAlpha(KC::Night, 0.82f), 26.f * S, 6.f * S, bLate ? KC::Hot : KC::Sun, 3.f * S);
	P.Transformed(Top + FVector2D(0.f, 46.f * S), Pulse).Text(SecondsText.Get(Seconds), KartUI::Font(EFont::Display, 64.f * S, 3.f * S, KC::Night), Top + FVector2D(0.f, 46.f * S),
		bLate ? KC::Hot : KC::White, FVector2D(0.5, 0.5), 4.f * S);
	const int32 WaveNow = WaveNumber(M.ElapsedMs);
	if (WaveNow != Wave.Value)
	{
		Wave.Value = WaveNow;
		Wave.Text = FString::Printf(TEXT("WAVE %d / %d"), WaveNow, WaveCount);
	}
	P.Text(Wave.Text, KartUI::Font(EFont::Label, 18.f * S), Top + FVector2D(0.f, 94.f * S), KC::Sun, FVector2D(0.5, 0.5));
}

void SSamuraiHud::PaintLanes(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 N = M.Players.Num();
	const float Width = FMath::Min(520.f * S, (P.Size().X * 0.9f) / FMath::Max(1, N) - 24.f * S);
	const float Height = 118.f * S;
	LaneTexts.SetNum(N);
	for (const FPlayer& Player : M.Players)
	{
		const int32 Lane = FMath::Clamp(Player.LaneIndex, 0, N - 1);
		const FSamuraiSeat* Seat = G.GetSeats().IsValidIndex(Lane) ? &G.GetSeats()[Lane] : nullptr;
		FLaneTexts& Texts = LaneTexts[Lane];
		const FLinearColor Color = Seat ? Seat->Color : KC::Sky;
		// Under each lane: where the swordsman stands, projected; the panels keep to the bottom of the frame.
		FVector2D At01(0.5, 0.9);
		if (View.Project(ASamuraiGame::W(FVector(LaneX(Player.LaneIndex, N), 0.0, 1.15)), At01))
		{
			At01.X = FMath::Clamp(At01.X, 0.12, 0.88);
		}
		const FVector2D Pos(At01.X * P.Size().X - Width / 2.f, P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height);
		const bool bFocus = Player.FocusRemainingMs > 0.0;
		const float Glow = bFocus ? 0.6f + 0.4f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 5.f)) : 0.f;
		if (bFocus)
		{
			// Focus: the panel burns gold.
			P.Box(Pos - FVector2D(6.f, 6.f) * S, FVector2D(Width, Height) + FVector2D(12.f, 12.f) * S, KC::WithAlpha(Gilt, 0.35f * Glow), 28.f * S);
		}
		P.Card(Pos, FVector2D(Width, Height), KC::WithAlpha(KC::Night, Player.bEliminated ? 0.5f : 0.86f), 22.f * S, 6.f * S, bFocus ? Gilt : Color, (bFocus ? 4.f : 2.5f) * S);
		// The seat's colour as a band down the left edge.
		P.Box(Pos + FVector2D(0.f, 18.f * S), FVector2D(6.f * S, Height - 36.f * S), Color, 3.f * S);
		P.Text(Player.Name, KartUI::Font(EFont::Title, 26.f * S, 1.5f * S, KC::Night), Pos + FVector2D(26.f * S, 32.f * S), KC::White, FVector2D(0.0, 0.5), 2.f * S);
		P.Text(Texts.Score.Get(Player.Score), KartUI::Font(EFont::Display, 50.f * S, 2.5f * S, KC::Night), Pos + FVector2D(Width - 22.f * S, 40.f * S), bFocus ? Gilt : KC::White,
			FVector2D(1.0, 0.5), 3.f * S);
		// Lives.
		for (int32 i = 0; i < StartingLives; ++i)
		{
			Heart(P, Pos + FVector2D((34.f + i * 30.f) * S, 84.f * S), 10.f * S, i < Player.Lives ? KC::Hot : KC::WithAlpha(KC::White, 0.18f), Scratch);
		}
		// Combo, between the lives and the Focus meter.
		if (Player.Combo >= 2)
		{
			P.Text(Texts.Combo.Get(Player.Combo, TEXT("x")), KartUI::Font(EFont::Display, 32.f * S, 2.f * S, KC::Night), Pos + FVector2D(122.f * S, 84.f * S),
				Player.Combo >= 6 ? Gilt : Jade, FVector2D(0.0, 0.5), 3.f * S);
		}
		// Focus: three pips charge it; while it burns, a gold bar counts its six seconds down with its label inside.
		const FVector2D FocusPos = Pos + FVector2D(Width - 186.f * S, 74.f * S);
		const FVector2D FocusSize(166.f * S, 22.f * S);
		if (bFocus)
		{
			const float Left = static_cast<float>(Player.FocusRemainingMs / FocusDurationMs);
			P.Box(FocusPos, FocusSize, KC::WithAlpha(KC::White, 0.15f), 11.f * S);
			P.Box(FocusPos, FVector2D(FocusSize.X * Left, FocusSize.Y), Gilt, 11.f * S);
			P.Text(NSLOCTEXT("Samurai", "Focus", "FOCUS x2"), KartUI::Font(EFont::Label, 15.f * S, 2.f * S, KC::Night), FocusPos + FocusSize * 0.5f, KC::White, FVector2D(0.5, 0.5));
		}
		else
		{
			P.Text(NSLOCTEXT("Samurai", "FocusCharge", "FOCUS"), KartUI::Font(EFont::Label, 14.f * S), FocusPos + FVector2D(0.f, FocusSize.Y * 0.5f), KC::WithAlpha(KC::White, 0.65f), FVector2D(0.0, 0.5));
			for (int32 i = 0; i < FocusCuts; ++i)
			{
				P.Circle(FocusPos + FVector2D((96.f + i * 28.f) * S, FocusSize.Y * 0.5f), 9.f * S, i < Player.FocusCharge ? Gilt : FLinearColor::Transparent, KC::WithAlpha(Gilt, 0.75f), 2.f * S);
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
		if (!View.Project(ASamuraiGame::W(Web), At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * 90.0) * S);
		const float In = KartUI::Ease::OutBack(Clamp01(Age / 0.2), 2.4f);
		const float Out = Clamp01((Life - Age) / 0.25);
		const bool bBig = E.Kind == EEffectKind::Combo || E.Kind == EEffectKind::Bomb;
		const FLinearColor Color = E.Kind == EEffectKind::Bomb ? KC::Hot : (E.Kind == EEffectKind::Miss ? KC::WithAlpha(KC::White, 0.85f) : MovecadeKart::Art::Hex(E.Color));
		P.Transformed(At, FMath::Max(0.01f, In) * (bBig ? 1.2f : 0.92f), bBig ? -5.f : 0.f).Faded(Out).Text(E.Label, KartUI::Font(EFont::Display, 46.f * S, 4.5f * S, KC::Night), At,
			Color, FVector2D(0.5, 0.5), 6.f * S);
	}
}

void SSamuraiHud::PaintNotes(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	for (const FSamuraiNote& Note : G.GetNotes())
	{
		FVector2D At01;
		if (!View.Project(Note.World, At01))
		{
			continue;
		}
		const double Age = G.GetClock() - Note.BornAt;
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * 60.0) * S);
		const float In = KartUI::Ease::OutBack(Clamp01(Age / 0.18), 2.f);
		const float Out = Clamp01((Note.Life - Age) / 0.25);
		P.Transformed(At, FMath::Max(0.01f, In) * Note.Scale).Faded(Out).Text(Note.Label, KartUI::Font(EFont::Display, 40.f * S, 4.f * S, KC::Night), At, Note.Color,
			FVector2D(0.5, 0.5), 5.f * S);
	}
}

void SSamuraiHud::PaintBanner(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const double Age = G.GetBannerAge();
	if (Age < 0.0 || Age > 1.5 || G.GetBanner().IsEmpty())
	{
		return;
	}
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.3f);
	const float In = KartUI::Ease::OutBack(Clamp01(Age / 0.28), 2.2f);
	const float Out = Clamp01((1.5 - Age) / 0.35);
	const FKartPainter F = P.Faded(Out);
	// An ink stroke behind the words, swept in from the left.
	const float Sweep = KartUI::Ease::OutCubic(Clamp01(Age / 0.22));
	const float BandW = 900.f * S * Sweep;
	F.Transformed(Centre, 1.f, -3.f).Box(Centre - FVector2D(450.f * S, 62.f * S), FVector2D(BandW, 124.f * S), KC::WithAlpha(KC::Night, 0.72f), 62.f * S);
	F.Transformed(Centre, 1.f, -3.f).Box(Centre - FVector2D(450.f * S, 62.f * S) + FVector2D(0.f, 116.f * S), FVector2D(BandW, 8.f * S), G.GetBannerColor(), 4.f * S);
	F.Transformed(Centre, FMath::Max(0.01f, In), -3.f).Text(G.GetBanner(), KartUI::Font(EFont::Display, 110.f * S, 6.f * S, KC::Night), Centre, G.GetBannerColor(), FVector2D(0.5, 0.5), 8.f * S);
}

void SSamuraiHud::PaintFocusFrame(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const float Mood = G.GetFocusMood();
	if (Mood < 0.01f)
	{
		return;
	}
	// Gold light leaking in from the frame's edges while Focus burns.
	const float A = 0.32f * Mood * (0.8f + 0.2f * FMath::Sin(static_cast<float>(G.GetClock()) * 5.f));
	const FVector2D Size = P.Size();
	const float Edge = 140.f * S;
	P.Gradient(FVector2D::ZeroVector, FVector2D(Size.X, Edge), KC::WithAlpha(Gilt, A), KC::WithAlpha(Gilt, 0.f), true);
	P.Gradient(FVector2D(0.f, Size.Y - Edge), FVector2D(Size.X, Edge), KC::WithAlpha(Gilt, 0.f), KC::WithAlpha(Gilt, A), true);
	P.Gradient(FVector2D::ZeroVector, FVector2D(Edge, Size.Y), KC::WithAlpha(Gilt, A), KC::WithAlpha(Gilt, 0.f), false);
	P.Gradient(FVector2D(Size.X - Edge, 0.f), FVector2D(Edge, Size.Y), KC::WithAlpha(Gilt, 0.f), KC::WithAlpha(Gilt, A), false);
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
		P.Transformed(Centre, FMath::Max(0.01f, Pop)).Circle(Centre, 150.f * S, KC::WithAlpha(Vermilion, 0.85f), KC::WithAlpha(KC::Night, 0.6f), 6.f * S);
		P.Transformed(Centre, FMath::Max(0.01f, Pop)).Text(CountText.Get(FMath::Max(1, N)), KartUI::Font(EFont::Display, 200.f * S, 9.f * S, KC::Night), Centre, KC::White,
			FVector2D(0.5, 0.5), 10.f * S);
		return;
	}
	if (M.Phase == EMatchPhase::Playing && M.ElapsedMs < 800.0)
	{
		const float A = FMath::Clamp(static_cast<float>((800.0 - M.ElapsedMs) / 300.0), 0.f, 1.f);
		const float Pop = KartUI::Ease::OutBack(Clamp01(M.ElapsedMs / 200.0), 2.f);
		P.Faded(A).Transformed(Centre, FMath::Max(0.01f, Pop), -4.f).Text(NSLOCTEXT("Samurai", "Go", "SLICE!"), KartUI::Font(EFont::Display, 160.f * S, 8.f * S, KC::Night), Centre,
			KC::Hot, FVector2D(0.5, 0.5), 9.f * S);
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
	F.Card(Centre - FVector2D(530.f, 40.f) * S, FVector2D(1060.f, 80.f) * S, KC::WithAlpha(KC::Night, 0.8f), 22.f * S, 6.f * S, KC::Sun, 2.f * S);
	F.Text(NSLOCTEXT("Samurai", "Controls", "Mouse or stick: swing fast through the fruit  ·  never the bombs"), KartUI::Font(EFont::Body, 24.f * S), Centre,
		KC::White, FVector2D(0.5, 0.5));
}

void SSamuraiHud::PaintIntro(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const double Age = G.GetStageAge();
	const bool bPractice = G.GetPractice() != ESamuraiPractice::None;
	const double End = bPractice ? ASamuraiGame::TitleSeconds - 0.3 : ASamuraiGame::TitleSeconds;
	const float A = Clamp01(Age / 0.45) * Clamp01((End - Age) / 0.4);
	const FVector2D Size = P.Size();
	// Letterbox bars: the round opens like a film.
	const float Bars = KartUI::Ease::OutCubic(Clamp01(Age / 0.5)) * Clamp01((End + 0.2 - Age) / 0.5);
	if (Bars > 0.f)
	{
		const float BarH = 96.f * S * Bars;
		P.Box(FVector2D::ZeroVector, FVector2D(Size.X, BarH), KC::WithAlpha(FLinearColor::Black, 0.92f));
		P.Box(FVector2D(0.f, Size.Y - BarH), FVector2D(Size.X, BarH), KC::WithAlpha(FLinearColor::Black, 0.92f));
	}
	if (A <= 0.f)
	{
		return;
	}
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(Size.X / 2.f, Size.Y * 0.4f);
	// The rising sun behind the title, and an ink stroke under it.
	const float Rise = KartUI::Ease::OutBack(Clamp01((Age - 0.1) / 0.6), 1.4f);
	F.Circle(Centre + FVector2D(0.f, (1.f - Rise) * 60.f * S - 10.f * S), 230.f * S * FMath::Max(0.01f, Rise), KC::WithAlpha(Vermilion, 0.88f));
	const float Stroke = KartUI::Ease::OutCubic(Clamp01((Age - 0.25) / 0.35));
	F.Transformed(Centre + FVector2D(0.f, 127.f * S), 1.f, -1.5f).Box(Centre + FVector2D(-560.f * S, 92.f * S), FVector2D(1120.f * S * Stroke, 70.f * S), KC::WithAlpha(KC::Night, 0.8f), 35.f * S);
	const float TitlePop = KartUI::Ease::OutBack(Clamp01((Age - 0.15) / 0.45), 1.8f);
	F.Transformed(Centre, FMath::Max(0.01f, TitlePop)).Text(NSLOCTEXT("Samurai", "Title", "SAMURAI SLICE"), KartUI::Font(EFont::Display, 150.f * S, 8.f * S, KC::Night),
		Centre - FVector2D(0.f, 20.f * S), KC::White, FVector2D(0.5, 0.5), 10.f * S);
	F.Text(NSLOCTEXT("Samurai", "Rules", "Cut the fruit. Never the bombs. Three precise cuts: Focus, double points."), KartUI::Font(EFont::Body, 26.f * S, 1.f * S, KC::Night),
		Centre + FVector2D(0.f, 127.f * S), KC::White, FVector2D(0.5, 0.5), 2.f * S);
	// The swordsmen, a chip each in their colour, popping in one by one.
	const TArray<FSamuraiSeat>& Seats = G.GetSeats();
	const float ChipW = 280.f * S, Gap = 20.f * S;
	const float RowW = Seats.Num() * ChipW + (Seats.Num() - 1) * Gap;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float In = KartUI::Ease::OutBack(Clamp01((Age - 0.6 - i * 0.12) / 0.35), 2.f);
		const FVector2D Pos(Size.X / 2.f - RowW / 2.f + i * (ChipW + Gap), Centre.Y + 190.f * S);
		const FKartPainter Chip = F.Transformed(Pos + FVector2D(ChipW / 2.f, 28.f * S), FMath::Max(0.01f, In));
		Chip.Card(Pos, FVector2D(ChipW, 56.f * S), KC::WithAlpha(KC::Night, 0.86f), 28.f * S, 4.f * S, Seats[i].Color, 3.f * S);
		Chip.Circle(Pos + FVector2D(30.f * S, 28.f * S), 11.f * S, Seats[i].Color);
		Chip.Text(Seats[i].Name, KartUI::Font(EFont::Title, 28.f * S), Pos + FVector2D(52.f * S, 28.f * S), KC::White, FVector2D(0.0, 0.5));
		if (Seats[i].Kind == ESwordKind::Bot)
		{
			Chip.Text(NSLOCTEXT("Samurai", "Cpu", "CPU"), KartUI::Font(EFont::Label, 14.f * S), Pos + FVector2D(ChipW - 18.f * S, 28.f * S), KC::WithAlpha(KC::White, 0.55f), FVector2D(1.0, 0.5));
		}
	}
}

void SSamuraiHud::PaintPractice(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const ESamuraiPractice State = G.GetPractice();
	if (State == ESamuraiPractice::None)
	{
		return;
	}
	const double Age = G.GetPracticeAge();
	const FVector2D Size = P.Size();
	// The swipe to make: an arc through the apple, a bright bead running along it.
	FVector2D Apple01;
	if (State != ESamuraiPractice::Cut && View.Project(G.GetPracticeWorld(), Apple01))
	{
		const FVector2D C = Apple01 * Size;
		const float R = 150.f * S;
		const float Fade = Clamp01((Age - 0.3) / 0.3);
		Scratch.Reset();
		for (int32 i = 0; i <= 16; ++i)
		{
			const float T = i / 16.f;
			const float Ang = FMath::DegreesToRadians(FMath::Lerp(-150.f, -30.f, T));
			Scratch.Add(C + FVector2D(FMath::Cos(Ang) * R, -FMath::Sin(Ang) * R * 0.55f - R * 0.55f));
		}
		const FKartPainter F = P.Faded(Fade);
		F.Lines(Scratch, KC::WithAlpha(KC::White, 0.55f), 5.f * S);
		const FVector2D Tail = Scratch.Last(), Before = Scratch[Scratch.Num() - 2];
		const FVector2D Dir = (Tail - Before).GetSafeNormal(), Side(-Dir.Y, Dir.X);
		F.Lines({ Tail - Dir * 22.f * S + Side * 14.f * S, Tail, Tail - Dir * 22.f * S - Side * 14.f * S }, KC::WithAlpha(KC::White, 0.8f), 5.f * S);
		const float Run = FMath::Fmod(static_cast<float>(Age), 1.1f) / 0.7f;
		if (Run <= 1.f)
		{
			const int32 k = FMath::Clamp(FMath::FloorToInt32(Run * 16.f), 0, 15);
			const FVector2D Bead = FMath::Lerp(Scratch[k], Scratch[k + 1], Run * 16.f - k);
			F.Circle(Bead, 13.f * S, Gilt, KC::WithAlpha(KC::Night, 0.6f), 2.f * S);
		}
	}
	// The coach mark.
	const FVector2D Centre(Size.X / 2.f, Size.Y * 0.76f);
	const float In = KartUI::Ease::OutBack(Clamp01((State == ESamuraiPractice::Cut ? 1.0 : Age) / 0.35), 1.8f);
	const FKartPainter Card = P.Transformed(Centre, FMath::Max(0.01f, In));
	const FLinearColor Edge = State == ESamuraiPractice::Cut ? Jade : (State == ESamuraiPractice::TooSlow ? Gilt : KC::Sun);
	Card.Card(Centre - FVector2D(560.f, 70.f) * S, FVector2D(1120.f, 140.f) * S, KC::WithAlpha(KC::Night, 0.88f), 26.f * S, 6.f * S, Edge, 3.f * S);
	Card.Box(Centre + FVector2D(-530.f, -88.f) * S, FVector2D(170.f, 36.f) * S, Edge, 18.f * S);
	Card.Text(NSLOCTEXT("Samurai", "PracticeChip", "PRACTICE"), KartUI::Font(EFont::Label, 18.f * S), Centre + FVector2D(-445.f, -70.f) * S, KC::Night, FVector2D(0.5, 0.5));
	FText Line, Sub;
	switch (State)
	{
	case ESamuraiPractice::TooSlow:
		Line = NSLOCTEXT("Samurai", "PracticeSlow", "Faster! A cut is a quick, sharp swing.");
		Sub = NSLOCTEXT("Samurai", "PracticeSlowSub", "Flick the mouse or stick right through the apple.");
		break;
	case ESamuraiPractice::Cut:
		Line = NSLOCTEXT("Samurai", "PracticeDone", "Clean cut! That's the stroke.");
		Sub = NSLOCTEXT("Samurai", "PracticeDoneSub", "Cut at the top of each arc for Perfect. Here comes the fruit...");
		break;
	default:
		Line = NSLOCTEXT("Samurai", "PracticeLine", "Slice the practice apple!");
		Sub = NSLOCTEXT("Samurai", "PracticeSub", "Mouse or right stick aims the blade. Swing fast through the fruit to cut.");
		break;
	}
	Card.Text(Line, KartUI::Font(EFont::Title, 36.f * S), Centre + FVector2D(0.f, -12.f * S), State == ESamuraiPractice::Cut ? Jade : KC::White, FVector2D(0.5, 0.5));
	Card.Text(Sub, KartUI::Font(EFont::Body, 22.f * S), Centre + FVector2D(0.f, 34.f * S), KC::WithAlpha(KC::White, 0.8f), FVector2D(0.5, 0.5));
	if (State != ESamuraiPractice::Cut)
	{
		P.Faded(Clamp01(Age / 0.35)).Text(NSLOCTEXT("Samurai", "PracticeSkip", "SPACE / (A)  skip"), KartUI::Font(EFont::Label, 16.f * S), Centre + FVector2D(540.f, 92.f) * S, KC::WithAlpha(KC::White, 0.7f),
			FVector2D(1.0, 0.5), 2.f * S);
	}
}

void SSamuraiHud::PaintStandings(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const TArray<FPlayer>& Players = G.GetMatch().Players;
	const double FinishedAt = G.GetClock() - G.GetStageAge();
	if (OrderFor != FinishedAt || Order.Num() != Players.Num())
	{
		OrderFor = FinishedAt;
		Order.Reset();
		OrderDetail.Reset();
		for (int32 i = 0; i < Players.Num(); ++i)
		{
			Order.Add(i);
		}
		Algo::StableSortBy(Order, [&Players](int32 i) { return -Players[i].Score; });
		for (const int32 i : Order)
		{
			OrderDetail.Add(FString::Printf(TEXT("best combo %d  ·  %d cuts  ·  %d bombs"), Players[i].BestCombo, Players[i].Hits, Players[i].Bombs));
		}
	}
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(Clamp01(Age / 0.5));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	F.Circle(Centre, 120.f * S * FMath::Max(0.01f, KartUI::Ease::OutBack(Clamp01(Age / 0.5))), KC::WithAlpha(Vermilion, 0.8f));
	F.Text(NSLOCTEXT("Samurai", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FPlayer& Player = Players[Order[i]];
		const FSamuraiSeat* Seat = G.GetSeats().IsValidIndex(Player.LaneIndex) ? &G.GetSeats()[Player.LaneIndex] : nullptr;
		const float In = KartUI::Ease::OutBack(Clamp01((Age - 0.3 - i * 0.18) / 0.4));
		const FVector2D Pos(P.Size().X / 2.f - 400.f * S, P.Size().Y * 0.3f + i * 104.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(400.f, 44.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = G.GetMatch().WinnerIds.Contains(Player.Id);
		Row.Card(Pos, FVector2D(800.f, 88.f) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::Sky, 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 52.f * S), Pos + FVector2D(48.f * S, 44.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Player.Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(OrderDetail[i], KartUI::Font(EFont::Body, 18.f * S), Pos + FVector2D(100.f * S, 64.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Player.Score), KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(770.f * S, 44.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
