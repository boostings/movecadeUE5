#include "Games/Sumo/SSumoHud.h"

#include "Games/Sumo/SumoGame.h"
#include "Games/Sumo/SumoPresentation.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Sumo;
namespace KC = KartUI::Color;
using KartUI::EFont;

// Layout at 1080p: every size and offset below is multiplied by the canvas scale S. Offsets inside a card are from its
// top-left corner unless they say otherwise; font sizes are in the same units.
namespace
{
	/** Paint never scales a transform to zero (Slate drops degenerate transforms). */
	constexpr float MinTransformScale = 0.01f;

	/** The round card at the top centre: "ROUND n", a dot per round, then the shrink notice under it. */
	namespace TopCard
	{
		const FVector2D Size(300.f, 96.f);
		constexpr float Radius = 26.f;
		constexpr float Shadow = 6.f;
		constexpr float Outline = 3.f;
		constexpr float Alpha = 0.82f;
		constexpr float TitleY = 36.f;
		constexpr float TitleFont = 44.f;
		constexpr float TitleOutline = 3.f;
		constexpr float DotsY = 74.f;
		constexpr float DotGap = 30.f;
		constexpr float DotRadius = 9.f;
		constexpr float DotOutline = 2.f;
		constexpr int32 MinDots = 3;
		constexpr float DrawDotAlpha = 0.35f;
		constexpr float FutureDotAlpha = 0.1f;
		constexpr float DotRingAlpha = 0.5f;
		/** The round being fought pulses. */
		constexpr float NowDotAlpha = 0.35f;
		constexpr float NowDotPulse = 0.25f;
		constexpr float NowDotRate = 5.f;
	}

	/** The shrink notice: calm "RING SHRINKS IN n" in the last seconds, a pulsing red bar once the band is warned. */
	namespace ShrinkNotice
	{
		constexpr float Y = 126.f;
		constexpr double CalmSeconds = 12.0;
		const FVector2D WarnHalf(190.f, 22.f);
		constexpr float WarnFont = 22.f;
		constexpr float WarnPulseRate = 14.f;
		constexpr float WarnPulseScale = 0.06f;
		constexpr float WarnDarken = 0.3f;
		constexpr float WarnNightAlpha = 0.9f;
		const FVector2D CalmHalf(150.f, 18.f);
		constexpr float CalmFont = 18.f;
		constexpr float CalmAlpha = 0.72f;
		constexpr float CalmTextAlpha = 0.85f;
	}

	/** The player panels along the bottom. */
	namespace Panel
	{
		constexpr float MaxWidth = 390.f;
		constexpr float Height = 128.f;
		constexpr float Gap = 22.f;
		constexpr float ScreenShare = 0.9f;
		constexpr float DownAlpha = 0.55f;
		constexpr float Radius = 22.f;
		constexpr float Shadow = 6.f;
		constexpr float Outline = 3.f;
		constexpr float CardAlpha = 0.86f;
		/** The colour tab down the left. */
		const FVector2D TabAt(14.f, 14.f);
		constexpr float TabWidth = 10.f;
		constexpr float TabInset = 28.f;
		constexpr float TabRadius = 5.f;
		const FVector2D NameAt(38.f, 30.f);
		constexpr float NameFont = 26.f;
		/** Round wins as stars. */
		constexpr float StarsX = 50.f;
		constexpr float StarsY = 70.f;
		constexpr float StarGap = 34.f;
		constexpr float StarRadius = 14.f;
		constexpr float EmptyStarAlpha = 0.16f;
		const FVector2D KnockoutsAt(128.f, 70.f);
		constexpr float KnockoutsFont = 18.f;
		constexpr float KnockoutsAlpha = 0.85f;
		/** The score, right-aligned. */
		constexpr float ScoreRight = 20.f;
		constexpr float ScoreY = 44.f;
		constexpr float ScoreFont = 50.f;
		constexpr float ScoreOutline = 3.f;
		/** The dash meter along the bottom: dim while it reloads, lit and labelled when ready. */
		const FVector2D BarAt(38.f, 98.f);
		constexpr float BarInset = 58.f;
		constexpr float BarHeight = 12.f;
		constexpr float BarTrackAlpha = 0.12f;
		constexpr float BarReloadAlpha = 0.45f;
		constexpr float DashLabelFont = 13.f;
		constexpr float DashLabelInset = 6.f;
		/** The power-up chip over the panel's top right, its fill running down. */
		constexpr float ChipFromRight = 150.f;
		constexpr float ChipY = -22.f;
		const FVector2D ChipSize(130.f, 34.f);
		constexpr float ChipAlpha = 0.9f;
		constexpr float ChipOutline = 2.f;
		constexpr float ChipInset = 4.f;
		constexpr float ChipFillAlpha = 0.4f;
		constexpr float ChipFont = 17.f;
		/** The banner over a panel: SPLASH! for a fallen puck, the phone nag for a quiet one. */
		constexpr float BannerY = -20.f;
		constexpr float SplashFont = 34.f;
		constexpr float QuietFont = 24.f;
		constexpr float BannerOutline = 3.f;
		constexpr float BannerShadow = 4.f;
	}

	/** Name tags float over the pucks (cm over a puck's root). */
	namespace Tag
	{
		constexpr double HeightCm = 195.0;
		constexpr float Font = 17.f;
		const FVector2D Padding(24.f, 10.f);
		constexpr float Alpha = 0.92f;
		constexpr float FallingAlpha = 0.4f;
		constexpr float CardAlpha = 0.82f;
		constexpr float Outline = 2.f;
		/** The pointer under the tag. */
		constexpr float PointerHalf = 7.f;
		constexpr float PointerDepth = 9.f;
	}

	/** The knockout feed, top right: rows that slide in, stay a while and fade. */
	namespace Feed
	{
		constexpr int32 Rows = 4;
		constexpr float TopGap = 8.f;
		constexpr float Width = 420.f;
		constexpr float RowPitch = 52.f;
		constexpr float RowHeight = 44.f;
		constexpr float Radius = 22.f;
		constexpr float CardAlpha = 0.78f;
		constexpr double Seconds = 6.0;
		constexpr double FadeSeconds = 0.5;
		constexpr double InSeconds = 0.2;
		constexpr double SlideSeconds = 0.25;
		constexpr float Slide = 60.f;
		constexpr float NameFont = 21.f;
		constexpr float NameInset = 22.f;
		constexpr float MidY = 22.f;
		constexpr float KoX = 210.f;
		constexpr float KoFont = 16.f;
		constexpr float RingOutFont = 18.f;
		constexpr float RingOutAlpha = 0.7f;
		constexpr float VictimRight = 398.f;
	}

	/** Callouts: punch in with an overshoot, rise (px/s) over the action, fade at the end; centred ones sit high. */
	namespace Callout
	{
		constexpr double InSeconds = 0.22;
		constexpr float Overshoot = 2.6f;
		constexpr double OutSeconds = 0.3;
		constexpr double Rise = 60.0;
		constexpr float CentreY = 0.25f;
		constexpr float BigTiltDeg = -3.f;
		constexpr float CentreBigFont = 88.f;
		constexpr float CentreFont = 60.f;
		constexpr float WorldBigFont = 58.f;
		constexpr float WorldFont = 42.f;
		constexpr float Outline = 5.f;
		constexpr float Shadow = 7.f;
		constexpr float CentreSubFont = 32.f;
		constexpr float WorldSubFont = 24.f;
		constexpr float SubOutline = 3.f;
		constexpr float SubShadow = 4.f;
		/** The sub-line sits under the word, by this share of its font size. */
		constexpr float SubDrop = 0.62f;
	}

	/** The 3-2-1 over the ring: "ROUND n" fades in above a digit that punches in each second. */
	namespace Countdown
	{
		constexpr float CentreY = 0.4f;
		constexpr int32 FirstDigit = 3;
		constexpr float PunchSeconds = 0.35f;
		constexpr double FadeMs = 300.0;
		constexpr float RoundY = -140.f;
		constexpr float RoundFont = 64.f;
		constexpr float RoundOutline = 4.f;
		constexpr float RoundShadow = 6.f;
		constexpr float DigitFont = 200.f;
		constexpr float DigitOutline = 9.f;
		constexpr float DigitShadow = 10.f;
	}

	/** The controls card: shown for the first seconds of the first round, then faded. */
	namespace Controls
	{
		constexpr double ShownMs = 9000.0;
		constexpr double FadeMs = 600.0;
		constexpr float CentreY = 0.68f;
		constexpr float Font = 22.f;
		constexpr float Padding = 64.f;
		constexpr float Height = 72.f;
		constexpr float Radius = 22.f;
		constexpr float Shadow = 6.f;
		constexpr float Outline = 2.f;
		constexpr float CardAlpha = 0.78f;
	}

	/** The intro card: the title pops in tilted, the line-up in their colours, the one-line rules. */
	namespace Intro
	{
		constexpr double FadeInSeconds = 0.5;
		constexpr double FadeOutSeconds = 0.4;
		constexpr double PopSeconds = 0.6;
		constexpr float PopOvershoot = 1.8f;
		constexpr float CentreY = 0.42f;
		constexpr float TitleY = -60.f;
		constexpr float TitleTiltDeg = -3.f;
		constexpr float TitleFont = 150.f;
		constexpr float TitleOutline = 8.f;
		constexpr float TitleShadow = 10.f;
		constexpr float NamesY = 56.f;
		constexpr float NameFont = 36.f;
		constexpr float NameOutline = 2.f;
		constexpr float NameShadow = 4.f;
		constexpr float NameGap = 60.f;
		constexpr float VersusY = 58.f;
		constexpr float VersusFont = 22.f;
		constexpr float VersusAlpha = 0.7f;
		constexpr float RulesY = 112.f;
		constexpr float RulesFont = 26.f;
		constexpr float RulesAlpha = 0.9f;
		constexpr float RulesShadow = 3.f;
	}

	/** The final standings, right of the champion: they wait for the champion's callout, then rows pop in in turn. */
	namespace Standings
	{
		constexpr double At = 1.2;
		constexpr double FadeSeconds = 0.5;
		constexpr double RowsAt = 1.5;
		constexpr double RowStagger = 0.15;
		constexpr double RowInSeconds = 0.4;
		/** The heading's centre: this far in from the safe area's right edge, this far down the screen. */
		constexpr float HeadFromRight = 400.f;
		constexpr float HeadY = 0.14f;
		constexpr float HeadFont = 64.f;
		constexpr float HeadOutline = 4.f;
		constexpr float HeadShadow = 6.f;
		/** Rows: centred under the heading, from this far down the screen. */
		constexpr float RowsY = 0.22f;
		const FVector2D RowSize(760.f, 80.f);
		constexpr float RowPitch = 94.f;
		constexpr float Radius = 22.f;
		constexpr float Shadow = 6.f;
		constexpr float Outline = 3.f;
		constexpr float CardAlpha = 0.86f;
		const FVector2D PlaceAt(44.f, 40.f);
		constexpr float PlaceFont = 48.f;
		constexpr float PlaceShadow = 3.f;
		const FVector2D TabAt(84.f, 18.f);
		const FVector2D TabSize(10.f, 44.f);
		constexpr float TabRadius = 5.f;
		const FVector2D NameAt(110.f, 28.f);
		constexpr float NameFont = 32.f;
		const FVector2D RecordAt(110.f, 58.f);
		constexpr float RecordFont = 18.f;
		constexpr float RecordAlpha = 0.75f;
		constexpr float StarsX = 560.f;
		constexpr float StarsY = 40.f;
		constexpr float StarGap = 32.f;
		constexpr float StarRadius = 14.f;
		const FVector2D ScoreAt(735.f, 40.f);
		constexpr float ScoreFont = 50.f;
	}

	/** A five-point star centred on C, outer radius R (the inner points at InnerShare of it). */
	void Star(const FKartPainter& P, const FVector2D& C, float R, const FLinearColor& Fill)
	{
		constexpr int32 Points = 5;
		constexpr float InnerShare = 0.45f;
		TArray<FVector2D, TInlineAllocator<Points * 2>> Corners;
		for (int32 i = 0; i < Points * 2; ++i)
		{
			const float A = -HALF_PI + PI * i / static_cast<float>(Points);
			const float Radius = i % 2 ? R * InnerShare : R;
			Corners.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
		}
		P.Polygon(Corners, Fill);
	}

	const FText& PowerLabel(EPower Power)
	{
		switch (Power)
		{
		case EPower::Heavy: return ARCADE_LABEL("Sumo", "PowerHeavy", "HEAVY");
		case EPower::Shield: return ARCADE_LABEL("Sumo", "PowerShield", "SHIELD");
		case EPower::Turbo: return ARCADE_LABEL("Sumo", "PowerTurbo", "TURBO");
		default: return FText::GetEmpty();
		}
	}

	/** A power-up's full time (ms), for its chip's countdown bar. */
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

	/** Round wins, knockouts and falls in one key for the standings row's line (each well under 2^16). */
	int64 RecordKey(const FPuck& Puck)
	{
		return (static_cast<int64>(Puck.RoundWins) << 32) | (static_cast<int64>(Puck.Knockouts & 0xffff) << 16) | static_cast<int64>(Puck.Falls & 0xffff);
	}

	FString RoundTitle(int32 Round)
	{
		return FString::Printf(TEXT("ROUND %d"), Round);
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
	{
		const FArcadeHudView View = FArcadeHudView::Capture(G->GetViewer());
		PaintCallouts(P, *G, View, S);
		PaintStandings(P, *G, S);
		break;
	}
	default:
	{
		// One projection for every point this paint (the tags and the callouts).
		const FArcadeHudView View = FArcadeHudView::Capture(G->GetViewer());
		PaintTags(P, *G, View, S);
		PaintTop(P, *G, S);
		PaintPanels(P, *G, S);
		PaintFeed(P, *G, S);
		PaintCallouts(P, *G, View, S);
		PaintControls(P, *G, S);
		PaintCountdown(P, *G, S);
		break;
	}
	}
	return Layer;
}

void SSumoHud::PaintTop(const FKartPainter& P, const ASumoGame& G, float S) const
{
	using namespace TopCard;
	const FMatch& M = G.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, P.Size().Y * KartUI::Metric::SafeFraction);
	const FVector2D CardSize = Size * S;
	P.Card(Top - FVector2D(CardSize.X / 2.f, 0.f), CardSize, KC::WithAlpha(KC::Night, Alpha), Radius * S, Shadow * S, KC::Sun, Outline * S);
	P.Text(RoundText.Get(M.Round, RoundTitle), KartUI::Font(EFont::Display, TitleFont * S, TitleOutline * S, KC::Night), Top + FVector2D(0.f, TitleY * S), KC::White,
		FVector2D(0.5, 0.5), TitleOutline * S);
	// The rounds so far: a dot per round in its winner's colour (grey for a draw), then the one being fought.
	const int32 Dots = FMath::Max(MinDots, M.RoundWinners.Num() + (M.Phase == EPhase::MatchOver ? 0 : 1));
	for (int32 i = 0; i < Dots; ++i)
	{
		const FVector2D C = Top + FVector2D((i - (Dots - 1) / 2.f) * DotGap * S, DotsY * S);
		if (M.RoundWinners.IsValidIndex(i))
		{
			const int32 W = M.RoundWinners[i];
			P.Circle(C, DotRadius * S, W == INDEX_NONE ? KC::WithAlpha(KC::White, DrawDotAlpha) : G.ColorOf(W), KC::Night, DotOutline * S);
		}
		else
		{
			const bool bNow = i == M.RoundWinners.Num();
			P.Circle(C, DotRadius * S, bNow ? KC::WithAlpha(KC::Sun, NowDotAlpha + NowDotPulse * FMath::Sin(static_cast<float>(G.GetClock()) * NowDotRate)) : KC::WithAlpha(KC::White, FutureDotAlpha),
				KC::WithAlpha(KC::White, DotRingAlpha), DotOutline * S);
		}
	}
	if (M.Phase == EPhase::Fight)
	{
		PaintShrink(P, G, Top + FVector2D(0.f, ShrinkNotice::Y * S), S);
	}
}

void SSumoHud::PaintShrink(const FKartPainter& P, const ASumoGame& G, const FVector2D& C, float S) const
{
	using namespace ShrinkNotice;
	const FMatch& M = G.GetMatch();
	const double Next = ShrinkAtMs(M.ShrinksDone);
	if (!FMath::IsFinite(Next))
	{
		return;
	}
	const double In = (Next - M.RoundMs) / 1000.0;
	if (M.IsShrinkPending())
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(G.GetClock()) * WarnPulseRate);
		const FKartPainter T = P.Transformed(C, 1.f + WarnPulseScale * Pulse);
		T.Box(C - WarnHalf * S, WarnHalf * 2.f * S, FMath::Lerp(KC::Hot, KC::WithAlpha(KC::Night, WarnNightAlpha), WarnDarken * Pulse), WarnHalf.Y * S);
		// Seconds and which band in one key: odd while the core is the one falling.
		const int32 Key = FMath::Max(1, FMath::CeilToInt32(In)) * 2 + (M.ShrinksDone >= RingBands ? 1 : 0);
		const FString& Line = ShrinkWarningText.Get(Key, [](int32 K) { return FString::Printf(TEXT("%s IN %d"), K % 2 ? TEXT("CORE FALLS") : TEXT("EDGE FALLS"), K / 2); });
		T.Text(Line, KartUI::Font(EFont::Label, WarnFont * S), C, KC::White, FVector2D(0.5, 0.5));
	}
	else if (In < CalmSeconds)
	{
		P.Box(C - CalmHalf * S, CalmHalf * 2.f * S, KC::WithAlpha(KC::Night, CalmAlpha), CalmHalf.Y * S);
		P.Text(ShrinkInText.Get(FMath::CeilToInt32(In), [](int32 Seconds) { return FString::Printf(TEXT("RING SHRINKS IN %d"), Seconds); }),
			KartUI::Font(EFont::Label, CalmFont * S), C, KC::WithAlpha(KC::White, CalmTextAlpha), FVector2D(0.5, 0.5));
	}
}

void SSumoHud::PaintPanels(const FKartPainter& P, const ASumoGame& G, float S) const
{
	const int32 N = G.GetMatch().Pucks.Num();
	PanelText.SetNum(N);
	const float Gap = Panel::Gap * S;
	const float Width = FMath::Min(Panel::MaxWidth * S, (P.Size().X * Panel::ScreenShare - Gap * (N - 1)) / FMath::Max(1, N));
	const float Height = Panel::Height * S;
	const float Total = Width * N + Gap * (N - 1);
	const float Y = P.Size().Y * (1.f - KartUI::Metric::SafeFraction) - Height;
	for (int32 i = 0; i < N; ++i)
	{
		PaintPanel(P, G, i, FVector2D(P.Size().X / 2.f - Total / 2.f + i * (Width + Gap), Y), FVector2D(Width, Height), S);
	}
}

void SSumoHud::PaintPanel(const FKartPainter& P, const ASumoGame& G, int32 Slot, const FVector2D& Pos, const FVector2D& Size, float S) const
{
	using namespace Panel;
	const FMatch& M = G.GetMatch();
	const FPuck& Puck = M.Pucks[Slot];
	const FSumoSeat* Seat = G.SeatOf(Slot);
	const FLinearColor Color = G.ColorOf(Slot);
	const float Width = static_cast<float>(Size.X);
	const float PanelHeight = static_cast<float>(Size.Y);
	const bool bDown = !Puck.bStanding;
	FPanelText& Text = PanelText[Slot];
	const FKartPainter F = P.Faded(bDown ? DownAlpha : 1.f);
	F.Card(Pos, Size, KC::WithAlpha(KC::Night, CardAlpha), Radius * S, Shadow * S, Color, Outline * S);
	F.Box(Pos + TabAt * S, FVector2D(TabWidth * S, PanelHeight - TabInset * S), Color, TabRadius * S);
	F.Text(Puck.Name, KartUI::Font(EFont::Title, NameFont * S), Pos + FVector2D(NameAt.X * S, NameAt.Y * S), KC::White, FVector2D(0.0, 0.5));
	for (int32 w = 0; w < WinsForMatch; ++w)
	{
		Star(F, Pos + FVector2D((StarsX + w * StarGap) * S, StarsY * S), StarRadius * S, w < Puck.RoundWins ? KC::Sun : KC::WithAlpha(KC::White, EmptyStarAlpha));
	}
	F.Text(Text.Knockouts.Get(Puck.Knockouts, [](int32 Knockouts) { return FString::Printf(TEXT("KO %d"), Knockouts); }), KartUI::Font(EFont::Label, KnockoutsFont * S),
		Pos + FVector2D(KnockoutsAt.X * S, KnockoutsAt.Y * S), KC::WithAlpha(KC::White, KnockoutsAlpha), FVector2D(0.0, 0.5));
	F.Text(Text.Score.Get(Puck.Score, [](int32 Score) { return FString::FromInt(Score); }), KartUI::Font(EFont::Display, ScoreFont * S), Pos + FVector2D(Width - ScoreRight * S, ScoreY * S),
		KC::White, FVector2D(1.0, 0.5), ScoreOutline * S);
	// The dash meter: filling while it reloads, lit when ready.
	const FVector2D Bar = Pos + FVector2D(BarAt.X * S, BarAt.Y * S);
	const float BarW = Width - BarInset * S;
	const float Fill = Puck.DashCooldownMs > 0.0 ? 1.f - static_cast<float>(Puck.DashCooldownMs / (Puck.Power == EPower::Turbo ? TurboCooldownMs : DashCooldownMs)) : 1.f;
	F.Box(Bar, FVector2D(BarW, BarHeight * S), KC::WithAlpha(KC::White, BarTrackAlpha), BarHeight / 2.f * S);
	F.Box(Bar, FVector2D(FMath::Max(BarHeight * S, BarW * FMath::Clamp(Fill, 0.f, 1.f)), BarHeight * S), Fill >= 1.f ? Color : KC::WithAlpha(Color, BarReloadAlpha), BarHeight / 2.f * S);
	if (Fill >= 1.f)
	{
		F.Text(ARCADE_LABEL("Sumo", "Dash", "DASH"), KartUI::Font(EFont::Label, DashLabelFont * S), Bar + FVector2D(BarW - DashLabelInset * S, BarHeight / 2.f * S), KC::Night,
			FVector2D(1.0, 0.5));
	}
	// A power-up chip over the panel (unfaded) with its time running down.
	if (Puck.Power != EPower::None && Puck.bStanding)
	{
		const FVector2D Chip = Pos + FVector2D(Width - ChipFromRight * S, ChipY * S);
		const float Left = FMath::Clamp(static_cast<float>(Puck.PowerMs / PowerTotal(Puck.Power)), 0.f, 1.f);
		P.Box(Chip, ChipSize * S, KC::WithAlpha(KC::Night, ChipAlpha), ChipSize.Y / 2.f * S, Color, ChipOutline * S);
		P.Box(Chip + FVector2D(ChipInset, ChipInset) * S, FVector2D((ChipSize.X - ChipInset * 2.f) * S * Left, (ChipSize.Y - ChipInset * 2.f) * S), KC::WithAlpha(KC::Sky, ChipFillAlpha),
			(ChipSize.Y / 2.f - ChipInset) * S);
		P.Text(PowerLabel(Puck.Power), KartUI::Font(EFont::Label, ChipFont * S), Chip + ChipSize / 2.f * S, KC::White, FVector2D(0.5, 0.5));
	}
	const bool bQuiet = Seat && Seat->Kind == ESumoPilot::Phone && G.IsPhoneQuiet(Slot) && M.Phase == EPhase::Fight;
	if (bDown || bQuiet)
	{
		const FText& Banner = bDown ? ARCADE_LABEL("Sumo", "Splash", "SPLASH!") : ARCADE_LABEL("Sumo", "Quiet", "PICK UP YOUR PHONE");
		P.Text(Banner, KartUI::Font(EFont::Display, (bQuiet ? QuietFont : SplashFont) * S, BannerOutline * S, KC::Night), Pos + FVector2D(Width / 2.f, BannerY * S), bDown ? KC::Sky : KC::Hot,
			FVector2D(0.5, 0.5), BannerShadow * S);
	}
}

void SSumoHud::PaintTags(const FKartPainter& P, const ASumoGame& G, const FArcadeHudView& View, float S) const
{
	using namespace Tag;
	const FText& You = ARCADE_LABEL("Sumo", "You", "YOU");
	const FMatch& M = G.GetMatch();
	const FSlateFontInfo TagFont = KartUI::Font(EFont::Label, Font * S);
	for (int32 i = 0; i < M.Pucks.Num(); ++i)
	{
		const FPuck& Puck = M.Pucks[i];
		FVector2D At01;
		if (Puck.bOut || !View.Project(G.PuckWorld(i) + FVector(0.0, 0.0, HeightCm), At01))
		{
			continue;
		}
		const FVector2D At = At01 * P.Size();
		const FLinearColor Color = G.ColorOf(i);
		const FSumoSeat* Seat = G.SeatOf(i);
		const FString& Label = Seat && Seat->Kind == ESumoPilot::Local ? You.ToString() : Puck.Name;
		const FVector2D Size = FKartPainter::Measure(Label, TagFont) + Padding * S;
		const FKartPainter F = P.Faded(Puck.bFalling ? FallingAlpha : Alpha);
		F.Box(At - FVector2D(Size.X / 2.f, Size.Y), Size, KC::WithAlpha(KC::Night, CardAlpha), Size.Y / 2.f, Color, Outline * S);
		F.Text(Label, TagFont, At - FVector2D(0.f, Size.Y / 2.f), KC::White, FVector2D(0.5, 0.5));
		F.Polygon({ At + FVector2D(-PointerHalf, 0.f) * S, At + FVector2D(PointerHalf, 0.f) * S, At + FVector2D(0.f, PointerDepth) * S }, Color);
	}
}

void SSumoHud::PaintFeed(const FKartPainter& P, const ASumoGame& G, float S) const
{
	using namespace Feed;
	const FMatch& M = G.GetMatch();
	const TArray<FSumoFeedLine>& Lines = G.GetFeed();
	const FVector2D Right(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction + TopGap * S);
	const FSlateFontInfo Font = KartUI::Font(EFont::Title, NameFont * S);
	int32 Row = 0;
	for (int32 k = Lines.Num() - 1; k >= 0 && Row < Rows; --k)
	{
		const FSumoFeedLine& L = Lines[k];
		const double Age = G.GetClock() - L.At;
		if (Age > Seconds || !M.Pucks.IsValidIndex(L.Victim))
		{
			continue;
		}
		const float A = FMath::Clamp(static_cast<float>((Seconds - Age) / FadeSeconds), 0.f, 1.f) * FMath::Clamp(static_cast<float>(Age / InSeconds), 0.f, 1.f);
		const FKartPainter F = P.Faded(A);
		const float SlideIn = (1.f - FMath::Clamp(static_cast<float>(Age / SlideSeconds), 0.f, 1.f)) * Slide * S;
		const FVector2D Pos = Right + FVector2D(-Width * S + SlideIn, Row * RowPitch * S);
		F.Box(Pos, FVector2D(Width * S, RowHeight * S), KC::WithAlpha(KC::Night, CardAlpha), Radius * S);
		if (M.Pucks.IsValidIndex(L.Killer))
		{
			F.Text(M.Pucks[L.Killer].Name, Font, Pos + FVector2D(NameInset * S, MidY * S), G.ColorOf(L.Killer), FVector2D(0.0, 0.5));
			F.Text(ARCADE_LABEL("Sumo", "FeedKo", "KO"), KartUI::Font(EFont::Label, KoFont * S), Pos + FVector2D(KoX * S, MidY * S), KC::Sun, FVector2D(0.5, 0.5));
		}
		else
		{
			F.Text(ARCADE_LABEL("Sumo", "FeedRingOut", "ring out"), KartUI::Font(EFont::Body, RingOutFont * S), Pos + FVector2D(NameInset * S, MidY * S),
				KC::WithAlpha(KC::White, RingOutAlpha), FVector2D(0.0, 0.5));
		}
		F.Text(M.Pucks[L.Victim].Name, Font, Pos + FVector2D(VictimRight * S, MidY * S), G.ColorOf(L.Victim), FVector2D(1.0, 0.5));
		++Row;
	}
}

void SSumoHud::PaintCallouts(const FKartPainter& P, const ASumoGame& G, const FArcadeHudView& View, float S) const
{
	using namespace Callout;
	for (const FSumoCallout& C : G.GetCallouts())
	{
		const double Age = G.GetClock() - C.At;
		FVector2D At;
		if (C.bCentre)
		{
			At = FVector2D(P.Size().X / 2.f, P.Size().Y * CentreY);
		}
		else
		{
			FVector2D At01;
			if (!View.Project(C.World, At01))
			{
				continue;
			}
			At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Age * Rise) * S);
		}
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / InSeconds), 0.f, 1.f), Overshoot);
		const float Out = FMath::Clamp(static_cast<float>((C.Duration - Age) / OutSeconds), 0.f, 1.f);
		const float Size = C.bCentre ? (C.bBig ? CentreBigFont : CentreFont) : (C.bBig ? WorldBigFont : WorldFont);
		const FKartPainter F = P.Transformed(At, FMath::Max(MinTransformScale, In), C.bBig ? BigTiltDeg : 0.f).Faded(Out);
		F.Text(C.Text, KartUI::Font(EFont::Display, Size * S, Outline * S, KC::Night), At, C.Color, FVector2D(0.5, 0.5), Shadow * S);
		if (!C.Sub.IsEmpty())
		{
			F.Text(C.Sub, KartUI::Font(EFont::Title, (C.bCentre ? CentreSubFont : WorldSubFont) * S, SubOutline * S, KC::Night), At + FVector2D(0.f, Size * SubDrop * S), KC::White,
				FVector2D(0.5, 0.5), SubShadow * S);
		}
	}
}

void SSumoHud::PaintCountdown(const FKartPainter& P, const ASumoGame& G, float S) const
{
	using namespace Countdown;
	const FMatch& M = G.GetMatch();
	if (M.Phase != EPhase::Countdown)
	{
		return;
	}
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	const double Remaining = (CountdownMs - M.PhaseMs) / 1000.0;
	const int32 N = FMath::Clamp(FMath::CeilToInt32(Remaining), 1, FirstDigit);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(1.0 - (N - Remaining)) / PunchSeconds, 0.f, 1.f));
	const float Fade = FMath::Clamp(static_cast<float>(M.PhaseMs / FadeMs), 0.f, 1.f);
	P.Faded(Fade).Text(RoundText.Get(M.Round, RoundTitle), KartUI::Font(EFont::Display, RoundFont * S, RoundOutline * S, KC::Night), Centre + FVector2D(0.f, RoundY * S), KC::White,
		FVector2D(0.5, 0.5), RoundShadow * S);
	P.Transformed(Centre, FMath::Max(MinTransformScale, Pop)).Text(CountdownText.Get(N, [](int32 Digit) { return FString::FromInt(Digit); }),
		KartUI::Font(EFont::Display, DigitFont * S, DigitOutline * S, KC::Night), Centre, KC::Sun, FVector2D(0.5, 0.5), DigitShadow * S);
}

void SSumoHud::PaintControls(const FKartPainter& P, const ASumoGame& G, float S) const
{
	using namespace Controls;
	const FMatch& M = G.GetMatch();
	if (!G.HasLocal() || M.Round > 1 || M.RoundMs > ShownMs)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((ShownMs - M.RoundMs) / FadeMs), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	const FKartPainter F = P.Faded(A);
	const FText& Line = ARCADE_LABEL("Sumo", "Controls", "Stick or WASD: roll  ·  A, Space or click: dash  ·  Push them off the edge!");
	const FSlateFontInfo LineFont = KartUI::Font(EFont::Body, Font * S);
	const float W = static_cast<float>(FKartPainter::Measure(Line.ToString(), LineFont).X) + Padding * S;
	F.Card(Centre - FVector2D(W / 2.f, Height / 2.f * S), FVector2D(W, Height * S), KC::WithAlpha(KC::Night, CardAlpha), Radius * S, Shadow * S, KC::Sun, Outline * S);
	F.Text(Line, LineFont, Centre, KC::White, FVector2D(0.5, 0.5));
}

void SSumoHud::PaintIntro(const FKartPainter& P, const ASumoGame& G, float S) const
{
	using namespace Intro;
	using MovecadeKart::Sumo::Presentation::IntroSeconds;
	const double Age = G.GetStageAge();
	const float A = FMath::Clamp(static_cast<float>(Age / FadeInSeconds), 0.f, 1.f) * FMath::Clamp(static_cast<float>((IntroSeconds - Age) / FadeOutSeconds), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	const float Pop = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / PopSeconds), 0.f, 1.f), PopOvershoot);
	const FVector2D TitleAt = Centre + FVector2D(0.f, TitleY * S);
	F.Transformed(TitleAt, FMath::Max(MinTransformScale, Pop), TitleTiltDeg).Text(ARCADE_LABEL("Sumo", "Title", "SUMO BUMPERS"),
		KartUI::Font(EFont::Display, TitleFont * S, TitleOutline * S), TitleAt, KC::Hot, FVector2D(0.5, 0.5), TitleShadow * S);
	// The line-up in their colours, "vs" between them.
	const TArray<FSumoSeat>& Seats = G.GetSeats();
	const FSlateFontInfo Font = KartUI::Font(EFont::Title, NameFont * S, NameOutline * S);
	const float Gap = NameGap * S;
	TArray<float, TInlineAllocator<MaxPlayers>> Widths;
	float Total = Gap * FMath::Max(0, Seats.Num() - 1);
	for (const FSumoSeat& Seat : Seats)
	{
		Total += Widths.Add_GetRef(static_cast<float>(FKartPainter::Measure(Seat.Name, Font).X));
	}
	float X = Centre.X - Total / 2.f;
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		const float W = Widths[i];
		F.Text(Seats[i].Name, Font, FVector2D(X + W / 2.f, Centre.Y + NamesY * S), G.ColorOf(i), FVector2D(0.5, 0.5), NameShadow * S);
		if (i + 1 < Seats.Num())
		{
			F.Text(ARCADE_LABEL("Sumo", "Versus", "vs"), KartUI::Font(EFont::Label, VersusFont * S), FVector2D(X + W + Gap / 2.f, Centre.Y + VersusY * S),
				KC::WithAlpha(KC::White, VersusAlpha), FVector2D(0.5, 0.5));
		}
		X += W + Gap;
	}
	F.Text(ARCADE_LABEL("Sumo", "Rules", "Knock everyone off the ring. The edge falls away. Two rounds wins it."), KartUI::Font(EFont::Body, RulesFont * S),
		Centre + FVector2D(0.f, RulesY * S), KC::WithAlpha(KC::White, RulesAlpha), FVector2D(0.5, 0.5), RulesShadow * S);
}

void SSumoHud::PaintStandings(const FKartPainter& P, const ASumoGame& G, float S) const
{
	using namespace Standings;
	const FMatch& M = G.GetMatch();
	const TArray<int32>& Order = Ranks.Update(M.Pucks.Num(), [&M](int32 Slot) { return StandingKey(M.Pucks[Slot]); });
	StandingText.SetNum(Order.Num());
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>((Age - At) / FadeSeconds), 0.f, 1.f));
	const FVector2D Head(P.Size().X - HeadFromRight * S - P.Size().X * KartUI::Metric::SafeFraction, P.Size().Y * HeadY);
	F.Text(ARCADE_LABEL("Sumo", "Final", "FINAL STANDINGS"), KartUI::Font(EFont::Display, HeadFont * S, HeadOutline * S), Head, KC::Sun, FVector2D(0.5, 0.5), HeadShadow * S);
	for (int32 k = 0; k < Order.Num(); ++k)
	{
		const FPuck& Puck = M.Pucks[Order[k]];
		FStandingText& Text = StandingText[k];
		const float In = KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>((Age - RowsAt - k * RowStagger) / RowInSeconds), 0.f, 1.f));
		const FVector2D Pos(Head.X - RowSize.X / 2.f * S, P.Size().Y * RowsY + k * RowPitch * S);
		const FKartPainter Row = F.Transformed(Pos + RowSize / 2.f * S, FMath::Max(MinTransformScale, In));
		const bool bWinner = Order[k] == M.MatchWinner;
		Row.Card(Pos, RowSize * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, CardAlpha), Radius * S, Shadow * S, G.ColorOf(Puck.Slot), Outline * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		Row.Text(Text.Place.Get(k + 1, [](int32 Place) { return FString::FromInt(Place); }), KartUI::Font(EFont::Display, PlaceFont * S), Pos + FVector2D(PlaceAt.X * S, PlaceAt.Y * S),
			KC::ForPlace(k + 1), FVector2D(0.5, 0.5), PlaceShadow * S);
		Row.Box(Pos + TabAt * S, TabSize * S, G.ColorOf(Puck.Slot), TabRadius * S);
		Row.Text(Puck.Name, KartUI::Font(EFont::Title, NameFont * S), Pos + FVector2D(NameAt.X * S, NameAt.Y * S), Ink, FVector2D(0.0, 0.5));
		const FString& Record = Text.Record.Get(RecordKey(Puck), [&Puck](int64)
		{
			return FString::Printf(TEXT("%d round %s  ·  %d KO  ·  %d fall%s"), Puck.RoundWins, Puck.RoundWins == 1 ? TEXT("win") : TEXT("wins"), Puck.Knockouts, Puck.Falls,
				Puck.Falls == 1 ? TEXT("") : TEXT("s"));
		});
		Row.Text(Record, KartUI::Font(EFont::Body, RecordFont * S), Pos + FVector2D(RecordAt.X * S, RecordAt.Y * S), KC::WithAlpha(Ink, RecordAlpha), FVector2D(0.0, 0.5));
		for (int32 w = 0; w < Puck.RoundWins; ++w)
		{
			Star(Row, Pos + FVector2D((StarsX + w * StarGap) * S, StarsY * S), StarRadius * S, KC::Sun);
		}
		Row.Text(Text.Score.Get(Puck.Score, [](int32 Score) { return FString::FromInt(Score); }), KartUI::Font(EFont::Display, ScoreFont * S), Pos + FVector2D(ScoreAt.X * S, ScoreAt.Y * S), Ink,
			FVector2D(1.0, 0.5));
	}
}
