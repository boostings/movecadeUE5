#include "Games/Samurai/SSamuraiHud.h"

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

	/** A line of text's face, size, outline and drop shadow, in 1080-line canvas units. */
	struct FTextStyle
	{
		EFont Face;
		float Size;
		float Outline;
		float Shadow;
	};

	/** A card's corner radius, drop shadow and outline width, in canvas units. */
	struct FCardStyle
	{
		float Radius;
		float Shadow;
		float OutlineWidth;
	};

	FSlateFontInfo Font(const FTextStyle& Style, float S)
	{
		return KartUI::Font(Style.Face, Style.Size * S, Style.Outline * S, KC::Night);
	}

	/** An entrance: it pops in over Seconds with an OutBack overshoot of Back. */
	struct FPop
	{
		double Seconds;
		float Back;
	};

	// ---- Layout and style, in canvas units (times S on screen); seconds for timings; alphas 0..1 ----
	namespace Clock
	{
		const FVector2D CardSize(220.f, 112.f);
		constexpr FCardStyle Card{ 26.f, 6.f, 3.f };
		constexpr float CardAlpha = 0.82f;
		constexpr FTextStyle Digits{ EFont::Display, 64.f, 3.f, 4.f };
		constexpr FTextStyle Wave{ EFont::Label, 18.f, 0.f, 0.f };
		constexpr float DigitsY = 46.f;
		constexpr float WaveY = 94.f;
		/** The last seconds pulse red. */
		constexpr int32 LateSeconds = 10;
		constexpr float LatePulse = 0.06f;
		constexpr float LatePulseRate = 6.f;
	}
	namespace Lane
	{
		constexpr float MaxWidth = 520.f;
		/** The panels share this much of the screen's width, with a gap between. */
		constexpr float ScreenShare = 0.9f;
		constexpr float Gap = 24.f;
		constexpr float Height = 118.f;
		/** The panel follows its swordsman across the screen, but stays this far in from either edge (0..1). */
		constexpr double MinX01 = 0.12;
		constexpr double MaxX01 = 0.88;
		const FVector2D FallbackAt01(0.5, 0.9);
		constexpr FCardStyle Card{ 22.f, 6.f, 2.5f };
		constexpr float FocusOutlineWidth = 4.f;
		constexpr float CardAlpha = 0.86f;
		constexpr float EliminatedAlpha = 0.5f;
		/** Focus: a gold glow pulsing around the panel. */
		constexpr float FocusGlowBase = 0.6f;
		constexpr float FocusGlowPulse = 0.4f;
		constexpr float FocusGlowRate = 5.f;
		constexpr float FocusGlowAlpha = 0.35f;
		constexpr float FocusHaloPad = 6.f;
		constexpr float FocusHaloRadius = 28.f;
		/** The seat's colour band down the left edge. */
		constexpr float BandInset = 18.f;
		constexpr float BandWidth = 6.f;
		constexpr float BandRadius = 3.f;
		constexpr FTextStyle Name{ EFont::Title, 26.f, 1.5f, 2.f };
		const FVector2D NameAt(26.f, 32.f);
		constexpr FTextStyle Score{ EFont::Display, 50.f, 2.5f, 3.f };
		/** The score's right edge, in from the panel's right. */
		const FVector2D ScoreAt(22.f, 40.f);
		constexpr float LivesX = 34.f;
		constexpr float LivesGap = 30.f;
		constexpr float LivesY = 84.f;
		constexpr float HeartRadius = 10.f;
		constexpr float LostHeartAlpha = 0.18f;
		constexpr FTextStyle Combo{ EFont::Display, 32.f, 2.f, 3.f };
		constexpr float ComboX = 122.f;
		/** From this combo the counter turns gold. */
		constexpr int32 GoldCombo = 6;
		constexpr FTextStyle Out{ EFont::Display, 44.f, 3.f, 4.f };
		constexpr float OutY = 59.f;
	}
	namespace Focus
	{
		constexpr float FromRight = 186.f;
		constexpr float Y = 74.f;
		const FVector2D Size(166.f, 22.f);
		constexpr float TrackAlpha = 0.15f;
		constexpr float BarRadius = 11.f;
		constexpr FTextStyle BarLabel{ EFont::Label, 15.f, 2.f, 0.f };
		constexpr FTextStyle ChargeLabel{ EFont::Label, 14.f, 0.f, 0.f };
		constexpr float ChargeLabelAlpha = 0.65f;
		constexpr float PipX = 96.f;
		constexpr float PipGap = 28.f;
		constexpr float PipRadius = 9.f;
		constexpr float PipOutlineAlpha = 0.75f;
		constexpr float PipOutlineWidth = 2.f;
	}
	namespace Callout
	{
		/** Where a lane's callout rises from when it struck nothing (web metres). */
		constexpr double RestY = 2.7;
		constexpr double Lift = 0.35;
		constexpr double RisePerSecond = 90.0;
		constexpr FTextStyle Text{ EFont::Display, 46.f, 4.5f, 6.f };
		constexpr float BigScale = 1.2f;
		constexpr float SmallScale = 0.92f;
		/** Safe-area margin for the pop's overshoot. */
		constexpr float Overshoot = 1.1f;
		constexpr float BigTilt = -5.f;
		constexpr FPop Pop{ 0.2, 2.4f };
		constexpr double FadeSeconds = 0.25;
		constexpr float MissAlpha = 0.85f;
	}
	namespace NoteText
	{
		constexpr double RisePerSecond = 60.0;
		constexpr FTextStyle Text{ EFont::Display, 40.f, 4.f, 5.f };
		constexpr FPop Pop{ 0.18, 2.f };
		constexpr double FadeSeconds = 0.25;
	}
	namespace Banner
	{
		constexpr double Life = 1.5;
		constexpr float Y01 = 0.3f;
		constexpr FTextStyle Text{ EFont::Display, 110.f, 6.f, 8.f };
		constexpr FPop Pop{ 0.28, 2.2f };
		constexpr double FadeSeconds = 0.35;
		/** The ink stroke behind the words, swept in from the left, and its coloured underline. */
		constexpr double SweepSeconds = 0.22;
		constexpr float BandWidth = 900.f;
		constexpr float BandHeight = 124.f;
		constexpr float BandAlpha = 0.72f;
		constexpr float UnderlineY = 116.f;
		constexpr float UnderlineHeight = 8.f;
		constexpr float Tilt = -3.f;
	}
	namespace FocusFrame
	{
		constexpr float MinMood = 0.01f;
		constexpr float Alpha = 0.32f;
		constexpr float PulseBase = 0.8f;
		constexpr float Pulse = 0.2f;
		constexpr float PulseRate = 5.f;
		constexpr float Edge = 140.f;
	}
	namespace Countdown
	{
		constexpr float Y01 = 0.36f;
		constexpr float PopSeconds = 0.35f;
		constexpr float DiscRadius = 150.f;
		constexpr float DiscAlpha = 0.85f;
		constexpr float DiscRimAlpha = 0.6f;
		constexpr float DiscRimWidth = 6.f;
		constexpr FTextStyle Digits{ EFont::Display, 200.f, 9.f, 10.f };
		/** "SLICE!" holds this long into the round, fading over the last part. */
		constexpr double GoMs = 800.0;
		constexpr double GoFadeMs = 300.0;
		constexpr double GoPopMs = 200.0;
		constexpr float GoPopBack = 2.f;
		constexpr float GoTilt = -4.f;
		constexpr FTextStyle Go{ EFont::Display, 160.f, 8.f, 9.f };
	}
	namespace Controls
	{
		/** The controls hint shows for the round's first seconds, fading over the last part. */
		constexpr double ShowMs = 9000.0;
		constexpr double FadeMs = 600.0;
		constexpr float Y01 = 0.7f;
		const FVector2D CardSize(1060.f, 80.f);
		constexpr FCardStyle Card{ 22.f, 6.f, 2.f };
		constexpr float CardAlpha = 0.8f;
		constexpr FTextStyle Text{ EFont::Body, 24.f, 0.f, 0.f };
	}
	namespace Intro
	{
		constexpr double FadeInSeconds = 0.45;
		constexpr double FadeOutSeconds = 0.4;
		/** The card clears this much earlier when the practice follows it. */
		constexpr double PracticeLead = 0.3;
		/** Letterbox bars. */
		constexpr double BarsInSeconds = 0.5;
		constexpr double BarsLinger = 0.2;
		constexpr float BarHeight = 96.f;
		constexpr float BarAlpha = 0.92f;
		constexpr float CentreY01 = 0.4f;
		/** The rising sun: when it starts, how long it takes, its overshoot, size, travel and alpha. */
		constexpr double SunFrom = 0.1;
		constexpr FPop SunPop{ 0.6, 1.4f };
		constexpr float SunRadius = 230.f;
		constexpr float SunRise = 60.f;
		constexpr float SunY = 10.f;
		constexpr float SunAlpha = 0.88f;
		/** The ink stroke under the rules line. */
		constexpr double StrokeFrom = 0.25;
		constexpr double StrokeSeconds = 0.35;
		constexpr float StrokeWidth = 1120.f;
		constexpr float StrokeHeight = 70.f;
		constexpr float StrokeY = 92.f;
		constexpr float StrokeTilt = -1.5f;
		constexpr float StrokeAlpha = 0.8f;
		constexpr double TitleFrom = 0.15;
		constexpr FPop TitlePop{ 0.45, 1.8f };
		constexpr FTextStyle Title{ EFont::Display, 150.f, 8.f, 10.f };
		constexpr float TitleY = 20.f;
		constexpr FTextStyle Rules{ EFont::Body, 26.f, 1.f, 2.f };
		constexpr float RulesY = 127.f;
		/** One chip per swordsman, popping in one by one. */
		constexpr float ChipWidth = 280.f;
		constexpr float ChipHeight = 56.f;
		constexpr float ChipGap = 20.f;
		constexpr float ChipsY = 190.f;
		constexpr double ChipsFrom = 0.6;
		constexpr double ChipStagger = 0.12;
		constexpr FPop ChipPop{ 0.35, 2.f };
		constexpr float ChipAlpha = 0.86f;
		constexpr float ChipShadow = 4.f;
		constexpr float ChipOutlineWidth = 3.f;
		constexpr float DotX = 30.f;
		constexpr float DotRadius = 11.f;
		constexpr FTextStyle ChipName{ EFont::Title, 28.f, 0.f, 0.f };
		constexpr float NameX = 52.f;
		constexpr FTextStyle Cpu{ EFont::Label, 14.f, 0.f, 0.f };
		constexpr float CpuFromRight = 18.f;
		constexpr float CpuAlpha = 0.55f;
	}
	namespace Practice
	{
		/** The swipe to make: an arc above the apple, from -150 to -30 degrees, and its bead's run. */
		constexpr double SwipeFrom = 0.3;
		constexpr double SwipeFadeSeconds = 0.3;
		constexpr float ArcRadius = 150.f;
		constexpr float ArcSquash = 0.55f;
		constexpr int32 ArcSteps = 16;
		constexpr float ArcFrom = -150.f;
		constexpr float ArcTo = -30.f;
		constexpr float ArcAlpha = 0.55f;
		constexpr float LineWidth = 5.f;
		constexpr float ArrowLength = 22.f;
		constexpr float ArrowWidth = 14.f;
		constexpr float ArrowAlpha = 0.8f;
		constexpr float BeadRadius = 13.f;
		constexpr float BeadPeriod = 1.1f;
		constexpr float BeadRun = 0.7f;
		constexpr float BeadRimAlpha = 0.6f;
		constexpr float BeadRimWidth = 2.f;
		/** The coach mark. */
		constexpr float CardY01 = 0.76f;
		const FVector2D CardSize(1120.f, 140.f);
		constexpr FCardStyle Card{ 26.f, 6.f, 3.f };
		constexpr float CardAlpha = 0.88f;
		constexpr FPop CardPop{ 0.35, 1.8f };
		const FVector2D ChipOffset(-530.f, -88.f);
		const FVector2D ChipSize(170.f, 36.f);
		const FVector2D ChipLabel(-445.f, -70.f);
		constexpr FTextStyle ChipText{ EFont::Label, 18.f, 0.f, 0.f };
		constexpr FTextStyle Line{ EFont::Title, 36.f, 0.f, 0.f };
		constexpr float LineY = -12.f;
		constexpr FTextStyle Sub{ EFont::Body, 22.f, 0.f, 0.f };
		constexpr float SubY = 34.f;
		constexpr float SubAlpha = 0.8f;
		const FVector2D SkipOffset(540.f, 92.f);
		constexpr FTextStyle Skip{ EFont::Label, 16.f, 0.f, 2.f };
		constexpr float SkipAlpha = 0.7f;
		constexpr double SkipFadeSeconds = 0.35;
	}
	namespace Standings
	{
		constexpr double FadeSeconds = 0.5;
		constexpr float TitleY01 = 0.2f;
		constexpr float SunRadius = 120.f;
		constexpr float SunAlpha = 0.8f;
		constexpr FTextStyle Title{ EFont::Display, 96.f, 5.f, 7.f };
		constexpr float RowsY01 = 0.3f;
		constexpr float RowPitch = 104.f;
		const FVector2D RowSize(800.f, 88.f);
		constexpr FCardStyle Card{ 22.f, 6.f, 3.f };
		constexpr float RowAlpha = 0.85f;
		constexpr double RowsFrom = 0.3;
		constexpr double RowStagger = 0.18;
		constexpr double RowPopSeconds = 0.4;
		constexpr FTextStyle Place{ EFont::Display, 52.f, 0.f, 3.f };
		constexpr float PlaceX = 48.f;
		constexpr FTextStyle Name{ EFont::Title, 36.f, 0.f, 0.f };
		const FVector2D NameAt(100.f, 30.f);
		constexpr FTextStyle Detail{ EFont::Body, 18.f, 0.f, 0.f };
		const FVector2D DetailAt(100.f, 64.f);
		constexpr float DetailAlpha = 0.75f;
		constexpr FTextStyle Score{ EFont::Display, 56.f, 0.f, 0.f };
		/** The score's right edge, in from the row's right. */
		constexpr float ScoreFromRight = 30.f;
	}

	const FVector2D Centred(0.5, 0.5);
	const FVector2D LeftMiddle(0.0, 0.5);
	const FVector2D RightMiddle(1.0, 0.5);

	void Heart(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill, TArray<FVector2D>& Scratch)
	{
		// Two lobes and a point, in heart radii.
		P.Circle(C + FVector2D(-R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		P.Circle(C + FVector2D(R * 0.5f, -R * 0.2f), R * 0.58f, Fill);
		Scratch.Reset();
		Scratch.Add(C + FVector2D(-R * 1.05f, -R * 0.05f));
		Scratch.Add(C + FVector2D(R * 1.05f, -R * 0.05f));
		Scratch.Add(C + FVector2D(0.f, R * 1.05f));
		P.Polygon(Scratch, Fill);
	}

	/**
	 * Keeps a centred line of text (drawn at Scale, which may overshoot while it pops in) inside the safe area:
	 * shifts its centre in from either edge by what it would hang over.
	 */
	FVector2D InsideSafe(const FVector2D& At, const FString& Text, const FSlateFontInfo& Font, float Scale, const FVector2D& Size)
	{
		const float Half = static_cast<float>(FKartPainter::Measure(Text, Font).X) * Scale * 0.5f;
		const float Margin = static_cast<float>(Size.X) * KartUI::Metric::SafeFraction;
		const float Lo = Margin + Half, Hi = static_cast<float>(Size.X) - Margin - Half;
		return FVector2D(Lo > Hi ? Size.X * 0.5f : FMath::Clamp(static_cast<float>(At.X), Lo, Hi), At.Y);
	}

	float Clamp01(double X)
	{
		return FMath::Clamp(static_cast<float>(X), 0.f, 1.f);
	}

	/** Scales below this collapse the transform: pops start from it instead of zero. */
	float PopScale(float Scale)
	{
		return FMath::Max(0.01f, Scale);
	}

	/** An entrance's scale Age seconds in. */
	float Popped(const FPop& Pop, double Age)
	{
		return KartUI::Ease::OutBack(Clamp01(Age / Pop.Seconds), Pop.Back);
	}

	FString FormatInt(int32 Value)
	{
		return FString::FromInt(Value);
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
	View = FArcadeHudView::Capture(G->GetViewer());
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
	const bool bLate = Seconds <= Clock::LateSeconds && M.Phase == EMatchPhase::Playing;
	const float Pulse = bLate ? 1.f + Clock::LatePulse * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * Clock::LatePulseRate)) : 1.f;
	P.Card(Top - FVector2D(Clock::CardSize.X / 2.f, 0.f) * S, Clock::CardSize * S, KC::WithAlpha(KC::Night, Clock::CardAlpha), Clock::Card.Radius * S,
		Clock::Card.Shadow * S, bLate ? KC::Hot : KC::Sun, Clock::Card.OutlineWidth * S);
	const FVector2D DigitsAt = Top + FVector2D(0.f, Clock::DigitsY * S);
	P.Transformed(DigitsAt, Pulse).Text(SecondsText.Get(Seconds, FormatInt), Font(Clock::Digits, S), DigitsAt, bLate ? KC::Hot : KC::White, Centred,
		Clock::Digits.Shadow * S);
	const FString& Wave = WaveText.Get(WaveNumber(M.ElapsedMs), [](int32 Number) { return FString::Printf(TEXT("WAVE %d / %d"), Number, WaveCount); });
	P.Text(Wave, Font(Clock::Wave, S), Top + FVector2D(0.f, Clock::WaveY * S), KC::Sun, Centred);
}

void SSamuraiHud::PaintLanes(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 Count = M.Players.Num();
	const float Width = FMath::Min(Lane::MaxWidth * S, (P.Size().X * Lane::ScreenShare) / FMath::Max(1, Count) - Lane::Gap * S);
	LaneTexts.SetNum(Count);
	for (const FPlayer& Player : M.Players)
	{
		PaintLanePanel(P, G, Player, Width, S);
	}
}

void SSamuraiHud::PaintLanePanel(const FKartPainter& P, const ASamuraiGame& G, const FPlayer& Player, float Width, float S) const
{
	static const FText OutLabel = NSLOCTEXT("Samurai", "Out", "OUT");
	const int32 Count = G.GetMatch().Players.Num();
	const int32 LaneIndex = FMath::Clamp(Player.LaneIndex, 0, Count - 1);
	const FSamuraiSeat* Seat = G.GetSeats().IsValidIndex(LaneIndex) ? &G.GetSeats()[LaneIndex] : nullptr;
	FLaneTexts& Texts = LaneTexts[LaneIndex];
	const FLinearColor Color = Seat ? Seat->Color : KC::Sky;
	const float Height = Lane::Height * S;
	// Under each lane: where the swordsman stands, projected; the panels keep to the bottom of the frame.
	FVector2D At01 = Lane::FallbackAt01;
	if (View.Project(ASamuraiGame::ToWorld(FVector(LaneX(Player.LaneIndex, Count), 0.0, FBladeRig::StandZ)), At01))
	{
		At01.X = FMath::Clamp(At01.X, Lane::MinX01, Lane::MaxX01);
	}
	const FVector2D Pos(At01.X * P.Size().X - Width / 2.f, P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height);
	const bool bFocus = Player.FocusRemainingMs > 0.0;
	if (bFocus)
	{
		// Focus: the panel burns gold.
		const float Glow = Lane::FocusGlowBase + Lane::FocusGlowPulse * FMath::Abs(FMath::Sin(static_cast<float>(G.GetClock()) * Lane::FocusGlowRate));
		const FVector2D Pad(Lane::FocusHaloPad, Lane::FocusHaloPad);
		P.Box(Pos - Pad * S, FVector2D(Width, Height) + Pad * 2.f * S, KC::WithAlpha(Gilt, Lane::FocusGlowAlpha * Glow), Lane::FocusHaloRadius * S);
	}
	P.Card(Pos, FVector2D(Width, Height), KC::WithAlpha(KC::Night, Player.bEliminated ? Lane::EliminatedAlpha : Lane::CardAlpha), Lane::Card.Radius * S,
		Lane::Card.Shadow * S, bFocus ? Gilt : Color, (bFocus ? Lane::FocusOutlineWidth : Lane::Card.OutlineWidth) * S);
	P.Box(Pos + FVector2D(0.f, Lane::BandInset * S), FVector2D(Lane::BandWidth * S, Height - 2.f * Lane::BandInset * S), Color, Lane::BandRadius * S);
	P.Text(Player.Name, Font(Lane::Name, S), Pos + Lane::NameAt * S, KC::White, LeftMiddle, Lane::Name.Shadow * S);
	P.Text(Texts.Score.Get(Player.Score, FormatInt), Font(Lane::Score, S), Pos + FVector2D(Width - Lane::ScoreAt.X * S, Lane::ScoreAt.Y * S),
		bFocus ? Gilt : KC::White, RightMiddle, Lane::Score.Shadow * S);
	for (int32 i = 0; i < StartingLives; ++i)
	{
		Heart(P, Pos + FVector2D((Lane::LivesX + i * Lane::LivesGap) * S, Lane::LivesY * S), Lane::HeartRadius * S,
			i < Player.Lives ? KC::Hot : KC::WithAlpha(KC::White, Lane::LostHeartAlpha), Scratch);
	}
	// Combo, between the lives and the Focus meter.
	if (Player.Combo >= 2)
	{
		const FString& Combo = Texts.Combo.Get(Player.Combo, [](int32 Value) { return FString::Printf(TEXT("%dx"), Value); });
		P.Text(Combo, Font(Lane::Combo, S), Pos + FVector2D(Lane::ComboX * S, Lane::LivesY * S), Player.Combo >= Lane::GoldCombo ? Gilt : Jade, LeftMiddle,
			Lane::Combo.Shadow * S);
	}
	PaintFocusMeter(P, Player, Pos, Width, S);
	if (Player.bEliminated)
	{
		P.Text(OutLabel, Font(Lane::Out, S), Pos + FVector2D(Width / 2.f, Lane::OutY * S), KC::Hot, Centred, Lane::Out.Shadow * S);
	}
}

void SSamuraiHud::PaintFocusMeter(const FKartPainter& P, const FPlayer& Player, const FVector2D& PanelPos, float PanelWidth, float S) const
{
	static const FText BarLabel = NSLOCTEXT("Samurai", "Focus", "FOCUS x2");
	static const FText ChargeLabel = NSLOCTEXT("Samurai", "FocusCharge", "FOCUS");
	// Three pips charge Focus; while it burns, a gold bar counts its six seconds down with its label inside.
	const FVector2D Pos = PanelPos + FVector2D(PanelWidth - Focus::FromRight * S, Focus::Y * S);
	const FVector2D Size = Focus::Size * S;
	if (Player.FocusRemainingMs > 0.0)
	{
		const float Left = static_cast<float>(Player.FocusRemainingMs / FocusDurationMs);
		P.Box(Pos, Size, KC::WithAlpha(KC::White, Focus::TrackAlpha), Focus::BarRadius * S);
		P.Box(Pos, FVector2D(Size.X * Left, Size.Y), Gilt, Focus::BarRadius * S);
		P.Text(BarLabel, Font(Focus::BarLabel, S), Pos + Size * 0.5f, KC::White, Centred);
		return;
	}
	P.Text(ChargeLabel, Font(Focus::ChargeLabel, S), Pos + FVector2D(0.f, Size.Y * 0.5f), KC::WithAlpha(KC::White, Focus::ChargeLabelAlpha), LeftMiddle);
	for (int32 i = 0; i < FocusCuts; ++i)
	{
		P.Circle(Pos + FVector2D((Focus::PipX + i * Focus::PipGap) * S, Size.Y * 0.5f), Focus::PipRadius * S, i < Player.FocusCharge ? Gilt : FLinearColor::Transparent,
			KC::WithAlpha(Gilt, Focus::PipOutlineAlpha), Focus::PipOutlineWidth * S);
	}
}

void SSamuraiHud::PaintCallouts(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 Count = M.Players.Num();
	const FSlateFontInfo CalloutFont = Font(Callout::Text, S);
	for (const FEffect& E : M.Effects)
	{
		if (E.Kind == EEffectKind::Slash || E.Label.IsEmpty())
		{
			continue;
		}
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		// Over the first target it struck, or the lane's middle.
		FVector Web(LaneX(E.LaneIndex, Count), Callout::RestY, TargetDepth);
		if (E.Struck.Num() > 0)
		{
			Web = TargetCentre(E.Struck[0], Count) + FVector(0.0, Callout::Lift, 0.0);
		}
		FVector2D At01;
		if (!View.Project(ASamuraiGame::ToWorld(Web), At01))
		{
			continue;
		}
		const bool bBig = E.Kind == EEffectKind::Combo || E.Kind == EEffectKind::Bomb;
		const float Scale = bBig ? Callout::BigScale : Callout::SmallScale;
		const FVector2D At = InsideSafe(At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * Callout::RisePerSecond) * S), E.Label, CalloutFont,
			Scale * Callout::Overshoot, P.Size());
		const float In = Popped(Callout::Pop, Age);
		const float Out = Clamp01((Life - Age) / Callout::FadeSeconds);
		const FLinearColor Color = E.Kind == EEffectKind::Bomb ? KC::Hot : (E.Kind == EEffectKind::Miss ? KC::WithAlpha(KC::White, Callout::MissAlpha) : CalloutColor(E.Color));
		P.Transformed(At, PopScale(In) * Scale, bBig ? Callout::BigTilt : 0.f).Faded(Out).Text(E.Label, CalloutFont, At, Color, Centred, Callout::Text.Shadow * S);
	}
}

const FLinearColor& SSamuraiHud::CalloutColor(const FString& Hex) const
{
	// The rules colour effects by hex (a handful of distinct values): parse each once.
	if (const FLinearColor* Found = CalloutColors.Find(Hex))
	{
		return *Found;
	}
	return CalloutColors.Add(Hex, MovecadeKart::Art::Hex(Hex));
}

void SSamuraiHud::PaintNotes(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FSlateFontInfo NoteFont = Font(NoteText::Text, S);
	for (const FSamuraiNote& Note : G.GetNotes())
	{
		FVector2D At01;
		if (!View.Project(Note.World, At01))
		{
			continue;
		}
		const double Age = G.GetClock() - Note.BornAt;
		const FVector2D At = InsideSafe(At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * NoteText::RisePerSecond) * S), Note.Label.ToString(), NoteFont,
			Note.Scale * Callout::Overshoot, P.Size());
		const float In = Popped(NoteText::Pop, Age);
		const float Out = Clamp01((Note.Life - Age) / NoteText::FadeSeconds);
		P.Transformed(At, PopScale(In) * Note.Scale).Faded(Out).Text(Note.Label, NoteFont, At, Note.Color, Centred, NoteText::Text.Shadow * S);
	}
}

void SSamuraiHud::PaintBanner(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const double Age = G.GetBannerAge();
	if (Age < 0.0 || Age > Banner::Life || G.GetBanner().IsEmpty())
	{
		return;
	}
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Banner::Y01);
	const float In = Popped(Banner::Pop, Age);
	const float Out = Clamp01((Banner::Life - Age) / Banner::FadeSeconds);
	const FKartPainter F = P.Faded(Out);
	// An ink stroke behind the words, swept in from the left.
	const float Sweep = KartUI::Ease::OutCubic(Clamp01(Age / Banner::SweepSeconds));
	const float BandW = Banner::BandWidth * S * Sweep;
	const FVector2D BandAt = Centre - FVector2D(Banner::BandWidth / 2.f * S, Banner::BandHeight / 2.f * S);
	const FKartPainter Tilted = F.Transformed(Centre, 1.f, Banner::Tilt);
	Tilted.Box(BandAt, FVector2D(BandW, Banner::BandHeight * S), KC::WithAlpha(KC::Night, Banner::BandAlpha), Banner::BandHeight / 2.f * S);
	Tilted.Box(BandAt + FVector2D(0.f, Banner::UnderlineY * S), FVector2D(BandW, Banner::UnderlineHeight * S), G.GetBannerColor(), Banner::UnderlineHeight / 2.f * S);
	F.Transformed(Centre, PopScale(In), Banner::Tilt).Text(G.GetBanner(), Font(Banner::Text, S), Centre, G.GetBannerColor(), Centred, Banner::Text.Shadow * S);
}

void SSamuraiHud::PaintFocusFrame(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const float Mood = G.GetFocusMood();
	if (Mood < FocusFrame::MinMood)
	{
		return;
	}
	// Gold light leaking in from the frame's edges while Focus burns.
	const float A = FocusFrame::Alpha * Mood * (FocusFrame::PulseBase + FocusFrame::Pulse * FMath::Sin(static_cast<float>(G.GetClock()) * FocusFrame::PulseRate));
	const FVector2D Size = P.Size();
	const float Edge = FocusFrame::Edge * S;
	P.Gradient(FVector2D::ZeroVector, FVector2D(Size.X, Edge), KC::WithAlpha(Gilt, A), KC::WithAlpha(Gilt, 0.f), true);
	P.Gradient(FVector2D(0.f, Size.Y - Edge), FVector2D(Size.X, Edge), KC::WithAlpha(Gilt, 0.f), KC::WithAlpha(Gilt, A), true);
	P.Gradient(FVector2D::ZeroVector, FVector2D(Edge, Size.Y), KC::WithAlpha(Gilt, A), KC::WithAlpha(Gilt, 0.f), false);
	P.Gradient(FVector2D(Size.X - Edge, 0.f), FVector2D(Edge, Size.Y), KC::WithAlpha(Gilt, 0.f), KC::WithAlpha(Gilt, A), false);
}

void SSamuraiHud::PaintCountdown(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	static const FText GoLabel = NSLOCTEXT("Samurai", "Go", "SLICE!");
	const FMatch& M = G.GetMatch();
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Countdown::Y01);
	if (M.Phase == EMatchPhase::Countdown)
	{
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 Number = FMath::CeilToInt32(Remaining);
		// Each number pops in at the start of its second.
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(1.0 - (Number - Remaining)) / Countdown::PopSeconds, 0.f, 1.f));
		const FKartPainter Disc = P.Transformed(Centre, PopScale(Pop));
		Disc.Circle(Centre, Countdown::DiscRadius * S, KC::WithAlpha(Vermilion, Countdown::DiscAlpha), KC::WithAlpha(KC::Night, Countdown::DiscRimAlpha),
			Countdown::DiscRimWidth * S);
		Disc.Text(CountText.Get(FMath::Max(1, Number), FormatInt), Font(Countdown::Digits, S), Centre, KC::White, Centred, Countdown::Digits.Shadow * S);
		return;
	}
	if (M.Phase == EMatchPhase::Playing && M.ElapsedMs < Countdown::GoMs)
	{
		const float A = FMath::Clamp(static_cast<float>((Countdown::GoMs - M.ElapsedMs) / Countdown::GoFadeMs), 0.f, 1.f);
		const float Pop = KartUI::Ease::OutBack(Clamp01(M.ElapsedMs / Countdown::GoPopMs), Countdown::GoPopBack);
		P.Faded(A).Transformed(Centre, PopScale(Pop), Countdown::GoTilt).Text(GoLabel, Font(Countdown::Go, S), Centre, KC::Hot, Centred, Countdown::Go.Shadow * S);
	}
}

void SSamuraiHud::PaintControls(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	static const FText Hint = NSLOCTEXT("Samurai", "Controls", "Mouse or stick: swing fast through the fruit  ·  never the bombs");
	if (!G.HasLocal() || G.GetMatch().ElapsedMs > Controls::ShowMs)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((Controls::ShowMs - G.GetMatch().ElapsedMs) / Controls::FadeMs), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Controls::Y01);
	const FKartPainter F = P.Faded(A);
	F.Card(Centre - Controls::CardSize / 2.f * S, Controls::CardSize * S, KC::WithAlpha(KC::Night, Controls::CardAlpha), Controls::Card.Radius * S, Controls::Card.Shadow * S,
		KC::Sun, Controls::Card.OutlineWidth * S);
	F.Text(Hint, Font(Controls::Text, S), Centre, KC::White, Centred);
}

void SSamuraiHud::PaintIntro(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	static const FText Title = NSLOCTEXT("Samurai", "Title", "SAMURAI SLICE");
	static const FText Rules = NSLOCTEXT("Samurai", "Rules", "Cut the fruit. Never the bombs. Three precise cuts: Focus, double points.");
	const double Age = G.GetStageAge();
	const bool bPractice = G.GetPractice() != ESamuraiPractice::None;
	const double End = bPractice ? ASamuraiGame::TitleSeconds - Intro::PracticeLead : ASamuraiGame::TitleSeconds;
	const float A = Clamp01(Age / Intro::FadeInSeconds) * Clamp01((End - Age) / Intro::FadeOutSeconds);
	const FVector2D Size = P.Size();
	// Letterbox bars: the round opens like a film.
	const float Bars = KartUI::Ease::OutCubic(Clamp01(Age / Intro::BarsInSeconds)) * Clamp01((End + Intro::BarsLinger - Age) / Intro::BarsInSeconds);
	if (Bars > 0.f)
	{
		const float BarH = Intro::BarHeight * S * Bars;
		P.Box(FVector2D::ZeroVector, FVector2D(Size.X, BarH), KC::WithAlpha(FLinearColor::Black, Intro::BarAlpha));
		P.Box(FVector2D(0.f, Size.Y - BarH), FVector2D(Size.X, BarH), KC::WithAlpha(FLinearColor::Black, Intro::BarAlpha));
	}
	if (A <= 0.f)
	{
		return;
	}
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(Size.X / 2.f, Size.Y * Intro::CentreY01);
	// The rising sun behind the title, and an ink stroke under it.
	const float Rise = Popped(Intro::SunPop, Age - Intro::SunFrom);
	F.Circle(Centre + FVector2D(0.f, (1.f - Rise) * Intro::SunRise * S - Intro::SunY * S), Intro::SunRadius * S * PopScale(Rise), KC::WithAlpha(Vermilion, Intro::SunAlpha));
	const float Stroke = KartUI::Ease::OutCubic(Clamp01((Age - Intro::StrokeFrom) / Intro::StrokeSeconds));
	const FVector2D RulesAt = Centre + FVector2D(0.f, Intro::RulesY * S);
	F.Transformed(RulesAt, 1.f, Intro::StrokeTilt).Box(Centre + FVector2D(-Intro::StrokeWidth / 2.f * S, Intro::StrokeY * S), FVector2D(Intro::StrokeWidth * S * Stroke, Intro::StrokeHeight * S),
		KC::WithAlpha(KC::Night, Intro::StrokeAlpha), Intro::StrokeHeight / 2.f * S);
	const float TitlePop = Popped(Intro::TitlePop, Age - Intro::TitleFrom);
	F.Transformed(Centre, PopScale(TitlePop)).Text(Title, Font(Intro::Title, S), Centre - FVector2D(0.f, Intro::TitleY * S), KC::White, Centred, Intro::Title.Shadow * S);
	F.Text(Rules, Font(Intro::Rules, S), RulesAt, KC::White, Centred, Intro::Rules.Shadow * S);
	PaintIntroSeats(F, G, Centre, Age, S);
}

void SSamuraiHud::PaintIntroSeats(const FKartPainter& F, const ASamuraiGame& G, const FVector2D& Centre, double Age, float S) const
{
	static const FText CpuLabel = NSLOCTEXT("Samurai", "Cpu", "CPU");
	// The swordsmen, a chip each in their colour, popping in one by one.
	const TArray<FSamuraiSeat>& Seats = G.GetSeats();
	const float ChipW = Intro::ChipWidth * S, Gap = Intro::ChipGap * S;
	const float ChipH = Intro::ChipHeight * S;
	const float RowW = Seats.Num() * ChipW + (Seats.Num() - 1) * Gap;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float In = Popped(Intro::ChipPop, Age - Intro::ChipsFrom - i * Intro::ChipStagger);
		const FVector2D Pos(F.Size().X / 2.f - RowW / 2.f + i * (ChipW + Gap), Centre.Y + Intro::ChipsY * S);
		const FKartPainter Chip = F.Transformed(Pos + FVector2D(ChipW / 2.f, ChipH / 2.f), PopScale(In));
		Chip.Card(Pos, FVector2D(ChipW, ChipH), KC::WithAlpha(KC::Night, Intro::ChipAlpha), ChipH / 2.f, Intro::ChipShadow * S, Seats[i].Color, Intro::ChipOutlineWidth * S);
		Chip.Circle(Pos + FVector2D(Intro::DotX * S, ChipH / 2.f), Intro::DotRadius * S, Seats[i].Color);
		Chip.Text(Seats[i].Name, Font(Intro::ChipName, S), Pos + FVector2D(Intro::NameX * S, ChipH / 2.f), KC::White, LeftMiddle);
		if (Seats[i].Kind == ESwordKind::Bot)
		{
			Chip.Text(CpuLabel, Font(Intro::Cpu, S), Pos + FVector2D(ChipW - Intro::CpuFromRight * S, ChipH / 2.f), KC::WithAlpha(KC::White, Intro::CpuAlpha), RightMiddle);
		}
	}
}

void SSamuraiHud::PaintPractice(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	static const FText ChipLabel = NSLOCTEXT("Samurai", "PracticeChip", "PRACTICE");
	static const FText SkipLabel = NSLOCTEXT("Samurai", "PracticeSkip", "SPACE / (A)  skip");
	static const FText SlowLine = NSLOCTEXT("Samurai", "PracticeSlow", "Faster! A cut is a quick, sharp swing.");
	static const FText SlowSub = NSLOCTEXT("Samurai", "PracticeSlowSub", "Flick the mouse or stick right through the apple.");
	static const FText DoneLine = NSLOCTEXT("Samurai", "PracticeDone", "Clean cut! That's the stroke.");
	static const FText DoneSub = NSLOCTEXT("Samurai", "PracticeDoneSub", "Cut at the top of each arc for Perfect. Here comes the fruit...");
	static const FText AskLine = NSLOCTEXT("Samurai", "PracticeLine", "Slice the practice apple!");
	static const FText AskSub = NSLOCTEXT("Samurai", "PracticeSub", "Mouse or right stick aims the blade. Swing fast through the fruit to cut.");
	const ESamuraiPractice State = G.GetPractice();
	if (State == ESamuraiPractice::None)
	{
		return;
	}
	if (State != ESamuraiPractice::Cut)
	{
		PaintPracticeSwipe(P, G, S);
	}
	// The coach mark.
	const double Age = G.GetPracticeAge();
	const FVector2D Size = P.Size();
	const FVector2D Centre(Size.X / 2.f, Size.Y * Practice::CardY01);
	const float In = Popped(Practice::CardPop, State == ESamuraiPractice::Cut ? Practice::CardPop.Seconds : Age);
	const FKartPainter Card = P.Transformed(Centre, PopScale(In));
	const FLinearColor Edge = State == ESamuraiPractice::Cut ? Jade : (State == ESamuraiPractice::TooSlow ? Gilt : KC::Sun);
	Card.Card(Centre - Practice::CardSize / 2.f * S, Practice::CardSize * S, KC::WithAlpha(KC::Night, Practice::CardAlpha), Practice::Card.Radius * S,
		Practice::Card.Shadow * S, Edge, Practice::Card.OutlineWidth * S);
	Card.Box(Centre + Practice::ChipOffset * S, Practice::ChipSize * S, Edge, Practice::ChipSize.Y / 2.f * S);
	Card.Text(ChipLabel, Font(Practice::ChipText, S), Centre + Practice::ChipLabel * S, KC::Night, Centred);
	const bool bCut = State == ESamuraiPractice::Cut;
	const bool bSlow = State == ESamuraiPractice::TooSlow;
	const FText& Line = bCut ? DoneLine : (bSlow ? SlowLine : AskLine);
	const FText& Sub = bCut ? DoneSub : (bSlow ? SlowSub : AskSub);
	Card.Text(Line, Font(Practice::Line, S), Centre + FVector2D(0.f, Practice::LineY * S), bCut ? Jade : KC::White, Centred);
	Card.Text(Sub, Font(Practice::Sub, S), Centre + FVector2D(0.f, Practice::SubY * S), KC::WithAlpha(KC::White, Practice::SubAlpha), Centred);
	if (!bCut)
	{
		P.Faded(Clamp01(Age / Practice::SkipFadeSeconds)).Text(SkipLabel, Font(Practice::Skip, S), Centre + Practice::SkipOffset * S, KC::WithAlpha(KC::White, Practice::SkipAlpha),
			RightMiddle, Practice::Skip.Shadow * S);
	}
}

void SSamuraiHud::PaintPracticeSwipe(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	// The swipe to make: an arc through the apple, a bright bead running along it.
	FVector2D Apple01;
	if (!View.Project(G.GetPracticeWorld(), Apple01))
	{
		return;
	}
	const double Age = G.GetPracticeAge();
	const FVector2D C = Apple01 * P.Size();
	const float R = Practice::ArcRadius * S;
	const float Fade = Clamp01((Age - Practice::SwipeFrom) / Practice::SwipeFadeSeconds);
	Scratch.Reset();
	for (int32 i = 0; i <= Practice::ArcSteps; ++i)
	{
		const float T = i / static_cast<float>(Practice::ArcSteps);
		const float Angle = FMath::DegreesToRadians(FMath::Lerp(Practice::ArcFrom, Practice::ArcTo, T));
		Scratch.Add(C + FVector2D(FMath::Cos(Angle) * R, -FMath::Sin(Angle) * R * Practice::ArcSquash - R * Practice::ArcSquash));
	}
	const FKartPainter F = P.Faded(Fade);
	F.Lines(Scratch, KC::WithAlpha(KC::White, Practice::ArcAlpha), Practice::LineWidth * S);
	// The arrowhead at the arc's end.
	const FVector2D Tail = Scratch.Last(), Before = Scratch[Scratch.Num() - 2];
	const FVector2D Dir = (Tail - Before).GetSafeNormal(), Side(-Dir.Y, Dir.X);
	const FVector2D Back = Tail - Dir * Practice::ArrowLength * S;
	F.Lines({ Back + Side * Practice::ArrowWidth * S, Tail, Back - Side * Practice::ArrowWidth * S }, KC::WithAlpha(KC::White, Practice::ArrowAlpha), Practice::LineWidth * S);
	const float Run = FMath::Fmod(static_cast<float>(Age), Practice::BeadPeriod) / Practice::BeadRun;
	if (Run <= 1.f)
	{
		const int32 k = FMath::Clamp(FMath::FloorToInt32(Run * Practice::ArcSteps), 0, Practice::ArcSteps - 1);
		const FVector2D Bead = FMath::Lerp(Scratch[k], Scratch[k + 1], Run * Practice::ArcSteps - k);
		F.Circle(Bead, Practice::BeadRadius * S, Gilt, KC::WithAlpha(KC::Night, Practice::BeadRimAlpha), Practice::BeadRimWidth * S);
	}
}

void SSamuraiHud::PaintStandings(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	static const FText Title = NSLOCTEXT("Samurai", "Final", "FINAL SCORES");
	const TArray<FPlayer>& Players = G.GetMatch().Players;
	const TArray<int32>& Order = Ranks.Update(Players.Num(), [&Players](int32 i) { return static_cast<int64>(Players[i].Score); });
	StandingTexts.SetNum(Players.Num());
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(Clamp01(Age / Standings::FadeSeconds));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Standings::TitleY01);
	F.Circle(Centre, Standings::SunRadius * S * PopScale(KartUI::Ease::OutBack(Clamp01(Age / Standings::FadeSeconds))), KC::WithAlpha(Vermilion, Standings::SunAlpha));
	F.Text(Title, Font(Standings::Title, S), Centre, KC::Sun, Centred, Standings::Title.Shadow * S);
	const FVector2D RowSize = Standings::RowSize * S;
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FPlayer& Player = Players[Order[i]];
		const FSamuraiSeat* Seat = G.GetSeats().IsValidIndex(Player.LaneIndex) ? &G.GetSeats()[Player.LaneIndex] : nullptr;
		const float In = KartUI::Ease::OutBack(Clamp01((Age - Standings::RowsFrom - i * Standings::RowStagger) / Standings::RowPopSeconds));
		const FVector2D Pos(P.Size().X / 2.f - RowSize.X / 2.f, P.Size().Y * Standings::RowsY01 + i * Standings::RowPitch * S);
		const FKartPainter Row = F.Transformed(Pos + RowSize / 2.f, PopScale(In));
		const bool bWinner = G.GetMatch().WinnerIds.Contains(Player.Id);
		Row.Card(Pos, RowSize, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, Standings::RowAlpha), Standings::Card.Radius * S, Standings::Card.Shadow * S,
			Seat ? Seat->Color : KC::Sky, Standings::Card.OutlineWidth * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		FStandingTexts& PlayerTexts = StandingTexts[Order[i]];
		const FString& Detail = PlayerTexts.Detail.Get(FIntVector(Player.BestCombo, Player.Hits, Player.Bombs), [](const FIntVector& Stats)
		{
			return FString::Printf(TEXT("best combo %d  ·  %d cuts  ·  %d bombs"), Stats.X, Stats.Y, Stats.Z);
		});
		Row.Text(StandingTexts[i].Place.Get(i + 1, FormatInt), Font(Standings::Place, S), Pos + FVector2D(Standings::PlaceX * S, RowSize.Y / 2.f), KC::ForPlace(i + 1), Centred,
			Standings::Place.Shadow * S);
		Row.Text(Player.Name, Font(Standings::Name, S), Pos + Standings::NameAt * S, Ink, LeftMiddle);
		Row.Text(Detail, Font(Standings::Detail, S), Pos + Standings::DetailAt * S, KC::WithAlpha(Ink, Standings::DetailAlpha), LeftMiddle);
		Row.Text(PlayerTexts.Score.Get(Player.Score, FormatInt), Font(Standings::Score, S), Pos + FVector2D(RowSize.X - Standings::ScoreFromRight * S, RowSize.Y / 2.f), Ink,
			RightMiddle);
	}
}
