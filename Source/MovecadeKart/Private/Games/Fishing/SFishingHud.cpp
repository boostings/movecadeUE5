#include "Games/Fishing/SFishingHud.h"

#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingLake.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Fishing;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	const FLinearColor& BandColor(int32 Band)
	{
		static const FLinearColor Colors[] = { FLinearColor::FromSRGBColor(FColor(0x6f, 0xd8, 0xe8)), FLinearColor::FromSRGBColor(FColor(0x5f, 0xd0, 0x6a)),
			FLinearColor::FromSRGBColor(FColor(0xff, 0xd2, 0x3c)), FLinearColor::FromSRGBColor(FColor(0xff, 0x5a, 0x4a)) };
		return Colors[FMath::Clamp(Band, 0, 3)];
	}

	/** Stars for how special a catch is: the species' rarity, a heavy one of its kind. */
	int32 Stars(const FSpecies& S, int32 WeightG)
	{
		if (S.Kind == EKind::Junk)
		{
			return 0;
		}
		if (S.Kind == EKind::Trophy)
		{
			return 5;
		}
		const double Frac = FMath::Clamp((WeightG / 1000.0 - S.MinKg) / FMath::Max(0.01, S.MaxKg - S.MinKg), 0.0, 1.0);
		const int32 Base = S.Strength >= 1.0 ? 3 : (S.Strength >= 0.7 ? 2 : 1);
		return FMath::Clamp(Base + (Frac > 0.66 ? 1 : 0), 1, 4);
	}

	FString Kg(int32 Grams)
	{
		return FString::Printf(TEXT("%.2f kg"), Grams / 1000.0);
	}

	void Star(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill)
	{
		TArray<FVector2D> Points;
		for (int32 k = 0; k < 10; ++k)
		{
			const float A = -HALF_PI + PI * k / 5.f;
			const float Rk = k % 2 == 0 ? R : R * 0.45f;
			Points.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rk);
		}
		P.Polygon(Points, Fill);
	}
}

void SFishingHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SFishingHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const AFishingGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	switch (G->GetStage())
	{
	case EFishingStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case EFishingStage::Finished:
		PaintCatchCards(P, *G, S);
		PaintStandings(P, *G, S);
		break;
	default:
		PaintPins(P, *G, S);
		PaintClock(P, *G, S);
		PaintLanes(P, *G, S);
		PaintCallouts(P, *G, S);
		PaintCatchCards(P, *G, S);
		PaintControls(P, *G, S);
		PaintCountdown(P, *G, S);
		break;
	}
	return Layer;
}

void SFishingHud::PaintClock(const FKartPainter& P, const AFishingGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * KartUI::Metric::SafeFraction);
	const int32 Seconds = FMath::CeilToInt32(M.TimeRemainingMs / 1000.0);
	const bool bLate = Seconds <= 15 && M.Phase == EMatchPhase::Playing;
	const float Pulse = bLate ? 1.f + 0.06f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 6.f)) : 1.f;
	P.Card(Top - FVector2D(110.f, 0.f) * S, FVector2D(220.f, 104.f) * S, KC::WithAlpha(KC::Night, 0.78f), 26.f * S, 6.f * S, bLate ? KC::Hot : KC::Sun, 3.f * S);
	P.Transformed(Top + FVector2D(0.f, 52.f * S), Pulse).Text(FString::Printf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60), KartUI::Font(EFont::Display, 56.f * S, 3.f * S, KC::Night),
		Top + FVector2D(0.f, 52.f * S), bLate ? KC::Hot : KC::White, FVector2D(0.5, 0.5), 4.f * S);
	// The round's biggest fish so far.
	if (M.RecordG > 0)
	{
		const FAngler* Holder = M.Find(M.RecordHolder);
		const FString Line = FString::Printf(TEXT("RECORD  %s  %s"), *Kg(M.RecordG), Holder ? *Holder->Name : TEXT(""));
		const FSlateFontInfo Font = KartUI::Font(EFont::Label, 17.f * S);
		const float W = static_cast<float>(FKartPainter::Measure(Line, Font).X) + 44.f * S;
		const FVector2D C = Top + FVector2D(0.f, 128.f * S);
		P.Box(C - FVector2D(W / 2.f, 17.f * S), FVector2D(W, 34.f * S), KC::WithAlpha(KC::Night, 0.72f), 17.f * S, KC::Gold, 2.f * S);
		Star(P, C - FVector2D(W / 2.f - 20.f * S, 0.f), 9.f * S, KC::Gold);
		P.Text(Line, Font, C + FVector2D(8.f * S, 0.f), KC::Gold, FVector2D(0.5, 0.5));
	}
}

void SFishingHud::PaintPins(const FKartPainter& P, const AFishingGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	for (int32 i = 0; i < M.Anglers.Num(); ++i)
	{
		const FAngler& A = M.Anglers[i];
		if (A.State != EAnglerState::Waiting && A.State != EAnglerState::Bite && A.State != EAnglerState::Fighting)
		{
			continue;
		}
		FVector2D At01;
		if (!G.ScreenOf(G.BobberOf(i) + FVector(0.0, 0.0, 30.0), At01))
		{
			continue;
		}
		const FLinearColor Color = AFishingLake::LaneColor(i);
		const FVector2D At = At01 * P.Size();
		const bool bBite = A.State == EAnglerState::Bite;
		const float Hop = bBite ? 10.f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 14.f)) * S : 0.f;
		const float R = (bBite ? 24.f : 15.f) * S;
		const FVector2D C = At - FVector2D(0.f, 26.f * S + R + Hop);
		P.Polygon({ At - FVector2D(0.f, 8.f * S + Hop), C + FVector2D(-R * 0.6f, R * 0.5f), C + FVector2D(R * 0.6f, R * 0.5f) }, bBite ? KC::Sun : Color);
		P.Circle(C, R, bBite ? KC::Sun : Color, KC::Night, 3.f * S);
		if (bBite)
		{
			P.Text(TEXT("!"), KartUI::Font(EFont::Display, 36.f * S), C + FVector2D(0.f, 1.f * S), KC::Night, FVector2D(0.5, 0.5));
		}
		else
		{
			P.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Label, 17.f * S), C, KC::TextOn(Color), FVector2D(0.5, 0.5));
		}
	}
}

void SFishingHud::PaintLanes(const FKartPainter& P, const AFishingGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 N = M.Anglers.Num();
	const float Gap = 18.f * S;
	const float Width = FMath::Min(440.f * S, (P.Size().X * 0.94f - Gap * (N - 1)) / FMath::Max(1, N));
	const float Height = 170.f * S;
	const float Left = (P.Size().X - (Width * N + Gap * (N - 1))) / 2.f;
	const float Clock = static_cast<float>(G.GetClock());
	for (int32 i = 0; i < N; ++i)
	{
		const FAngler& A = M.Anglers[i];
		const FLinearColor Color = AFishingLake::LaneColor(i);
		const FVector2D Pos(Left + i * (Width + Gap), P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height);
		const bool bFight = A.State == EAnglerState::Fighting;
		const bool bDanger = bFight && (A.Fight.StrainMs > 80.0 || A.Fight.SlackMs > 600.0);
		const float Flash = bDanger ? 0.5f + 0.5f * FMath::Sin(Clock * 16.f) : 0.f;
		P.Card(Pos, FVector2D(Width, Height), KC::WithAlpha(KC::Night, 0.84f), 22.f * S, 6.f * S, bDanger ? FMath::Lerp(Color, KC::Hot, Flash) : Color, (bDanger ? 4.f : 2.5f) * S);
		P.Circle(Pos + FVector2D(30.f * S, 32.f * S), 12.f * S, Color, KC::Night, 2.f * S);
		P.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Label, 14.f * S), Pos + FVector2D(30.f * S, 32.f * S), KC::TextOn(Color), FVector2D(0.5, 0.5));
		P.Text(A.Name, KartUI::Font(EFont::Title, 25.f * S), Pos + FVector2D(52.f * S, 32.f * S), KC::White, FVector2D(0.0, 0.5));
		P.Text(Kg(A.Score), KartUI::Font(EFont::Display, 38.f * S), Pos + FVector2D(Width - 20.f * S, 34.f * S), KC::White, FVector2D(1.0, 0.5), 3.f * S);
		FString Sub = FString::Printf(TEXT("%d %s"), A.Catches, A.Catches == 1 ? TEXT("fish") : TEXT("fish"));
		if (A.BiggestG > 0)
		{
			Sub += FString::Printf(TEXT("  ·  best %s"), *Kg(A.BiggestG));
		}
		P.Text(Sub, KartUI::Font(EFont::Label, 16.f * S), Pos + FVector2D(24.f * S, 68.f * S), KC::WithAlpha(KC::White, 0.78f), FVector2D(0.0, 0.5));
		if (A.Combo >= 2)
		{
			const FString Chip = FString::Printf(TEXT("COMBO x%.2f"), ComboMultiplier(A.Combo));
			const FSlateFontInfo Font = KartUI::Font(EFont::Label, 15.f * S);
			const float W = static_cast<float>(FKartPainter::Measure(Chip, Font).X) + 20.f * S;
			const FVector2D ChipPos(Pos.X + Width - 18.f * S - W, Pos.Y + 56.f * S);
			P.Box(ChipPos, FVector2D(W, 24.f * S), A.Combo >= 4 ? KC::Sun : KC::Mint, 12.f * S);
			P.Text(Chip, Font, ChipPos + FVector2D(W / 2.f, 12.f * S), KC::Night, FVector2D(0.5, 0.5));
		}
		// The working area: a meter (cast or tension) or what to do now.
		const FVector2D Bar = Pos + FVector2D(22.f * S, 98.f * S);
		const float BarW = Width - 44.f * S;
		const double Charge = G.ChargeOf(i);
		if (bFight)
		{
			const FFight& F = A.Fight;
			const double Max = 1.2;
			auto X = [&](double V) { return Bar.X + static_cast<float>(FMath::Clamp(V / Max, 0.0, 1.0)) * BarW; };
			const float H = 22.f * S;
			P.Box(Bar, FVector2D(BarW, H), KC::WithAlpha(KC::Sky, 0.35f), 11.f * S);
			P.Box(FVector2D(X(GreenLow), Bar.Y), FVector2D(X(GreenHigh) - X(GreenLow), H), KC::WithAlpha(KC::Mint, 0.85f), 4.f * S);
			P.Box(FVector2D(X(GreenHigh), Bar.Y), FVector2D(X(RedLine) - X(GreenHigh), H), KC::WithAlpha(KC::Sun, 0.85f), 4.f * S);
			P.Box(FVector2D(X(RedLine), Bar.Y), FVector2D(Bar.X + BarW - X(RedLine), H), KC::WithAlpha(KC::Hot, 0.9f), 11.f * S);
			const bool bRun = F.WarnMs > 0.0 || F.BurstMs > 0.0;
			const float Needle = X(F.Tension);
			const float Wobble = bRun ? 2.f * FMath::Sin(Clock * 40.f) * S : 0.f;
			P.Box(FVector2D(Needle - 3.f * S + Wobble, Bar.Y - 7.f * S), FVector2D(6.f * S, H + 14.f * S), KC::White, 3.f * S, KC::Night, 2.f * S);
			// The two ways to lose it, filling up underneath.
			const float Under = Bar.Y + H + 12.f * S;
			const float Snap = static_cast<float>(FMath::Clamp(F.StrainMs / SnapStrainMs, 0.0, 1.0));
			const float Slack = static_cast<float>(FMath::Clamp(F.SlackMs / EscapeSlackMs, 0.0, 1.0));
			if (Snap > 0.f)
			{
				P.Box(FVector2D(Bar.X + BarW * (1.f - Snap), Under), FVector2D(BarW * Snap, 6.f * S), KC::Hot, 3.f * S);
			}
			if (Slack > 0.f)
			{
				P.Box(FVector2D(Bar.X, Under), FVector2D(BarW * Slack, 6.f * S), KC::Sky, 3.f * S);
			}
			FText Hint = NSLOCTEXT("Fishing", "Reel", "REEL!  keep it in the green");
			FLinearColor Ink = KC::Mint;
			if (bRun)
			{
				Hint = NSLOCTEXT("Fishing", "Run", "IT'S RUNNING!  ease off");
				Ink = KC::Sun;
			}
			else if (Snap > 0.15f)
			{
				Hint = NSLOCTEXT("Fishing", "TooTight", "TOO TIGHT!  stop reeling");
				Ink = KC::Hot;
			}
			else if (Slack > 0.25f)
			{
				Hint = NSLOCTEXT("Fishing", "TooSlack", "SLACK!  reel faster");
				Ink = KC::Sky;
			}
			P.Text(Hint, KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X, Under + 22.f * S), Ink, FVector2D(0.0, 0.5));
			P.Text(FString::Printf(TEXT("%.0f m"), F.DistanceM), KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X + BarW, Under + 22.f * S), KC::WithAlpha(KC::White, 0.8f), FVector2D(1.0, 0.5));
		}
		else if (Charge >= 0.0)
		{
			// The cast meter over the bands, the needle at the power.
			const float H = 22.f * S;
			const double Edges[] = { MinCastM, ShallowsEndM, ReedsEndM, DeepEndM, MaxCastM };
			for (int32 b = 0; b < 4; ++b)
			{
				const float X0 = Bar.X + static_cast<float>((Edges[b] - MinCastM) / (MaxCastM - MinCastM)) * BarW;
				const float X1 = Bar.X + static_cast<float>((Edges[b + 1] - MinCastM) / (MaxCastM - MinCastM)) * BarW;
				P.Box(FVector2D(X0 + 1.f * S, Bar.Y), FVector2D(X1 - X0 - 2.f * S, H), KC::WithAlpha(BandColor(b), 0.8f), 5.f * S);
			}
			const float Needle = Bar.X + static_cast<float>(Charge) * BarW;
			P.Box(FVector2D(Bar.X, Bar.Y + H * 0.35f), FVector2D(Needle - Bar.X, H * 0.3f), KC::White, 3.f * S);
			P.Box(FVector2D(Needle - 3.f * S, Bar.Y - 7.f * S), FVector2D(6.f * S, H + 14.f * S), KC::White, 3.f * S, KC::Night, 2.f * S);
			const EBand Band = BandAt(CastDistance(Charge));
			P.Text(FString::Printf(TEXT("%s  ·  %.0f m"), BandName(Band), CastDistance(Charge)), KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X, Bar.Y + H + 30.f * S),
				BandColor(static_cast<int32>(Band)), FVector2D(0.0, 0.5));
			P.Text(NSLOCTEXT("Fishing", "LetGo", "let go to cast"), KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X + BarW, Bar.Y + H + 30.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(1.0, 0.5));
		}
		else
		{
			FText Line;
			FLinearColor Ink = KC::White;
			float Size = 26.f;
			switch (A.State)
			{
			case EAnglerState::Idle: Line = NSLOCTEXT("Fishing", "CastNow", "CAST!"); Ink = Color; break;
			case EAnglerState::Casting: Line = FText::FromString(FString::Printf(TEXT("%.0f m out"), A.CastM)); Ink = BandColor(static_cast<int32>(BandAt(A.CastM))); break;
			case EAnglerState::Waiting: Line = FText::FromString(FString::Printf(TEXT("waiting...  %s"), BandName(BandAt(A.LureM)))); Ink = KC::WithAlpha(KC::White, 0.8f); Size = 22.f; break;
			case EAnglerState::Bite: Line = NSLOCTEXT("Fishing", "BiteNow", "BITE! HOOK IT!"); Ink = FMath::Lerp(KC::Sun, KC::White, 0.5f + 0.5f * FMath::Sin(Clock * 20.f)); Size = 32.f; break;
			case EAnglerState::Landed: Line = FText::FromString(Species()[A.LastCatch.Species].Name); Ink = KC::Sun; break;
			case EAnglerState::Lost:
				Line = A.LastLoss == ELoss::Snap ? NSLOCTEXT("Fishing", "Snapped", "Line snapped!") : (A.LastLoss == ELoss::Escape ? NSLOCTEXT("Fishing", "Escaped", "It got away...") : NSLOCTEXT("Fishing", "Stolen", "Bait stolen!"));
				Ink = KC::Hot;
				break;
			default: break;
			}
			P.Text(Line, KartUI::Font(EFont::Display, Size * S, 2.f * S, KC::Night), Pos + FVector2D(Width / 2.f, 128.f * S), Ink, FVector2D(0.5, 0.5), 3.f * S);
		}
	}
}

void SFishingHud::PaintCallouts(const FKartPainter& P, const AFishingGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	for (const FEffect& E : M.Effects)
	{
		if (E.Label.IsEmpty() || E.Kind == EEffectKind::Bite || E.Kind == EEffectKind::Cast || E.Kind == EEffectKind::Catch || E.Kind == EEffectKind::Record || E.Kind == EEffectKind::Junk)
		{
			continue;
		}
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		// Over the water where it happened for the fish's doings, over the head for the angler's.
		const bool bWater = E.Kind == EEffectKind::Splash || E.Kind == EEffectKind::Burst || E.Kind == EEffectKind::Escape || E.Kind == EEffectKind::Hooked;
		const FVector World = bWater ? AFishingLake::WaterPoint(E.LaneIndex, M.Anglers.Num(), E.DistanceM) + FVector(0.0, 0.0, 70.0) : G.HeadOf(E.LaneIndex) + FVector(0.0, 0.0, 30.0);
		FVector2D At01;
		if (!G.ScreenOf(World, At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * 50.0) * S);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.2), 0.f, 1.f), 2.2f);
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / 0.25), 0.f, 1.f);
		FLinearColor Color = KC::White;
		float Size = 30.f;
		switch (E.Kind)
		{
		case EEffectKind::Splash: Color = BandColor(static_cast<int32>(BandAt(E.DistanceM))); Size = 24.f; break;
		case EEffectKind::Hooked: Color = KC::Mint; Size = 34.f; break;
		case EEffectKind::Burst: Color = KC::Sun; break;
		case EEffectKind::Snap: Color = KC::Hot; Size = 46.f; break;
		case EEffectKind::Escape:
		case EEffectKind::StolenBait:
		case EEffectKind::TooEarly: Color = KC::Hot; break;
		default: break;
		}
		P.Transformed(At, FMath::Max(0.01f, In), E.Kind == EEffectKind::Snap ? -6.f : 0.f).Faded(Out).Text(E.Label, KartUI::Font(EFont::Display, Size * S, 3.f * S, KC::Night), At, Color,
			FVector2D(0.5, 0.5), 5.f * S);
	}
}

void SFishingHud::PaintCatchCards(const FKartPainter& P, const AFishingGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const bool bFinished = G.GetStage() == EFishingStage::Finished;
	for (int32 i = 0; i < M.Anglers.Num(); ++i)
	{
		const FAngler& A = M.Anglers[i];
		if (bFinished || A.State != EAnglerState::Landed)
		{
			continue;
		}
		const FCatch& C = A.LastCatch;
		const FSpecies& Sp = Species()[C.Species];
		const double Age = A.StateMs / 1000.0;
		const double Life = LandedHoldMs / 1000.0;
		FVector2D At01;
		if (!G.ScreenOf(G.HeadOf(i) + FVector(0.0, 0.0, 50.0), At01))
		{
			continue;
		}
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.35) / 0.3), 0.f, 1.f), 2.f);
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / 0.3), 0.f, 1.f);
		if (In <= 0.f)
		{
			continue;
		}
		const bool bJunk = Sp.Kind == EKind::Junk;
		const bool bTrophy = Sp.Kind == EKind::Trophy;
		const FVector2D Size(300.f * S, (bJunk ? 104.f : 132.f) * S);
		FVector2D Pos = At01 * P.Size() - FVector2D(Size.X / 2.f, Size.Y + 10.f * S);
		Pos.X = FMath::Clamp(Pos.X, 16.f * S, P.Size().X - Size.X - 16.f * S);
		Pos.Y = FMath::Max(Pos.Y, 170.f * S);
		const FKartPainter F = P.Transformed(Pos + Size / 2.f, In, bTrophy ? 3.f * FMath::Sin(static_cast<float>(G.GetClock()) * 4.f) : 0.f).Faded(Out);
		const FLinearColor Edge = C.bRecord || bTrophy ? KC::Gold : AFishingLake::LaneColor(i);
		F.Card(Pos, Size, bJunk ? KC::WithAlpha(KC::Night, 0.9f) : KC::Paper, 20.f * S, 8.f * S, Edge, (C.bRecord ? 5.f : 3.f) * S);
		const FLinearColor Ink = bJunk ? KC::White : KC::Navy;
		F.Text(FString(Sp.Name).ToUpper(), KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(Size.X / 2.f, 24.f * S), bJunk ? KC::Sun : KC::NavyDim, FVector2D(0.5, 0.5));
		if (bJunk)
		{
			F.Text(NSLOCTEXT("Fishing", "Junk", "Not quite a fish..."), KartUI::Font(EFont::Title, 24.f * S), Pos + FVector2D(Size.X / 2.f, 64.f * S), KC::White, FVector2D(0.5, 0.5));
		}
		else
		{
			F.Text(Kg(C.WeightG), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(Size.X / 2.f, 66.f * S), Ink, FVector2D(0.5, 0.5));
			const int32 N = Stars(Sp, C.WeightG);
			for (int32 k = 0; k < 5; ++k)
			{
				Star(F, Pos + FVector2D(Size.X / 2.f + (k - 2) * 26.f * S, 108.f * S), 10.f * S, k < N ? KC::Gold : KC::WithAlpha(KC::NavyDim, 0.25f));
			}
			if (C.Combo >= 2)
			{
				F.Text(FString::Printf(TEXT("x%.2f"), ComboMultiplier(C.Combo)), KartUI::Font(EFont::Label, 17.f * S), Pos + FVector2D(Size.X - 18.f * S, 108.f * S), KC::Mint, FVector2D(1.0, 0.5));
			}
		}
		if (C.bRecord || bTrophy)
		{
			// The ribbon across the top corner.
			const FVector2D R = Pos + FVector2D(Size.X / 2.f, -12.f * S);
			const float Pop = 1.f + 0.06f * FMath::Sin(static_cast<float>(G.GetClock()) * 9.f);
			const FKartPainter Rib = F.Transformed(R, Pop, -4.f);
			Rib.Box(R - FVector2D(118.f, 20.f) * S, FVector2D(236.f, 40.f) * S, KC::Gold, 10.f * S, KC::Night, 3.f * S);
			Rib.Text(C.bRecord ? NSLOCTEXT("Fishing", "Record", "NEW RECORD!") : NSLOCTEXT("Fishing", "Trophy", "TROPHY!"), KartUI::Font(EFont::Display, 28.f * S), R, KC::Night, FVector2D(0.5, 0.5));
		}
	}
}

void SFishingHud::PaintCountdown(const FKartPainter& P, const AFishingGame& G, float S) const
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
		P.Faded(A).Text(NSLOCTEXT("Fishing", "Go", "FISH!"), KartUI::Font(EFont::Display, 150.f * S, 8.f * S, KC::Night), Centre, KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	}
	if (M.Phase == EMatchPhase::Playing && M.TimeRemainingMs <= 10000.0 && M.TimeRemainingMs > 0.0)
	{
		const int32 N = FMath::CeilToInt32(M.TimeRemainingMs / 1000.0);
		const double Frac = FMath::Frac(M.TimeRemainingMs / 1000.0);
		P.Faded(static_cast<float>(FMath::Clamp(Frac * 2.0, 0.0, 1.0)) * 0.85f).Text(FString::FromInt(N), KartUI::Font(EFont::Display, 120.f * S, 6.f * S, KC::Night),
			FVector2D(P.Size().X / 2.f, P.Size().Y * 0.3f), KC::Hot, FVector2D(0.5, 0.5), 7.f * S);
	}
}

void SFishingHud::PaintControls(const FKartPainter& P, const AFishingGame& G, float S) const
{
	if (!G.HasLocal() || G.GetMatch().ElapsedMs > 12000.0)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((12000.0 - G.GetMatch().ElapsedMs) / 600.0), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.58f);
	const FKartPainter F = P.Faded(A);
	F.Card(Centre - FVector2D(520.f, 44.f) * S, FVector2D(1040.f, 88.f) * S, KC::WithAlpha(KC::Night, 0.78f), 22.f * S, 6.f * S, KC::Sun, 2.f * S);
	F.Text(NSLOCTEXT("Fishing", "Controls1", "Hold Space / A / click and let go to cast  ·  press again when the bobber dips"), KartUI::Font(EFont::Body, 21.f * S),
		Centre - FVector2D(0.f, 16.f * S), KC::White, FVector2D(0.5, 0.5));
	F.Text(NSLOCTEXT("Fishing", "Controls2", "Reel: spin the right stick, alternate A/D or scroll the wheel  ·  keep the line in the green"), KartUI::Font(EFont::Body, 21.f * S),
		Centre + FVector2D(0.f, 18.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5));
}

void SFishingHud::PaintIntro(const FKartPainter& P, const AFishingGame& G, float S) const
{
	const double Age = G.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>((3.6 - Age) / 0.4), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.3f);
	F.Text(NSLOCTEXT("Fishing", "Title", "FISHING FRENZY"), KartUI::Font(EFont::Display, 130.f * S, 7.f * S), Centre - FVector2D(0.f, 40.f * S), KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	FString Names;
	for (const FFishingSeat& Seat : G.GetSeats())
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
	}
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 62.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(NSLOCTEXT("Fishing", "Rules", "Cast far for the big ones. Hook on the bite. Keep the line in the green. Heaviest catch wins."), KartUI::Font(EFont::Body, 24.f * S),
		Centre + FVector2D(0.f, 110.f * S), KC::WithAlpha(KC::White, 0.9f), FVector2D(0.5, 0.5), 3.f * S);
}

void SFishingHud::PaintStandings(const FKartPainter& P, const AFishingGame& G, float S) const
{
	const TArray<const FAngler*> Order = G.Standings();
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.1f);
	F.Text(NSLOCTEXT("Fishing", "Final", "THE WEIGH-IN"), KartUI::Font(EFont::Display, 84.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FAngler& A = *Order[i];
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.6 - i * 0.18) / 0.4), 0.f, 1.f));
		const FVector2D Size(720.f * S, 84.f * S);
		const FVector2D Pos(P.Size().X / 2.f - Size.X / 2.f, P.Size().Y * 0.18f + i * 96.f * S);
		const FKartPainter Row = F.Transformed(Pos + Size / 2.f, FMath::Max(0.01f, In));
		const bool bWinner = G.GetMatch().WinnerIds.Contains(A.PlayerId);
		Row.Card(Pos, Size, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, AFishingLake::LaneColor(A.LaneIndex), 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(44.f * S, 42.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(A.Name, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(92.f * S, 28.f * S), Ink, FVector2D(0.0, 0.5));
		const FString Best = A.BiggestSpecies >= 0 ? FString::Printf(TEXT("best: %s %s"), Species()[A.BiggestSpecies].Name, *Kg(A.BiggestG)) : FString(TEXT("no fish today"));
		FString Detail = FString::Printf(TEXT("%d fish  ·  %s"), A.Catches, *Best);
		if (A.JunkCount > 0)
		{
			Detail += FString::Printf(TEXT("  ·  %d junk"), A.JunkCount);
		}
		Row.Text(Detail, KartUI::Font(EFont::Body, 17.f * S), Pos + FVector2D(92.f * S, 60.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(Kg(A.Score), KartUI::Font(EFont::Display, 44.f * S), Pos + FVector2D(Size.X - 24.f * S, 42.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
