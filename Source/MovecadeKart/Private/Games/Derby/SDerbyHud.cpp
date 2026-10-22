#include "Games/Derby/SDerbyHud.h"

#include "Games/Derby/DerbyGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Derby;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	/** The flavour of a ball in play, by how it left the bat. */
	FString HitKind(const FOutcome& O)
	{
		if (O.Flight.bOffWall) return TEXT("OFF THE WALL!");
		if (O.Contact.LaunchDeg < 10.0) return TEXT("GROUND BALL");
		if (O.Contact.LaunchDeg < 20.0) return TEXT("LINE DRIVE");
		if (O.Contact.LaunchDeg > 50.0) return TEXT("POP UP");
		return O.DistanceFt > 320 ? TEXT("WARNING TRACK!") : TEXT("FLY BALL");
	}

	FString StrikeWord(int32 N)
	{
		switch (N)
		{
		case 1: return TEXT("STRIKE ONE");
		case 2: return TEXT("STRIKE TWO");
		default: return TEXT("STRIKE THREE");
		}
	}
}

void SDerbyHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
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
	const float M = P.Size().Y * KartUI::Metric::SafeFraction;
	const FVector2D Pos(M, M);
	const FVector2D Size(500.f * S, 150.f * S);
	P.Card(Pos, Size, KC::WithAlpha(KC::Night, 0.88f), 20.f * S, 6.f * S, KC::WithAlpha(KC::White, 0.15f), 2.f * S);
	// The batter's colour down the left edge, the name and points.
	P.Box(Pos + FVector2D(10.f, 12.f) * S, FVector2D(10.f * S, Size.Y - 24.f * S), Seat->Color, 5.f * S);
	P.Text(Seat->Name.ToUpper(), KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(36.f * S, 34.f * S), KC::White, FVector2D(0.0, 0.5));
	P.Text(FString::FromInt(Stats->Score), KartUI::Font(EFont::Display, 54.f * S, 3.f * S, KC::Night), Pos + FVector2D(Size.X - 22.f * S, 40.f * S), KC::Sun, FVector2D(1.0, 0.5), 3.f * S);
	P.Text(FString::Printf(TEXT("%d HR  ·  ROUND %d/%d"), Stats->HomeRuns, FMath::Min(G.GetMatch().Round + 1, G.GetMatch().Rounds), G.GetMatch().Rounds), KartUI::Font(EFont::Label, 17.f * S),
		Pos + FVector2D(36.f * S, 70.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.0, 0.5));
	// Ten balls for the turn's pitches (thrown ones spent), three for the strikes.
	const int32 Thrown = G.ShownPitch();
	for (int32 i = 0; i < PitchesPerTurn; ++i)
	{
		const FVector2D C = Pos + FVector2D((44.f + i * 26.f) * S, 112.f * S);
		const bool bLive = i == Thrown && (G.GetPhase() == EDerbyPhase::Windup || G.GetPhase() == EDerbyPhase::Pitch || G.GetPhase() == EDerbyPhase::Set);
		const float Pulse = bLive ? 1.f + 0.18f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 6.f)) : 1.f;
		P.Circle(C, 9.f * S * Pulse, i < Thrown ? KC::WithAlpha(KC::White, 0.18f) : KC::White, KC::Night, 2.f * S);
	}
	const int32 Strikes = G.ShownStrikes();
	P.Text(NSLOCTEXT("Derby", "Strikes", "K"), KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(Size.X - 118.f * S, 112.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(1.0, 0.5));
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
		const FDerbySeat* Seat = G.SeatOf(B.Id);
		const FVector2D Row = Pos + FVector2D(0.f, 8.f * S + i * RowH);
		const bool bUp = Up && Seat == Up;
		if (bUp)
		{
			P.Box(Row + FVector2D(6.f * S, 2.f * S), FVector2D(W - 12.f * S, RowH - 4.f * S), KC::WithAlpha(Seat->Color, 0.3f), 12.f * S);
		}
		P.Circle(Row + FVector2D(26.f * S, RowH / 2.f), 9.f * S, Seat ? Seat->Color : KC::White, KC::Night, 2.f * S);
		P.Text(Seat ? Seat->Name : B.Id, KartUI::Font(EFont::Button, 22.f * S), Row + FVector2D(46.f * S, RowH / 2.f), KC::White, FVector2D(0.0, 0.5));
		P.Text(FString::Printf(TEXT("%d HR"), B.HomeRuns), KartUI::Font(EFont::Label, 15.f * S), Row + FVector2D(W - 112.f * S, RowH / 2.f), KC::WithAlpha(KC::White, 0.65f), FVector2D(1.0, 0.5));
		P.Text(FString::FromInt(B.Score), KartUI::Font(EFont::Display, 28.f * S), Row + FVector2D(W - 18.f * S, RowH / 2.f), bUp ? KC::Sun : KC::White, FVector2D(1.0, 0.5));
	}
}

void SDerbyHud::PaintPitch(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FPitch* Pitch = G.GetPitch();
	if (!Pitch || (G.GetPhase() != EDerbyPhase::Pitch && G.GetPhase() != EDerbyPhase::Flight && G.GetPhase() != EDerbyPhase::Result))
	{
		return;
	}
	// The radar gun, under the score bug.
	const float M = P.Size().Y * KartUI::Metric::SafeFraction;
	const FVector2D Pos(M, M + 166.f * S);
	const double Age = G.GetPhase() == EDerbyPhase::Pitch ? G.GetPhaseAge() : 10.0;
	const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.25), 0.f, 1.f));
	const FKartPainter F = P.Transformed(Pos + FVector2D(0.f, 26.f * S), FMath::Max(0.01f, In));
	const FLinearColor Ink = Pitch->Type == EPitchType::Curve ? KC::Grape : (Pitch->Type == EPitchType::Changeup ? KC::Mint : KC::Hot);
	F.Card(Pos, FVector2D(330.f * S, 52.f * S), KC::WithAlpha(KC::Night, 0.82f), 16.f * S, 4.f * S, Ink, 2.f * S);
	F.Text(PitchName(Pitch->Type), KartUI::Font(EFont::Label, 18.f * S), Pos + FVector2D(18.f * S, 26.f * S), Ink, FVector2D(0.0, 0.5));
	F.Text(FString::Printf(TEXT("%d MPH"), FMath::RoundToInt32(Pitch->SpeedMph)), KartUI::Font(EFont::Display, 28.f * S), Pos + FVector2D(312.f * S, 26.f * S), KC::White, FVector2D(1.0, 0.5));
}

void SDerbyHud::PaintCall(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FOutcome* O = G.GetOutcome();
	const EDerbyPhase Phase = G.GetPhase();
	if (!O || (Phase != EDerbyPhase::Flight && Phase != EDerbyPhase::Result && Phase != EDerbyPhase::Pitch))
	{
		return;
	}
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	const double Since = G.GetClock() - G.GetOutcomeAt();
	// The swing's timing, small, just after contact (or the miss).
	if (O->Result != EResult::Take && Since < 1.6)
	{
		const double Ms = O->TimingMs;
		const bool bPerfect = O->Contact.Quality >= 0.97;
		const FString Word = bPerfect ? FString(TEXT("PERFECT!")) : FString::Printf(TEXT("%s  %d MS"), Ms < 0.0 ? TEXT("EARLY") : TEXT("LATE"), FMath::RoundToInt32(FMath::Abs(Ms)));
		const float A = FMath::Clamp(static_cast<float>((1.6 - Since) / 0.3), 0.f, 1.f);
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Since / 0.18), 0.f, 1.f), 2.2f);
		const FVector2D At(P.Size().X / 2.f, P.Size().Y * 0.66f);
		P.Transformed(At, FMath::Max(0.01f, Pop)).Faded(A).Text(Word, KartUI::Font(EFont::Display, (bPerfect ? 56.f : 40.f) * S, 4.f * S, KC::Night), At,
			bPerfect ? KC::Sun : KC::White, FVector2D(0.5, 0.5), 5.f * S);
	}
	// The live distance while it carries, from the plate along the ground.
	if (Phase == EDerbyPhase::Flight && O->Result != EResult::Foul)
	{
		const FVector Ball = G.GetBallWorld();
		const int32 Ft = FMath::RoundToInt32(FVector2D(Ball.X, Ball.Y).Size() / 100.0 / MetresPerFoot);
		const bool bGone = IsHomeRun(O->Result) && Ft >= FMath::RoundToInt32(O->Flight.FenceM / MetresPerFoot);
		const float Pulse = 1.f + (bGone ? 0.08f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 9.f)) : 0.f);
		// The broadcast's distance tracker, low and centre.
		const FVector2D Low(P.Size().X / 2.f, P.Size().Y * 0.8f);
		const FKartPainter T = P.Transformed(Low, Pulse);
		T.Card(Low - FVector2D(190.f, 52.f) * S, FVector2D(380.f, 104.f) * S, KC::WithAlpha(KC::Night, 0.8f), 52.f * S, 6.f * S, bGone ? KC::Sun : KC::WithAlpha(KC::White, 0.3f), 3.f * S);
		T.Text(FString::Printf(TEXT("%d FT"), Ft), KartUI::Font(EFont::Display, 78.f * S, 4.f * S, KC::Night), Low, bGone ? KC::Sun : KC::White, FVector2D(0.5, 0.5), 5.f * S);
		if (G.IsSlowMo())
		{
			P.Text(NSLOCTEXT("Derby", "Deep", "IT'S DEEP..."), KartUI::Font(EFont::Display, 60.f * S, 4.f * S, KC::Night), Centre, KC::White, FVector2D(0.5, 0.5), 6.f * S);
		}
		return;
	}
	if (Phase != EDerbyPhase::Result)
	{
		return;
	}
	const double Age = G.GetPhaseAge();
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.3), 0.f, 1.f), 2.f);
	FString Title, Sub;
	FLinearColor Ink = KC::White;
	float Size = 120.f;
	switch (O->Result)
	{
	case EResult::Moonshot:
		Title = TEXT("MOONSHOT!");
		Sub = FString::Printf(TEXT("%d FT!"), O->DistanceFt);
		Ink = KC::Gold;
		Size = 150.f;
		break;
	case EResult::HomeRun:
		Title = TEXT("HOME RUN!");
		Sub = FString::Printf(TEXT("%d FT!"), O->DistanceFt);
		Ink = KC::Sun;
		Size = 140.f;
		break;
	case EResult::InPlay:
		Title = FString::Printf(TEXT("%d FT"), O->DistanceFt);
		Sub = HitKind(*O);
		Ink = KC::Sky;
		Size = 110.f;
		break;
	case EResult::Foul:
		Title = TEXT("FOUL BALL");
		Sub = O->bStrike ? StrikeWord(G.ShownStrikes()) : FString(TEXT("STILL ALIVE"));
		Ink = KC::White;
		Size = 96.f;
		break;
	case EResult::Whiff:
		Title = G.ShownStrikes() >= StrikesPerOut ? FString(TEXT("STRUCK OUT!")) : FString(TEXT("SWING AND A MISS"));
		Sub = G.ShownStrikes() >= StrikesPerOut ? FString(TEXT("TURN OVER")) : StrikeWord(G.ShownStrikes());
		Ink = KC::Hot;
		Size = 92.f;
		break;
	default:
		Title = G.ShownStrikes() >= StrikesPerOut ? FString(TEXT("STRUCK OUT!")) : FString(TEXT("TAKEN"));
		Sub = G.ShownStrikes() >= StrikesPerOut ? FString(TEXT("CALLED STRIKE THREE")) : StrikeWord(G.ShownStrikes());
		Ink = KC::Hot;
		Size = 92.f;
		break;
	}
	const FKartPainter F = P.Transformed(Centre, FMath::Max(0.01f, Pop), IsHomeRun(O->Result) ? -4.f : 0.f);
	F.Text(Title, KartUI::Font(EFont::Display, Size * S, 7.f * S, KC::Night), Centre, Ink, FVector2D(0.5, 0.5), 9.f * S);
	F.Text(Sub, KartUI::Font(EFont::Display, (IsHomeRun(O->Result) ? 64.f : 40.f) * S, 4.f * S, KC::Night), Centre + FVector2D(0.f, (Size * 0.72f + 26.f) * S), KC::White,
		FVector2D(0.5, 0.5), 5.f * S);
	if (O->Points > 0)
	{
		const float Rise = static_cast<float>(FMath::Min(1.0, Age / 1.2));
		const FVector2D At = Centre + FVector2D(0.f, (Size * 0.72f + (IsHomeRun(O->Result) ? 110.f : 86.f) - 20.f * Rise) * S);
		P.Faded(FMath::Clamp(static_cast<float>((3.5 - Age) / 0.4), 0.f, 1.f)).Text(FString::Printf(TEXT("+%d"), O->Points), KartUI::Font(EFont::Display, 54.f * S, 4.f * S, KC::Night), At, KC::Mint,
			FVector2D(0.5, 0.5), 5.f * S);
	}
	// "412 FT!" over where it came down.
	FVector Landing;
	FVector2D At01;
	if (O->Result != EResult::Foul && G.GetLanding(Landing) && G.ScreenOf(Landing + FVector(0.0, 0.0, 600.0), At01) && At01.X > 0.05 && At01.X < 0.95 && At01.Y > 0.44 && At01.Y < 0.92)
	{
		const FVector2D At = At01 * P.Size();
		const float A = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.25), 0.f, 1.f));
		const FKartPainter L = P.Transformed(At, FMath::Max(0.01f, A));
		L.Card(At - FVector2D(90.f, 30.f) * S, FVector2D(180.f, 60.f) * S, KC::WithAlpha(KC::Night, 0.85f), 30.f * S, 4.f * S, Ink, 3.f * S);
		L.Text(FString::Printf(TEXT("%d FT"), O->DistanceFt), KartUI::Font(EFont::Display, 34.f * S), At, KC::White, FVector2D(0.5, 0.5));
		L.Polygon({ At + FVector2D(-12.f, 30.f) * S, At + FVector2D(12.f, 30.f) * S, At + FVector2D(0.f, 46.f) * S }, Ink);
	}
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
		const float Pulse = Phase == EDerbyPhase::Pitch ? 1.f + 0.06f * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * 10.f)) : 1.f;
		const FVector2D C = Base - FVector2D(0.f, 50.f * S);
		P.Transformed(C, Pulse).Card(C - FVector2D(330.f, 40.f) * S, FVector2D(660.f, 80.f) * S, KC::WithAlpha(KC::Night, 0.82f), 40.f * S, 6.f * S, Up->Color, 3.f * S);
		P.Transformed(C, Pulse).Text(Phase == EDerbyPhase::Pitch ? NSLOCTEXT("Derby", "SwingNow", "SWING YOUR PHONE!") : NSLOCTEXT("Derby", "PhoneHint", "Swing your phone like a bat"),
			KartUI::Font(EFont::Title, 32.f * S), C, KC::White, FVector2D(0.5, 0.5));
		const float Charge = static_cast<float>(G.GetPhoneCharge());
		P.Box(C + FVector2D(-300.f, 46.f) * S, FVector2D(600.f * S * Charge, 8.f * S), Up->Color, 4.f * S);
		return;
	}
	if (Up->Kind != EDerbyKind::Local)
	{
		return;
	}
	const FDerbyLocal& L = G.GetLocal();
	// The aim: a fan of the field from left to right, the needle where the swing will send it.
	const FVector2D Pivot = Base - FVector2D(0.f, 40.f * S);
	const float R = 120.f * S;
	TArray<FVector2D> Fan;
	Fan.Add(Pivot);
	for (int32 i = 0; i <= 16; ++i)
	{
		const float A = FMath::DegreesToRadians(-135.f + 90.f * i / 16.f);
		Fan.Add(Pivot + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
	}
	P.Polygon(Fan, KC::WithAlpha(FLinearColor(0.12f, 0.45f, 0.2f), 0.75f));
	TArray<FVector2D> Arc;
	for (int32 i = 0; i <= 16; ++i)
	{
		const float A = FMath::DegreesToRadians(-135.f + 90.f * i / 16.f);
		Arc.Add(Pivot + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R);
	}
	P.Lines(Arc, KC::Sun, 4.f * S);
	const float Needle = FMath::DegreesToRadians(-90.f + static_cast<float>(L.Aim) * 36.f);
	P.Lines({ Pivot, Pivot + FVector2D(FMath::Cos(Needle), FMath::Sin(Needle)) * R * 0.95f }, KC::White, 5.f * S);
	P.Circle(Pivot + FVector2D(FMath::Cos(Needle), FMath::Sin(Needle)) * R * 0.95f, 9.f * S, KC::White, KC::Night, 2.f * S);
	P.Text(TEXT("PULL"), KartUI::Font(EFont::Label, 15.f * S), Pivot + FVector2D(-R * 0.85f, -R * 0.2f), KC::WithAlpha(KC::White, 0.7f), FVector2D(1.0, 0.5));
	P.Text(TEXT("OPPO"), KartUI::Font(EFont::Label, 15.f * S), Pivot + FVector2D(R * 0.85f, -R * 0.2f), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.0, 0.5));
	// The load meter beside it.
	const FVector2D MeterPos = Pivot + FVector2D(R + 60.f * S, -96.f * S);
	const FVector2D MeterSize(28.f * S, 110.f * S);
	P.Box(MeterPos, MeterSize, KC::WithAlpha(KC::Night, 0.8f), 10.f * S, KC::WithAlpha(KC::White, 0.25f), 2.f * S);
	const float Load = static_cast<float>(L.bLoading ? L.Load : 0.0);
	const FLinearColor Fill = Load >= 0.999f ? KC::Hot : FMath::Lerp(KC::Mint, KC::Sun, Load);
	P.Box(MeterPos + FVector2D(4.f * S, MeterSize.Y - 4.f * S - (MeterSize.Y - 8.f * S) * Load), FVector2D(MeterSize.X - 8.f * S, (MeterSize.Y - 8.f * S) * Load), Fill, 7.f * S);
	P.Text(TEXT("POWER"), KartUI::Font(EFont::Label, 13.f * S), MeterPos + FVector2D(MeterSize.X / 2.f, MeterSize.Y + 14.f * S), KC::WithAlpha(KC::White, 0.7f), FVector2D(0.5, 0.5));
	if (G.ShownPitch() < 2 && G.GetMatch().Round == 0)
	{
		P.Text(NSLOCTEXT("Derby", "Controls", "Hold Space, RT or click to load  ·  let go to swing  ·  mouse, A/D or stick to aim"), KartUI::Font(EFont::Body, 21.f * S),
			Base - FVector2D(0.f, 200.f * S), KC::White, FVector2D(0.5, 0.5), 3.f * S);
	}
}

void SDerbyHud::PaintStepIn(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FDerbySeat* Up = G.GetBatter();
	if (!Up)
	{
		return;
	}
	const double Age = G.GetPhaseAge();
	const float In = KartUI::Ease::OutCubic(FMath::Clamp(static_cast<float>(Age / 0.35), 0.f, 1.f));
	const float Out = FMath::Clamp(static_cast<float>((2.3 - Age) / 0.3), 0.f, 1.f);
	const FKartPainter F = P.Faded(Out);
	const float X = P.Size().Y * KartUI::Metric::SafeFraction + (1.f - In) * -400.f * S;
	const FVector2D Pos(X, P.Size().Y * 0.6f);
	F.Box(Pos, FVector2D(760.f * S, 64.f * S), Up->Color, 10.f * S);
	F.Text(NSLOCTEXT("Derby", "NowBatting", "NOW BATTING"), KartUI::Font(EFont::Label, 28.f * S), Pos + FVector2D(24.f * S, 32.f * S), KC::TextOn(Up->Color), FVector2D(0.0, 0.5));
	F.Text(Up->Name.ToUpper(), KartUI::Font(EFont::Display, 110.f * S, 6.f * S, KC::Night), Pos + FVector2D(18.f * S, 130.f * S), KC::White, FVector2D(0.0, 0.5), 8.f * S);
	FString Line;
	switch (Up->Kind)
	{
	case EDerbyKind::Phone: Line = TEXT("Swing your phone like a bat"); break;
	case EDerbyKind::Local: Line = TEXT("Hold to load, let go to swing"); break;
	default: Line = TEXT("House player"); break;
	}
	const FMatch& M = G.GetMatch();
	F.Text(FString::Printf(TEXT("%s  ·  %d pitches  ·  three strikes and you're out"), *Line, PitchesPerTurn), KartUI::Font(EFont::Body, 26.f * S, 2.f * S),
		Pos + FVector2D(24.f * S, 214.f * S), KC::WithAlpha(KC::White, 0.92f), FVector2D(0.0, 0.5), 3.f * S);
	if (M.Rounds > 1)
	{
		F.Text(FString::Printf(TEXT("ROUND %d OF %d"), M.Round + 1, M.Rounds), KartUI::Font(EFont::Label, 22.f * S), Pos + FVector2D(560.f * S, 32.f * S), KC::TextOn(Up->Color), FVector2D(0.0, 0.5));
	}
}

void SDerbyHud::PaintIntro(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const double Age = G.GetPhaseAge();
	const float A = FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>((3.4 - Age) / 0.4), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.42f);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.6), 0.f, 1.f), 1.6f);
	F.Transformed(Centre - FVector2D(0.f, 60.f * S), FMath::Max(0.01f, Pop), -3.f).Text(NSLOCTEXT("Derby", "Title", "HOME RUN DERBY"), KartUI::Font(EFont::Display, 140.f * S, 7.f * S),
		Centre - FVector2D(0.f, 60.f * S), KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	FString Names;
	for (const FDerbySeat& Seat : G.GetSeats())
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
	}
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(NSLOCTEXT("Derby", "Rules", "Ten pitches each. Time your swing, aim it, send it. Home runs score their distance."), KartUI::Font(EFont::Body, 24.f * S),
		Centre + FVector2D(0.f, 100.f * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5), 3.f * S);
}

void SDerbyHud::PaintFinal(const FKartPainter& P, const ADerbyGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const TArray<int32> Order = Ranking(M);
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / 0.5), 0.f, 1.f));
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * 0.12f);
	F.Text(NSLOCTEXT("Derby", "Final", "FINAL STANDINGS"), KartUI::Font(EFont::Display, 84.f * S, 5.f * S), Top, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FBatter& B = M.Batters[Order[i]];
		const FDerbySeat* Seat = G.SeatOf(B.Id);
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - 0.5 - i * 0.18) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X - 800.f * S, P.Size().Y * 0.22f + i * 100.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(360.f, 42.f) * S, FMath::Max(0.01f, In));
		const bool bWin = i == 0;
		const FLinearColor Ink = bWin ? KC::Night : KC::White;
		Row.Card(Pos, FVector2D(720.f, 84.f) * S, bWin ? KC::Paper : KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::White, 3.f * S);
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 48.f * S), Pos + FVector2D(44.f * S, 42.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Seat ? Seat->Name : B.Id, KartUI::Font(EFont::Title, 32.f * S), Pos + FVector2D(92.f * S, 28.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(FString::Printf(TEXT("%d HR  ·  %d moonshots  ·  longest %d ft"), B.HomeRuns, B.Moonshots, B.LongestFt), KartUI::Font(EFont::Body, 17.f * S),
			Pos + FVector2D(92.f * S, 60.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(B.Score), KartUI::Font(EFont::Display, 50.f * S), Pos + FVector2D(695.f * S, 42.f * S), Ink, FVector2D(1.0, 0.5));
	}
}
