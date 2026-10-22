#include "Games/Sumo/SSumoHud.h"

#include "Games/Sumo/SumoGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Sumo;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	/** A five-point star centred on C, outer radius R. */
	void Star(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill)
	{
		TArray<FVector2D> Pts;
		for (int32 i = 0; i < 10; ++i)
		{
			const float A = -HALF_PI + PI * i / 5.f;
			const float Rad = i % 2 ? R * 0.45f : R;
			Pts.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rad);
		}
		P.Polygon(Pts, Fill);
	}

	const TCHAR* PowerLabel(EPower Power)
	{
		switch (Power)
		{
		case EPower::Heavy: return TEXT("HEAVY");
		case EPower::Shield: return TEXT("SHIELD");
		case EPower::Turbo: return TEXT("TURBO");
		default: return TEXT("");
		}
	}

	double PowerTotal(EPower Power)
	{
		switch (Power)
		{
		case EPower::Heavy: return HeavyMs;
		case EPower::Shield: return ShieldMs;
		case EPower::Turbo: return TurboMs;
		default: return 1.0;
		}
	}
}

void SSumoHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SSumoHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const ASumoGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	switch (G->GetStage())
	{
	case ESumoStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case ESumoStage::Finished:
		PaintCallouts(P, *G, S);
		PaintStandings(P, *G, S);
		break;
	default:
		PaintTags(P, *G, S);
		PaintTop(P, *G, S);
		PaintPanels(P, *G, S);
		PaintFeed(P, *G, S);
		PaintCallouts(P, *G, S);
		PaintControls(P, *G, S);
		PaintCountdown(P, *G, S);
		break;
	}
	return Layer;
}

void SSumoHud::PaintTop(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * KartUI::Metric::SafeFraction);
	const FVector2D Size(300.f * S, 96.f * S);
	P.Card(Top - FVector2D(Size.X / 2.f, 0.f), Size, KC::WithAlpha(KC::Night, 0.82f), 26.f * S, 6.f * S, KC::Sun, 3.f * S);
	P.Text(FString::Printf(TEXT("ROUND %d"), M.Round), KartUI::Font(EFont::Display, 44.f * S, 3.f * S, KC::Night), Top + FVector2D(0.f, 36.f * S), KC::White, FVector2D(0.5, 0.5), 3.f * S);
	// The rounds so far: a dot per round in its winner's colour (grey for a draw), then the one being fought.
	const int32 Dots = FMath::Max(3, M.RoundWinners.Num() + (M.Phase == EPhase::MatchOver ? 0 : 1));
	for (int32 i = 0; i < Dots; ++i)
	{
		const FVector2D C = Top + FVector2D((i - (Dots - 1) / 2.f) * 30.f * S, 74.f * S);
		if (M.RoundWinners.IsValidIndex(i))
		{
			const int32 W = M.RoundWinners[i];
			P.Circle(C, 9.f * S, W == INDEX_NONE ? KC::WithAlpha(KC::White, 0.35f) : G.ColorOf(W), KC::Night, 2.f * S);
		}
		else
		{
			const bool bNow = i == M.RoundWinners.Num();
			P.Circle(C, 9.f * S, bNow ? KC::WithAlpha(KC::Sun, 0.35f + 0.25f * FMath::Sin(static_cast<float>(G.GetClock()) * 5.f)) : KC::WithAlpha(KC::White, 0.1f),
				KC::WithAlpha(KC::White, 0.5f), 2.f * S);
		}
	}
	// The ring: when the next band goes, or the warning.
	if (M.Phase != EPhase::Fight)
	{
		return;
	}
	const double Next = ShrinkAtMs(M.ShrinksDone);
	if (!FMath::IsFinite(Next))
	{
		return;
	}
	const double In = (Next - M.RoundMs) / 1000.0;
	const FVector2D C = Top + FVector2D(0.f, 126.f * S);
	if (M.IsShrinkPending())
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(G.GetClock()) * 14.f);
		const FKartPainter T = P.Transformed(C, 1.f + 0.06f * Pulse);
		T.Box(C - FVector2D(190.f, 22.f) * S, FVector2D(380.f, 44.f) * S, FMath::Lerp(KC::Hot, KC::WithAlpha(KC::Night, 0.9f), 0.3f * Pulse), 22.f * S);
		T.Text(FString::Printf(TEXT("%s IN %d"), M.ShrinksDone >= RingBands ? TEXT("CORE FALLS") : TEXT("EDGE FALLS"), FMath::Max(1, FMath::CeilToInt32(In))),
			KartUI::Font(EFont::Label, 22.f * S), C, KC::White, FVector2D(0.5, 0.5));
	}
	else if (In < 12.0)
	{
		P.Box(C - FVector2D(150.f, 18.f) * S, FVector2D(300.f, 36.f) * S, KC::WithAlpha(KC::Night, 0.72f), 18.f * S);
		P.Text(FString::Printf(TEXT("RING SHRINKS IN %d"), FMath::CeilToInt32(In)), KartUI::Font(EFont::Label, 18.f * S), C, KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5));
	}
}

void SSumoHud::PaintPanels(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 N = M.Pucks.Num();
	const float Gap = 22.f * S;
	const float Width = FMath::Min(390.f * S, (P.Size().X * 0.9f - Gap * (N - 1)) / FMath::Max(1, N));
	const float Height = 128.f * S;
	const float Total = Width * N + Gap * (N - 1);
	const float Y = P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height;
	for (int32 i = 0; i < N; ++i)
	{
		const FPuck& Puck = M.Pucks[i];
		const FSumoSeat* Seat = G.SeatOf(i);
		const FLinearColor Color = G.ColorOf(i);
		const FVector2D Pos(P.Size().X / 2.f - Total / 2.f + i * (Width + Gap), Y);
		const bool bDown = !Puck.bStanding;
		const FKartPainter F = P.Faded(bDown ? 0.55f : 1.f);
		F.Card(Pos, FVector2D(Width, Height), KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, Color, 3.f * S);
		// The colour tab, the name, the round wins as stars.
		F.Box(Pos + FVector2D(14.f, 14.f) * S, FVector2D(10.f * S, Height - 28.f * S), Color, 5.f * S);
		F.Text(Puck.Name, KartUI::Font(EFont::Title, 26.f * S), Pos + FVector2D(38.f * S, 30.f * S), KC::White, FVector2D(0.0, 0.5));
		for (int32 w = 0; w < WinsForMatch; ++w)
		{
			Star(F, Pos + FVector2D((50.f + w * 34.f) * S, 70.f * S), 14.f * S, w < Puck.RoundWins ? KC::Sun : KC::WithAlpha(KC::White, 0.16f));
		}
		F.Text(FString::Printf(TEXT("KO %d"), Puck.Knockouts), KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(128.f * S, 70.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.0, 0.5));
		F.Text(FString::FromInt(Puck.Score), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(Width - 20.f * S, 44.f * S), KC::White, FVector2D(1.0, 0.5), 3.f * S);
		// The dash meter: filling while it reloads, lit when ready.
		const FVector2D Bar = Pos + FVector2D(38.f * S, 98.f * S);
		const float BarW = Width - 58.f * S;
		const float Fill = Puck.DashCooldownMs > 0.0 ? 1.f - static_cast<float>(Puck.DashCooldownMs / (Puck.Power == EPower::Turbo ? TurboCooldownMs : DashCooldownMs)) : 1.f;
		F.Box(Bar, FVector2D(BarW, 12.f * S), KC::WithAlpha(KC::White, 0.12f), 6.f * S);
		F.Box(Bar, FVector2D(FMath::Max(12.f * S, BarW * FMath::Clamp(Fill, 0.f, 1.f)), 12.f * S), Fill >= 1.f ? Color : KC::WithAlpha(Color, 0.45f), 6.f * S);
		F.Text(Fill >= 1.f ? TEXT("DASH") : TEXT(""), KartUI::Font(EFont::Label, 13.f * S), Bar + FVector2D(BarW - 6.f * S, 6.f * S), KC::Night, FVector2D(1.0, 0.5));
		// A power-up chip with its time running down.
		if (Puck.Power != EPower::None && Puck.bStanding)
		{
			const FVector2D Chip = Pos + FVector2D(Width - 150.f * S, -22.f * S);
			const float Left = FMath::Clamp(static_cast<float>(Puck.PowerMs / PowerTotal(Puck.Power)), 0.f, 1.f);
			P.Box(Chip, FVector2D(130.f * S, 34.f * S), KC::WithAlpha(KC::Night, 0.9f), 17.f * S, Color, 2.f * S);
			P.Box(Chip + FVector2D(4.f, 4.f) * S, FVector2D((130.f - 8.f) * S * Left, 26.f * S), KC::WithAlpha(KC::Sky, 0.4f), 13.f * S);
			P.Text(PowerLabel(Puck.Power), KartUI::Font(EFont::Label, 17.f * S), Chip + FVector2D(65.f, 17.f) * S, KC::White, FVector2D(0.5, 0.5));
		}
		const bool bQuiet = Seat && Seat->Kind == ESumoPilot::Phone && G.IsPhoneQuiet(i) && M.Phase == EPhase::Fight;
		if (bDown || bQuiet)
		{
			const FText Banner = bDown ? NSLOCTEXT("Sumo", "Splash", "SPLASH!") : NSLOCTEXT("Sumo", "Quiet", "PICK UP YOUR PHONE");
			P.Text(Banner, KartUI::Font(EFont::Display, (bQuiet ? 24.f : 34.f) * S, 3.f * S, KC::Night), Pos + FVector2D(Width / 2.f, -20.f * S), bDown ? KC::Sky : KC::Hot,
				FVector2D(0.5, 0.5), 4.f * S);
		}
	}
}

void SSumoHud::PaintTags(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	for (int32 i = 0; i < M.Pucks.Num(); ++i)
	{
		const FPuck& Puck = M.Pucks[i];
		if (Puck.bOut)
		{
			continue;
		}
		FVector2D At01;
		if (!G.ScreenOf(G.PuckWorld(i) + FVector(0.0, 0.0, 195.0), At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size();
		const FLinearColor Color = G.ColorOf(i);
		const FSumoSeat* Seat = G.SeatOf(i);
		const FString Label = Seat && Seat->Kind == ESumoPilot::Local ? FString(TEXT("YOU")) : Puck.Name;
		const FSlateFontInfo Font = KartUI::Font(EFont::Label, 17.f * S);
		const FVector2D Size = FKartPainter::Measure(Label, Font) + FVector2D(24.f, 10.f) * S;
		const FKartPainter F = P.Faded(Puck.bFalling ? 0.4f : 0.92f);
		F.Box(At - FVector2D(Size.X / 2.f, Size.Y), Size, KC::WithAlpha(KC::Night, 0.82f), Size.Y / 2.f, Color, 2.f * S);
		F.Text(Label, Font, At - FVector2D(0.f, Size.Y / 2.f), KC::White, FVector2D(0.5, 0.5));
		F.Polygon({ At + FVector2D(-7.f, 0.f) * S, At + FVector2D(7.f, 0.f) * S, At + FVector2D(0.f, 9.f) * S }, Color);
	}
}

void SSumoHud::PaintFeed(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const TArray<FSumoFeedLine>& Feed = G.GetFeed();
	const FVector2D Right(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction + 8.f * S);
	int32 Row = 0;
	for (int32 k = Feed.Num() - 1; k >= 0 && Row < 4; --k)
	{
		const FSumoFeedLine& L = Feed[k];
		const double Age = G.GetClock() - L.At;
		if (Age > 6.0 || !M.Pucks.IsValidIndex(L.Victim))
		{
			continue;
		}
		const float A = FMath::Clamp(static_cast<float>((6.0 - Age) / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>(Age / 0.2), 0.f, 1.f);
		const FKartPainter F = P.Faded(A);
		const FVector2D Pos = Right + FVector2D(-420.f * S + (1.f - FMath::Clamp(static_cast<float>(Age / 0.25), 0.f, 1.f)) * 60.f * S, Row * 52.f * S);
		F.Box(Pos, FVector2D(420.f * S, 44.f * S), KC::WithAlpha(KC::Night, 0.78f), 22.f * S);
		const FSlateFontInfo Font = KartUI::Font(EFont::Title, 21.f * S);
		const FString Victim = M.Pucks[L.Victim].Name;
		if (M.Pucks.IsValidIndex(L.Killer))
		{
			F.Text(M.Pucks[L.Killer].Name, Font, Pos + FVector2D(22.f * S, 22.f * S), G.ColorOf(L.Killer), FVector2D(0.0, 0.5));
			F.Text(TEXT("KO"), KartUI::Font(EFont::Label, 16.f * S), Pos + FVector2D(210.f * S, 22.f * S), KC::Sun, FVector2D(0.5, 0.5));
		}
		else
		{
			F.Text(TEXT("ring out"), KartUI::Font(EFont::Body, 18.f * S), Pos + FVector2D(22.f * S, 22.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.0, 0.5));
		}
		F.Text(Victim, Font, Pos + FVector2D(398.f * S, 22.f * S), G.ColorOf(L.Victim), FVector2D(1.0, 0.5));
		++Row;
	}
}

void SSumoHud::PaintCallouts(const FKartPainter& P, const ASumoGame& G, float S) const
{
	for (const FSumoCallout& C : G.GetCallouts())
	{
		const double Age = G.GetClock() - C.At;
		FVector2D At;
		if (C.bCentre)
		{
			At = FVector2D(P.Size().X / 2.f, P.Size().Y * 0.25f);
		}
		else
		{
			FVector2D At01;
			if (!G.ScreenOf(C.World, At01))
			{
				continue;
			}
			At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * 60.0) * S);
		}
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.22), 0.f, 1.f), 2.6f);
		const float Out = FMath::Clamp(static_cast<float>((C.Duration - Age) / 0.3), 0.f, 1.f);
		const float Size = C.bCentre ? (C.bBig ? 88.f : 60.f) : (C.bBig ? 58.f : 42.f);
		const FKartPainter F = P.Transformed(At, FMath::Max(0.01f, In), C.bBig ? -3.f : 0.f).Faded(Out);
		F.Text(C.Text, KartUI::Font(EFont::Display, Size * S, 5.f * S, KC::Night), At, C.Color, FVector2D(0.5, 0.5), 7.f * S);
		if (!C.Sub.IsEmpty())
		{
			F.Text(C.Sub, KartUI::Font(EFont::Title, (C.bCentre ? 32.f : 24.f) * S, 3.f * S, KC::Night), At + FVector2D(0.f, Size * 0.62f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
		}
	}
}

void SSumoHud::PaintCountdown(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	if (M.Phase != EPhase::Countdown)
	{
		return;
	}
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.4f);
	const double Remaining = (CountdownMs - M.PhaseMs) / 1000.0;
	const int32 N = FMath::Clamp(FMath::CeilToInt32(Remaining), 1, 3);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(1.0 - (N - Remaining)) / 0.35f, 0.f, 1.f));
	const float Fade = FMath::Clamp(static_cast<float>(M.PhaseMs / 300.0), 0.f, 1.f);
	P.Faded(Fade).Text(FString::Printf(TEXT("ROUND %d"), M.Round), KartUI::Font(EFont::Display, 64.f * S, 4.f * S, KC::Night), Centre - FVector2D(0.f, 140.f * S), KC::White,
		FVector2D(0.5, 0.5), 6.f * S);
	P.Transformed(Centre, FMath::Max(0.01f, Pop)).Text(FString::FromInt(N), KartUI::Font(EFont::Display, 200.f * S, 9.f * S, KC::Night), Centre, KC::Sun, FVector2D(0.5, 0.5), 10.f * S);
}

void SSumoHud::PaintControls(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	if (!G.HasLocal() || M.Round > 1 || M.RoundMs > 9000.0)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((9000.0 - M.RoundMs) / 600.0), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.68f);
	const FKartPainter F = P.Faded(A);
	const FText Line = NSLOCTEXT("Sumo", "Controls", "Stick or WASD: roll  ·  A, Space or click: dash  ·  Push them off the edge!");
	const FSlateFontInfo Font = KartUI::Font(EFont::Body, 22.f * S);
	const float W = static_cast<float>(FKartPainter::Measure(Line.ToString(), Font).X) + 64.f * S;
	F.Card(Centre - FVector2D(W / 2.f, 36.f * S), FVector2D(W, 72.f * S), KC::WithAlpha(KC::Night, 0.78f), 22.f * S, 6.f * S, KC::Sun, 2.f * S);
	F.Text(Line, Font, Centre, KC::White, FVector2D(0.5, 0.5));
}

void SSumoHud::PaintIntro(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const double Age = G.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>((3.6 - Age) / 0.4), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.42f);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.6), 0.f, 1.f), 1.8f);
	F.Transformed(Centre - FVector2D(0.f, 60.f * S), FMath::Max(0.01f, Pop), -3.f).Text(NSLOCTEXT("Sumo", "Title", "SUMO BUMPERS"), KartUI::Font(EFont::Display, 150.f * S, 8.f * S),
		Centre - FVector2D(0.f, 60.f * S), KC::Hot, FVector2D(0.5, 0.5), 10.f * S);
	// The line-up in their colours.
	const TArray<FSumoSeat>& Seats = G.GetSeats();
	const FSlateFontInfo Font = KartUI::Font(EFont::Title, 36.f * S, 2.f * S);
	const float Gap = 60.f * S;
	float Total = 0.f;
	for (const FSumoSeat& Seat : Seats)
	{
		Total += FKartPainter::Measure(Seat.Name, Font).X;
	}
	Total += Gap * FMath::Max(0, Seats.Num() - 1);
	float X = Centre.X - Total / 2.f;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float W = FKartPainter::Measure(Seats[i].Name, Font).X;
		F.Text(Seats[i].Name, Font, FVector2D(X + W / 2.f, Centre.Y + 56.f * S), G.ColorOf(i), FVector2D(0.5, 0.5), 4.f * S);
		if (i + 1 < Seats.Num())
		{
			F.Text(TEXT("vs"), KartUI::Font(EFont::Label, 22.f * S), FVector2D(X + W + Gap / 2.f, Centre.Y + 58.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.5, 0.5));
		}
		X += W + Gap;
	}
	F.Text(NSLOCTEXT("Sumo", "Rules", "Knock everyone off the ring. The edge falls away. Two rounds wins it."), KartUI::Font(EFont::Body, 26.f * S),
		Centre + FVector2D(0.f, 112.f * S), KC::WithAlpha(KC::White, 0.9f), FVector2D(0.5, 0.5), 3.f * S);
}

void SSumoHud::PaintStandings(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const TArray<int32> Order = Standings(M);
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>((Age - 1.2) / 0.5), 0.f, 1.f));
	const FVector2D Head(P.Size().X - 400.f * S - P.Size().X * KartUI::Metric::SafeFraction, P.Size().Y * 0.14f);
	F.Text(NSLOCTEXT("Sumo", "Final", "FINAL STANDINGS"), KartUI::Font(EFont::Display, 64.f * S, 4.f * S), Head, KC::Sun, FVector2D(0.5, 0.5), 6.f * S);
	for (int32 k = 0; k < Order.Num(); ++k)
	{
		const FPuck& Puck = M.Pucks[Order[k]];
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 1.5 - k * 0.15) / 0.4), 0.f, 1.f));
		const FVector2D Pos(Head.X - 380.f * S, P.Size().Y * 0.22f + k * 94.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(380.f, 40.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = Order[k] == M.MatchWinner;
		Row.Card(Pos, FVector2D(760.f, 80.f) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, G.ColorOf(Puck.Slot), 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(k + 1), KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(44.f * S, 40.f * S), KC::ForPlace(k + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Box(Pos + FVector2D(84.f, 18.f) * S, FVector2D(10.f, 44.f) * S, G.ColorOf(Puck.Slot), 5.f * S);
		Row.Text(Puck.Name, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(110.f * S, 28.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(FString::Printf(TEXT("%d round %s  ·  %d KO  ·  %d fall%s"), Puck.RoundWins, Puck.RoundWins == 1 ? TEXT("win") : TEXT("wins"), Puck.Knockouts, Puck.Falls,
			Puck.Falls == 1 ? TEXT("") : TEXT("s")), KartUI::Font(EFont::Body, 18.f * S), Pos + FVector2D(110.f * S, 58.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		for (int32 w = 0; w < Puck.RoundWins; ++w)
		{
			Star(Row, Pos + FVector2D((560.f + w * 32.f) * S, 40.f * S), 14.f * S, KC::Sun);
		}
		Row.Text(FString::FromInt(Puck.Score), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(735.f * S, 40.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
