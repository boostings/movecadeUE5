#include "Games/Darts/SDartsHud.h"

#include "Games/Darts/DartsGame.h"
#include "Games/Darts/DartsGameTuning.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Darts;
namespace KC = KartUI::Color;
using KartUI::EFont;

#define LOCTEXT_NAMESPACE "Darts"

// All sizes are canvas units (KartUI::Metric::CanvasH tall), scaled by S at paint time.
namespace
{
	/** The bull's flash is skipped below this (it would only cost a full-screen box). */
	constexpr float MinFlash = 0.002f;
	/** At the results the last callouts fade out over this while the standings come in. */
	constexpr double CalloutsOutSeconds = 0.4;

	namespace Scoreboard
	{
		constexpr float RowHeight = 62.f;
		constexpr float RowGap = 8.f;
		constexpr float NameWidth = 238.f;
		constexpr float SlotWidth = 36.f;
		constexpr float VisitGap = 16.f;
		constexpr float TotalWidth = 132.f;
		/** A name is shrunk (down to MinNameSize) and then shortened to fit this. */
		constexpr float NameRoom = 186.f;
		constexpr float NameSize = 26.f;
		constexpr float MinNameSize = 16.f;
		constexpr float DartRadius = 15.f;
		constexpr float EmptyDartRadius = 11.f;
		constexpr float ScoreSize = 38.f;
	}

	namespace UpPanel
	{
		constexpr float RoundSize = 60.f;
		const FVector2D ChipSize(300.0, 62.0);
		constexpr float ChipBelow = 84.f;
		/** The darts in hand, right to left under the chip. */
		constexpr float DartsBelow = 186.f;
		constexpr float DartSpacing = 36.f;
	}

	namespace Countdown
	{
		/** The 3-2-1 pops in over the first BeatPop of each second. */
		constexpr float BeatPop = 0.35f;
		constexpr float DigitSize = 170.f;
		/** "THROW!" holds for ThrowSeconds after the countdown, fading over its last ThrowFade. */
		constexpr double ThrowSeconds = 0.8;
		constexpr double ThrowFade = 0.3;
		constexpr float ThrowSize = 120.f;
	}

	namespace Callout
	{
		/** A callout rises from RiseFrom to RiseTo (x RisePixels) over its life, popping in over PopSeconds. */
		constexpr double RiseFrom = 0.26;
		constexpr double RiseTo = 0.68;
		constexpr double RisePixels = 110.0;
		constexpr double PopSeconds = 0.22;
		constexpr double FadeSeconds = 0.25;
		constexpr float LabelSize = 56.f;
		/** The bull and the bounty land bigger and tilted. */
		constexpr float BigScale = 1.3f;
		constexpr float BigTiltDegrees = -4.f;
		/** The finish banner, over the board's top. */
		constexpr double BannerBoardY = 1.62;
		constexpr double BannerPopSeconds = 0.3;
		constexpr float BannerSize = 46.f;
		constexpr float BannerHeight = 68.f;
	}

	namespace VisitCard
	{
		constexpr double LifeSeconds = 2.4;
		/** It lingers this long past the next dart's landing, fading over FadeSeconds. */
		constexpr double LingerSeconds = 0.3;
		constexpr double FadeSeconds = 0.3;
		constexpr double PopSeconds = 0.3;
		const FVector2D Size(640.0, 176.0);
		constexpr float CentreY = 0.7f;
		constexpr float CallSize = 44.f;
		constexpr float BigCallSize = 54.f;
		constexpr float RibbonHeight = 68.f;
		/** The three darts' chips pop in one after another. */
		const FVector2D ChipSize(112.0, 84.0);
		constexpr float ChipPitch = 128.f;
		constexpr double ChipDelay = 0.12;
		constexpr double ChipStagger = 0.1;
		constexpr double ChipPopSeconds = 0.25;
		/** A chip's label shrinks to fit LabelRoom at LabelSize. */
		constexpr float LabelSize = 26.f;
		constexpr float LabelRoom = 96.f;
		constexpr float PlainLabelScale = 1.35f;
		constexpr float TotalSize = 64.f;
	}

	namespace Gauge
	{
		const FVector2D Size(20.0, 170.0);
		/** Right of the reticle. */
		constexpr float OffsetX = 70.f;
		const FVector2D HintPill(176.0, 34.0);
		constexpr float HintBelow = 70.f;
		constexpr float HintPulseRate = 4.f;
	}

	namespace Controls
	{
		/** Lower right, clear of the thrower and the board. */
		constexpr float LeftOfSafeRight = 400.f;
		constexpr float CentreY = 0.87f;
		const FVector2D RemoteCard(700.0, 88.0);
		const FVector2D LocalCard(800.0, 140.0);
		const FVector2D MeterSize(520.0, 22.0);
	}

	namespace Coach
	{
		constexpr float TopY = 0.36f;
		constexpr float StepPitch = 100.f;
		const FVector2D CardSize(470.0, 88.0);
		constexpr float PulseRate = 6.f;
		constexpr float Pulse = 0.04f;
		/** The aim step is done once the reticle has moved this far off centre. */
		constexpr double AimedAway = 0.02;
	}

	namespace Intro
	{
		constexpr double FadeInSeconds = 0.4;
		constexpr double FadeOutSeconds = 0.35;
		constexpr double PopSeconds = 0.45;
		constexpr float CentreY = 0.6f;
		constexpr float TitleSize = 132.f;
	}

	namespace Standings
	{
		constexpr double FadeInSeconds = 0.5;
		constexpr float TitleY = 0.2f;
		constexpr float TopY = 0.3f;
		constexpr float RowPitch = 104.f;
		const FVector2D RowSize(800.0, 88.0);
		/** Rows pop in one after another. */
		constexpr float RowDelay = 0.3f;
		constexpr float RowStagger = 0.18f;
		constexpr double RowPopSeconds = 0.4;
	}

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

	/** A card's life: in with a pop, out with a fade (0 when not showing). */
	float CardAlpha(double Age, double Life)
	{
		if (Age < 0.0 || Age > Life)
		{
			return 0.f;
		}
		return FMath::Clamp(static_cast<float>((Life - Age) / VisitCard::FadeSeconds), 0.f, 1.f);
	}

	/** A pop-in scale Age seconds into a pop of Seconds: eased with an overshoot, never quite 0 (it scales a painter). */
	float PopScale(double Age, double Seconds, float Overshoot = 1.70158f)
	{
		return FMath::Max(0.01f, KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / Seconds), 0.f, 1.f), Overshoot));
	}

	/** The visit the board shows a player on: the one in play, capped at the last. */
	int32 LiveVisit(const FDartsPlayer& Player, const FDartsMatch& Match)
	{
		return FMath::Min((Player.DartsThrown - Match.TurnDarts) / DartsPerRound, RoundCount - 1);
	}

	/** "T20  +60", "BULLSEYE  +50", "+18" (a plain single), "MISS". */
	FString CalloutLabel(const FDartsEffect& E)
	{
		if (E.Kind == EEffectKind::Miss)
		{
			return TEXT("MISS");
		}
		if (E.ScoreDelta <= 0)
		{
			return E.Label;
		}
		const bool bPlain = E.Label == FString::FromInt(E.ScoreDelta);
		return bPlain ? FString::Printf(TEXT("+%d"), E.ScoreDelta) : FString::Printf(TEXT("%s  +%d"), *E.Label.ToUpper(), E.ScoreDelta);
	}

	FString PointsText(int32 Points)
	{
		return Points == 0 ? FString(TEXT("–")) : FString::FromInt(Points);
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
	// The big hit's flash washes the frame first, under everything else.
	if (const float Flash = G->GetFlash(); Flash > MinFlash)
	{
		P.Box(FVector2D::ZeroVector, P.Size(), KC::WithAlpha(G->GetFlashColor(), Flash));
	}
	// One projection per paint for every callout and the gauge (the camera's own view rect: Darts letterboxes nothing,
	// but a constrained camera would still line up).
	const FArcadeHudView View = FArcadeHudView::Capture(G->GetViewer());
	switch (G->GetStage())
	{
	case EDartsStage::Intro:
		PaintIntro(P, *G, S);
		break;
	case EDartsStage::Finished:
		// The last callouts give way to the standings.
		PaintCallouts(P.Faded(FMath::Clamp(1.f - static_cast<float>(G->GetFinishedAge() / CalloutsOutSeconds), 0.f, 1.f)), *G, View, S);
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
	using namespace Scoreboard;
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
		F.Size = NameSize * S;
		const float Room = NameRoom * S;
		const float Wide = static_cast<float>(FKartPainter::Measure(F.Shown, KartUI::Font(EFont::Title, F.Size)).X);
		if (Wide > Room)
		{
			F.Size = FMath::Max(MinNameSize * S, F.Size * Room / Wide);
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
	using namespace Scoreboard;
	const FDartsMatch& M = G.GetMatch();
	RowText.SetNum(M.Players.Num(), EAllowShrinking::No);
	const FVector2D Origin = P.Size() * KartUI::Metric::SafeFraction;
	for (int32 r = 0; r < M.Players.Num(); ++r)
	{
		PaintBoardRow(P, G, r, Origin + FVector2D(0.f, r * (RowHeight * S + RowGap * S)), S);
	}
}

void SDartsHud::PaintBoardRow(const FKartPainter& P, const ADartsGame& G, int32 Row, const FVector2D& Pos, float S) const
{
	using namespace Scoreboard;
	const FDartsMatch& M = G.GetMatch();
	const FDartsPlayer& Player = M.Players[Row];
	const FDartsPlayer* Up = M.ActivePlayer();
	const float RowH = RowHeight * S, NameW = NameWidth * S, Slot = SlotWidth * S, Gap = VisitGap * S;
	const float Width = NameW + Slot * DartsPerPlayer + Gap * (RoundCount - 1) + 20.f * S + TotalWidth * S;
	const FDartsSeat* Seat = G.SeatOf(Player);
	const FLinearColor Color = Seat ? Seat->Color : KC::Sky;
	const bool bUp = Up && Up->Id == Player.Id;
	P.Card(Pos, FVector2D(Width, RowH), bUp ? KC::Paper : KC::WithAlpha(KC::Night, 0.84f), 16.f * S, 5.f * S,
		bUp ? Color : FLinearColor::Transparent, bUp ? 3.f * S : 0.f);
	// The colour tab down the left edge.
	P.Box(Pos + FVector2D(0.f, 10.f * S), FVector2D(6.f * S, RowH - 20.f * S), Color, 3.f * S);
	const FLinearColor Ink = bUp ? KC::Night : KC::White;
	float FittedSize = 0.f;
	const FString& Name = FittedName(Row, Player.Name, S, FittedSize);
	P.Text(Name, KartUI::Font(EFont::Title, FittedSize), Pos + FVector2D(26.f * S, RowH / 2.f), Ink, FVector2D(0.0, 0.5));
	// The visit in play gets a soft band behind its three slots.
	const int32 Live = bUp ? LiveVisit(Player, M) : -1;
	if (Live >= 0 && M.Phase == EMatchPhase::Playing)
	{
		const FVector2D Band = Pos + FVector2D(NameW + Live * (Slot * DartsPerRound + Gap) - 4.f * S, 7.f * S);
		P.Box(Band, FVector2D(Slot * DartsPerRound + 8.f * S, RowH - 14.f * S), KC::WithAlpha(Color, 0.22f), 12.f * S);
	}
	// Nine darts, three visits: filled by points, ringed by the bounty, empty while to come.
	FRowText& Text = RowText[Row];
	static_assert(UE_ARRAY_COUNT(Text.Darts) == DartsPerPlayer, "one cached label per dart");
	for (int32 d = 0; d < DartsPerPlayer; ++d)
	{
		const int32 Visit = d / DartsPerRound;
		const FVector2D C = Pos + FVector2D(NameW + d * Slot + Visit * Gap + Slot / 2.f, RowH / 2.f);
		if (!Player.Landings.IsValidIndex(d))
		{
			P.Circle(C, EmptyDartRadius * S, FLinearColor::Transparent, KC::WithAlpha(bUp ? KC::Night : KC::White, 0.28f), 2.f * S);
			continue;
		}
		const FDartLanding& L = Player.Landings[d];
		const FLinearColor Fill = L.ScoreDelta == 0 ? KC::WithAlpha(KC::NavyDim, 0.75f) : (L.Hit.Ring == ERing::Bull ? KC::Sun : Color);
		P.Circle(C, DartRadius * S, Fill, L.bBounty ? KC::Sun : KC::WithAlpha(KC::Night, 0.6f), (L.bBounty ? 3.f : 1.5f) * S);
		P.Text(Text.Darts[d].Get(L.ScoreDelta, PointsText), KartUI::Font(EFont::Label, (L.ScoreDelta >= 100 ? 11.f : 14.f) * S), C, KC::TextOn(Fill),
			FVector2D(0.5, 0.5));
	}
	P.Text(Text.Score.Get(Player.Score, [](int32 V) { return FString::FromInt(V); }), KartUI::Font(EFont::Display, ScoreSize * S),
		Pos + FVector2D(Width - 20.f * S, RowH / 2.f), Ink, FVector2D(1.0, 0.5));
}

void SDartsHud::PaintUp(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// Who's up, the visit, the called shot, the darts in hand.
	using namespace UpPanel;
	const FDartsMatch& M = G.GetMatch();
	const FDartsPlayer* Up = M.ActivePlayer();
	if (!Up || M.Phase == EMatchPhase::Countdown)
	{
		return;
	}
	const FDartsSeat* Seat = G.SeatOf(*Up);
	const int32 Visit = LiveVisit(*Up, M);
	const FVector2D TopRight(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction);
	const FString& Round = RoundText.Get(Visit, [](int32 V) { return FText::Format(LOCTEXT("RoundOf", "ROUND {0} / {1}"), V + 1, RoundCount).ToString(); });
	P.Text(Round, KartUI::Font(EFont::Display, RoundSize * S, 4.f * S), TopRight, KC::White, FVector2D(1.0, 0.0), 5.f * S);
	// The bounty chip: the called sector's number in the wedge's gold, "x2".
	const FVector2D Chip(TopRight.X - ChipSize.X * S, TopRight.Y + ChipBelow * S);
	const FVector2D Size = ChipSize * S;
	P.Card(Chip, Size, KC::WithAlpha(KC::Night, 0.88f), 31.f * S, 4.f * S, KC::Sun, 3.f * S);
	P.Text(LOCTEXT("Bounty", "BOUNTY"), KartUI::Font(EFont::Label, 18.f * S), Chip + FVector2D(26.f * S, Size.Y / 2.f), KC::Sun, FVector2D(0.0, 0.5));
	P.Text(BountyText.Get(BountyForRound(Visit), [](int32 V) { return FString::FromInt(V); }), KartUI::Font(EFont::Display, 40.f * S),
		Chip + FVector2D(Size.X - 82.f * S, Size.Y / 2.f), KC::Sun, FVector2D(1.0, 0.5));
	static const FString Times2(TEXT("×2"));
	P.Text(Times2, KartUI::Font(EFont::Display, 28.f * S), Chip + FVector2D(Size.X - 26.f * S, Size.Y / 2.f), KC::White, FVector2D(1.0, 0.5));
	// Darts in hand, as three little darts.
	const int32 Left = FMath::Max(0, DartsPerRound - M.TurnDarts);
	for (int32 i = 0; i < DartsPerRound; ++i)
	{
		const FVector2D C(TopRight.X - 20.f * S - i * DartSpacing * S, TopRight.Y + DartsBelow * S);
		const FLinearColor Fill = i < Left ? (Seat ? Seat->Color : KC::Sky) : KC::WithAlpha(KC::White, 0.2f);
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
	using namespace Countdown;
	const FDartsMatch& M = G.GetMatch();
	// High in the frame, clear of the board.
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * 0.2f);
	if (M.Phase == EMatchPhase::Countdown)
	{
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 N = FMath::CeilToInt32(Remaining);
		const float Beat = static_cast<float>(1.0 - (N - Remaining));
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(Beat / BeatPop, 0.f, 1.f));
		const FString& Digit = CountdownText.Get(FMath::Max(1, N), [](int32 V) { return FString::FromInt(V); });
		P.Transformed(Centre, FMath::Max(0.01f, Pop)).Text(Digit, KartUI::Font(EFont::Display, DigitSize * S, 8.f * S, KC::Night), Centre,
			KC::Sun, FVector2D(0.5, 0.5), 10.f * S);
		return;
	}
	const double Since = M.ElapsedMs / 1000.0;
	if (M.Phase == EMatchPhase::Playing && Since < ThrowSeconds)
	{
		const float A = FMath::Clamp(static_cast<float>((ThrowSeconds - Since) / ThrowFade), 0.f, 1.f);
		// Wide, so it sits lower, over the board, clear of the scoreboard and the round header.
		const FVector2D ThrowAt(P.Size().X / 2.f, P.Size().Y * 0.42f);
		P.Faded(A).Text(LOCTEXT("Throw", "THROW!"), KartUI::Font(EFont::Display, ThrowSize * S, 7.f * S, KC::Night), ThrowAt, KC::Mint, FVector2D(0.5, 0.5), 9.f * S);
	}
}

void SDartsHud::PaintCallouts(const FKartPainter& P, const ADartsGame& G, const FArcadeHudView& View, float S) const
{
	// The landing callouts rise off the board; the game's own banner closes the match (the visit card replaces
	// the engine's round banner).
	using namespace Callout;
	const TArray<FDartsEffect>& Effects = G.GetMatch().Effects;
	Callouts.SetNum(Effects.Num(), EAllowShrinking::No);
	for (int32 i = 0; i < Effects.Num(); ++i)
	{
		const FDartsEffect& E = Effects[i];
		if (E.Kind == EEffectKind::Round)
		{
			continue;
		}
		if (E.Kind == EEffectKind::Finished)
		{
			PaintFinishBanner(P, G, View, i, E, S);
			continue;
		}
		FVector2D At01;
		if (!View.ProjectToViewRect(G.BoardWorld(E.X, E.Y), At01))
		{
			continue;
		}
		FCalloutText& Text = Callouts[i];
		if (Text.EffectId != E.Id)
		{
			Text.EffectId = E.Id;
			Text.Label = CalloutLabel(E);
		}
		const FDartsSeat* Seat = G.SeatOf(E.PlayerId);
		const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
		const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
		const double Life = E.DurationMs / 1000.0;
		const float Out = FMath::Clamp(static_cast<float>((Life - Age) / FadeSeconds), 0.f, 1.f);
		const double Rise = FMath::Lerp(RiseFrom, RiseTo, FMath::Clamp(Age / Life, 0.0, 1.0));
		const FVector2D At = At01 * P.Size() - FVector2D(0.f, static_cast<float>(Rise * RisePixels) * S);
		const bool bBig = E.Kind == EEffectKind::Bull || E.Kind == EEffectKind::Bounty;
		const FKartPainter C = P.Transformed(At, PopScale(Age, PopSeconds, 2.4f) * (bBig ? BigScale : 1.f), bBig ? BigTiltDegrees : 0.f).Faded(Out);
		C.Text(Text.Label, KartUI::Font(EFont::Display, LabelSize * S, 5.f * S, KC::Night), At, EffectColor(E, Lane), FVector2D(0.5, 0.5), 6.f * S);
	}
}

void SDartsHud::PaintFinishBanner(const FKartPainter& P, const ADartsGame& G, const FArcadeHudView& View, int32 Slot, const FDartsEffect& E, float S) const
{
	using namespace Callout;
	FVector2D At01;
	if (!View.ProjectToViewRect(G.BoardWorld(0.0, BannerBoardY), At01))
	{
		return;
	}
	const FDartsSeat* Seat = G.SeatOf(E.PlayerId);
	const FSlateFontInfo Font = KartUI::Font(EFont::Display, BannerSize * S, 3.f * S, KC::Night);
	FCalloutText& Text = Callouts[Slot];
	if (Text.EffectId != E.Id || Text.Scale != S)
	{
		Text.EffectId = E.Id;
		Text.Scale = S;
		Text.Label = FString::Printf(TEXT("%s  ·  %s"), Seat ? *Seat->Name : TEXT(""), *E.Label);
		Text.Width = static_cast<float>(FKartPainter::Measure(Text.Label, Font).X) + 60.f * S;
	}
	const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
	const double Life = E.DurationMs / 1000.0;
	const float Out = FMath::Clamp(static_cast<float>((Life - Age) / FadeSeconds), 0.f, 1.f);
	const FVector2D At = At01 * P.Size();
	const FKartPainter B = P.Transformed(At, PopScale(Age, BannerPopSeconds)).Faded(Out);
	const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
	B.Card(At - FVector2D(Text.Width / 2.f, BannerHeight / 2.f * S), FVector2D(Text.Width, BannerHeight * S), Lane, 34.f * S, 6.f * S, KC::White, 3.f * S);
	B.Text(Text.Label, Font, At, KC::White, FVector2D(0.5, 0.5), 4.f * S);
}

void SDartsHud::RefreshVisitText(const FDartsVisitSummary& V, float S) const
{
	using namespace VisitCard;
	if (VisitText.At == V.At && VisitText.Scale == S)
	{
		return;
	}
	VisitText.At = V.At;
	VisitText.Scale = S;
	const bool bBig = V.Total >= 100 || V.Bounties >= DartsPerRound;
	VisitText.Call = V.Call.ToString();
	VisitText.CallWidth = static_cast<float>(FKartPainter::Measure(VisitText.Call, KartUI::Font(EFont::Display, (bBig ? BigCallSize : CallSize) * S, 4.f * S, KC::Night)).X)
		+ 64.f * S;
	VisitText.Total = FString::FromInt(V.Total + V.Bonus);
	VisitText.Footer = V.Bonus > 0 ? FText::Format(LOCTEXT("VisitBonus", "incl. +{0} bonus"), V.Bonus) : LOCTEXT("VisitPoints", "points");
	VisitText.Points.Reset();
	VisitText.LabelSizes.Reset();
	for (int32 i = 0; i < V.Labels.Num(); ++i)
	{
		VisitText.Points.Add(V.Points[i] == 0 ? FString(TEXT("0")) : FString::Printf(TEXT("+%d"), V.Points[i]));
		const float Wide = static_cast<float>(FKartPainter::Measure(V.Labels[i], KartUI::Font(EFont::Title, LabelSize)).X);
		VisitText.LabelSizes.Add(FMath::Min(LabelSize, LabelSize * LabelRoom / FMath::Max(1.f, Wide)) * S);
	}
}

void SDartsHud::PaintVisit(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// The visit card: the call, the three darts, the visit's points and the round bonus, low and central.
	using namespace VisitCard;
	const FDartsVisitSummary& V = G.GetLastVisit();
	const double Age = G.GetClock() - V.At;
	const float A = CardAlpha(Age, FMath::Min(LifeSeconds, V.EndAt - V.At + LingerSeconds));
	if (A <= 0.f || V.PlayerId.IsEmpty())
	{
		return;
	}
	RefreshVisitText(V, S);
	const FDartsSeat* Seat = G.SeatOf(V.PlayerId);
	const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
	const bool bBig = V.Total >= 100 || V.Bounties >= DartsPerRound;
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	const FKartPainter C = P.Transformed(Centre, PopScale(Age, PopSeconds)).Faded(A);
	const FVector2D CardSize = Size * S;
	const FVector2D Pos = Centre - CardSize / 2.f;
	C.Card(Pos, CardSize, KC::WithAlpha(KC::Night, 0.9f), 28.f * S, 8.f * S, bBig ? KC::Sun : Lane, 4.f * S);
	// The call, on a ribbon that overhangs the card's top.
	const FSlateFontInfo CallFont = KartUI::Font(EFont::Display, (bBig ? BigCallSize : CallSize) * S, 4.f * S, KC::Night);
	C.Card(FVector2D(Centre.X - VisitText.CallWidth / 2.f, Pos.Y - RibbonHeight / 2.f * S), FVector2D(VisitText.CallWidth, RibbonHeight * S), bBig ? KC::Sun : Lane,
		34.f * S, 6.f * S);
	C.Text(VisitText.Call, CallFont, FVector2D(Centre.X, Pos.Y), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	PaintVisitChips(C, V, Pos, Lane, Age, S);
	// The total, and the round's bonus under it.
	const FVector2D TotalAt(Pos.X + CardSize.X - 34.f * S, Pos.Y + 92.f * S);
	C.Text(VisitText.Total, KartUI::Font(EFont::Display, TotalSize * S, 3.f * S), TotalAt, bBig ? KC::Sun : KC::White, FVector2D(1.0, 0.5), 4.f * S);
	C.Text(VisitText.Footer, KartUI::Font(EFont::Label, 17.f * S), TotalAt + FVector2D(0.f, 46.f * S), KC::WithAlpha(KC::White, 0.75f), FVector2D(1.0, 0.5));
}

void SDartsHud::PaintVisitChips(const FKartPainter& Card, const FDartsVisitSummary& V, const FVector2D& Pos, const FLinearColor& Lane, double Age, float S) const
{
	// The three darts as chips, landing one after another.
	using namespace VisitCard;
	for (int32 i = 0; i < V.Labels.Num(); ++i)
	{
		const FVector2D Chip(Pos.X + 40.f * S + i * ChipPitch * S, Pos.Y + 58.f * S);
		const FKartPainter D = Card.Transformed(Chip + FVector2D(56.f, 42.f) * S, PopScale(Age - ChipDelay - i * ChipStagger, ChipPopSeconds));
		const bool bMiss = V.Points[i] == 0;
		const FLinearColor Fill = bMiss ? KC::NavyDim : Lane;
		// A plain single reads once ("18"); a treble, double, bull or bounty shows what it paid ("T20" over "+60").
		const bool bPlain = !bMiss && V.Labels[i] == FString::FromInt(V.Points[i]);
		D.Card(Chip, ChipSize * S, bMiss ? KC::WithAlpha(KC::NavyDim, 0.9f) : Lane, 16.f * S, 4.f * S);
		const float Label = VisitText.LabelSizes[i];
		D.Text(V.Labels[i], KartUI::Font(EFont::Title, bPlain ? Label * PlainLabelScale : Label), Chip + FVector2D(56.f * S, (bPlain ? 42.f : 28.f) * S),
			KC::TextOn(Fill), FVector2D(0.5, 0.5));
		if (!bPlain)
		{
			D.Text(VisitText.Points[i], KartUI::Font(EFont::Label, 20.f * S), Chip + FVector2D(56.f * S, 60.f * S), KC::WithAlpha(KC::TextOn(Fill), 0.85f),
				FVector2D(0.5, 0.5));
		}
	}
}

void SDartsHud::PaintGauge(const FKartPainter& P, const ADartsGame& G, const FArcadeHudView& View, float S) const
{
	// The aim read: by the reticle, a vertical power gauge while the dart is gripped (the sweet band in mint), and a
	// quiet "hold" hint while it isn't.
	using namespace Gauge;
	const FDartsSeat* Thrower = G.GetThrower();
	FVector2D Aim;
	if (!Thrower || Thrower->Kind != EThrowerKind::Local || !G.GetReticle(Aim) || G.IsReacting())
	{
		return;
	}
	FVector2D At01;
	if (!View.ProjectToViewRect(G.BoardWorld(Aim.X * AimReach, Aim.Y * AimReach), At01))
	{
		return;
	}
	const FVector2D At = At01 * P.Size();
	const FLocalThrow& L = G.GetLocal();
	if (!L.bGripped)
	{
		if (G.GetCoachAlpha() <= 0.f)
		{
			const float Pulse = 0.7f + 0.3f * FMath::Sin(static_cast<float>(G.GetClock()) * HintPulseRate);
			const FKartPainter H = P.Faded(Pulse);
			const FVector2D PillSize = HintPill * S;
			const FVector2D PillAt = At + FVector2D(-PillSize.X / 2.f, HintBelow * S);
			H.Card(PillAt, PillSize, KC::WithAlpha(KC::Night, 0.82f), 17.f * S, 3.f * S, Thrower->Color, 2.f * S);
			H.Text(LOCTEXT("GaugeHold", "HOLD TO THROW"), KartUI::Font(EFont::Label, 15.f * S), PillAt + PillSize / 2.f, KC::White, FVector2D(0.5, 0.5));
		}
		return;
	}
	const FVector2D GaugeSize = Size * S;
	const FVector2D Pos = At + FVector2D(OffsetX * S, -GaugeSize.Y / 2.f);
	P.Box(Pos - FVector2D(4.f, 4.f) * S, GaugeSize + FVector2D(8.f, 8.f) * S, KC::WithAlpha(KC::Night, 0.8f), 12.f * S);
	auto YOf = [&](double Power) { return Pos.Y + GaugeSize.Y * static_cast<float>(1.0 - Power); };
	P.Box(FVector2D(Pos.X, YOf(SweetSpot + Tuning::SweetBand)), FVector2D(GaugeSize.X, GaugeSize.Y * 0.1f), KC::WithAlpha(KC::Mint, 0.45f), 4.f * S);
	const float Power = static_cast<float>(FMath::Clamp(L.Power, 0.0, 1.0));
	const bool bSweet = Tuning::IsSweet(Power);
	const FLinearColor Fill = bSweet ? KC::Mint : (Power > SweetSpot ? KC::Hot : KC::Sun);
	P.Box(FVector2D(Pos.X, YOf(Power)), FVector2D(GaugeSize.X, GaugeSize.Y * Power), Fill, 8.f * S);
	// The marker, and what letting go now does.
	P.Box(FVector2D(Pos.X - 8.f * S, YOf(Power) - 2.f * S), FVector2D(GaugeSize.X + 16.f * S, 4.f * S), KC::White, 2.f * S);
	const FText Read = bSweet ? LOCTEXT("GaugeNow", "NOW!") : (Power > SweetSpot ? LOCTEXT("GaugeHigh", "HIGH") : LOCTEXT("GaugeLow", "LOW"));
	P.Text(Read, KartUI::Font(EFont::Display, (bSweet ? 30.f : 20.f) * S, 3.f * S), FVector2D(Pos.X + GaugeSize.X + 14.f * S, YOf(Power)), Fill, FVector2D(0.0, 0.5), 3.f * S);
}

void SDartsHud::PaintControls(const FKartPainter& P, const ADartsGame& G, float S) const
{
	using namespace Controls;
	const FDartsSeat* Thrower = G.GetThrower();
	const FDartsMatch& M = G.GetMatch();
	if (!Thrower || M.Phase != EMatchPhase::Playing)
	{
		return;
	}
	const FVector2D Centre(P.Size().X * (1.f - KartUI::Metric::SafeFraction) - LeftOfSafeRight * S, P.Size().Y * CentreY);
	const int32 SeatIndex = static_cast<int32>(Thrower - G.GetSeats().GetData());
	const FString& Up = UpText.Get(SeatIndex, [Thrower](int32) { return FText::Format(LOCTEXT("Up", "{0} is up"), FText::FromString(Thrower->Name)).ToString(); });
	if (Thrower->Kind != EThrowerKind::Local)
	{
		const FText Hint = Thrower->Kind == EThrowerKind::Phone ? LOCTEXT("PhoneHint", "Aim with your phone, pinch, flick and let go")
			: LOCTEXT("BotHint", "Lining up...");
		P.Card(Centre - RemoteCard / 2.0 * S, RemoteCard * S, KC::WithAlpha(KC::Night, 0.86f), 22.f * S, 6.f * S, Thrower->Color, 3.f * S);
		P.Text(Up, KartUI::Font(EFont::Title, 32.f * S), Centre - FVector2D(0.f, 13.f * S), KC::White, FVector2D(0.5, 0.5));
		P.Text(Hint, KartUI::Font(EFont::Body, 21.f * S), Centre + FVector2D(0.f, 22.f * S), KC::WithAlpha(KC::White, 0.82f), FVector2D(0.5, 0.5));
		return;
	}
	const FLocalThrow& L = G.GetLocal();
	P.Card(Centre - LocalCard / 2.0 * S, LocalCard * S, KC::WithAlpha(KC::Night, 0.86f), 24.f * S, 6.f * S, Thrower->Color, 3.f * S);
	P.Text(Up, KartUI::Font(EFont::Title, 30.f * S), Centre + FVector2D(0.f, -40.f * S), KC::White, FVector2D(0.5, 0.5));
	const FText Hint = L.bGripped ? LOCTEXT("Release", "Let go in the green  ·  or flick the mouse up")
		: LOCTEXT("Grip", "Aim: WASD, stick or mouse  ·  Throw: hold SPACE, RT or click");
	P.Text(Hint, KartUI::Font(EFont::Body, 18.f * S), Centre + FVector2D(0.f, -6.f * S), KC::WithAlpha(KC::White, 0.88f), FVector2D(0.5, 0.5));
	// The meter, with the sweet spot (a throw that lands on the aim) marked; under it drops, over it sails.
	const FVector2D BarSize = MeterSize * S;
	const FVector2D BarPos = Centre + FVector2D(-BarSize.X / 2.f, 26.f * S);
	P.Box(BarPos, BarSize, KC::WithAlpha(KC::White, 0.12f), 11.f * S);
	P.Box(BarPos + FVector2D(BarSize.X * static_cast<float>(SweetSpot - Tuning::SweetBand), 0.f), FVector2D(BarSize.X * 0.1f, BarSize.Y), KC::WithAlpha(KC::Mint, 0.45f), 11.f * S);
	const float Power = static_cast<float>(L.bGripped ? L.Power : 0.0);
	if (Power > 0.f)
	{
		const FLinearColor To = Tuning::IsSweet(Power) ? KC::Mint : (Power > SweetSpot ? KC::Hot : KC::Sun);
		P.Gradient(BarPos, FVector2D(FMath::Max(BarSize.Y, BarSize.X * Power), BarSize.Y), KC::Sky, To, false, 11.f * S);
	}
	P.Text(LOCTEXT("Low", "LOW"), KartUI::Font(EFont::Label, 16.f * S), BarPos - FVector2D(14.f * S, -BarSize.Y / 2.f), KC::Sun, FVector2D(1.0, 0.5));
	P.Text(LOCTEXT("High", "HIGH"), KartUI::Font(EFont::Label, 16.f * S), BarPos + FVector2D(BarSize.X + 14.f * S, BarSize.Y / 2.f), KC::Hot, FVector2D(0.0, 0.5));
}

void SDartsHud::PaintCoach(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// First local visit: three numbered steps down the left, the current one lit; they fade once the dart flies.
	using namespace Coach;
	const float A = G.GetCoachAlpha();
	const FDartsSeat* Thrower = G.GetThrower();
	if (A <= 0.f || !Thrower || Thrower->Kind != EThrowerKind::Local || G.GetMatch().Phase == EMatchPhase::Ended)
	{
		return;
	}
	struct FStep
	{
		FText Title;
		FText Keys;
		FString Number;
	};
	static const FStep Steps[] = {
		{ LOCTEXT("CoachAim", "AIM"), LOCTEXT("CoachAimKeys", "WASD  ·  left stick  ·  mouse"), TEXT("1") },
		{ LOCTEXT("CoachHold", "HOLD"), LOCTEXT("CoachHoldKeys", "SPACE  ·  RT  ·  left click"), TEXT("2") },
		{ LOCTEXT("CoachLetGo", "LET GO"), LOCTEXT("CoachLetGoKeys", "when the meter is in the green"), TEXT("3") },
	};
	const FLocalThrow& L = G.GetLocal();
	const FKartPainter F = P.Faded(A);
	const int32 Current = L.bGripped ? 2 : (L.Aim.IsNearlyZero(AimedAway) ? 0 : 1);
	const FVector2D Origin(P.Size().X * KartUI::Metric::SafeFraction, P.Size().Y * TopY);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Steps); ++i)
	{
		const bool bOn = i == Current;
		const float Pop = bOn ? 1.f + Pulse * FMath::Sin(static_cast<float>(G.GetClock()) * PulseRate) : 1.f;
		const FVector2D Pos = Origin + FVector2D(0.f, i * StepPitch * S);
		const FKartPainter R = F.Transformed(Pos + FVector2D(0.f, CardSize.Y / 2.0 * S), Pop);
		R.Card(Pos, CardSize * S, bOn ? KC::Paper : KC::WithAlpha(KC::Night, 0.82f), 22.f * S, 6.f * S, bOn ? KC::Mint : FLinearColor::Transparent, bOn ? 3.f * S : 0.f);
		const FLinearColor Ink = bOn ? KC::Night : KC::White;
		R.Circle(Pos + FVector2D(46.f * S, 44.f * S), 26.f * S, bOn ? KC::Mint : KC::WithAlpha(KC::White, 0.15f));
		R.Text(Steps[i].Number, KartUI::Font(EFont::Display, 32.f * S), Pos + FVector2D(46.f * S, 44.f * S), bOn ? KC::Night : KC::White, FVector2D(0.5, 0.5));
		R.Text(Steps[i].Title, KartUI::Font(EFont::Display, 30.f * S), Pos + FVector2D(90.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		R.Text(Steps[i].Keys, KartUI::Font(EFont::Body, 19.f * S), Pos + FVector2D(90.f * S, 62.f * S), KC::WithAlpha(Ink, 0.8f), FVector2D(0.0, 0.5));
	}
}

void SDartsHud::PaintIntro(const FKartPainter& P, const ADartsGame& G, float S) const
{
	using namespace Intro;
	const double Age = G.GetStageAge();
	const double Life = ADartsGame::IntroSeconds;
	const float A = FMath::Clamp(static_cast<float>(Age / FadeInSeconds), 0.f, 1.f) * FMath::Clamp(static_cast<float>((Life - Age) / FadeOutSeconds), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	const FVector2D TitleAt = Centre - FVector2D(0.f, 60.f * S);
	F.Transformed(TitleAt, PopScale(Age, PopSeconds)).Text(LOCTEXT("Title", "BULLSEYE BASH"), KartUI::Font(EFont::Display, TitleSize * S, 7.f * S),
		TitleAt, KC::Hot, FVector2D(0.5, 0.5), 9.f * S);
	// The seats don't change for the game's life: the line-up is joined once.
	const FString& Names = IntroNames.Get(G.GetSeats().Num(), [&G](int32)
	{
		FString Joined;
		for (const FDartsSeat& Seat : G.GetSeats())
		{
			Joined += (Joined.IsEmpty() ? TEXT("") : TEXT("   ·   ")) + Seat.Name;
		}
		return Joined;
	});
	F.Text(Names, KartUI::Font(EFont::Title, 34.f * S, 2.f * S), Centre + FVector2D(0.f, 50.f * S), KC::White, FVector2D(0.5, 0.5), 4.f * S);
	F.Text(LOCTEXT("Rules", "Three darts a visit, three visits. Hit the called sector for double points."), KartUI::Font(EFont::Body, 26.f * S, 2.f * S),
		Centre + FVector2D(0.f, 102.f * S), KC::WithAlpha(KC::White, 0.92f), FVector2D(0.5, 0.5), 3.f * S);
}

void SDartsHud::PaintStandings(const FKartPainter& P, const ADartsGame& G, float S) const
{
	using namespace Standings;
	const FDartsMatch& M = G.GetMatch();
	// Best first, ties in seat order: the rules' Leaderboard order.
	const TArray<int32>& Order = Ranks.Update(M.Players.Num(), [&M](int32 i) { return static_cast<int64>(M.Players[i].Score); });
	StandingText.SetNum(Order.Num(), EAllowShrinking::No);
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / FadeInSeconds), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * TitleY);
	F.Text(LOCTEXT("Final", "FINAL SCORES"), KartUI::Font(EFont::Display, 96.f * S, 5.f * S), Centre, KC::Sun, FVector2D(0.5, 0.5), 7.f * S);
	const auto Number = [](int32 V) { return FString::FromInt(V); };
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FDartsPlayer& Player = M.Players[Order[i]];
		const FDartsSeat* Seat = G.SeatOf(Player);
		const float In = PopScale(Age - (RowDelay + i * RowStagger), RowPopSeconds);
		const FVector2D Pos(P.Size().X / 2.f - RowSize.X / 2.0 * S, P.Size().Y * TopY + i * RowPitch * S);
		const FKartPainter Row = F.Transformed(Pos + RowSize / 2.0 * S, In);
		const bool bWinner = M.WinnerIds.Contains(Player.Id);
		Row.Card(Pos, RowSize * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, 0.88f), 22.f * S, 6.f * S, Seat ? Seat->Color : KC::Sky, 3.f * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		FStandingText& Text = StandingText[i];
		Row.Text(Text.Place.Get(i + 1, Number), KartUI::Font(EFont::Display, 52.f * S), Pos + FVector2D(48.f * S, 44.f * S), KC::ForPlace(i + 1), FVector2D(0.5, 0.5), 3.f * S);
		Row.Text(Player.Name, KartUI::Font(EFont::Title, 36.f * S), Pos + FVector2D(100.f * S, 30.f * S), Ink, FVector2D(0.0, 0.5));
		const int64 StatsKey = (static_cast<int64>(Player.BestRound) << 32) | (static_cast<int64>(Player.Bulls & 0xffff) << 16) | (Player.Bounties & 0xffff);
		const FString& Stats = Text.Stats.Get(StatsKey, [&Player](int64)
		{
			return FText::Format(LOCTEXT("StandingStats", "best round {0}  ·  {1} bulls  ·  {2} bounties"), Player.BestRound, Player.Bulls, Player.Bounties).ToString();
		});
		Row.Text(Stats, KartUI::Font(EFont::Body, 19.f * S), Pos + FVector2D(100.f * S, 64.f * S), KC::WithAlpha(Ink, 0.78f), FVector2D(0.0, 0.5));
		Row.Text(Text.Score.Get(Player.Score, Number), KartUI::Font(EFont::Display, 56.f * S), Pos + FVector2D(770.f * S, 44.f * S), Ink, FVector2D(1.0, 0.5));
	}
}

#undef LOCTEXT_NAMESPACE
