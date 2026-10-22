#include "Games/Darts/SDartsHud.h"

#include "Games/Darts/DartsGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Darts;
namespace KC = KartUI::Color;
using KartUI::EFont;

#define LOCTEXT_NAMESPACE "Darts"

namespace
{
	FLinearColor EffectColor(const FDartsEffect& E, const FLinearColor& Lane)
	{
		switch (E.Kind)
		{
		case EEffectKind::Bull: return KC::Sun;
		case EEffectKind::Bounty: return Lane;
		case EEffectKind::Miss: return KC::WithAlpha(KC::White, 0.8f);
		default: return KC::Mint;
		}
	}

	/** The card's life: in with a pop, out with a fade (0 when not showing). */
	float DartsCardAlpha(double Age, double Life)
	{
		if (Age < 0.0 || Age > Life)
		{
			return 0.f;
		}
		return FMath::Clamp(static_cast<float>((Life - Age) / 0.3), 0.f, 1.f);
	}

	constexpr double DartsVisitCardSeconds = 2.4;
}

void SDartsHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SDartsHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const ADartsGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	// The bull's flash washes the frame first, under everything else.
	if (const float Flash = G->GetFlash(); Flash > 0.002f)
	{
		P.Box(FVector2D::ZeroVector, P.Size(), KC::WithAlpha(G->GetFlashColor(), Flash));
	}
	// One projection per paint for every callout and the gauge.
	const FDartsView View = G->MakeView();
	switch (G->GetStage())
	{
	case EDartsStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case EDartsStage::Finished:
		// The last callouts give way to the standings.
		PaintCallouts(P.Faded(FMath::Clamp(1.f - static_cast<float>(G->GetFinishedAge() / 0.4), 0.f, 1.f)), *G, View, S);
		PaintStandings(P, *G, S);
		break;
	default:
		PaintBoard(P, *G, S);
		PaintUp(P, *G, S);
		PaintCallouts(P, *G, View, S);
		PaintVisit(P, *G, S);
		PaintGauge(P, *G, View, S);
		PaintControls(P, *G, S);
		PaintCoach(P, *G, S);
		PaintCountdown(P, *G, S);
		break;
	}
	return Layer;
}

const FString& SDartsHud::FittedName(int32 Row, const FString& Name, float S, float& OutSize) const
{
	if (Fitted.Num() <= Row)
	{
		Fitted.SetNum(Row + 1);
	}
	FFitted& F = Fitted[Row];
	if (F.Scale != S || !F.Source.Equals(Name, ESearchCase::CaseSensitive))
	{
		F.Source = Name;
		F.Scale = S;
		F.Shown = Name;
		F.Size = 26.f * S;
		const float Room = 186.f * S;
		const float Wide = static_cast<float>(FKartPainter::Measure(F.Shown, KartUI::Font(EFont::Title, F.Size)).X);
		if (Wide > Room)
		{
			F.Size = FMath::Max(16.f * S, F.Size * Room / Wide);
			while (F.Shown.Len() > 2 && FKartPainter::Measure(F.Shown, KartUI::Font(EFont::Title, F.Size)).X > Room)
			{
				F.Shown = F.Shown.LeftChop(2) + TEXT(".");
			}
		}
	}
	OutSize = F.Size;
	return F.Shown;
}

void SDartsHud::PaintBoard(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// The scoreboard: a row per thrower, nine dart slots grouped in visits, the running score.
	const FDartsMatch& M = G.GetMatch();
	const FDartsPlayer* Up = M.ActivePlayer();
	const FVector2D Origin = P.Size() * KartUI::Metric::SafeFraction;
	const float Row = 62.f * S, NameW = 238.f * S, Slot = 36.f * S, Gap = 16.f * S, TotalW = 132.f * S;
	const float Width = NameW + Slot * DartsPerPlayer + Gap * (RoundCount - 1) + 20.f * S + TotalW;
	for (int32 r = 0; r < M.Players.Num(); ++r)
	{
		const FDartsPlayer& Player = M.Players[r];
		const FDartsSeat* Seat = G.SeatOf(Player);
		const FLinearColor Color = Seat ? Seat->Color : KC::Sky;
		const bool bUp = Up && Up->Id == Player.Id;
		const FVector2D Pos = Origin + FVector2D(0.f, r * (Row + 8.f * S));
		P.Card(Pos, FVector2D(Width, Row), bUp ? KC::Paper : KC::WithAlpha(KC::Night, 0.84f), 16.f * S, 5.f * S,
			bUp ? Color : FLinearColor::Transparent, bUp ? 3.f * S : 0.f);
		// The colour tab down the left edge.
		P.Box(Pos + FVector2D(0.f, 10.f * S), FVector2D(6.f * S, Row - 20.f * S), Color, 3.f * S);
		const FLinearColor Ink = bUp ? KC::Night : KC::White;
		float NameSize = 0.f;
		const FString& Name = FittedName(r, Player.Name, S, NameSize);
		P.Text(Name, KartUI::Font(EFont::Title, NameSize), Pos + FVector2D(26.f * S, Row / 2.f), Ink, FVector2D(0.0, 0.5));
		// The visit in play gets a soft band behind its three slots.
		const int32 LiveVisit = bUp ? FMath::Min((Player.DartsThrown - M.TurnDarts) / DartsPerRound, RoundCount - 1) : -1;
		if (LiveVisit >= 0 && M.Phase == EMatchPhase::Playing)
		{
			const FVector2D Band = Pos + FVector2D(NameW + LiveVisit * (Slot * DartsPerRound + Gap) - 4.f * S, 7.f * S);
			P.Box(Band, FVector2D(Slot * DartsPerRound + 8.f * S, Row - 14.f * S), KC::WithAlpha(Color, 0.22f), 12.f * S);
		}
		// Nine darts, three visits: filled by points, ringed by the bounty, empty while to come.
		for (int32 d = 0; d < DartsPerPlayer; ++d)
		{
			const int32 Visit = d / DartsPerRound;
			const FVector2D C = Pos + FVector2D(NameW + d * Slot + Visit * Gap + Slot / 2.f, Row / 2.f);
			if (Player.Landings.IsValidIndex(d))
			{
				const FDartLanding& L = Player.Landings[d];
				const FLinearColor Fill = L.ScoreDelta == 0 ? KC::WithAlpha(KC::NavyDim, 0.75f) : (L.Hit.Ring == ERing::Bull ? KC::Sun : Color);
				P.Circle(C, 15.f * S, Fill, L.bBounty ? KC::Sun : KC::WithAlpha(KC::Night, 0.6f), (L.bBounty ? 3.f : 1.5f) * S);
				const FString Points = L.ScoreDelta == 0 ? FString(TEXT("–")) : FString::FromInt(L.ScoreDelta);
				P.Text(Points, KartUI::Font(EFont::Label, (L.ScoreDelta >= 100 ? 11.f : 14.f) * S), C, KC::TextOn(Fill), FVector2D(0.5, 0.5));
			}
			else
			{
				P.Circle(C, 11.f * S, FLinearColor::Transparent, KC::WithAlpha(bUp ? KC::Night : KC::White, 0.28f), 2.f * S);
			}
		}
		P.Text(FString::FromInt(Player.Score), KartUI::Font(EFont::Display, 38.f * S), Pos + FVector2D(Width - 20.f * S, Row / 2.f), Ink, FVector2D(1.0, 0.5));
	}
}

void SDartsHud::PaintUp(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// Who's up, the visit, the called shot, the darts in hand.
	const FDartsMatch& M = G.GetMatch();
	const FDartsPlayer* Up = M.ActivePlayer();
	if (!Up || M.Phase == EMatchPhase::Countdown)
	{
		return;
	}
	const FDartsSeat* Seat = G.SeatOf(*Up);
	const int32 Visit = FMath::Min((Up->DartsThrown - M.TurnDarts) / DartsPerRound, RoundCount - 1);
	const FVector2D TopRight(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction);
	P.Text(FText::Format(LOCTEXT("RoundOf", "ROUND {0} / {1}"), Visit + 1, RoundCount), KartUI::Font(EFont::Display, 60.f * S, 4.f * S), TopRight,
		KC::White, FVector2D(1.0, 0.0), 5.f * S);
	// The bounty chip: the called sector's number in the wedge's gold, "x2".
	const int32 Bounty = BountyForRound(Visit);
	const FVector2D ChipSize(300.f * S, 62.f * S);
	const FVector2D Chip(TopRight.X - ChipSize.X, TopRight.Y + 84.f * S);
	P.Card(Chip, ChipSize, KC::WithAlpha(KC::Night, 0.88f), 31.f * S, 4.f * S, KC::Sun, 3.f * S);
	P.Text(LOCTEXT("Bounty", "BOUNTY"), KartUI::Font(EFont::Label, 18.f * S), Chip + FVector2D(26.f * S, ChipSize.Y / 2.f), KC::Sun, FVector2D(0.0, 0.5));
	P.Text(FString::Printf(TEXT("%d"), Bounty), KartUI::Font(EFont::Display, 40.f * S), Chip + FVector2D(ChipSize.X - 82.f * S, ChipSize.Y / 2.f), KC::Sun, FVector2D(1.0, 0.5));
	P.Text(TEXT("×2"), KartUI::Font(EFont::Display, 28.f * S), Chip + FVector2D(ChipSize.X - 26.f * S, ChipSize.Y / 2.f), KC::White, FVector2D(1.0, 0.5));
	// Darts in hand, as three little darts.
	const int32 Left = FMath::Max(0, DartsPerRound - M.TurnDarts);
	for (int32 i = 0; i < DartsPerRound; ++i)
	{
		const FVector2D C(TopRight.X - 20.f * S - i * 36.f * S, TopRight.Y + 186.f * S);
		const bool bHave = i < Left;
		const FLinearColor Fill = bHave ? (Seat ? Seat->Color : KC::Sky) : KC::WithAlpha(KC::White, 0.2f);
		Poly.Reset();
		Poly.Add(C + FVector2D(0.f, -24.f * S));
		Poly.Add(C + FVector2D(5.5f * S, -6.f * S));
		Poly.Add(C + FVector2D(0.f, 24.f * S));
		Poly.Add(C + FVector2D(-5.5f * S, -6.f * S));
		P.Polygon(Poly, Fill);
		P.Box(C + FVector2D(-10.f * S, 10.f * S), FVector2D(20.f, 13.f) * S, Fill, 3.f * S);
	}
}

void SDartsHud::PaintCountdown(const FKartPainter& P, const ADartsGame& G, float S) const
{
	const FDartsMatch& M = G.GetMatch();
	// High in the frame, clear of the board.
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	if (M.Phase == EMatchPhase::Countdown)
	{
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 N = FMath::CeilToInt32(Remaining);
		const float Beat = static_cast<float>(1.0 - (N - Remaining));
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(Beat / 0.35f, 0.f, 1.f));
		P.Transformed(Centre, FMath::Max(0.01f, Pop)).Text(FString::FromInt(FMath::Max(1, N)), KartUI::Font(EFont::Display, 170.f * S, 8.f * S, KC::Night), Centre,
			KC::Sun, FVector2D(0.5, 0.5), 10.f * S);
		return;
	}
	const double Since = M.ElapsedMs / 1000.0;
	if (M.Phase == EMatchPhase::Playing && Since < 0.8)
	{
		const float A = FMath::Clamp(static_cast<float>((0.8 - Since) / 0.3), 0.f, 1.f);
		P.Faded(A).Text(LOCTEXT("Throw", "THROW!"), KartUI::Font(EFont::Display, 120.f * S, 7.f * S, KC::Night), Centre, KC::Mint, FVector2D(0.5, 0.5), 9.f * S);
	}
}

void SDartsHud::PaintCallouts(const FKartPainter& P, const ADartsGame& G, const FDartsView& View, float S) const
{
	// The landing callouts rise off the board; the game's own banner closes the match (the visit card replaces
	// the engine's round banner).
	for (const FDartsEffect& E : G.GetMatch().Effects)
	{
		if (E.Kind == EEffectKind::Round)
		{
			continue;
		}
		const FDartsSeat* Seat = G.SeatOf(E.PlayerId);
		const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / 0.25), 0.f, 1.f);
		if (E.Kind == EEffectKind::Finished)
		{
			FVector2D At01;
			if (!View.Project(G.BoardWorld(0.0, 1.62), At01))
			{
				continue;
			}
			const FVector2D At = At01 * P.Size();
			const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.3), 0.f, 1.f));
			const FKartPainter B = P.Transformed(At, FMath::Max(0.01f, In)).Faded(Out);
			const FString Label = FString::Printf(TEXT("%s  ·  %s"), Seat ? *Seat->Name : TEXT(""), *E.Label);
			const FSlateFontInfo Font = KartUI::Font(EFont::Display, 46.f * S, 3.f * S, KC::Night);
			const float W = static_cast<float>(FKartPainter::Measure(Label, Font).X) + 60.f * S;
			B.Card(At - FVector2D(W / 2.f, 34.f * S), FVector2D(W, 68.f * S), Lane, 34.f * S, 6.f * S, KC::White, 3.f * S);
			B.Text(Label, Font, At, KC::White, FVector2D(0.5, 0.5), 4.f * S);
			continue;
		}
		FVector2D At01;
		if (!View.Project(G.BoardWorld(E.X, E.Y), At01))
		{
			continue;
		}
		const double Rise = FMath::Lerp(0.26, 0.68, FMath::Clamp(Age / Life, 0.0, 1.0));
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Rise * 110.0) * S);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.22), 0.f, 1.f), 2.4f);
		const bool bBig = E.Kind == EEffectKind::Bull || E.Kind == EEffectKind::Bounty;
		const FKartPainter C = P.Transformed(At, FMath::Max(0.01f, In) * (bBig ? 1.3f : 1.f), bBig ? -4.f : 0.f).Faded(Out);
		const FString Label = E.Kind == EEffectKind::Miss ? FString(TEXT("MISS")) : (E.ScoreDelta > 0 ? FString::Printf(TEXT("%s  +%d"), *E.Label, E.ScoreDelta) : E.Label);
		C.Text(Label, KartUI::Font(EFont::Display, 56.f * S, 5.f * S, KC::Night), At, EffectColor(E, Lane), FVector2D(0.5, 0.5), 6.f * S);
	}
}

void SDartsHud::PaintVisit(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// The visit card: the call, the three darts, the visit's points and the round bonus, low and central.
	const FDartsVisitSummary& V = G.GetLastVisit();
	const double Age = G.GetClock() - V.At;
	const float A = DartsCardAlpha(Age, FMath::Min(DartsVisitCardSeconds, V.EndAt - V.At + 0.3));
	if (A <= 0.f || V.PlayerId.IsEmpty())
	{
		return;
	}
	const FDartsSeat* Seat = G.SeatOf(V.PlayerId);
	const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
	const bool bBig = V.Total >= 100 || V.Bounties >= DartsPerRound;
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.7f);
	const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.3), 0.f, 1.f));
	const FKartPainter C = P.Transformed(Centre, FMath::Max(0.01f, In)).Faded(A);
	const FVector2D Size(640.f * S, 176.f * S);
	const FVector2D Pos = Centre - Size / 2.f;
	C.Card(Pos, Size, KC::WithAlpha(KC::Night, 0.9f), 28.f * S, 8.f * S, bBig ? KC::Sun : Lane, 4.f * S);
	// The call, on a ribbon that overhangs the card's top.
	const FSlateFontInfo CallFont = KartUI::Font(EFont::Display, (bBig ? 54.f : 44.f) * S, 4.f * S, KC::Night);
	const float CallW = static_cast<float>(FKartPainter::Measure(V.Call.ToString(), CallFont).X) + 64.f * S;
	C.Card(FVector2D(Centre.X - CallW / 2.f, Pos.Y - 34.f * S), FVector2D(CallW, 68.f * S), bBig ? KC::Sun : Lane, 34.f * S, 6.f * S);
	C.Text(V.Call, CallFont, FVector2D(Centre.X, Pos.Y), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	// The three darts as chips, landing one after another.
	for (int32 i = 0; i < V.Labels.Num(); ++i)
	{
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.12 - i * 0.1) / 0.25), 0.f, 1.f));
		const FVector2D Chip(Pos.X + 40.f * S + i * 128.f * S, Pos.Y + 58.f * S);
		const FKartPainter D = C.Transformed(Chip + FVector2D(56.f, 42.f) * S, FMath::Max(0.01f, Pop));
		const bool bMiss = V.Points[i] == 0;
		D.Card(Chip, FVector2D(112.f, 84.f) * S, bMiss ? KC::WithAlpha(KC::NavyDim, 0.9f) : Lane, 16.f * S, 4.f * S);
		const float LabelSize = FMath::Min(26.f, 26.f * 96.f / FMath::Max(1.f, static_cast<float>(FKartPainter::Measure(V.Labels[i], KartUI::Font(EFont::Title, 26.f)).X))) * S;
		D.Text(V.Labels[i], KartUI::Font(EFont::Title, LabelSize), Chip + FVector2D(56.f * S, 28.f * S), KC::TextOn(bMiss ? KC::NavyDim : Lane), FVector2D(0.5, 0.5));
		D.Text(bMiss ? FString(TEXT("0")) : FString::Printf(TEXT("+%d"), V.Points[i]), KartUI::Font(EFont::Label, 20.f * S), Chip + FVector2D(56.f * S, 60.f * S),
			KC::WithAlpha(KC::TextOn(bMiss ? KC::NavyDim : Lane), 0.85f), FVector2D(0.5, 0.5));
	}
	// The total, and the round's bonus under it.
	const FVector2D TotalAt(Pos.X + Size.X - 34.f * S, Pos.Y + 92.f * S);
	C.Text(FString::FromInt(V.Total + V.Bonus), KartUI::Font(EFont::Display, 64.f * S, 3.f * S), TotalAt, bBig ? KC::Sun : KC::White, FVector2D(1.0, 0.5), 4.f * S);
	C.Text(V.Bonus > 0 ? FText::Format(LOCTEXT("VisitBonus", "incl. +{0} bonus"), V.Bonus) : LOCTEXT("VisitPoints", "points"), KartUI::Font(EFont::Label, 17.f * S),
		TotalAt + FVector2D(0.f, 46.f * S), KC::WithAlpha(KC::White, 0.75f), FVector2D(1.0, 0.5));
}

void SDartsHud::PaintGauge(const FKartPainter& P, const ADartsGame& G, const FDartsView& View, float S) const
{
	// The aim read: by the reticle, a vertical power gauge while the dart is gripped (the sweet band in mint), and a
	// quiet "hold" hint while it isn't.
	const FDartsSeat* Thrower = G.GetThrower();
	FVector2D Aim;
	if (!Thrower || Thrower->Kind != EThrowerKind::Local || !G.GetReticle(Aim))
	{
		return;
	}
	FVector2D At01;
	if (!View.Project(G.BoardWorld(Aim.X * AimReach, Aim.Y * AimReach), At01))
	{
		return;
	}
	const FVector2D At = At01 * P.Size();
	const FLocalThrow& L = G.GetLocal();
	if (!L.bGripped)
	{
		if (G.GetCoachAlpha() <= 0.f)
		{
			const float Pulse = 0.7f + 0.3f * FMath::Sin(static_cast<float>(G.GetClock()) * 4.f);
			const FKartPainter H = P.Faded(Pulse);
			const FVector2D PillSize(176.f * S, 34.f * S);
			const FVector2D PillAt = At + FVector2D(-PillSize.X / 2.f, 70.f * S);
			H.Card(PillAt, PillSize, KC::WithAlpha(KC::Night, 0.82f), 17.f * S, 3.f * S, Thrower->Color, 2.f * S);
			H.Text(LOCTEXT("GaugeHold", "HOLD TO THROW"), KartUI::Font(EFont::Label, 15.f * S), PillAt + PillSize / 2.f, KC::White, FVector2D(0.5, 0.5));
		}
		return;
	}
	const FVector2D Size(20.f * S, 170.f * S);
	const FVector2D Pos = At + FVector2D(70.f * S, -Size.Y / 2.f);
	P.Box(Pos - FVector2D(4.f, 4.f) * S, Size + FVector2D(8.f, 8.f) * S, KC::WithAlpha(KC::Night, 0.8f), 12.f * S);
	auto YOf = [&](double Power) { return Pos.Y + Size.Y * static_cast<float>(1.0 - Power); };
	P.Box(FVector2D(Pos.X, YOf(SweetSpot + 0.05)), FVector2D(Size.X, Size.Y * 0.1f), KC::WithAlpha(KC::Mint, 0.45f), 4.f * S);
	const float Power = static_cast<float>(FMath::Clamp(L.Power, 0.0, 1.0));
	const bool bSweet = FMath::Abs(Power - SweetSpot) < 0.05;
	const FLinearColor Fill = bSweet ? KC::Mint : (Power > SweetSpot ? KC::Hot : KC::Sun);
	P.Box(FVector2D(Pos.X, YOf(Power)), FVector2D(Size.X, Size.Y * Power), Fill, 8.f * S);
	// The marker, and what letting go now does.
	P.Box(FVector2D(Pos.X - 8.f * S, YOf(Power) - 2.f * S), FVector2D(Size.X + 16.f * S, 4.f * S), KC::White, 2.f * S);
	const FText Read = bSweet ? LOCTEXT("GaugeNow", "NOW!") : (Power > SweetSpot ? LOCTEXT("GaugeHigh", "HIGH") : LOCTEXT("GaugeLow", "LOW"));
	P.Text(Read, KartUI::Font(EFont::Display, (bSweet ? 30.f : 20.f) * S, 3.f * S), FVector2D(Pos.X + Size.X + 14.f * S, YOf(Power)), Fill, FVector2D(0.0, 0.5), 3.f * S);
}

void SDartsHud::PaintControls(const FKartPainter& P, const ADartsGame& G, float S) const
{
	const FDartsSeat* Thrower = G.GetThrower();
	const FDartsMatch& M = G.GetMatch();
	if (!Thrower || M.Phase != EMatchPhase::Playing)
	{
		return;
	}
	// Lower right, clear of the thrower and the board.
	const FVector2D Centre(P.Size().X * (1.f - KartUI::Metric::SafeFraction) - 400.f * S, P.Size().Y * 0.87f);
	const FText Up = FText::Format(LOCTEXT("Up", "{0} is up"), FText::FromString(Thrower->Name));
	if (Thrower->Kind != EThrowerKind::Local)
	{
		const FText Hint = Thrower->Kind == EThrowerKind::Phone ? LOCTEXT("PhoneHint", "Aim with your phone, pinch, flick and let go")
			: LOCTEXT("BotHint", "Lining up...");
		P.Card(Centre - FVector2D(300.f, 44.f) * S, FVector2D(600.f, 88.f) * S, KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, Thrower->Color, 3.f * S);
		P.Text(Up, KartUI::Font(EFont::Title, 32.f * S), Centre - FVector2D(0.f, 13.f * S), KC::White, FVector2D(0.5, 0.5));
		P.Text(Hint, KartUI::Font(EFont::Body, 21.f * S), Centre + FVector2D(0.f, 22.f * S), KC::WithAlpha(KC::White, 0.82f), FVector2D(0.5, 0.5));
		return;
	}
	const FLocalThrow& L = G.GetLocal();
	P.Card(Centre - FVector2D(400.f, 70.f) * S, FVector2D(800.f, 140.f) * S, KC::WithAlpha(KC::Night, 0.86f), 24.f * S, 6.f * S, Thrower->Color, 3.f * S);
	P.Text(Up, KartUI::Font(EFont::Title, 30.f * S), Centre + FVector2D(0.f, -40.f * S), KC::White, FVector2D(0.5, 0.5));
	const FText Hint = L.bGripped ? LOCTEXT("Release", "Let go in the green  ·  or flick the mouse up")
		: LOCTEXT("Grip", "Aim: WASD, stick or mouse  ·  Throw: hold SPACE, RT or click");
	P.Text(Hint, KartUI::Font(EFont::Body, 18.f * S), Centre + FVector2D(0.f, -6.f * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5));
	// The meter, with the sweet spot (a throw that lands on the aim) marked; under it drops, over it sails.
	const FVector2D BarPos = Centre + FVector2D(-260.f * S, 26.f * S);
	const FVector2D BarSize(520.f * S, 22.f * S);
	P.Box(BarPos, BarSize, KC::WithAlpha(KC::White, 0.12f), 11.f * S);
	P.Box(BarPos + FVector2D(BarSize.X * static_cast<float>(SweetSpot - 0.05), 0.f), FVector2D(BarSize.X * 0.1f, BarSize.Y), KC::WithAlpha(KC::Mint, 0.45f), 11.f * S);
	const float Power = static_cast<float>(L.bGripped ? L.Power : 0.0);
	if (Power > 0.f)
	{
		const bool bSweet = FMath::Abs(Power - SweetSpot) < 0.05;
		P.Gradient(BarPos, FVector2D(FMath::Max(BarSize.Y, BarSize.X * Power), BarSize.Y), KC::Sky, bSweet ? KC::Mint : (Power > SweetSpot ? KC::Hot : KC::Sun), false, 11.f * S);
	}
	P.Text(LOCTEXT("Low", "LOW"), KartUI::Font(EFont::Label, 16.f * S), BarPos - FVector2D(14.f * S, -BarSize.Y / 2.f), KC::Sun, FVector2D(1.0, 0.5));
	P.Text(LOCTEXT("High", "HIGH"), KartUI::Font(EFont::Label, 16.f * S), BarPos + FVector2D(BarSize.X + 14.f * S, BarSize.Y / 2.f), KC::Hot, FVector2D(0.0, 0.5));
}

void SDartsHud::PaintCoach(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// First local visit: three numbered steps down the left, the current one lit; they fade once the dart flies.
	const float A = G.GetCoachAlpha();
	const FDartsSeat* Thrower = G.GetThrower();
	if (A <= 0.f || !Thrower || Thrower->Kind != EThrowerKind::Local || G.GetMatch().Phase == EMatchPhase::Ended)
	{
		return;
	}
	const FLocalThrow& L = G.GetLocal();
	const FKartPainter F = P.Faded(A);
	struct FStep { FText Title; FText Keys; };
	const FStep Steps[] = {
		{ LOCTEXT("CoachAim", "AIM"), LOCTEXT("CoachAimKeys", "WASD  ·  left stick  ·  mouse") },
		{ LOCTEXT("CoachHold", "HOLD"), LOCTEXT("CoachHoldKeys", "SPACE  ·  RT  ·  left click") },
		{ LOCTEXT("CoachLetGo", "LET GO"), LOCTEXT("CoachLetGoKeys", "when the meter is in the green") },
	};
	const int32 Current = L.bGripped ? 2 : (L.Aim.IsNearlyZero(0.02) ? 0 : 1);
	const FVector2D Origin(P.Size().X * KartUI::Metric::SafeFraction, P.Size().Y * 0.36f);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Steps); ++i)
	{
		const bool bOn = i == Current;
		const float Pop = bOn ? 1.f + 0.04f * FMath::Sin(static_cast<float>(G.GetClock()) * 6.f) : 1.f;
		const FVector2D Pos = Origin + FVector2D(0.f, i * 100.f * S);
		const FKartPainter R = F.Transformed(Pos + FVector2D(0.f, 44.f * S), Pop);
		R.Card(Pos, FVector2D(470.f, 88.f) * S, bOn ? KC::Paper : KC::WithAlpha(KC::Night, 0.82f), 22.f * S, 6.f * S, bOn ? KC::Mint : FLinearColor::Transparent, bOn ? 3.f * S : 0.f);
		const FLinearColor Ink = bOn ? KC::Night : KC::White;
		R.Circle(Pos + FVector2D(46.f * S, 44.f * S), 26.f * S, bOn ? KC::Mint : KC::WithAlpha(KC::White, 0.15f));
		R.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 32.f * S), Pos + FVector2D(46.f * S, 44.f * S), bOn ? KC::Night : KC::White, FVector2D(0.5, 0.5));
		R.Text(Steps[i].Title, KartUI::Font(EFont::Display, 30.f * S), Pos + FVector2D(90.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		R.Text(Steps[i].Keys, KartUI::Font(EFont::Body, 19.f * S), Pos + FVector2D(90.f * S, 62.f * S), KC::WithAlpha(Ink, 0.8f), FVector2D(0.0, 0.5));
	}
}

void SDartsHud::PaintIntro(const FKartPainter& P, const ADartsGame& G, float S) const
{
	const double Age = G.GetStageAge();
	const double Life = ADartsGame::IntroSeconds;
	const float A = FMath::Clamp(static_cast<float>(Age / 0.4), 0.f, 1.f) * FMath::Clamp(static_cast<float>((Life - Age) / 0.35), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.6f);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.45), 0.f, 1.f));
	F.Transformed(Centre - FVector2D(0.f, 60.f * S), FMath::Max(0.01f, Pop)).Text(LOCTEXT("Title", "BULLSEYE BASH"), KartUI::Font(EFont::Display, 132.f * S, 7.f * S),
		Centre - FVector2D(0.f, 60.f * S), KC::Hot, FVector2D(0.5, 0.5), 9.f * S);
	FString Names;
	for (const FDartsSeat& Seat : G.GetSeats())
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
	}
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(LOCTEXT("Rules", "Three darts a visit, three visits. Hit the called sector for double points."), KartUI::Font(EFont::Body, 26.f * S, 2.f * S),
		Centre + FVector2D(0.f, 102.f * S), KC::WithAlpha(KC::White, 0.92f), FVector2D(0.5, 0.5), 3.f * S);
}

void SDartsHud::PaintStandings(const FKartPainter& P, const ADartsGame& G, float S) const
{
	const FDartsMatch& M = G.GetMatch();
	const TArray<int32>& Order = G.GetStandingsOrder();
	const double Age = G.GetFinishedAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	F.Text(LOCTEXT("Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		if (!M.Players.IsValidIndex(Order[i]))
		{
			continue;
		}
		const FDartsPlayer& Player = M.Players[Order[i]];
		const FDartsSeat* Seat = G.SeatOf(Player);
		const float Delay = 0.3f + i * 0.18f;
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - Delay) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X / 2.f - 400.f * S, P.Size().Y * 0.3f + i * 104.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(400.f, 44.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = M.WinnerIds.Contains(Player.Id);
		Row.Card(Pos, FVector2D(800.f, 88.f) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.88f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::Sky, 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 52.f * S), Pos + FVector2D(48.f * S, 44.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Player.Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(FText::Format(LOCTEXT("StandingStats", "best round {0}  ·  {1} bulls  ·  {2} bounties"), Player.BestRound, Player.Bulls, Player.Bounties),
			KartUI::Font(EFont::Body, 19.f * S), Pos + FVector2D(100.f * S, 64.f * S), KC::WithAlpha(Ink, 0.78f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Player.Score), KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(770.f * S, 44.f * S), Ink, FVector2D(1.0, 0.5));
	}
}

#undef LOCTEXT_NAMESPACE
