#include "Games/Darts/SDartsHud.h"

#include "Games/Darts/DartsGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Darts;
namespace KC = KartUI::Color;
using KartUI::EFont;

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
	switch (G->GetStage())
	{
	case EDartsStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case EDartsStage::Finished:
		PaintCallouts(P, *G, S);
		PaintStandings(P, *G, S);
		break;
	default:
		PaintBoard(P, *G, S);
		PaintUp(P, *G, S);
		PaintCallouts(P, *G, S);
		PaintControls(P, *G, S);
		PaintCountdown(P, *G, S);
		break;
	}
	return Layer;
}

void SDartsHud::PaintBoard(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// The scoreboard: a row per thrower, nine dart slots grouped in visits, the running score.
	const FDartsMatch& M = G.GetMatch();
	const FDartsPlayer* Up = M.ActivePlayer();
	const FVector2D Origin = P.Size() * KartUI::Metric::SafeFraction;
	const float Row = 58.f * S, NameW = 236.f * S, Slot = 30.f * S, TotalW = 96.f * S;
	for (int32 r = 0; r < M.Players.Num(); ++r)
	{
		const FDartsPlayer& Player = M.Players[r];
		const FDartsSeat* Seat = G.SeatOf(Player.Id);
		const FLinearColor Color = Seat ? Seat->Color : KC::Sky;
		const bool bUp = Up && Up->Id == Player.Id;
		const FVector2D Pos = Origin + FVector2D(0.f, r * (Row + 8.f * S));
		const float Width = NameW + Slot * 9.f + 2.f * 14.f * S + TotalW;
		P.Card(Pos, FVector2D(Width, Row), bUp ? KC::Paper : KC::WithAlpha(KC::Night, 0.78f), 14.f * S, 5.f * S,
			bUp ? Color : FLinearColor::Transparent, bUp ? 3.f * S : 0.f);
		const FLinearColor Ink = bUp ? KC::Night : KC::White;
		P.Circle(Pos + FVector2D(26.f * S, Row / 2.f), 10.f * S, Color, KC::Night, 2.f * S);
		FString Name = Player.Name;
		float NameSize = 24.f * S;
		const float Room = NameW - 56.f * S;
		if (FKartPainter::Measure(Name, KartUI::Font(EFont::Title, NameSize)).X > Room)
		{
			NameSize = FMath::Max(15.f * S, NameSize * Room / static_cast<float>(FKartPainter::Measure(Name, KartUI::Font(EFont::Title, NameSize)).X));
			while (Name.Len() > 2 && FKartPainter::Measure(Name, KartUI::Font(EFont::Title, NameSize)).X > Room)
			{
				Name = Name.LeftChop(2) + TEXT(".");
			}
		}
		P.Text(Name, KartUI::Font(EFont::Title, NameSize), Pos + FVector2D(46.f * S, Row / 2.f), Ink, FVector2D(0.0, 0.5));
		// Nine darts, three visits: filled by points, ringed by the bounty, empty while to come.
		for (int32 d = 0; d < DartsPerPlayer; ++d)
		{
			const int32 Visit = d / DartsPerRound;
			const FVector2D C = Pos + FVector2D(NameW + d * Slot + Visit * 14.f * S + Slot / 2.f, Row / 2.f);
			if (Player.Landings.IsValidIndex(d))
			{
				const FDartLanding& L = Player.Landings[d];
				const FLinearColor Fill = L.ScoreDelta == 0 ? KC::WithAlpha(KC::NavyDim, 0.6f) : (L.Hit.Ring == ERing::Bull ? KC::Sun : Color);
				P.Circle(C, 12.f * S, Fill, L.bBounty ? KC::Sun : KC::Night, (L.bBounty ? 3.f : 1.5f) * S);
				P.Text(L.ScoreDelta == 0 ? FString(TEXT("–")) : FString::FromInt(L.ScoreDelta), KartUI::Font(EFont::Label, (L.ScoreDelta >= 100 ? 8.5f : 12.f) * S), C,
					KC::TextOn(Fill), FVector2D(0.5, 0.5));
			}
			else
			{
				P.Circle(C, 10.f * S, FLinearColor::Transparent, KC::WithAlpha(bUp ? KC::Night : KC::White, 0.3f), 2.f * S);
			}
		}
		P.Text(FString::FromInt(Player.Score), KartUI::Font(EFont::Display, 36.f * S), Pos + FVector2D(Width - 18.f * S, Row / 2.f), Ink, FVector2D(1.0, 0.5));
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
	const FDartsSeat* Seat = G.SeatOf(Up->Id);
	const int32 Visit = FMath::Min((Up->DartsThrown - M.TurnDarts) / DartsPerRound, RoundCount - 1);
	const FVector2D TopRight(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction);
	P.Text(FString::Printf(TEXT("ROUND %d / %d"), Visit + 1, RoundCount), KartUI::Font(EFont::Display, 64.f * S, 4.f * S), TopRight,
		KC::White, FVector2D(1.0, 0.0), 5.f * S);
	// The bounty chip: the sector's number in the wedge's gold, "x2".
	const int32 Bounty = BountyForRound(Visit);
	const FVector2D Chip(TopRight.X - 250.f * S, TopRight.Y + 86.f * S);
	P.Card(Chip, FVector2D(250.f, 58.f) * S, KC::WithAlpha(KC::Night, 0.82f), 29.f * S, 4.f * S, KC::Sun, 3.f * S);
	P.Text(NSLOCTEXT("Darts", "Bounty", "BOUNTY"), KartUI::Font(EFont::Label, 18.f * S), Chip + FVector2D(22.f * S, 29.f * S), KC::Sun, FVector2D(0.0, 0.5));
	P.Text(FString::Printf(TEXT("%d ×2"), Bounty), KartUI::Font(EFont::Display, 34.f * S), Chip + FVector2D(228.f * S, 29.f * S), KC::White, FVector2D(1.0, 0.5));
	// Darts in hand, as three little darts.
	const int32 Left = FMath::Max(0, DartsPerRound - M.TurnDarts);
	for (int32 i = 0; i < DartsPerRound; ++i)
	{
		const FVector2D C(TopRight.X - 20.f * S - i * 34.f * S, TopRight.Y + 176.f * S);
		const bool bHave = i < Left;
		const FLinearColor Fill = bHave ? (Seat ? Seat->Color : KC::Sky) : KC::WithAlpha(KC::White, 0.2f);
		P.Polygon({ C + FVector2D(0.f, -22.f * S), C + FVector2D(5.f * S, -6.f * S), C + FVector2D(0.f, 22.f * S), C + FVector2D(-5.f * S, -6.f * S) }, Fill);
		P.Box(C + FVector2D(-9.f * S, 10.f * S), FVector2D(18.f, 12.f) * S, Fill, 3.f * S);
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
		P.Faded(A).Text(NSLOCTEXT("Darts", "Throw", "THROW!"), KartUI::Font(EFont::Display, 120.f * S, 7.f * S, KC::Night), Centre, KC::Mint, FVector2D(0.5, 0.5), 9.f * S);
	}
}

void SDartsHud::PaintCallouts(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// The landing callouts rise off the board; round banners sit above it (DartsScene popup and banner).
	for (const FDartsEffect& E : G.GetMatch().Effects)
	{
		const FDartsSeat* Seat = G.SeatOf(E.PlayerId);
		const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / 0.25), 0.f, 1.f);
		if (E.Kind == EEffectKind::Round || E.Kind == EEffectKind::Finished)
		{
			FVector2D At01;
			if (!G.ScreenOf(G.BoardWorld(0.0, 1.62), At01))
			{
				continue;
			}
			const FVector2D At = At01 * P.Size();
			const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.3), 0.f, 1.f));
			const FKartPainter B = P.Transformed(At, FMath::Max(0.01f, In)).Faded(Out);
			const FString Label = E.Kind == EEffectKind::Finished ? FString::Printf(TEXT("%s  ·  %s"), Seat ? *Seat->Name : TEXT(""), *E.Label) : E.Label;
			const FSlateFontInfo Font = KartUI::Font(EFont::Display, 46.f * S, 3.f * S, KC::Night);
			const float W = static_cast<float>(FKartPainter::Measure(Label, Font).X) + 60.f * S;
			B.Card(At - FVector2D(W / 2.f, 34.f * S), FVector2D(W, 68.f * S), E.ScoreDelta > 0 || E.Kind == EEffectKind::Finished ? Lane : KC::WithAlpha(KC::Night, 0.85f),
				34.f * S, 6.f * S, KC::White, 3.f * S);
			B.Text(Label, Font, At, KC::White, FVector2D(0.5, 0.5), 4.f * S);
			continue;
		}
		FVector2D At01;
		if (!G.ScreenOf(G.BoardWorld(E.X, E.Y), At01))
		{
			continue;
		}
		const double Rise = FMath::Lerp(0.26, 0.68, FMath::Clamp(Age / Life, 0.0, 1.0));
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Rise * 110.0) * S);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.22), 0.f, 1.f), 2.4f);
		const bool bBig = E.Kind == EEffectKind::Bull || E.Kind == EEffectKind::Bounty;
		const FKartPainter C = P.Transformed(At, FMath::Max(0.01f, In) * (bBig ? 1.25f : 1.f), bBig ? -4.f : 0.f).Faded(Out);
		const FString Label = E.Kind == EEffectKind::Miss ? FString(TEXT("MISS")) : (E.ScoreDelta > 0 ? FString::Printf(TEXT("%s  +%d"), *E.Label, E.ScoreDelta) : E.Label);
		C.Text(Label, KartUI::Font(EFont::Display, 54.f * S, 4.f * S, KC::Night), At, EffectColor(E, Lane), FVector2D(0.5, 0.5), 6.f * S);
	}
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
	const FVector2D Centre(P.Size().X * (1.f - KartUI::Metric::SafeFraction) - 420.f * S, P.Size().Y * 0.86f);
	const FText Up = FText::Format(NSLOCTEXT("Darts", "Up", "{0} is up"), FText::FromString(Thrower->Name));
	if (Thrower->Kind != EThrowerKind::Local)
	{
		const FText Hint = Thrower->Kind == EThrowerKind::Phone ? NSLOCTEXT("Darts", "PhoneHint", "Aim with your phone, pinch, flick and let go")
			: NSLOCTEXT("Darts", "BotHint", "Lining up...");
		P.Card(Centre - FVector2D(330.f, 46.f) * S, FVector2D(660.f, 92.f) * S, KC::WithAlpha(KC::Night, 0.82f), 22.f * S, 6.f * S, Thrower->Color, 3.f * S);
		P.Text(Up, KartUI::Font(EFont::Title, 32.f * S), Centre - FVector2D(0.f, 14.f * S), KC::White, FVector2D(0.5, 0.5));
		P.Text(Hint, KartUI::Font(EFont::Body, 20.f * S), Centre + FVector2D(0.f, 22.f * S), KC::WithAlpha(KC::White, 0.8f), FVector2D(0.5, 0.5));
		return;
	}
	const FLocalThrow& L = G.GetLocal();
	P.Card(Centre - FVector2D(420.f, 70.f) * S, FVector2D(840.f, 140.f) * S, KC::WithAlpha(KC::Night, 0.82f), 24.f * S, 6.f * S, Thrower->Color, 3.f * S);
	P.Text(Up, KartUI::Font(EFont::Title, 30.f * S), Centre + FVector2D(0.f, -40.f * S), KC::White, FVector2D(0.5, 0.5));
	const FText Hint = L.bGripped ? NSLOCTEXT("Darts", "Release", "Let go to throw  ·  or flick the mouse up")
		: NSLOCTEXT("Darts", "Grip", "WASD, stick or mouse: aim  ·  hold SPACE, RT or click to throw");
	P.Text(Hint, KartUI::Font(EFont::Body, 19.f * S), Centre + FVector2D(0.f, -6.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5));
	// The meter, with the sweet spot (a throw that lands on the aim) marked; under it drops, over it sails.
	const FVector2D BarPos = Centre + FVector2D(-260.f * S, 26.f * S);
	const FVector2D BarSize(520.f * S, 22.f * S);
	P.Box(BarPos, BarSize, KC::WithAlpha(KC::White, 0.12f), 11.f * S);
	P.Box(BarPos + FVector2D(BarSize.X * static_cast<float>(SweetSpot - 0.05), 0.f), FVector2D(BarSize.X * 0.1f, BarSize.Y), KC::WithAlpha(KC::Mint, 0.35f), 11.f * S);
	const float Power = static_cast<float>(L.bGripped ? L.Power : 0.0);
	if (Power > 0.f)
	{
		const bool bSweet = FMath::Abs(Power - SweetSpot) < 0.05;
		P.Gradient(BarPos, FVector2D(FMath::Max(BarSize.Y, BarSize.X * Power), BarSize.Y), KC::Sky, bSweet ? KC::Mint : (Power > SweetSpot ? KC::Hot : KC::Sun), false, 11.f * S);
	}
	P.Text(NSLOCTEXT("Darts", "Low", "LOW"), KartUI::Font(EFont::Label, 16.f * S), BarPos - FVector2D(14.f * S, -BarSize.Y / 2.f), KC::Sun, FVector2D(1.0, 0.5));
	P.Text(NSLOCTEXT("Darts", "High", "HIGH"), KartUI::Font(EFont::Label, 16.f * S), BarPos + FVector2D(BarSize.X + 14.f * S, BarSize.Y / 2.f), KC::Hot, FVector2D(0.0, 0.5));
}

void SDartsHud::PaintIntro(const FKartPainter& P, const ADartsGame& G, float S) const
{
	const double Age = G.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>((3.0 - Age) / 0.4), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.6f);
	F.Text(NSLOCTEXT("Darts", "Title", "BULLSEYE BASH"), KartUI::Font(EFont::Display, 132.f * S, 7.f * S), Centre - FVector2D(0.f, 60.f * S), KC::Hot, FVector2D(0.5, 0.5), 9.f * S);
	FString Names;
	for (const FDartsSeat& Seat : G.GetSeats())
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
	}
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(NSLOCTEXT("Darts", "Rules", "Three darts a visit, three visits. Hit the called sector for double points."), KartUI::Font(EFont::Body, 24.f * S),
		Centre + FVector2D(0.f, 100.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5), 3.f * S);
}

void SDartsHud::PaintStandings(const FKartPainter& P, const ADartsGame& G, float S) const
{
	const TArray<const FDartsPlayer*> Order = Leaderboard(G.GetMatch());
	const double Age = G.GetFinishedAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	F.Text(NSLOCTEXT("Darts", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FDartsPlayer& Player = *Order[i];
		const FDartsSeat* Seat = G.SeatOf(Player.Id);
		const float Delay = 0.3f + i * 0.18f;
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - Delay) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X / 2.f - 400.f * S, P.Size().Y * 0.3f + i * 104.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(400.f, 44.f) * S, FMath::Max(0.01f, In));
		const bool bWinner = G.GetMatch().WinnerIds.Contains(Player.Id);
		Row.Card(Pos, FVector2D(800.f, 88.f) * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::Sky, 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 52.f * S), Pos + FVector2D(48.f * S, 44.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Player.Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(FString::Printf(TEXT("best round %d  ·  %d bulls  ·  %d bounties"), Player.BestRound, Player.Bulls, Player.Bounties),
			KartUI::Font(EFont::Body, 18.f * S), Pos + FVector2D(100.f * S, 64.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Player.Score), KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(770.f * S, 44.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
