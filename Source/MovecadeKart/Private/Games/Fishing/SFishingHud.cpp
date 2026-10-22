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
	// Canvas pixels (1080 high; scaled by S) and seconds.
	/** The lane panels along the bottom. */
	constexpr float LaneGap = 18.f;
	constexpr float LaneMaxWidth = 440.f;
	constexpr float LaneHeight = 150.f;
	/** The tension or cast meter's height, and the share of the bar the tension scale covers (1.2 = 20% past the red line). */
	constexpr float MeterHeight = 22.f;
	constexpr double TensionScaleMax = 1.2;
	/** A fight flashes its panel red once the line strains this long, or goes slack this long (ms). */
	constexpr double DangerStrainMs = 80.0;
	constexpr double DangerSlackMs = 600.0;
	/** The clock pulses red in the last seconds. */
	constexpr int32 LateSeconds = 15;
	/** Callouts rise this fast (canvas px/s), pop in over 0.2 s and fade over their last 0.25 s. */
	constexpr double CalloutRise = 50.0;
	constexpr double CalloutPopSeconds = 0.2;
	constexpr double CalloutFadeSeconds = 0.25;
	/** The catch card pops in once the fish is up (s after landing) and fades as it is put away. */
	constexpr double CardInAt = 0.35;
	constexpr double CardPopSeconds = 0.3;
	constexpr double CardFadeSeconds = 0.3;
	/** "FISH!" holds for the first 0.8 s of the round, the big last-ten countdown, the controls card for 12 s. */
	constexpr double GoShowMs = 800.0;
	constexpr double LastCountdownMs = 10000.0;
	constexpr double ControlsShowMs = 12000.0;
	constexpr double ControlsFadeMs = 600.0;
	/** The intro card fades in over 0.5 s and out over the intro's last 0.4 s; standings rows arrive 0.18 s apart. */
	constexpr double IntroFadeIn = 0.5;
	constexpr double IntroFadeOut = 0.4;
	constexpr double StandingsDelay = 0.6;
	constexpr double StandingsStagger = 0.18;

	const FLinearColor& BandColor(int32 Band)
	{
		static const FLinearColor Colors[] = { FLinearColor::FromSRGBColor(FColor(0x6f, 0xd8, 0xe8)), FLinearColor::FromSRGBColor(FColor(0x5f, 0xd0, 0x6a)),
			FLinearColor::FromSRGBColor(FColor(0xff, 0xd2, 0x3c)), FLinearColor::FromSRGBColor(FColor(0xff, 0x5a, 0x4a)) };
		return Colors[FMath::Clamp(Band, 0, 3)];
	}

	/** "1".."4": the lane badges, made once. */
	const FString& LaneNumber(int32 Lane)
	{
		static const FString Numbers[] = { TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4") };
		return Numbers[FMath::Clamp(Lane, 0, 3)];
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

	/** Whole metres as "%.0f" prints them (round half to even), for the labels keyed by the number they show. */
	int32 ShownMetres(double Metres)
	{
		return static_cast<int32>(FMath::RoundHalfToEven(Metres));
	}

	void Star(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill)
	{
		TArray<FVector2D, TInlineAllocator<10>> Points;
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

SFishingHud::FLaneTexts& SFishingHud::TextsOf(int32 Lane) const
{
	check(LaneTexts.IsValidIndex(Lane));
	return LaneTexts[Lane];
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
	// One set of labels per angler, sized before any paint step holds a reference into it.
	if (LaneTexts.Num() < G->LaneCount())
	{
		LaneTexts.SetNum(G->LaneCount());
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const FFrame F{ P, *G, static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH), FArcadeHudView::Capture(G->GetViewer()) };
	switch (G->GetStage())
	{
	case EFishingStage::Intro:
		PaintIntro(F);
		break;
	case EFishingStage::Finished:
		PaintCatchCards(F);
		PaintStandings(F);
		break;
	default:
		PaintPins(F);
		PaintClock(F);
		PaintLanes(F);
		PaintCallouts(F);
		PaintCatchCards(F);
		PaintControls(F);
		PaintCountdown(F);
		break;
	}
	return Layer;
}

void SFishingHud::PaintClock(const FFrame& F) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FMatch& M = F.Game.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * KartUI::Metric::SafeFraction);
	const int32 Seconds = FMath::CeilToInt32(M.TimeRemainingMs / 1000.0);
	const bool bLate = Seconds <= LateSeconds && M.Phase == EMatchPhase::Playing;
	const float Pulse = bLate ? 1.f + 0.06f * FMath::Abs(FMath::Sin(static_cast<float>(F.Game.GetClock()) * 6.f)) : 1.f;
	P.Card(Top - FVector2D(110.f, 0.f) * S, FVector2D(220.f, 104.f) * S, KC::WithAlpha(KC::Night, 0.78f), 26.f * S, 6.f * S, bLate ? KC::Hot : KC::Sun, 3.f * S);
	const FString& Clock = ClockText.Get(Seconds, [](int32 V) { return FString::Printf(TEXT("%d:%02d"), V / 60, V % 60); });
	P.Transformed(Top + FVector2D(0.f, 52.f * S), Pulse).Text(Clock, KartUI::Font(EFont::Display, 56.f * S, 3.f * S, KC::Night),
		Top + FVector2D(0.f, 52.f * S), bLate ? KC::Hot : KC::White, FVector2D(0.5, 0.5), 4.f * S);
	if (M.RecordG <= 0)
	{
		return;
	}
	// The round's biggest fish so far.
	const FAngler* Holder = M.Find(M.RecordHolder);
	const FString& Line = RecordText.Get({ M.RecordG, Holder ? Holder->LaneIndex : INDEX_NONE }, [&M, Holder](const FRecordKey&)
	{
		return FString::Printf(TEXT("RECORD  %s  %s"), *Kg(M.RecordG), Holder ? *Holder->Name : TEXT(""));
	});
	const FSlateFontInfo Font = KartUI::Font(EFont::Label, 17.f * S);
	const float W = static_cast<float>(FKartPainter::Measure(Line, Font).X) + 44.f * S;
	const FVector2D C = Top + FVector2D(0.f, 128.f * S);
	P.Box(C - FVector2D(W / 2.f, 17.f * S), FVector2D(W, 34.f * S), KC::WithAlpha(KC::Night, 0.72f), 17.f * S, KC::Gold, 2.f * S);
	Star(P, C - FVector2D(W / 2.f - 20.f * S, 0.f), 9.f * S, KC::Gold);
	P.Text(Line, Font, C + FVector2D(8.f * S, 0.f), KC::Gold, FVector2D(0.5, 0.5));
}

void SFishingHud::PaintPins(const FFrame& F) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FMatch& M = F.Game.GetMatch();
	for (int32 i = 0; i < M.Anglers.Num(); ++i)
	{
		const FAngler& A = M.Anglers[i];
		if (A.State != EAnglerState::Waiting && A.State != EAnglerState::Bite && A.State != EAnglerState::Fighting)
		{
			continue;
		}
		FVector2D At01;
		if (!F.View.Project(F.Game.BobberOf(i) + FVector(0.0, 0.0, 30.0), At01))
		{
			continue;
		}
		// A pin over the bobber, numbered by lane; on a bite it turns yellow, grows and hops.
		const FLinearColor Color = AFishingLake::LaneColor(i);
		const FVector2D At = At01 * P.Size();
		const bool bBite = A.State == EAnglerState::Bite;
		const float Hop = bBite ? 10.f * FMath::Abs(FMath::Sin(static_cast<float>(F.Game.GetClock()) * 14.f)) * S : 0.f;
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
			P.Text(LaneNumber(i), KartUI::Font(EFont::Label, 17.f * S), C, KC::TextOn(Color), FVector2D(0.5, 0.5));
		}
	}
}

void SFishingHud::PaintLanes(const FFrame& F) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const int32 N = F.Game.GetMatch().Anglers.Num();
	const float Gap = LaneGap * S;
	const float Width = FMath::Min(LaneMaxWidth * S, (P.Size().X * 0.94f - Gap * (N - 1)) / FMath::Max(1, N));
	const float Height = LaneHeight * S;
	const float Left = (P.Size().X - (Width * N + Gap * (N - 1))) / 2.f;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D Pos(Left + i * (Width + Gap), P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height);
		PaintLaneCard(F, i, Pos, FVector2D(Width, Height));
	}
}

void SFishingHud::PaintLaneCard(const FFrame& F, int32 i, const FVector2D& Pos, const FVector2D& Size) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const float Width = Size.X;
	const FAngler& A = F.Game.GetMatch().Anglers[i];
	FLaneTexts& Texts = TextsOf(i);
	const FLinearColor Color = AFishingLake::LaneColor(i);
	const bool bFight = A.State == EAnglerState::Fighting;
	const bool bDanger = bFight && (A.Fight.StrainMs > DangerStrainMs || A.Fight.SlackMs > DangerSlackMs);
	const float Flash = bDanger ? 0.5f + 0.5f * FMath::Sin(static_cast<float>(F.Game.GetClock()) * 16.f) : 0.f;
	P.Card(Pos, Size, KC::WithAlpha(KC::Night, 0.84f), 22.f * S, 6.f * S, bDanger ? FMath::Lerp(Color, KC::Hot, Flash) : Color, (bDanger ? 4.f : 2.5f) * S);
	P.Circle(Pos + FVector2D(30.f * S, 32.f * S), 12.f * S, Color, KC::Night, 2.f * S);
	P.Text(LaneNumber(i), KartUI::Font(EFont::Label, 14.f * S), Pos + FVector2D(30.f * S, 32.f * S), KC::TextOn(Color), FVector2D(0.5, 0.5));
	P.Text(A.Name, KartUI::Font(EFont::Title, 25.f * S), Pos + FVector2D(52.f * S, 32.f * S), KC::White, FVector2D(0.0, 0.5));
	P.Text(Texts.Score.Get(A.Score, Kg), KartUI::Font(EFont::Display, 38.f * S), Pos + FVector2D(Width - 20.f * S, 34.f * S), KC::White, FVector2D(1.0, 0.5), 3.f * S);
	const int64 CatchKey = (static_cast<int64>(A.Catches) << 32) | static_cast<uint32>(A.BiggestG);
	const FString& Sub = Texts.Catches.Get(CatchKey, [&A](int64)
	{
		FString Line = FString::Printf(TEXT("%d fish"), A.Catches);
		if (A.BiggestG > 0)
		{
			Line += FString::Printf(TEXT("  ·  best %s"), *Kg(A.BiggestG));
		}
		return Line;
	});
	P.Text(Sub, KartUI::Font(EFont::Label, 16.f * S), Pos + FVector2D(24.f * S, 68.f * S), KC::WithAlpha(KC::White, 0.78f), FVector2D(0.0, 0.5));
	if (A.Combo >= 2)
	{
		const FString& Chip = Texts.Combo.Get(A.Combo, [](int32 Combo) { return FString::Printf(TEXT("COMBO x%.2f"), ComboMultiplier(Combo)); });
		const FSlateFontInfo Font = KartUI::Font(EFont::Label, 15.f * S);
		const float W = static_cast<float>(FKartPainter::Measure(Chip, Font).X) + 20.f * S;
		const FVector2D ChipPos(Pos.X + Width - 18.f * S - W, Pos.Y + 56.f * S);
		P.Box(ChipPos, FVector2D(W, 24.f * S), A.Combo >= 4 ? KC::Sun : KC::Mint, 12.f * S);
		P.Text(Chip, Font, ChipPos + FVector2D(W / 2.f, 12.f * S), KC::Night, FVector2D(0.5, 0.5));
	}
	// The working area: a meter (cast or tension) or what to do now.
	const FVector2D Bar = Pos + FVector2D(22.f * S, 88.f * S);
	const float BarW = Width - 44.f * S;
	const double Charge = F.Game.ChargeOf(i);
	if (bFight)
	{
		PaintTension(F, i, Bar, BarW);
	}
	else if (Charge >= 0.0)
	{
		PaintCastMeter(F, i, Charge, Bar, BarW);
	}
	else
	{
		PaintLaneStatus(F, i, Pos, Width);
	}
}

void SFishingHud::PaintTension(const FFrame& F, int32 i, const FVector2D& Bar, float BarW) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FFight& Fight = F.Game.GetMatch().Anglers[i].Fight;
	const float Clock = static_cast<float>(F.Game.GetClock());
	auto X = [&](double V) { return Bar.X + static_cast<float>(FMath::Clamp(V / TensionScaleMax, 0.0, 1.0)) * BarW; };
	const float H = MeterHeight * S;
	// Slack, the green, strain, and past the red line.
	P.Box(Bar, FVector2D(BarW, H), KC::WithAlpha(KC::Sky, 0.35f), 11.f * S);
	P.Box(FVector2D(X(GreenLow), Bar.Y), FVector2D(X(GreenHigh) - X(GreenLow), H), KC::WithAlpha(KC::Mint, 0.85f), 4.f * S);
	P.Box(FVector2D(X(GreenHigh), Bar.Y), FVector2D(X(RedLine) - X(GreenHigh), H), KC::WithAlpha(KC::Sun, 0.85f), 4.f * S);
	P.Box(FVector2D(X(RedLine), Bar.Y), FVector2D(Bar.X + BarW - X(RedLine), H), KC::WithAlpha(KC::Hot, 0.9f), 11.f * S);
	const bool bRun = Fight.WarnMs > 0.0 || Fight.BurstMs > 0.0;
	const float Needle = X(Fight.Tension);
	const float Wobble = bRun ? 2.f * FMath::Sin(Clock * 40.f) * S : 0.f;
	P.Box(FVector2D(Needle - 3.f * S + Wobble, Bar.Y - 7.f * S), FVector2D(6.f * S, H + 14.f * S), KC::White, 3.f * S, KC::Night, 2.f * S);
	// The two ways to lose it, filling up underneath.
	const float Under = Bar.Y + H + 10.f * S;
	const float Snap = static_cast<float>(FMath::Clamp(Fight.StrainMs / SnapStrainMs, 0.0, 1.0));
	const float Slack = static_cast<float>(FMath::Clamp(Fight.SlackMs / EscapeSlackMs, 0.0, 1.0));
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
	P.Text(Hint, KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X, Under + 19.f * S), Ink, FVector2D(0.0, 0.5));
	const FString& Distance = TextsOf(i).FightDistance.Get(ShownMetres(Fight.DistanceM), [](int32 M) { return FString::Printf(TEXT("%d m"), M); });
	P.Text(Distance, KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X + BarW, Under + 19.f * S), KC::WithAlpha(KC::White, 0.8f), FVector2D(1.0, 0.5));
}

void SFishingHud::PaintCastMeter(const FFrame& F, int32 i, double Charge, const FVector2D& Bar, float BarW) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	// The cast meter over the bands, the needle at the power.
	const float H = MeterHeight * S;
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
	const double Metres = CastDistance(Charge);
	const EBand Band = BandAt(Metres);
	const int32 AimKey = static_cast<int32>(Band) * 1000 + ShownMetres(Metres);
	const FString& Aim = TextsOf(i).CastAim.Get(AimKey, [](int32 Key) { return FString::Printf(TEXT("%s  ·  %d m"), BandName(static_cast<EBand>(Key / 1000)), Key % 1000); });
	P.Text(Aim, KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X, Bar.Y + H + 30.f * S), BandColor(static_cast<int32>(Band)), FVector2D(0.0, 0.5));
	P.Text(NSLOCTEXT("Fishing", "LetGo", "let go to cast"), KartUI::Font(EFont::Label, 16.f * S), FVector2D(Bar.X + BarW, Bar.Y + H + 30.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(1.0, 0.5));
}

void SFishingHud::PaintLaneStatus(const FFrame& F, int32 i, const FVector2D& Pos, float Width) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FAngler& A = F.Game.GetMatch().Anglers[i];
	FLaneTexts& Texts = TextsOf(i);
	const float Clock = static_cast<float>(F.Game.GetClock());
	// What to do now, big in the middle of the working area.
	FText Line;
	const FString* Dynamic = nullptr;
	FLinearColor Ink = KC::White;
	float Size = 26.f;
	switch (A.State)
	{
	case EAnglerState::Idle:
		Line = NSLOCTEXT("Fishing", "CastNow", "CAST!");
		Ink = AFishingLake::LaneColor(i);
		break;
	case EAnglerState::Casting:
		Dynamic = &Texts.CastOut.Get(ShownMetres(A.CastM), [](int32 M) { return FString::Printf(TEXT("%d m out"), M); });
		Ink = BandColor(static_cast<int32>(BandAt(A.CastM)));
		break;
	case EAnglerState::Waiting:
		Dynamic = &Texts.Waiting.Get(static_cast<int32>(BandAt(A.LureM)), [](int32 Band) { return FString::Printf(TEXT("waiting...  %s"), BandName(static_cast<EBand>(Band))); });
		Ink = KC::WithAlpha(KC::White, 0.8f);
		Size = 22.f;
		break;
	case EAnglerState::Bite:
		Line = NSLOCTEXT("Fishing", "BiteNow", "BITE! HOOK IT!");
		Ink = FMath::Lerp(KC::Sun, KC::White, 0.5f + 0.5f * FMath::Sin(Clock * 20.f));
		Size = 32.f;
		break;
	case EAnglerState::Landed:
		Dynamic = &Texts.LandedName.Get(A.LastCatch.Species, [](int32 Species) { return FString(MovecadeKart::Fishing::Species()[Species].Name); });
		Ink = KC::Sun;
		break;
	case EAnglerState::Lost:
		Line = A.LastLoss == ELoss::Snap ? NSLOCTEXT("Fishing", "Snapped", "Line snapped!") : (A.LastLoss == ELoss::Escape ? NSLOCTEXT("Fishing", "Escaped", "It got away...") : NSLOCTEXT("Fishing", "Stolen", "Bait stolen!"));
		Ink = KC::Hot;
		break;
	default:
		break;
	}
	const FSlateFontInfo Font = KartUI::Font(EFont::Display, Size * S, 2.f * S, KC::Night);
	const FVector2D At = Pos + FVector2D(Width / 2.f, 112.f * S);
	if (Dynamic)
	{
		P.Text(*Dynamic, Font, At, Ink, FVector2D(0.5, 0.5), 3.f * S);
	}
	else
	{
		P.Text(Line, Font, At, Ink, FVector2D(0.5, 0.5), 3.f * S);
	}
}

void SFishingHud::PaintCallouts(const FFrame& F) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FMatch& M = F.Game.GetMatch();
	for (const FEffect& E : M.Effects)
	{
		// The bite has its pin, the cast its meter, and the catches their card.
		if (E.Label.IsEmpty() || E.Kind == EEffectKind::Bite || E.Kind == EEffectKind::Cast || E.Kind == EEffectKind::Catch || E.Kind == EEffectKind::Record || E.Kind == EEffectKind::Junk)
		{
			continue;
		}
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		// Over the water where it happened for the fish's doings, over the head for the angler's.
		const bool bWater = E.Kind == EEffectKind::Splash || E.Kind == EEffectKind::Burst || E.Kind == EEffectKind::Escape || E.Kind == EEffectKind::Hooked;
		const FVector World = bWater ? AFishingLake::WaterPoint(E.LaneIndex, M.Anglers.Num(), E.DistanceM) + FVector(0.0, 0.0, 70.0) : F.Game.HeadOf(E.LaneIndex) + FVector(0.0, 0.0, 30.0);
		FVector2D At01;
		if (!F.View.Project(World, At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * CalloutRise) * S);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / CalloutPopSeconds), 0.f, 1.f), 2.2f);
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / CalloutFadeSeconds), 0.f, 1.f);
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

void SFishingHud::PaintCatchCards(const FFrame& F) const
{
	if (F.Game.GetStage() == EFishingStage::Finished)
	{
		return;
	}
	const FMatch& M = F.Game.GetMatch();
	for (int32 i = 0; i < M.Anglers.Num(); ++i)
	{
		FVector2D At01;
		if (M.Anglers[i].State == EAnglerState::Landed && F.View.Project(F.Game.HeadOf(i) + FVector(0.0, 0.0, 50.0), At01))
		{
			PaintCatchCard(F, i, At01 * F.P.Size());
		}
	}
}

void SFishingHud::PaintCatchCard(const FFrame& F, int32 i, const FVector2D& Anchor) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FAngler& A = F.Game.GetMatch().Anglers[i];
	FLaneTexts& Texts = TextsOf(i);
	const FCatch& C = A.LastCatch;
	const FSpecies& Sp = Species()[C.Species];
	const double Age = A.StateMs / 1000.0;
	const double Life = LandedHoldMs / 1000.0;
	const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - CardInAt) / CardPopSeconds), 0.f, 1.f), 2.f);
	const float Out = FMath::Clamp(static_cast<float>((Life - Age) / CardFadeSeconds), 0.f, 1.f);
	if (In <= 0.f)
	{
		return;
	}
	const bool bJunk = Sp.Kind == EKind::Junk;
	const bool bTrophy = Sp.Kind == EKind::Trophy;
	const float Clock = static_cast<float>(F.Game.GetClock());
	// Over the angler's head, kept on screen and clear of the clock.
	const FVector2D Size(300.f * S, (bJunk ? 104.f : 132.f) * S);
	FVector2D Pos = Anchor - FVector2D(Size.X / 2.f, Size.Y + 10.f * S);
	Pos.X = FMath::Clamp(Pos.X, 16.f * S, P.Size().X - Size.X - 16.f * S);
	Pos.Y = FMath::Max(Pos.Y, 170.f * S);
	const FKartPainter Card = P.Transformed(Pos + Size / 2.f, In, bTrophy ? 3.f * FMath::Sin(Clock * 4.f) : 0.f).Faded(Out);
	const FLinearColor Edge = C.bRecord || bTrophy ? KC::Gold : AFishingLake::LaneColor(i);
	Card.Card(Pos, Size, bJunk ? KC::WithAlpha(KC::Night, 0.9f) : KC::Paper, 20.f * S, 8.f * S, Edge, (C.bRecord ? 5.f : 3.f) * S);
	const FLinearColor Ink = bJunk ? KC::White : KC::Navy;
	const FString& Name = Texts.CardName.Get(C.Species, [](int32 Species) { return FString(MovecadeKart::Fishing::Species()[Species].Name).ToUpper(); });
	Card.Text(Name, KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(Size.X / 2.f, 24.f * S), bJunk ? KC::Sun : KC::NavyDim, FVector2D(0.5, 0.5));
	if (bJunk)
	{
		Card.Text(NSLOCTEXT("Fishing", "Junk", "Not quite a fish..."), KartUI::Font(EFont::Title, 24.f * S), Pos + FVector2D(Size.X / 2.f, 64.f * S), KC::White, FVector2D(0.5, 0.5));
	}
	else
	{
		Card.Text(Texts.CardWeight.Get(C.WeightG, Kg), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(Size.X / 2.f, 66.f * S), Ink, FVector2D(0.5, 0.5));
		const int32 N = Stars(Sp, C.WeightG);
		for (int32 k = 0; k < 5; ++k)
		{
			Star(Card, Pos + FVector2D(Size.X / 2.f + (k - 2) * 26.f * S, 108.f * S), 10.f * S, k < N ? KC::Gold : KC::WithAlpha(KC::NavyDim, 0.25f));
		}
		if (C.Combo >= 2)
		{
			const FString& Combo = Texts.CardCombo.Get(C.Combo, [](int32 Value) { return FString::Printf(TEXT("x%.2f"), ComboMultiplier(Value)); });
			Card.Text(Combo, KartUI::Font(EFont::Label, 17.f * S), Pos + FVector2D(Size.X - 18.f * S, 108.f * S), KC::Mint, FVector2D(1.0, 0.5));
		}
	}
	if (C.bRecord || bTrophy)
	{
		// The ribbon across the top.
		const FVector2D R = Pos + FVector2D(Size.X / 2.f, -12.f * S);
		const float Pop = 1.f + 0.06f * FMath::Sin(Clock * 9.f);
		const FKartPainter Ribbon = Card.Transformed(R, Pop, -4.f);
		Ribbon.Box(R - FVector2D(118.f, 20.f) * S, FVector2D(236.f, 40.f) * S, KC::Gold, 10.f * S, KC::Night, 3.f * S);
		Ribbon.Text(C.bRecord ? NSLOCTEXT("Fishing", "Record", "NEW RECORD!") : NSLOCTEXT("Fishing", "Trophy", "TROPHY!"), KartUI::Font(EFont::Display, 28.f * S), R, KC::Night, FVector2D(0.5, 0.5));
	}
}

void SFishingHud::PaintCountdown(const FFrame& F) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FMatch& M = F.Game.GetMatch();
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.36f);
	if (M.Phase == EMatchPhase::Countdown)
	{
		// 3-2-1, each number popping in.
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 N = FMath::CeilToInt32(Remaining);
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(1.0 - (N - Remaining)) / 0.35f, 0.f, 1.f));
		const FString& Number = CountdownText.Get(FMath::Max(1, N), [](int32 V) { return FString::FromInt(V); });
		P.Transformed(Centre, FMath::Max(0.01f, Pop)).Text(Number, KartUI::Font(EFont::Display, 200.f * S, 9.f * S, KC::Night), Centre, KC::Sun,
			FVector2D(0.5, 0.5), 10.f * S);
		return;
	}
	if (M.Phase == EMatchPhase::Playing && M.ElapsedMs < GoShowMs)
	{
		const float A = FMath::Clamp(static_cast<float>((GoShowMs - M.ElapsedMs) / 300.0), 0.f, 1.f);
		P.Faded(A).Text(NSLOCTEXT("Fishing", "Go", "FISH!"), KartUI::Font(EFont::Display, 150.f * S, 8.f * S, KC::Night), Centre, KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	}
	if (M.Phase == EMatchPhase::Playing && M.TimeRemainingMs <= LastCountdownMs && M.TimeRemainingMs > 0.0)
	{
		// The last ten seconds, each fading through its second.
		const int32 N = FMath::CeilToInt32(M.TimeRemainingMs / 1000.0);
		const double Frac = FMath::Frac(M.TimeRemainingMs / 1000.0);
		const FString& Number = LastSecondsText.Get(N, [](int32 V) { return FString::FromInt(V); });
		P.Faded(static_cast<float>(FMath::Clamp(Frac * 2.0, 0.0, 1.0)) * 0.85f).Text(Number, KartUI::Font(EFont::Display, 120.f * S, 6.f * S, KC::Night),
			FVector2D(P.Size().X / 2.f, P.Size().Y * 0.3f), KC::Hot, FVector2D(0.5, 0.5), 7.f * S);
	}
}

void SFishingHud::PaintControls(const FFrame& F) const
{
	const double Elapsed = F.Game.GetMatch().ElapsedMs;
	if (!F.Game.HasLocal() || Elapsed > ControlsShowMs)
	{
		return;
	}
	const FKartPainter& P = F.P;
	const float S = F.S;
	const float A = FMath::Clamp(static_cast<float>((ControlsShowMs - Elapsed) / ControlsFadeMs), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.58f);
	const FKartPainter Faded = P.Faded(A);
	Faded.Card(Centre - FVector2D(520.f, 44.f) * S, FVector2D(1040.f, 88.f) * S, KC::WithAlpha(KC::Night, 0.78f), 22.f * S, 6.f * S, KC::Sun, 2.f * S);
	Faded.Text(NSLOCTEXT("Fishing", "Controls1", "Hold Space / A / click and let go to cast  ·  press again when the bobber dips"), KartUI::Font(EFont::Body, 21.f * S),
		Centre - FVector2D(0.f, 16.f * S), KC::White, FVector2D(0.5, 0.5));
	Faded.Text(NSLOCTEXT("Fishing", "Controls2", "Reel: spin the right stick, alternate A/D or scroll the wheel  ·  keep the line in the green"), KartUI::Font(EFont::Body, 21.f * S),
		Centre + FVector2D(0.f, 18.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5));
}

void SFishingHud::PaintIntro(const FFrame& F) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const double Age = F.Game.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / IntroFadeIn), 0.f, 1.f) * FMath::Clamp(static_cast<float>((AFishingGame::IntroSeconds - Age) / IntroFadeOut), 0.f, 1.f);
	const FKartPainter Faded = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.3f);
	Faded.Text(NSLOCTEXT("Fishing", "Title", "FISHING FRENZY"), KartUI::Font(EFont::Display, 130.f * S, 7.f * S), Centre - FVector2D(0.f, 40.f * S), KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	const TArray<FFishingSeat>& Seats = F.Game.GetSeats();
	const FString& Names = IntroNames.Get(Seats.Num(), [&Seats](int32)
	{
		FString Joined;
		for (const FFishingSeat& Seat : Seats)
		{
			Joined += (Joined.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
		}
		return Joined;
	});
	Faded.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 62.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	Faded.Text(NSLOCTEXT("Fishing", "Rules", "Cast far for the big ones. Hook on the bite. Keep the line in the green. Heaviest catch wins."), KartUI::Font(EFont::Body, 24.f * S),
		Centre + FVector2D(0.f, 110.f * S), KC::WithAlpha(KC::White, 0.9f), FVector2D(0.5, 0.5), 3.f * S);
}

void SFishingHud::PaintStandings(const FFrame& F) const
{
	const FKartPainter& P = F.P;
	const float S = F.S;
	const FMatch& M = F.Game.GetMatch();
	// By score, then the biggest fish (FishingRules' Leaderboard order; both are whole grams, never negative).
	const TArray<int32>& Order = Ranks.Update(M.Anglers.Num(), [&M](int32 Lane)
	{
		const FAngler& A = M.Anglers[Lane];
		return (static_cast<int64>(A.Score) << 32) | static_cast<uint32>(A.BiggestG);
	});
	const double Age = F.Game.GetFinishedAge();
	const FKartPainter Faded = P.Faded(FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.1f);
	Faded.Text(NSLOCTEXT("Fishing", "Final", "THE WEIGH-IN"), KartUI::Font(EFont::Display, 84.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 Place = 0; Place < Order.Num(); ++Place)
	{
		const int32 Lane = Order[Place];
		const FAngler& A = M.Anglers[Lane];
		FLaneTexts& Texts = TextsOf(Lane);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - StandingsDelay - Place * StandingsStagger) / 0.4), 0.f, 1.f));
		const FVector2D Size(900.f * S, 84.f * S);
		const FVector2D Pos(P.Size().X / 2.f - Size.X / 2.f, P.Size().Y * 0.18f + Place * 96.f * S);
		const FKartPainter Row = Faded.Transformed(Pos + Size / 2.f, FMath::Max(0.01f, In));
		const bool bWinner = M.WinnerIds.Contains(A.PlayerId);
		Row.Card(Pos, Size, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, AFishingLake::LaneColor(A.LaneIndex), 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(LaneNumber(Place), KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(44.f * S, 42.f * S), KC::ForPlace(Place + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(A.Name, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(92.f * S, 28.f * S), Ink, FVector2D(0.0, 0.5));
		const FString& Detail = Texts.Detail.Get({ A.Catches, A.BiggestSpecies, A.BiggestG, A.JunkCount }, [&A](const FDetailKey&)
		{
			const FString Best = A.BiggestSpecies >= 0 ? FString::Printf(TEXT("best: %s %s"), Species()[A.BiggestSpecies].Name, *Kg(A.BiggestG)) : FString(TEXT("no fish today"));
			FString Line = FString::Printf(TEXT("%d fish  ·  %s"), A.Catches, *Best);
			if (A.JunkCount > 0)
			{
				Line += FString::Printf(TEXT("  ·  %d junk"), A.JunkCount);
			}
			return Line;
		});
		Row.Text(Detail, KartUI::Font(EFont::Body, 17.f * S), Pos + FVector2D(92.f * S, 60.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(Texts.Score.Get(A.Score, Kg), KartUI::Font(EFont::Display, 44.f * S), Pos + FVector2D(Size.X - 24.f * S, 42.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
