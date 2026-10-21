#include "Games/Bowling/SBowlingHud.h"

#include "Games/Bowling/BowlingGame.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Bowling;
namespace KC = KartUI::Color;
using KartUI::EFont;

namespace
{
	FText BeatText(EBowlingBeat Beat, int32 Felled)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey: return NSLOCTEXT("Bowling", "Turkey", "TURKEY!");
		case EBowlingBeat::Strike: return NSLOCTEXT("Bowling", "Strike", "STRIKE!");
		case EBowlingBeat::Spare: return NSLOCTEXT("Bowling", "Spare", "SPARE!");
		case EBowlingBeat::SplitConvert: return NSLOCTEXT("Bowling", "SplitConvert", "SPLIT CONVERTED!");
		case EBowlingBeat::Split: return NSLOCTEXT("Bowling", "Split", "SPLIT");
		case EBowlingBeat::Gutter: return NSLOCTEXT("Bowling", "Gutter", "GUTTER");
		default:
			return Felled == 0 ? NSLOCTEXT("Bowling", "Miss", "MISS") : FText::Format(NSLOCTEXT("Bowling", "Pins", "{0} {0}|plural(one=PIN,other=PINS)"), Felled);
		}
	}

	FLinearColor BeatColor(EBowlingBeat Beat)
	{
		switch (Beat)
		{
		case EBowlingBeat::Turkey:
		case EBowlingBeat::Strike: return KC::Sun;
		case EBowlingBeat::Spare:
		case EBowlingBeat::SplitConvert: return KC::Mint;
		case EBowlingBeat::Split: return KC::Grape;
		case EBowlingBeat::Gutter: return KC::Hot;
		default: return KC::Sky;
		}
	}
}

void SBowlingHud::Construct(const FArguments& InArgs)
{
	Game = InArgs._Game;
	SetVisibility(EVisibility::HitTestInvisible);
}

int32 SBowlingHud::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = LayerId;
	const ABowlingGame* G = Game.Get();
	if (!G)
	{
		return Layer;
	}
	const FKartPainter P(AllottedGeometry, OutDrawElements, Layer);
	const float S = static_cast<float>(P.Size().Y / KartUI::Metric::CanvasH);
	if (G->GetPhase() == EBowlingPhase::Intro)
	{
		PaintIntro(P, *G, S);
		return Layer;
	}
	if (G->GetPhase() == EBowlingPhase::Finished)
	{
		PaintStandings(P, *G, S);
		return Layer;
	}
	PaintSheet(P, *G, S);
	PaintUp(P, *G, S);
	PaintControls(P, *G, S);
	PaintBeat(P, *G, S);
	return Layer;
}

void SBowlingHud::PaintSheet(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	// One row per bowler: colour, name, ten frames (marks over the running total), and the score.
	const FMatch& M = G.GetMatch();
	const FVector2D Origin = P.Size() * KartUI::Metric::SafeFraction;
	const float Row = 62.f * S, NameW = 190.f * S, Box = 58.f * S, TotalW = 92.f * S;
	const FString* Up = M.Bowler();
	for (int32 r = 0; r < G.GetSeats().Num(); ++r)
	{
		const FBowlerSeat& Seat = G.GetSeats()[r];
		const FCard* Card = M.CardOf(Seat.Id);
		const bool bUp = Up && *Up == Seat.Id;
		const FVector2D Pos = Origin + FVector2D(0.f, r * (Row + 8.f * S));
		const float Width = NameW + Box * 10.f + 4.f * S + TotalW;
		P.Card(Pos, FVector2D(Width, Row), bUp ? KC::Paper : KC::WithAlpha(KC::Night, 0.78f), 14.f * S, 5.f * S,
			bUp ? Seat.Color : FLinearColor::Transparent, bUp ? 3.f * S : 0.f);
		const FLinearColor Ink = bUp ? KC::Night : KC::White;
		P.Circle(Pos + FVector2D(26.f * S, Row / 2.f), 10.f * S, Seat.Color, KC::Night, 2.f * S);
		P.Text(Seat.Name, KartUI::Font(EFont::Title, 24.f * S), Pos + FVector2D(46.f * S, Row / 2.f), Ink, FVector2D(0.0, 0.5));
		for (int32 f = 0; f < Frames; ++f)
		{
			const FVector2D BoxPos = Pos + FVector2D(NameW + f * Box, 6.f * S);
			const FFrameScore* Frame = Card && Card->FrameList.IsValidIndex(f) ? &Card->FrameList[f] : nullptr;
			const bool bCurrent = bUp && Card && FrameNumberFor(*Card) == f + 1;
			P.Box(BoxPos, FVector2D(Box - 3.f * S, Row - 12.f * S), bCurrent ? KC::WithAlpha(Seat.Color, 0.35f) : KC::WithAlpha(Ink, 0.08f), 6.f * S,
				bCurrent ? Seat.Color : FLinearColor::Transparent, bCurrent ? 2.f * S : 0.f);
			if (!Frame)
			{
				continue;
			}
			const TArray<FString> Marks = FrameMarks(*Frame);
			const FString Marked = FString::Join(Marks, TEXT(" "));
			const bool bStrikeMark = Marks.Num() > 0 && Marks[0] == TEXT("X");
			P.Text(Marked, KartUI::Font(EFont::Label, 15.f * S), BoxPos + FVector2D(Box - 9.f * S, 4.f * S),
				bStrikeMark ? (bUp ? KC::SunDeep : KC::Sun) : Ink, FVector2D(1.0, 0.0));
			if (Frame->Cumulative.IsSet())
			{
				P.Text(FString::FromInt(Frame->Cumulative.GetValue()), KartUI::Font(EFont::Title, 20.f * S),
					BoxPos + FVector2D(4.f * S, Row - 16.f * S), Ink, FVector2D(0.0, 1.0));
			}
		}
		P.Text(FString::FromInt(Card ? Card->Score : 0), KartUI::Font(EFont::Display, 34.f * S),
			Pos + FVector2D(Width - 16.f * S, Row / 2.f), Ink, FVector2D(1.0, 0.5));
	}
}

void SBowlingHud::PaintUp(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const FBowlerSeat* Bowler = G.GetBowler();
	const FCard* Card = Bowler ? G.GetMatch().CardOf(Bowler->Id) : nullptr;
	if (!Bowler || !Card)
	{
		return;
	}
	const FVector2D TopRight(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction);
	const FString Frame = FString::Printf(TEXT("FRAME %d / 10"), FrameNumberFor(*Card));
	const FString BallText = FString::Printf(TEXT("BALL %d"), G.GetMatch().RollInFrame);
	P.Text(Frame, KartUI::Font(EFont::Display, 46.f * S, 3.f * S), TopRight, KC::White, FVector2D(1.0, 0.0), 4.f * S);
	P.Text(BallText, KartUI::Font(EFont::Label, 22.f * S), TopRight + FVector2D(0.f, 62.f * S), KC::Sun, FVector2D(1.0, 0.0), 2.f * S);
	P.Text(FString::Printf(TEXT("%d %s STANDING"), G.GetStanding(), G.GetStanding() == 1 ? TEXT("PIN") : TEXT("PINS")), KartUI::Font(EFont::Label, 18.f * S), TopRight + FVector2D(0.f, 92.f * S),
		KC::WithAlpha(KC::White, 0.8f), FVector2D(1.0, 0.0), 2.f * S);
}

void SBowlingHud::PaintControls(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const FBowlerSeat* Bowler = G.GetBowler();
	if (!Bowler || G.GetPhase() != EBowlingPhase::Aiming)
	{
		return;
	}
	// Lower right, clear of the bowler standing in the middle of the shot.
	const FVector2D Centre(P.Size().X * (1.f - KartUI::Metric::SafeFraction) - 420.f * S, P.Size().Y * 0.86f);
	const FText Up = FText::Format(NSLOCTEXT("Bowling", "Up", "{0} is up"), FText::FromString(Bowler->Name));
	if (Bowler->Kind != EBowlerKind::Local)
	{
		const FText Hint = Bowler->Kind == EBowlerKind::Phone ? NSLOCTEXT("Bowling", "PhoneHint", "Swing your phone and let go to bowl")
			: NSLOCTEXT("Bowling", "BotHint", "Lining up...");
		P.Card(Centre - FVector2D(330.f, 46.f) * S, FVector2D(660.f, 92.f) * S, KC::WithAlpha(KC::Night, 0.82f), 22.f * S, 6.f * S, Bowler->Color, 3.f * S);
		P.Text(Up, KartUI::Font(EFont::Title, 32.f * S), Centre - FVector2D(0.f, 14.f * S), KC::White, FVector2D(0.5, 0.5));
		P.Text(Hint, KartUI::Font(EFont::Body, 20.f * S), Centre + FVector2D(0.f, 22.f * S), KC::WithAlpha(KC::White, 0.8f), FVector2D(0.5, 0.5));
		return;
	}
	const FLocalBowl& L = G.GetLocal();
	P.Card(Centre - FVector2D(420.f, 70.f) * S, FVector2D(840.f, 140.f) * S, KC::WithAlpha(KC::Night, 0.82f), 24.f * S, 6.f * S, Bowler->Color, 3.f * S);
	P.Text(Up, KartUI::Font(EFont::Title, 30.f * S), Centre + FVector2D(0.f, -40.f * S), KC::White, FVector2D(0.5, 0.5));
	const FText Hint = L.bGripped
		? NSLOCTEXT("Bowling", "Release", "Let go to bowl  ·  flick the mouse up for a hard ball")
		: NSLOCTEXT("Bowling", "Grip", "A/D or stick: line  ·  Q/E or right stick: hook  ·  hold SPACE, RT or click to grip");
	P.Text(Hint, KartUI::Font(EFont::Body, 19.f * S), Centre + FVector2D(0.f, -6.f * S), KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5));
	// Power meter, with the 7-9 m/s sweet band of the web's speed dial marked.
	const FVector2D BarPos = Centre + FVector2D(-300.f * S, 26.f * S);
	const FVector2D BarSize(600.f * S, 22.f * S);
	P.Box(BarPos, BarSize, KC::WithAlpha(KC::White, 0.12f), 11.f * S);
	P.Box(BarPos + FVector2D(BarSize.X * 0.6f, 0.f), FVector2D(BarSize.X * 0.4f, BarSize.Y), KC::WithAlpha(KC::Mint, 0.25f), 11.f * S);
	const float Power = static_cast<float>(L.bGripped ? L.Power : 0.0);
	if (Power > 0.f)
	{
		P.Gradient(BarPos, FVector2D(FMath::Max(BarSize.Y, BarSize.X * Power), BarSize.Y), KC::Sky, Power > 0.6f ? KC::Mint : KC::Sun, false, 11.f * S);
	}
	P.Text(FString::Printf(TEXT("%.1f m/s"), SpeedForPower(FMath::Max(0.04f, Power))), KartUI::Font(EFont::Label, 16.f * S),
		BarPos + FVector2D(BarSize.X + 14.f * S, BarSize.Y / 2.f), KC::White, FVector2D(0.0, 0.5));
	const FString Hook = FMath::Abs(L.Curve) < 0.08 ? TEXT("STRAIGHT") : (L.Curve < 0.0 ? TEXT("HOOK LEFT") : TEXT("HOOK RIGHT"));
	P.Text(Hook, KartUI::Font(EFont::Label, 16.f * S), BarPos - FVector2D(14.f * S, -BarSize.Y / 2.f), KC::Sun, FVector2D(1.0, 0.5));
}

void SBowlingHud::PaintBeat(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const double Age = G.GetClock() - G.GetLastBeatAt();
	if (Age < 0.0 || Age > 1.8 || G.GetLastBeat() == EBowlingBeat::None)
	{
		return;
	}
	const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / 0.35), 0.f, 1.f));
	const float Out = FMath::Clamp(static_cast<float>((1.8 - Age) / 0.3), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.4f);
	const FKartPainter Pop = P.Transformed(Centre, FMath::Max(0.01f, In), -4.f * (1.f - In)).Faded(Out);
	const EBowlingBeat Beat = G.GetLastBeat();
	const bool bBig = Beat == EBowlingBeat::Strike || Beat == EBowlingBeat::Turkey || Beat == EBowlingBeat::Spare || Beat == EBowlingBeat::SplitConvert;
	Pop.Text(BeatText(Beat, G.GetLastFelled()), KartUI::Font(EFont::Display, (bBig ? 132.f : 84.f) * S, 6.f * S, KC::Night), Centre,
		BeatColor(Beat), FVector2D(0.5, 0.5), 8.f * S);
}

void SBowlingHud::PaintIntro(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const float A = FMath::Clamp(static_cast<float>(G.GetPhaseAge() / 0.5), 0.f, 1.f) * FMath::Clamp(static_cast<float>((2.4 - G.GetPhaseAge()) / 0.4), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre = P.Size() / 2.f;
	F.Text(NSLOCTEXT("Bowling", "Title", "BOWLING"), KartUI::Font(EFont::Display, 150.f * S, 7.f * S), Centre - FVector2D(0.f, 60.f * S), KC::Sun, FVector2D(0.5, 0.5), 9.f * S);
	FString Names;
	for (const FBowlerSeat& Seat : G.GetSeats())
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
	}
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 60.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(NSLOCTEXT("Bowling", "Rules", "Ten frames. Knock them all down."), KartUI::Font(EFont::Body, 24.f * S), Centre + FVector2D(0.f, 110.f * S),
		KC::WithAlpha(KC::White, 0.85f), FVector2D(0.5, 0.5), 3.f * S);
}

void SBowlingHud::PaintStandings(const FKartPainter& P, const ABowlingGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const TArray<FString> Order = Standings(M);
	const float A = FMath::Clamp(static_cast<float>(G.GetFinishedAge() / 0.5), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.22f);
	F.Text(NSLOCTEXT("Bowling", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FBowlerSeat* Seat = G.GetSeats().FindByPredicate([&](const FBowlerSeat& X) { return X.Id == Order[i]; });
		const FCard* Card = M.CardOf(Order[i]);
		if (!Seat || !Card)
		{
			continue;
		}
		const float Delay = 0.3f + i * 0.18f;
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((G.GetFinishedAge() - Delay) / 0.4), 0.f, 1.f));
		const FVector2D Pos(P.Size().X / 2.f - 360.f * S, P.Size().Y * 0.32f + i * 104.f * S);
		const FKartPainter Row = F.Transformed(Pos + FVector2D(360.f, 44.f) * S, FMath::Max(0.01f, In));
		Row.Card(Pos, FVector2D(720.f, 88.f) * S, i == 0 ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), 22.f * S, 6.f * S, Seat->Color, 3.f * S);
		const FLinearColor Ink = i == 0 ? KC::Night : KC::White;
		Row.Text(FString::FromInt(i + 1), KartUI::Font(EFont::Display, 52.f * S), Pos + FVector2D(48.f * S, 44.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Seat->Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 44.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(FString::FromInt(Card->Score), KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(690.f * S, 44.f * S), Ink, FVector2D(1.0, 0.5));
	}
	if (G.GetFinishedAge() > 2.0)
	{
		const float Pulse = 0.75f + 0.25f * FMath::Sin(static_cast<float>(G.GetClock()) * 3.f);
		F.Faded(Pulse).Text(NSLOCTEXT("Bowling", "Again", "ENTER / A  play again     ESC / B  menu"), KartUI::Font(EFont::Label, 24.f * S),
			FVector2D(P.Size().X / 2.f, P.Size().Y * 0.9f), KC::White, FVector2D(0.5, 0.5), 3.f * S);
	}
}
