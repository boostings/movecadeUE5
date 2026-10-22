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

	// Layout, in 1080-line canvas units (times S on screen); seconds for timings.
	namespace Clock
	{
		const FVector2D CardSize(220.f, 112.f);
		constexpr float CardRadius = 26.f;
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
		constexpr float FocusGlowBase = 0.6f;
		constexpr float FocusGlowPulse = 0.4f;
		constexpr float FocusGlowRate = 5.f;
		constexpr float FocusHaloPad = 6.f;
		constexpr float LivesX = 34.f;
		constexpr float LivesGap = 30.f;
		constexpr float LivesY = 84.f;
		constexpr float HeartRadius = 10.f;
		constexpr float ComboX = 122.f;
		/** From this combo the counter turns gold. */
		constexpr int32 GoldCombo = 6;
	}
	namespace Focus
	{
		constexpr float FromRight = 186.f;
		constexpr float Y = 74.f;
		const FVector2D Size(166.f, 22.f);
		constexpr float PipX = 96.f;
		constexpr float PipGap = 28.f;
		constexpr float PipRadius = 9.f;
	}
	namespace Callout
	{
		/** Where a lane's callout rises from when it struck nothing (web metres). */
		constexpr double RestY = 2.7;
		constexpr double Lift = 0.35;
		constexpr double RiseCmPerSecond = 90.0;
		constexpr float BigScale = 1.2f;
		constexpr float SmallScale = 0.92f;
		/** Safe-area margin for the pop's overshoot. */
		constexpr float Overshoot = 1.1f;
		constexpr float BigTilt = -5.f;
		constexpr float PopSeconds = 0.2f;
		constexpr float PopBack = 2.4f;
		constexpr double FadeSeconds = 0.25;
	}
	namespace NoteText
	{
		constexpr double RisePerSecond = 60.0;
		constexpr float PopSeconds = 0.18f;
		constexpr float PopBack = 2.f;
		constexpr double FadeSeconds = 0.25;
	}
	namespace Banner
	{
		constexpr double Life = 1.5;
		constexpr float Y01 = 0.3f;
		constexpr float PopSeconds = 0.28f;
		constexpr float PopBack = 2.2f;
		constexpr double FadeSeconds = 0.35;
		constexpr float SweepSeconds = 0.22f;
		constexpr float BandWidth = 900.f;
		constexpr float BandHeight = 124.f;
		constexpr float UnderlineY = 116.f;
		constexpr float UnderlineHeight = 8.f;
		constexpr float Tilt = -3.f;
	}
	namespace FocusFrame
	{
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
		/** "SLICE!" holds this long into the round, fading over the last part. */
		constexpr double GoMs = 800.0;
		constexpr double GoFadeMs = 300.0;
		constexpr double GoPopMs = 200.0;
		constexpr float GoTilt = -4.f;
	}
	namespace Controls
	{
		/** The controls hint shows for the round's first seconds, fading over the last part. */
		constexpr double ShowMs = 9000.0;
		constexpr double FadeMs = 600.0;
		constexpr float Y01 = 0.7f;
		const FVector2D CardSize(1060.f, 80.f);
	}
	namespace Intro
	{
		constexpr double FadeInSeconds = 0.45;
		constexpr double FadeOutSeconds = 0.4;
		/** The card clears this much earlier when the practice follows it. */
		constexpr double PracticeLead = 0.3;
		constexpr double BarsInSeconds = 0.5;
		constexpr double BarsLinger = 0.2;
		constexpr float BarHeight = 96.f;
		constexpr float CentreY01 = 0.4f;
		constexpr float SunRadius = 230.f;
		constexpr float SunRise = 60.f;
		constexpr float SunY = 10.f;
		constexpr float StrokeWidth = 1120.f;
		constexpr float StrokeHeight = 70.f;
		constexpr float StrokeY = 92.f;
		constexpr float RulesY = 127.f;
		constexpr float TitleY = 20.f;
		constexpr float ChipWidth = 280.f;
		constexpr float ChipHeight = 56.f;
		constexpr float ChipGap = 20.f;
		constexpr float ChipsY = 190.f;
		constexpr double ChipsFrom = 0.6;
		constexpr double ChipStagger = 0.12;
	}
	namespace Practice
	{
		/** The swipe to make: an arc above the apple, from -150 to -30 degrees, and its bead's run. */
		constexpr float ArcRadius = 150.f;
		constexpr float ArcSquash = 0.55f;
		constexpr int32 ArcSteps = 16;
		constexpr float ArcFrom = -150.f;
		constexpr float ArcTo = -30.f;
		constexpr float ArrowLength = 22.f;
		constexpr float ArrowWidth = 14.f;
		constexpr float BeadRadius = 13.f;
		constexpr float BeadPeriod = 1.1f;
		constexpr float BeadRun = 0.7f;
		constexpr float CardY01 = 0.76f;
		const FVector2D CardSize(1120.f, 140.f);
		const FVector2D ChipOffset(-530.f, -88.f);
		const FVector2D ChipSize(170.f, 36.f);
		const FVector2D ChipLabel(-445.f, -70.f);
		const FVector2D SkipOffset(540.f, 92.f);
	}
	namespace Standings
	{
		constexpr float TitleY01 = 0.2f;
		constexpr float SunRadius = 120.f;
		constexpr float RowsY01 = 0.3f;
		constexpr float RowPitch = 104.f;
		const FVector2D RowSize(800.f, 88.f);
		constexpr double RowsFrom = 0.3;
		constexpr double RowStagger = 0.18;
	}

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
	P.Card(Top - FVector2D(Clock::CardSize.X / 2.f, 0.f) * S, Clock::CardSize * S, KC::WithAlpha(KC::Night, 0.82f), Clock::CardRadius * S, 6.f * S, bLate ? KC::Hot : KC::Sun, 3.f * S);
	const FVector2D DigitsAt = Top + FVector2D(0.f, Clock::DigitsY * S);
	P.Transformed(DigitsAt, Pulse).Text(SecondsText.Get(Seconds, FormatInt), KartUI::Font(EFont::Display, 64.f * S, 3.f * S, KC::Night), DigitsAt,
		bLate ? KC::Hot : KC::White, FVector2D(0.5, 0.5), 4.f * S);
	const FString& Wave = WaveText.Get(WaveNumber(M.ElapsedMs), [](int32 Number) { return FString::Printf(TEXT("WAVE %d / %d"), Number, WaveCount); });
	P.Text(Wave, KartUI::Font(EFont::Label, 18.f * S), Top + FVector2D(0.f, Clock::WaveY * S), KC::Sun, FVector2D(0.5, 0.5));
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
		P.Box(Pos - Pad * S, FVector2D(Width, Height) + Pad * 2.f * S, KC::WithAlpha(Gilt, 0.35f * Glow), 28.f * S);
	}
	P.Card(Pos, FVector2D(Width, Height), KC::WithAlpha(KC::Night, Player.bEliminated ? 0.5f : 0.86f), 22.f * S, 6.f * S, bFocus ? Gilt : Color, (bFocus ? 4.f : 2.5f) * S);
	// The seat's colour as a band down the left edge.
	P.Box(Pos + FVector2D(0.f, 18.f * S), FVector2D(6.f * S, Height - 36.f * S), Color, 3.f * S);
	P.Text(Player.Name, KartUI::Font(EFont::Title, 26.f * S, 1.5f * S, KC::Night), Pos + FVector2D(26.f * S, 32.f * S), KC::White, FVector2D(0.0, 0.5), 2.f * S);
	P.Text(Texts.Score.Get(Player.Score, FormatInt), KartUI::Font(EFont::Display, 50.f * S, 2.5f * S, KC::Night), Pos + FVector2D(Width - 22.f * S, 40.f * S),
		bFocus ? Gilt : KC::White, FVector2D(1.0, 0.5), 3.f * S);
	for (int32 i = 0; i < StartingLives; ++i)
	{
		Heart(P, Pos + FVector2D((Lane::LivesX + i * Lane::LivesGap) * S, Lane::LivesY * S), Lane::HeartRadius * S,
			i < Player.Lives ? KC::Hot : KC::WithAlpha(KC::White, 0.18f), Scratch);
	}
	// Combo, between the lives and the Focus meter.
	if (Player.Combo >= 2)
	{
		const FString& Combo = Texts.Combo.Get(Player.Combo, [](int32 Value) { return FString::Printf(TEXT("%dx"), Value); });
		P.Text(Combo, KartUI::Font(EFont::Display, 32.f * S, 2.f * S, KC::Night), Pos + FVector2D(Lane::ComboX * S, Lane::LivesY * S),
			Player.Combo >= Lane::GoldCombo ? Gilt : Jade, FVector2D(0.0, 0.5), 3.f * S);
	}
	PaintFocusMeter(P, Player, Pos, Width, S);
	if (Player.bEliminated)
	{
		P.Text(NSLOCTEXT("Samurai", "Out", "OUT"), KartUI::Font(EFont::Display, 44.f * S, 3.f * S, KC::Night), Pos + FVector2D(Width / 2.f, 59.f * S), KC::Hot,
			FVector2D(0.5, 0.5), 4.f * S);
	}
}

void SSamuraiHud::PaintFocusMeter(const FKartPainter& P, const FPlayer& Player, const FVector2D& PanelPos, float PanelWidth, float S) const
{
	// Three pips charge Focus; while it burns, a gold bar counts its six seconds down with its label inside.
	const FVector2D Pos = PanelPos + FVector2D(PanelWidth - Focus::FromRight * S, Focus::Y * S);
	const FVector2D Size = Focus::Size * S;
	if (Player.FocusRemainingMs > 0.0)
	{
		const float Left = static_cast<float>(Player.FocusRemainingMs / FocusDurationMs);
		P.Box(Pos, Size, KC::WithAlpha(KC::White, 0.15f), 11.f * S);
		P.Box(Pos, FVector2D(Size.X * Left, Size.Y), Gilt, 11.f * S);
		P.Text(NSLOCTEXT("Samurai", "Focus", "FOCUS x2"), KartUI::Font(EFont::Label, 15.f * S, 2.f * S, KC::Night), Pos + Size * 0.5f, KC::White, FVector2D(0.5, 0.5));
		return;
	}
	P.Text(NSLOCTEXT("Samurai", "FocusCharge", "FOCUS"), KartUI::Font(EFont::Label, 14.f * S), Pos + FVector2D(0.f, Size.Y * 0.5f), KC::WithAlpha(KC::White, 0.65f), FVector2D(0.0, 0.5));
	for (int32 i = 0; i < FocusCuts; ++i)
	{
		P.Circle(Pos + FVector2D((Focus::PipX + i * Focus::PipGap) * S, Size.Y * 0.5f), Focus::PipRadius * S, i < Player.FocusCharge ? Gilt : FLinearColor::Transparent,
			KC::WithAlpha(Gilt, 0.75f), 2.f * S);
	}
}

void SSamuraiHud::PaintCallouts(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const int32 Count = M.Players.Num();
	const FSlateFontInfo Font = KartUI::Font(EFont::Display, 46.f * S, 4.5f * S, KC::Night);
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
		const FVector2D At = InsideSafe(At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * Callout::RiseCmPerSecond) * S), E.Label, Font, Scale * Callout::Overshoot, P.Size());
		const float In = KartUI::Ease::OutBack(Clamp01(Age / Callout::PopSeconds), Callout::PopBack);
		const float Out = Clamp01((Life - Age) / Callout::FadeSeconds);
		const FLinearColor Color = E.Kind == EEffectKind::Bomb ? KC::Hot : (E.Kind == EEffectKind::Miss ? KC::WithAlpha(KC::White, 0.85f) : CalloutColor(E.Color));
		P.Transformed(At, PopScale(In) * Scale, bBig ? Callout::BigTilt : 0.f).Faded(Out).Text(E.Label, Font, At, Color, FVector2D(0.5, 0.5), 6.f * S);
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
	const FSlateFontInfo Font = KartUI::Font(EFont::Display, 40.f * S, 4.f * S, KC::Night);
	for (const FSamuraiNote& Note : G.GetNotes())
	{
		FVector2D At01;
		if (!View.Project(Note.World, At01))
		{
			continue;
		}
		const double Age = G.GetClock() - Note.BornAt;
		const FVector2D At = InsideSafe(At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * NoteText::RisePerSecond) * S), Note.Label.ToString(), Font, Note.Scale * Callout::Overshoot, P.Size());
		const float In = KartUI::Ease::OutBack(Clamp01(Age / NoteText::PopSeconds), NoteText::PopBack);
		const float Out = Clamp01((Note.Life - Age) / NoteText::FadeSeconds);
		P.Transformed(At, PopScale(In) * Note.Scale).Faded(Out).Text(Note.Label, Font, At, Note.Color, FVector2D(0.5, 0.5), 5.f * S);
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
	const float In = KartUI::Ease::OutBack(Clamp01(Age / Banner::PopSeconds), Banner::PopBack);
	const float Out = Clamp01((Banner::Life - Age) / Banner::FadeSeconds);
	const FKartPainter F = P.Faded(Out);
	// An ink stroke behind the words, swept in from the left.
	const float Sweep = KartUI::Ease::OutCubic(Clamp01(Age / Banner::SweepSeconds));
	const float BandW = Banner::BandWidth * S * Sweep;
	const FVector2D BandAt = Centre - FVector2D(Banner::BandWidth / 2.f * S, Banner::BandHeight / 2.f * S);
	const FKartPainter Tilted = F.Transformed(Centre, 1.f, Banner::Tilt);
	Tilted.Box(BandAt, FVector2D(BandW, Banner::BandHeight * S), KC::WithAlpha(KC::Night, 0.72f), Banner::BandHeight / 2.f * S);
	Tilted.Box(BandAt + FVector2D(0.f, Banner::UnderlineY * S), FVector2D(BandW, Banner::UnderlineHeight * S), G.GetBannerColor(), Banner::UnderlineHeight / 2.f * S);
	F.Transformed(Centre, PopScale(In), Banner::Tilt).Text(G.GetBanner(), KartUI::Font(EFont::Display, 110.f * S, 6.f * S, KC::Night), Centre, G.GetBannerColor(),
		FVector2D(0.5, 0.5), 8.f * S);
}

void SSamuraiHud::PaintFocusFrame(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const float Mood = G.GetFocusMood();
	if (Mood < 0.01f)
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
	const FMatch& M = G.GetMatch();
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Countdown::Y01);
	if (M.Phase == EMatchPhase::Countdown)
	{
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 Number = FMath::CeilToInt32(Remaining);
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(1.0 - (Number - Remaining)) / Countdown::PopSeconds, 0.f, 1.f));
		const FKartPainter Popped = P.Transformed(Centre, PopScale(Pop));
		Popped.Circle(Centre, Countdown::DiscRadius * S, KC::WithAlpha(Vermilion, 0.85f), KC::WithAlpha(KC::Night, 0.6f), 6.f * S);
		Popped.Text(CountText.Get(FMath::Max(1, Number), FormatInt), KartUI::Font(EFont::Display, 200.f * S, 9.f * S, KC::Night), Centre, KC::White,
			FVector2D(0.5, 0.5), 10.f * S);
		return;
	}
	if (M.Phase == EMatchPhase::Playing && M.ElapsedMs < Countdown::GoMs)
	{
		const float A = FMath::Clamp(static_cast<float>((Countdown::GoMs - M.ElapsedMs) / Countdown::GoFadeMs), 0.f, 1.f);
		const float Pop = KartUI::Ease::OutBack(Clamp01(M.ElapsedMs / Countdown::GoPopMs), 2.f);
		P.Faded(A).Transformed(Centre, PopScale(Pop), Countdown::GoTilt).Text(NSLOCTEXT("Samurai", "Go", "SLICE!"), KartUI::Font(EFont::Display, 160.f * S, 8.f * S, KC::Night),
			Centre, KC::Hot, FVector2D(0.5, 0.5), 9.f * S);
	}
}

void SSamuraiHud::PaintControls(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	if (!G.HasLocal() || G.GetMatch().ElapsedMs > Controls::ShowMs)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((Controls::ShowMs - G.GetMatch().ElapsedMs) / Controls::FadeMs), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Controls::Y01);
	const FKartPainter F = P.Faded(A);
	F.Card(Centre - Controls::CardSize / 2.f * S, Controls::CardSize * S, KC::WithAlpha(KC::Night, 0.8f), 22.f * S, 6.f * S, KC::Sun, 2.f * S);
	F.Text(NSLOCTEXT("Samurai", "Controls", "Mouse or stick: swing fast through the fruit  ·  never the bombs"), KartUI::Font(EFont::Body, 24.f * S), Centre,
		KC::White, FVector2D(0.5, 0.5));
}

void SSamuraiHud::PaintIntro(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
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
		P.Box(FVector2D::ZeroVector, FVector2D(Size.X, BarH), KC::WithAlpha(FLinearColor::Black, 0.92f));
		P.Box(FVector2D(0.f, Size.Y - BarH), FVector2D(Size.X, BarH), KC::WithAlpha(FLinearColor::Black, 0.92f));
	}
	if (A <= 0.f)
	{
		return;
	}
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(Size.X / 2.f, Size.Y * Intro::CentreY01);
	// The rising sun behind the title, and an ink stroke under it.
	const float Rise = KartUI::Ease::OutBack(Clamp01((Age - 0.1) / 0.6), 1.4f);
	F.Circle(Centre + FVector2D(0.f, (1.f - Rise) * Intro::SunRise * S - Intro::SunY * S), Intro::SunRadius * S * PopScale(Rise), KC::WithAlpha(Vermilion, 0.88f));
	const float Stroke = KartUI::Ease::OutCubic(Clamp01((Age - 0.25) / 0.35));
	const FVector2D RulesAt = Centre + FVector2D(0.f, Intro::RulesY * S);
	F.Transformed(RulesAt, 1.f, -1.5f).Box(Centre + FVector2D(-Intro::StrokeWidth / 2.f * S, Intro::StrokeY * S), FVector2D(Intro::StrokeWidth * S * Stroke, Intro::StrokeHeight * S),
		KC::WithAlpha(KC::Night, 0.8f), Intro::StrokeHeight / 2.f * S);
	const float TitlePop = KartUI::Ease::OutBack(Clamp01((Age - 0.15) / 0.45), 1.8f);
	F.Transformed(Centre, PopScale(TitlePop)).Text(NSLOCTEXT("Samurai", "Title", "SAMURAI SLICE"), KartUI::Font(EFont::Display, 150.f * S, 8.f * S, KC::Night),
		Centre - FVector2D(0.f, Intro::TitleY * S), KC::White, FVector2D(0.5, 0.5), 10.f * S);
	F.Text(NSLOCTEXT("Samurai", "Rules", "Cut the fruit. Never the bombs. Three precise cuts: Focus, double points."), KartUI::Font(EFont::Body, 26.f * S, 1.f * S, KC::Night),
		RulesAt, KC::White, FVector2D(0.5, 0.5), 2.f * S);
	PaintIntroSeats(F, G, Centre, Age, S);
}

void SSamuraiHud::PaintIntroSeats(const FKartPainter& F, const ASamuraiGame& G, const FVector2D& Centre, double Age, float S) const
{
	// The swordsmen, a chip each in their colour, popping in one by one.
	const TArray<FSamuraiSeat>& Seats = G.GetSeats();
	const float ChipW = Intro::ChipWidth * S, Gap = Intro::ChipGap * S;
	const float ChipH = Intro::ChipHeight * S;
	const float RowW = Seats.Num() * ChipW + (Seats.Num() - 1) * Gap;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float In = KartUI::Ease::OutBack(Clamp01((Age - Intro::ChipsFrom - i * Intro::ChipStagger) / 0.35), 2.f);
		const FVector2D Pos(F.Size().X / 2.f - RowW / 2.f + i * (ChipW + Gap), Centre.Y + Intro::ChipsY * S);
		const FKartPainter Chip = F.Transformed(Pos + FVector2D(ChipW / 2.f, ChipH / 2.f), PopScale(In));
		Chip.Card(Pos, FVector2D(ChipW, ChipH), KC::WithAlpha(KC::Night, 0.86f), ChipH / 2.f, 4.f * S, Seats[i].Color, 3.f * S);
		Chip.Circle(Pos + FVector2D(30.f * S, ChipH / 2.f), 11.f * S, Seats[i].Color);
		Chip.Text(Seats[i].Name, KartUI::Font(EFont::Title, 28.f * S), Pos + FVector2D(52.f * S, ChipH / 2.f), KC::White, FVector2D(0.0, 0.5));
		if (Seats[i].Kind == ESwordKind::Bot)
		{
			Chip.Text(NSLOCTEXT("Samurai", "Cpu", "CPU"), KartUI::Font(EFont::Label, 14.f * S), Pos + FVector2D(ChipW - 18.f * S, ChipH / 2.f), KC::WithAlpha(KC::White, 0.55f),
				FVector2D(1.0, 0.5));
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
	if (State != ESamuraiPractice::Cut)
	{
		PaintPracticeSwipe(P, G, S);
	}
	// The coach mark.
	const double Age = G.GetPracticeAge();
	const FVector2D Size = P.Size();
	const FVector2D Centre(Size.X / 2.f, Size.Y * Practice::CardY01);
	const float In = KartUI::Ease::OutBack(Clamp01((State == ESamuraiPractice::Cut ? 1.0 : Age) / 0.35), 1.8f);
	const FKartPainter Card = P.Transformed(Centre, PopScale(In));
	const FLinearColor Edge = State == ESamuraiPractice::Cut ? Jade : (State == ESamuraiPractice::TooSlow ? Gilt : KC::Sun);
	Card.Card(Centre - Practice::CardSize / 2.f * S, Practice::CardSize * S, KC::WithAlpha(KC::Night, 0.88f), 26.f * S, 6.f * S, Edge, 3.f * S);
	Card.Box(Centre + Practice::ChipOffset * S, Practice::ChipSize * S, Edge, Practice::ChipSize.Y / 2.f * S);
	Card.Text(NSLOCTEXT("Samurai", "PracticeChip", "PRACTICE"), KartUI::Font(EFont::Label, 18.f * S), Centre + Practice::ChipLabel * S, KC::Night, FVector2D(0.5, 0.5));
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
		P.Faded(Clamp01(Age / 0.35)).Text(NSLOCTEXT("Samurai", "PracticeSkip", "SPACE / (A)  skip"), KartUI::Font(EFont::Label, 16.f * S), Centre + Practice::SkipOffset * S,
			KC::WithAlpha(KC::White, 0.7f), FVector2D(1.0, 0.5), 2.f * S);
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
	const float Fade = Clamp01((Age - 0.3) / 0.3);
	Scratch.Reset();
	for (int32 i = 0; i <= Practice::ArcSteps; ++i)
	{
		const float T = i / static_cast<float>(Practice::ArcSteps);
		const float Angle = FMath::DegreesToRadians(FMath::Lerp(Practice::ArcFrom, Practice::ArcTo, T));
		Scratch.Add(C + FVector2D(FMath::Cos(Angle) * R, -FMath::Sin(Angle) * R * Practice::ArcSquash - R * Practice::ArcSquash));
	}
	const FKartPainter F = P.Faded(Fade);
	F.Lines(Scratch, KC::WithAlpha(KC::White, 0.55f), 5.f * S);
	// The arrowhead at the arc's end.
	const FVector2D Tail = Scratch.Last(), Before = Scratch[Scratch.Num() - 2];
	const FVector2D Dir = (Tail - Before).GetSafeNormal(), Side(-Dir.Y, Dir.X);
	const FVector2D Back = Tail - Dir * Practice::ArrowLength * S;
	F.Lines({ Back + Side * Practice::ArrowWidth * S, Tail, Back - Side * Practice::ArrowWidth * S }, KC::WithAlpha(KC::White, 0.8f), 5.f * S);
	const float Run = FMath::Fmod(static_cast<float>(Age), Practice::BeadPeriod) / Practice::BeadRun;
	if (Run <= 1.f)
	{
		const int32 k = FMath::Clamp(FMath::FloorToInt32(Run * Practice::ArcSteps), 0, Practice::ArcSteps - 1);
		const FVector2D Bead = FMath::Lerp(Scratch[k], Scratch[k + 1], Run * Practice::ArcSteps - k);
		F.Circle(Bead, Practice::BeadRadius * S, Gilt, KC::WithAlpha(KC::Night, 0.6f), 2.f * S);
	}
}

void SSamuraiHud::PaintStandings(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const TArray<FPlayer>& Players = G.GetMatch().Players;
	const TArray<int32>& Order = Ranks.Update(Players.Num(), [&Players](int32 i) { return static_cast<int64>(Players[i].Score); });
	StandingTexts.SetNum(Players.Num());
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(Clamp01(Age / 0.5));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * Standings::TitleY01);
	F.Circle(Centre, Standings::SunRadius * S * PopScale(KartUI::Ease::OutBack(Clamp01(Age / 0.5))), KC::WithAlpha(Vermilion, 0.8f));
	F.Text(NSLOCTEXT("Samurai", "Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	const FVector2D RowSize = Standings::RowSize * S;
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FPlayer& Player = Players[Order[i]];
		const FSamuraiSeat* Seat = G.GetSeats().IsValidIndex(Player.LaneIndex) ? &G.GetSeats()[Player.LaneIndex] : nullptr;
		const float In = KartUI::Ease::OutBack(Clamp01((Age - Standings::RowsFrom - i * Standings::RowStagger) / 0.4));
		const FVector2D Pos(P.Size().X / 2.f - RowSize.X / 2.f, P.Size().Y * Standings::RowsY01 + i * Standings::RowPitch * S);
		const FKartPainter Row = F.Transformed(Pos + RowSize / 2.f, PopScale(In));
		const bool bWinner = G.GetMatch().WinnerIds.Contains(Player.Id);
		Row.Card(Pos, RowSize, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.85f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::Sky, 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		FStandingTexts& PlayerTexts = StandingTexts[Order[i]];
		const FString& Detail = PlayerTexts.Detail.Get(FIntVector(Player.BestCombo, Player.Hits, Player.Bombs), [](const FIntVector& Stats)
		{
			return FString::Printf(TEXT("best combo %d  ·  %d cuts  ·  %d bombs"), Stats.X, Stats.Y, Stats.Z);
		});
		Row.Text(StandingTexts[i].Place.Get(i + 1, FormatInt), KartUI::Font(EFont::Display, 52.f * S), Pos + FVector2D(48.f * S, RowSize.Y / 2.f), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Player.Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		Row.Text(Detail, KartUI::Font(EFont::Body, 18.f * S), Pos + FVector2D(100.f * S, 64.f * S), KC::WithAlpha(Ink, 0.75f), FVector2D(0.0, 0.5));
		Row.Text(PlayerTexts.Score.Get(Player.Score, FormatInt), KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(RowSize.X - 30.f * S, RowSize.Y / 2.f), Ink, FVector2D(1.0, 0.5));
	}
}
