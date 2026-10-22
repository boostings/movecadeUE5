#include "Games/Derby/SDerbyHud.h"

#include "Games/Derby/DerbyGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Derby;
namespace KC = KartUI::Color;
using KartUI::EFont;

// Layout is in 1080p canvas units, scaled by S (KartUI::Metric::CanvasH) at paint.
namespace
{
	/** Labels that never change (made once; NSLOCTEXT per paint would look the string up every frame). */
	namespace Label
	{
		const FText& Strikes() { static const FText T = NSLOCTEXT("Derby", "Strikes", "K"); return T; }
		const FText& Deep() { static const FText T = NSLOCTEXT("Derby", "Deep", "IT'S DEEP..."); return T; }
		const FText& SwingNow() { static const FText T = NSLOCTEXT("Derby", "SwingNow", "SWING YOUR PHONE!"); return T; }
		const FText& PhoneHint() { static const FText T = NSLOCTEXT("Derby", "PhoneHint", "Swing your phone like a bat"); return T; }
		const FText& Pull() { static const FText T = NSLOCTEXT("Derby", "Pull", "PULL"); return T; }
		const FText& Oppo() { static const FText T = NSLOCTEXT("Derby", "Oppo", "OPPO"); return T; }
		const FText& Power() { static const FText T = NSLOCTEXT("Derby", "Power", "POWER"); return T; }
		const FText& Controls()
		{
			static const FText T = NSLOCTEXT("Derby", "Controls", "Hold Space, RT or click to load  ·  let go to swing  ·  mouse, A/D or stick to aim");
			return T;
		}
		const FText& NowBatting() { static const FText T = NSLOCTEXT("Derby", "NowBatting", "NOW BATTING"); return T; }
		const FText& Title() { static const FText T = NSLOCTEXT("Derby", "Title", "HOME RUN DERBY"); return T; }
		const FText& Rules()
		{
			static const FText T = NSLOCTEXT("Derby", "Rules", "Ten pitches each. Time your swing, aim it, send it. Home runs score their distance.");
			return T;
		}
		const FText& Final() { static const FText T = NSLOCTEXT("Derby", "Final", "FINAL STANDINGS"); return T; }
	}

	// ---- Timing (seconds).
	/** The swing's EARLY / PERFECT / LATE stays up this long after contact, fading over the last FadeOut. */
	constexpr double TimingShowSeconds = 1.6;
	constexpr double TimingFadeSeconds = 0.3;
	constexpr double TimingPopSeconds = 0.18;
	/** The call pops in, its points rise and fade, the landing tag pops. */
	constexpr double CallPopSeconds = 0.3;
	constexpr double PointsRiseSeconds = 1.2;
	constexpr double PointsGoneAt = 3.5;
	constexpr double PointsFadeSeconds = 0.4;
	constexpr double TagPopSeconds = 0.25;
	constexpr double RadarPopSeconds = 0.25;
	constexpr double StepInSlideSeconds = 0.35;
	constexpr double StepInFadeSeconds = 0.3;
	constexpr double IntroFadeInSeconds = 0.5;
	constexpr double IntroFadeOutSeconds = 0.4;
	constexpr double IntroPopSeconds = 0.6;
	constexpr double FinalFadeSeconds = 0.5;
	constexpr double FinalRowDelay = 0.5;
	constexpr double FinalRowStagger = 0.18;
	constexpr double FinalRowPopSeconds = 0.4;
	/** The radar gun reads "stale" after the pitch: no pop-in. */
	constexpr double LongAgo = 10.0;

	/** The landing tag only shows when its spot is well inside the frame (0..1 of the viewport). */
	constexpr double TagMinX = 0.05;
	constexpr double TagMaxX = 0.95;
	constexpr double TagMinY = 0.44;
	constexpr double TagMaxY = 0.92;
	/** It hangs this far above where the ball came down (cm). */
	constexpr double TagAboveCm = 600.0;

	/** The aim fan: a quarter of the field from pull (-135 degrees, screen space) to opposite, the needle 36 degrees per unit. */
	constexpr int32 FanSegments = 16;
	constexpr float FanFromDeg = -135.f;
	constexpr float FanSpanDeg = 90.f;
	constexpr float NeedleDegPerAim = 36.f;
	const FLinearColor FanGrass(0.12f, 0.45f, 0.2f);
	/** The controls hint only shows for the first pitches of the first round. */
	constexpr int32 HintPitches = 2;

	/** The flavour of a ball in play, by how it left the bat (degrees, feet). */
	const TCHAR* HitKind(const FOutcome& O)
	{
		if (O.Flight.bOffWall) return TEXT("OFF THE WALL!");
		if (O.Contact.LaunchDeg < 10.0) return TEXT("GROUND BALL");
		if (O.Contact.LaunchDeg < 20.0) return TEXT("LINE DRIVE");
		if (O.Contact.LaunchDeg > 50.0) return TEXT("POP UP");
		return O.DistanceFt > 320 ? TEXT("WARNING TRACK!") : TEXT("FLY BALL");
	}

	const TCHAR* StrikeWord(int32 N)
	{
		switch (N)
		{
		case 1: return TEXT("STRIKE ONE");
		case 2: return TEXT("STRIKE TWO");
		default: return TEXT("STRIKE THREE");
		}
	}

	/** The big call's look: its colour and size (canvas px) by result. */
	void CallStyle(EResult Result, FLinearColor& OutInk, float& OutSize)
	{
		switch (Result)
		{
		case EResult::Moonshot: OutInk = KC::Gold; OutSize = 150.f; break;
		case EResult::HomeRun: OutInk = KC::Sun; OutSize = 140.f; break;
		case EResult::InPlay: OutInk = KC::Sky; OutSize = 110.f; break;
		case EResult::Foul: OutInk = KC::White; OutSize = 96.f; break;
		default: OutInk = KC::Hot; OutSize = 92.f; break;
		}
	}

	FString CallTitleFor(const FOutcome& O, int32 Strikes)
	{
		const bool bOut = Strikes >= StrikesPerOut;
		switch (O.Result)
		{
		case EResult::Moonshot: return TEXT("MOONSHOT!");
		case EResult::HomeRun: return TEXT("HOME RUN!");
		case EResult::InPlay: return FString::Printf(TEXT("%d FT"), O.DistanceFt);
		case EResult::Foul: return TEXT("FOUL BALL");
		case EResult::Whiff: return bOut ? TEXT("STRUCK OUT!") : TEXT("SWING AND A MISS");
		default: return bOut ? TEXT("STRUCK OUT!") : TEXT("TAKEN");
		}
	}

	FString CallSubtitleFor(const FOutcome& O, int32 Strikes)
	{
		const bool bOut = Strikes >= StrikesPerOut;
		switch (O.Result)
		{
		case EResult::Moonshot:
		case EResult::HomeRun: return FString::Printf(TEXT("%d FT!"), O.DistanceFt);
		case EResult::InPlay: return HitKind(O);
		case EResult::Foul: return O.bStrike ? StrikeWord(Strikes) : TEXT("STILL ALIVE");
		case EResult::Whiff: return bOut ? TEXT("TURN OVER") : StrikeWord(Strikes);
		default: return bOut ? TEXT("CALLED STRIKE THREE") : StrikeWord(Strikes);
		}
	}

	/** EARLY / LATE by how many ms, or PERFECT, as one comparable key: perfect, early (negative) or late. */
	int32 TimingKey(const FOutcome& O)
	{
		if (O.Contact.Quality >= PerfectCallQuality)
		{
			return MIN_int32;
		}
		const int32 Ms = FMath::RoundToInt32(FMath::Abs(O.TimingMs));
		return O.TimingMs < 0.0 ? -1 - Ms : Ms;
	}

	/** Best first: score, then home runs, then the longest (DerbyRules' Ranking), packed into one key. */
	int64 StandingKey(const FBatter& B)
	{
		constexpr int64 HomeRunShift = 24;
		constexpr int64 ScoreShift = 36;
		const int64 Longest = FMath::Clamp<int64>(B.LongestFt, 0, (1 << HomeRunShift) - 1);
		const int64 HomeRuns = FMath::Clamp<int64>(B.HomeRuns, 0, (1 << (ScoreShift - HomeRunShift)) - 1);
		return (static_cast<int64>(FMath::Max(0, B.Score)) << ScoreShift) | (HomeRuns << HomeRunShift) | Longest;
	}
}

void SDerbyHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
}

SDerbyHud::FBatterText& SDerbyHud::BatterText(const ADerbyGame& G, int32 BatterIndex) const
{
	const TArray<FBatter>& Stats = G.GetMatch().Batters;
	if (Batters.Num() != Stats.Num())
	{
		Batters.SetNum(Stats.Num());
		PlaceLabels.SetNum(Stats.Num());
		for (int32 i = 0; i < Stats.Num(); ++i)
		{
			PlaceLabels[i] = FString::FromInt(i + 1);
			const FDerbySeat* Seat = G.GetSeat(i);
			Batters[i].Name = Seat ? Seat->Name : Stats[i].Id;
			Batters[i].NameUpper = Batters[i].Name.ToUpper();
		}
	}
	check(Batters.IsValidIndex(BatterIndex));
	return Batters[BatterIndex];
}

int32 SDerbyHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const ADerbyGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	switch (G->GetPhase())
	{
	case EDerbyPhase::Intro:
		PaintIntro(P, *G, S);
		break;
	case EDerbyPhase::Finished:
		PaintFinal(P, *G, S);
		break;
	case EDerbyPhase::StepIn:
		PaintBoard(P, *G, S);
		PaintStepIn(P, *G, S);
		break;
	default:
		PaintBug(P, *G, S);
		PaintBoard(P, *G, S);
		PaintPitch(P, *G, S);
		PaintCall(P, *G, S);
		PaintControls(P, *G, S);
		break;
	}
	return Layer;
}

void SDerbyHud::PaintBug(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FDerbySeat* Seat = G.GetBatter();
	const FBatter* Stats = G.GetBatterStats();
	if (!Seat || !Stats)
	{
		return;
	}
	FBatterText& Text = BatterText(G, G.GetBatterIndex());
	const FMatch& M = G.GetMatch();
	const float Margin = P.Size().Y * KartUI::Metric::SafeFraction;
	const FVector2D Pos(Margin, Margin);
	const FVector2D Size(500.f * S, 150.f * S);
	P.Card(Pos, Size, KC::WithAlpha(KC::Night, 0.88f), 20.f * S, 6.f * S, KC::WithAlpha(KC::White, 0.15f), 2.f * S);
	// The batter's colour down the left edge, the name and points.
	P.Box(Pos + FVector2D(10.f, 12.f) * S, FVector2D(10.f * S, Size.Y - 24.f * S), Seat->Color, 5.f * S);
	P.Text(Text.NameUpper, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(36.f * S, 34.f * S), KC::White, FVector2D(0.0, 0.5));
	P.Text(Text.Score.Get(Stats->Score, [](int32 V) { return FString::FromInt(V); }), KartUI::Font(EFont::Display, 54.f * S, 3.f * S, KC::Night),
		Pos + FVector2D(Size.X - 22.f * S, 40.f * S), KC::Sun, FVector2D(1.0, 0.5), 3.f * S);
	const FIntVector Line(Stats->HomeRuns, FMath::Min(M.Round + 1, M.Rounds), M.Rounds);
	P.Text(BugLine.Get(Line, [](const FIntVector& V) { return FString::Printf(TEXT("%d HR  ·  ROUND %d/%d"), V.X, V.Y, V.Z); }), KartUI::Font(EFont::Label, 17.f * S),
		Pos + FVector2D(36.f * S, 70.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.0, 0.5));
	// Ten balls for the turn's pitches (thrown ones spent, the live one pulsing), three for the strikes.
	const int32 Thrown = G.ShownPitch();
	const EDerbyPhase Phase = G.GetPhase();
	const bool bPitchLive = Phase == EDerbyPhase::Windup || Phase == EDerbyPhase::Pitch || Phase == EDerbyPhase::Set;
	for (int32 i = 0; i < PitchesPerTurn; ++i)
	{
		const FVector2D C = Pos + FVector2D((44.f + i * 26.f) * S, 112.f * S);
		const bool bLive = i == Thrown && bPitchLive;
		const float Pulse = bLive ? 1.f + 0.18f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 6.f)) : 1.f;
		P.Circle(C, 9.f * S * Pulse, i < Thrown ? KC::WithAlpha(KC::White, 0.18f) : KC::White, KC::Night, 2.f * S);
	}
	const int32 Strikes = G.ShownStrikes();
	P.Text(Label::Strikes(), KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(Size.X - 118.f * S, 112.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(1.0, 0.5));
	for (int32 i = 0; i < StrikesPerOut; ++i)
	{
		const FVector2D C = Pos + FVector2D(Size.X - (92.f - i * 30.f) * S, 112.f * S);
		P.Circle(C, 10.f * S, i < Strikes ? KC::Hot : KC::WithAlpha(KC::White, 0.14f), KC::Night, 2.f * S);
	}
}

void SDerbyHud::PaintBoard(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	if (M.Batters.Num() < 2)
	{
		return;
	}
	const float Margin = P.Size().Y * KartUI::Metric::SafeFraction;
	const float W = 330.f * S, RowH = 50.f * S;
	const FVector2D Pos(P.Size().X - Margin - W, Margin);
	P.Card(Pos, FVector2D(W, RowH * M.Batters.Num() + 16.f * S), KC::WithAlpha(KC::Night, 0.8f), 18.f * S, 6.f * S);
	const FDerbySeat* Up = G.GetBatter();
	for (int32 i = 0; i < M.Batters.Num(); ++i)
	{
		const FBatter& B = M.Batters[i];
		const FDerbySeat* Seat = G.GetSeat(i);
		FBatterText& Text = BatterText(G, i);
		const FVector2D Row = Pos + FVector2D(0.f, 8.f * S + i * RowH);
		const bool bUp = Up && Seat == Up;
		if (bUp)
		{
			P.Box(Row + FVector2D(6.f * S, 2.f * S), FVector2D(W - 12.f * S, RowH - 4.f * S), KC::WithAlpha(Seat->Color, 0.3f), 12.f * S);
		}
		P.Circle(Row + FVector2D(26.f * S, RowH / 2.f), 9.f * S, Seat ? Seat->Color : KC::White, KC::Night, 2.f * S);
		P.Text(Text.Name, KartUI::Font(EFont::Button, 22.f * S), Row + FVector2D(46.f * S, RowH / 2.f), KC::White, FVector2D(0.0, 0.5));
		P.Text(Text.HomeRuns.Get(B.HomeRuns, [](int32 V) { return FString::Printf(TEXT("%d HR"), V); }), KartUI::Font(EFont::Label, 15.f * S),
			Row + FVector2D(W - 112.f * S, RowH / 2.f), KC::WithAlpha(KC::White, 0.65f), FVector2D(1.0, 0.5));
		P.Text(Text.Score.Get(B.Score, [](int32 V) { return FString::FromInt(V); }), KartUI::Font(EFont::Display, 28.f * S), Row + FVector2D(W - 18.f * S, RowH / 2.f),
			bUp ? KC::Sun : KC::White, FVector2D(1.0, 0.5));
	}
}

void SDerbyHud::PaintPitch(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FPitch* Pitch = G.GetPitch();
	const EDerbyPhase Phase = G.GetPhase();
	if (!Pitch || (Phase != EDerbyPhase::Pitch && Phase != EDerbyPhase::Flight && Phase != EDerbyPhase::Result))
	{
		return;
	}
	// The radar gun, under the score bug: it pops in as the pitch leaves the hand.
	const float Margin = P.Size().Y * KartUI::Metric::SafeFraction;
	const FVector2D Pos(Margin, Margin + 166.f * S);
	const double Age = Phase == EDerbyPhase::Pitch ? G.GetPhaseAge() : LongAgo;
	const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / RadarPopSeconds), 0.f, 1.f));
	const FKartPainter F = P.Transformed(Pos + FVector2D(0.f, 26.f * S), FMath::Max(0.01f, In));
	const FLinearColor Ink = Pitch->Type == EPitchType::Curve ? KC::Grape : (Pitch->Type == EPitchType::Changeup ? KC::Mint : KC::Hot);
	F.Card(Pos, FVector2D(330.f * S, 52.f * S), KC::WithAlpha(KC::Night, 0.82f), 16.f * S, 4.f * S, Ink, 2.f * S);
	F.Text(PitchType.Get(static_cast<int32>(Pitch->Type), [](int32 V) { return FString(PitchName(static_cast<EPitchType>(V))); }), KartUI::Font(EFont::Label, 18.f * S),
		Pos + FVector2D(18.f * S, 26.f * S), Ink, FVector2D(0.0, 0.5));
	F.Text(PitchSpeed.Get(FMath::RoundToInt32(Pitch->SpeedMph), [](int32 V) { return FString::Printf(TEXT("%d MPH"), V); }), KartUI::Font(EFont::Display, 28.f * S),
		Pos + FVector2D(312.f * S, 26.f * S), KC::White, FVector2D(1.0, 0.5));
}

void SDerbyHud::PaintTiming(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	// The swing's timing, small, just after contact (or the miss).
	const FOutcome& O = *G.GetOutcome();
	const double Since = G.GetClock() - G.GetOutcomeAt();
	if (O.Result == EResult::Take || Since >= TimingShowSeconds)
	{
		return;
	}
	const bool bPerfect = O.Contact.Quality >= PerfectCallQuality;
	const FString& Word = TimingWord.Get(TimingKey(O), [&O](int32 Key)
	{
		return Key == MIN_int32 ? FString(TEXT("PERFECT!")) : FString::Printf(TEXT("%s  %d MS"), O.TimingMs < 0.0 ? TEXT("EARLY") : TEXT("LATE"), FMath::RoundToInt32(FMath::Abs(O.TimingMs)));
	});
	const float Alpha = FMath::Clamp(static_cast<float>((TimingShowSeconds - Since) / TimingFadeSeconds), 0.f, 1.f);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Since / TimingPopSeconds), 0.f, 1.f), 2.2f);
	const FVector2D At(P.Size().X / 2.f, P.Size().Y * 0.66f);
	P.Transformed(At, FMath::Max(0.01f, Pop)).Faded(Alpha).Text(Word, KartUI::Font(EFont::Display, (bPerfect ? 56.f : 40.f) * S, 4.f * S, KC::Night), At,
		bPerfect ? KC::Sun : KC::White, FVector2D(0.5, 0.5), 5.f * S);
}

void SDerbyHud::PaintTracker(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	// The broadcast's distance tracker, low and centre: the live distance from the plate along the ground.
	const FOutcome& O = *G.GetOutcome();
	const FVector Ball = G.GetBallWorld();
	const int32 Ft = FMath::RoundToInt32(FVector2D(Ball.X, Ball.Y).Size() / 100.0 / MetresPerFoot);
	const bool bGone = IsHomeRun(O.Result) && Ft >= FMath::RoundToInt32(O.Flight.FenceM / MetresPerFoot);
	const float Pulse = 1.f + (bGone ? 0.08f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 9.f)) : 0.f);
	const FVector2D Low(P.Size().X / 2.f, P.Size().Y * 0.8f);
	const FKartPainter T = P.Transformed(Low, Pulse);
	T.Card(Low - FVector2D(190.f, 52.f) * S, FVector2D(380.f, 104.f) * S, KC::WithAlpha(KC::Night, 0.8f), 52.f * S, 6.f * S, bGone ? KC::Sun : KC::WithAlpha(KC::White, 0.3f), 3.f * S);
	T.Text(LiveDistance.Get(Ft, [](int32 V) { return FString::Printf(TEXT("%d FT"), V); }), KartUI::Font(EFont::Display, 78.f * S, 4.f * S, KC::Night), Low,
		bGone ? KC::Sun : KC::White, FVector2D(0.5, 0.5), 5.f * S);
	if (G.IsSlowMo())
	{
		const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
		P.Text(Label::Deep(), KartUI::Font(EFont::Display, 60.f * S, 4.f * S, KC::Night), Centre, KC::White, FVector2D(0.5, 0.5), 6.f * S);
	}
}

void SDerbyHud::PaintCall(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FOutcome* O = G.GetOutcome();
	const EDerbyPhase Phase = G.GetPhase();
	if (!O || (Phase != EDerbyPhase::Flight && Phase != EDerbyPhase::Result && Phase != EDerbyPhase::Pitch))
	{
		return;
	}
	PaintTiming(P, G, S);
	if (Phase == EDerbyPhase::Flight && O->Result != EResult::Foul)
	{
		PaintTracker(P, G, S);
		return;
	}
	if (Phase != EDerbyPhase::Result)
	{
		return;
	}
	const double Age = G.GetPhaseAge();
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / CallPopSeconds), 0.f, 1.f), 2.f);
	const int32 Strikes = G.ShownStrikes();
	const FCallKey Key{ G.GetOutcomeAt(), Strikes };
	FLinearColor Ink = KC::White;
	float Size = 120.f;
	CallStyle(O->Result, Ink, Size);
	const bool bHomeRun = IsHomeRun(O->Result);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	const FKartPainter F = P.Transformed(Centre, FMath::Max(0.01f, Pop), bHomeRun ? -4.f : 0.f);
	F.Text(CallTitle.Get(Key, [O](const FCallKey& K) { return CallTitleFor(*O, K.Strikes); }), KartUI::Font(EFont::Display, Size * S, 7.f * S, KC::Night), Centre, Ink,
		FVector2D(0.5, 0.5), 9.f * S);
	F.Text(CallSubtitle.Get(Key, [O](const FCallKey& K) { return CallSubtitleFor(*O, K.Strikes); }), KartUI::Font(EFont::Display, (bHomeRun ? 64.f : 40.f) * S, 4.f * S, KC::Night),
		Centre + FVector2D(0.f, (Size * 0.72f + 26.f) * S), KC::White, FVector2D(0.5, 0.5), 5.f * S);
	if (O->Points > 0)
	{
		const float Rise = static_cast<float>(FMath::Min(1.0, Age / PointsRiseSeconds));
		const FVector2D At = Centre + FVector2D(0.f, (Size * 0.72f + (bHomeRun ? 110.f : 86.f) - 20.f * Rise) * S);
		P.Faded(FMath::Clamp(static_cast<float>((PointsGoneAt - Age) / PointsFadeSeconds), 0.f, 1.f))
			.Text(CallPoints.Get(O->Points, [](int32 V) { return FString::Printf(TEXT("+%d"), V); }), KartUI::Font(EFont::Display, 54.f * S, 4.f * S, KC::Night), At, KC::Mint,
				FVector2D(0.5, 0.5), 5.f * S);
	}
	if (O->Result != EResult::Foul)
	{
		PaintLanding(P, G, S, Ink);
	}
}

void SDerbyHud::PaintLanding(const FKartPainter& P, const ADerbyGame& G, float S, const FLinearColor& Ink) const
{
	// "412 FT!" over where it came down, when that spot is well in the frame.
	FVector Landing;
	if (!G.GetLanding(Landing))
	{
		return;
	}
	FVector2D At01;
	const FArcadeHudView View = FArcadeHudView::Capture(G.GetViewer());
	if (!View.Project(Landing + FVector(0.0, 0.0, TagAboveCm), At01) || At01.X <= TagMinX || At01.X >= TagMaxX || At01.Y <= TagMinY || At01.Y >= TagMaxY)
	{
		return;
	}
	const FOutcome& O = *G.GetOutcome();
	const FVector2D At = At01 * P.Size();
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(G.GetPhaseAge() / TagPopSeconds), 0.f, 1.f));
	const FKartPainter L = P.Transformed(At, FMath::Max(0.01f, Pop));
	L.Card(At - FVector2D(90.f, 30.f) * S, FVector2D(180.f, 60.f) * S, KC::WithAlpha(KC::Night, 0.85f), 30.f * S, 4.f * S, Ink, 3.f * S);
	L.Text(LandingDistance.Get(O.DistanceFt, [](int32 V) { return FString::Printf(TEXT("%d FT"), V); }), KartUI::Font(EFont::Display, 34.f * S), At, KC::White, FVector2D(0.5, 0.5));
	L.Polygon({ At + FVector2D(-12.f, 30.f) * S, At + FVector2D(12.f, 30.f) * S, At + FVector2D(0.f, 46.f) * S }, Ink);
}

void SDerbyHud::PaintControls(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FDerbySeat* Up = G.GetBatter();
	const EDerbyPhase Phase = G.GetPhase();
	if (!Up || (Phase != EDerbyPhase::Set && Phase != EDerbyPhase::Windup && Phase != EDerbyPhase::Pitch))
	{
		return;
	}
	const FVector2D Base(P.Size().X / 2.f, P.Size().Y * (1.f - KartUI::Metric::SafeFraction));
	if (Up->Kind == EDerbyKind::Phone)
	{
		PaintPhonePrompt(P, G, *Up, Base, S);
	}
	else if (Up->Kind == EDerbyKind::Local)
	{
		PaintAim(P, G, Base, S);
	}
}

void SDerbyHud::PaintPhonePrompt(const FKartPainter& P, const ADerbyGame& G, const FDerbySeat& Up, const FVector2D& Base, float S) const
{
	const bool bPitch = G.GetPhase() == EDerbyPhase::Pitch;
	const float Pulse = bPitch ? 1.f + 0.06f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 10.f)) : 1.f;
	const FVector2D C = Base - FVector2D(0.f, 50.f * S);
	const FKartPainter Pulsed = P.Transformed(C, Pulse);
	Pulsed.Card(C - FVector2D(330.f, 40.f) * S, FVector2D(660.f, 80.f) * S, KC::WithAlpha(KC::Night, 0.82f), 40.f * S, 6.f * S, Up.Color, 3.f * S);
	Pulsed.Text(bPitch ? Label::SwingNow() : Label::PhoneHint(), KartUI::Font(EFont::Title, 32.f * S), C, KC::White, FVector2D(0.5, 0.5));
	// The phone's live swing energy under the prompt.
	const float Charge = static_cast<float>(G.GetPhoneCharge());
	P.Box(C + FVector2D(-300.f, 46.f) * S, FVector2D(600.f * S * Charge, 8.f * S), Up.Color, 4.f * S);
}

void SDerbyHud::PaintAim(const FKartPainter& P, const ADerbyGame& G, const FVector2D& Base, float S) const
{
	const FDerbyLocalSwing& L = G.GetLocal();
	// The aim: a fan of the field from left to right, the needle where the swing will send it.
	const FVector2D Pivot = Base - FVector2D(0.f, 40.f * S);
	const float R = 120.f * S;
	TArray<FVector2D, TInlineAllocator<FanSegments + 2>> Fan;
	Fan.Add(Pivot);
	for (int32 i = 0; i <= FanSegments; ++i)
	{
		const float A = FMath::DegreesToRadians(FanFromDeg + FanSpanDeg * i / FanSegments);
		Fan.Add(Pivot + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
	}
	P.Polygon(Fan, KC::WithAlpha(FanGrass, 0.75f));
	// The fan's rim: the same points without the pivot.
	P.Lines(TConstArrayView<FVector2D>(Fan).RightChop(1), KC::Sun, 4.f * S);
	const float Needle = FMath::DegreesToRadians(-90.f + static_cast<float>(L.Aim) * NeedleDegPerAim);
	const FVector2D Tip = Pivot + FVector2D(FMath::Cos(Needle), FMath::Sin(Needle)) * R * 0.95f;
	P.Lines({ Pivot, Tip }, KC::White, 5.f * S);
	P.Circle(Tip, 9.f * S, KC::White, KC::Night, 2.f * S);
	P.Text(Label::Pull(), KartUI::Font(EFont::Label, 15.f * S), Pivot + FVector2D(-R * 0.85f, -R * 0.2f), KC::WithAlpha(KC::White, 0.7f), FVector2D(1.0, 0.5));
	P.Text(Label::Oppo(), KartUI::Font(EFont::Label, 15.f * S), Pivot + FVector2D(R * 0.85f, -R * 0.2f), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.0, 0.5));
	// The load meter beside it: mint to sun as it builds, hot when full.
	const FVector2D MeterPos = Pivot + FVector2D(R + 60.f * S, -96.f * S);
	const FVector2D MeterSize(28.f * S, 110.f * S);
	P.Box(MeterPos, MeterSize, KC::WithAlpha(KC::Night, 0.8f), 10.f * S, KC::WithAlpha(KC::White, 0.25f), 2.f * S);
	const float Load = static_cast<float>(L.bLoading ? L.Load : 0.0);
	const FLinearColor Fill = Load >= 0.999f ? KC::Hot : FMath::Lerp(KC::Mint, KC::Sun, Load);
	const float Inner = MeterSize.Y - 8.f * S;
	P.Box(MeterPos + FVector2D(4.f * S, MeterSize.Y - 4.f * S - Inner * Load), FVector2D(MeterSize.X - 8.f * S, Inner * Load), Fill, 7.f * S);
	P.Text(Label::Power(), KartUI::Font(EFont::Label, 13.f * S), MeterPos + FVector2D(MeterSize.X / 2.f, MeterSize.Y + 14.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.5, 0.5));
	if (G.ShownPitch() < HintPitches && G.GetMatch().Round == 0)
	{
		P.Text(Label::Controls(), KartUI::Font(EFont::Body, 21.f * S), Base - FVector2D(0.f, 200.f * S), KC::White, FVector2D(0.5, 0.5), 3.f * S);
	}
}

void SDerbyHud::PaintStepIn(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FDerbySeat* Up = G.GetBatter();
	if (!Up)
	{
		return;
	}
	const FBatterText& Text = BatterText(G, G.GetBatterIndex());
	const double Age = G.GetPhaseAge();
	const float In = KartUI::Ease::OutCubic(FMath::Clamp(static_cast<float>(Age / StepInSlideSeconds), 0.f, 1.f));
	const float Out = FMath::Clamp(static_cast<float>((G.GetTuning().StepInSeconds - Age) / StepInFadeSeconds), 0.f, 1.f);
	const FKartPainter F = P.Faded(Out);
	const float X = P.Size().Y * KartUI::Metric::SafeFraction + (1.f - In) * -400.f * S;
	const FVector2D Pos(X, P.Size().Y * 0.6f);
	F.Box(Pos, FVector2D(760.f * S, 64.f * S), Up->Color, 10.f * S);
	F.Text(Label::NowBatting(), KartUI::Font(EFont::Label, 28.f * S), Pos + FVector2D(24.f * S, 32.f * S), KC::TextOn(Up->Color), FVector2D(0.0, 0.5));
	F.Text(Text.NameUpper, KartUI::Font(EFont::Display, 110.f * S, 6.f * S, KC::Night), Pos + FVector2D(18.f * S, 130.f * S), KC::White, FVector2D(0.0, 0.5), 8.f * S);
	const FString& Line = StepInLine.Get(static_cast<int32>(Up->Kind), [](int32 Kind)
	{
		const TCHAR* How = Kind == static_cast<int32>(EDerbyKind::Phone) ? TEXT("Swing your phone like a bat")
			: Kind == static_cast<int32>(EDerbyKind::Local) ? TEXT("Hold to load, let go to swing") : TEXT("House player");
		return FString::Printf(TEXT("%s  ·  %d pitches  ·  three strikes and you're out"), How, PitchesPerTurn);
	});
	F.Text(Line, KartUI::Font(EFont::Body, 26.f * S, 2.f * S), Pos + FVector2D(24.f * S, 214.f * S), KC::WithAlpha(KC::White, 0.92f), FVector2D(0.0, 0.5), 3.f * S);
	const FMatch& M = G.GetMatch();
	if (M.Rounds > 1)
	{
		F.Text(StepInRound.Get(FIntPoint(M.Round + 1, M.Rounds), [](const FIntPoint& V) { return FString::Printf(TEXT("ROUND %d OF %d"), V.X, V.Y); }),
			KartUI::Font(EFont::Label, 22.f * S), Pos + FVector2D(560.f * S, 32.f * S), KC::TextOn(Up->Color), FVector2D(0.0, 0.5));
	}
}

void SDerbyHud::PaintIntro(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const double Age = G.GetPhaseAge();
	const float FadeIn = FMath::Clamp(static_cast<float>(Age / IntroFadeInSeconds), 0.f, 1.f);
	const float FadeOut = FMath::Clamp(static_cast<float>((G.GetTuning().IntroSeconds - Age) / IntroFadeOutSeconds), 0.f, 1.f);
	const FKartPainter F = P.Faded(FadeIn * FadeOut);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.42f);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / IntroPopSeconds), 0.f, 1.f), 1.6f);
	F.Transformed(Centre - FVector2D(0.f, 60.f * S), FMath::Max(0.01f, Pop), -3.f).Text(Label::Title(), KartUI::Font(EFont::Display, 140.f * S, 7.f * S),
		Centre - FVector2D(0.f, 60.f * S), KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	if (IntroNames.IsEmpty())
	{
		for (const FDerbySeat& Seat : G.GetSeats())
		{
			IntroNames += (IntroNames.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
		}
	}
	F.Text(IntroNames, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(Label::Rules(), KartUI::Font(EFont::Body, 24.f * S), Centre + FVector2D(0.f, 100.f * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5), 3.f * S);
}

void SDerbyHud::PaintFinal(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const TArray<int32>& Order = Ranks.Update(M.Batters.Num(), [&M](int32 i) { return StandingKey(M.Batters[i]); });
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / FinalFadeSeconds), 0.f, 1.f));
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * 0.12f);
	F.Text(Label::Final(), KartUI::Font(EFont::Display, 84.f * S, 5.f * S), Top, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FBatter& B = M.Batters[Order[i]];
		const FDerbySeat* Seat = G.GetSeat(Order[i]);
		FBatterText& Text = BatterText(G, Order[i]);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - FinalRowDelay - i * FinalRowStagger) / FinalRowPopSeconds), 0.f, 1.f));
		const FVector2D Pos(P.Size().X - 800.f * S, P.Size().Y * 0.22f + i * 100.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(360.f, 42.f) * S, FMath::Max(0.01f, In));
		const bool bWin = i == 0;
		const FLinearColor Ink = bWin ? KC::Night : KC::White;
		Row.Card(Pos, FVector2D(720.f, 84.f) * S, bWin ? KC::Paper : KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::White, 3.f * S);
		Row.Text(PlaceLabels[i], KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(44.f * S, 42.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Text.Name, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(92.f * S, 28.f * S), Ink, FVector2D(0.0, 0.5));
		const FString& Summary = Text.Summary.Get(FIntVector(B.HomeRuns, B.Moonshots, B.LongestFt), [](const FIntVector& V)
		{
			return FString::Printf(TEXT("%d HR  ·  %d moonshots  ·  longest %d ft"), V.X, V.Y, V.Z);
		});
		Row.Text(Summary, KartUI::Font(EFont::Body, 17.f * S), Pos + FVector2D(92.f * S, 60.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(Text.Score.Get(B.Score, [](int32 V) { return FString::FromInt(V); }), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(695.f * S, 42.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
