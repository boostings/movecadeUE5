#include "Games/Darts/SDartsHud.h"

#include "Games/Darts/DartsGame.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/SDartsHudLayout.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUIStyle.h"

using namespace MovecadeKart::Darts;
namespace KC = KartUI::Color;
using KartUI::EFont;
using namespace MovecadeKart::Darts::HudLayout;

namespace
{
	const FVector2D Centred(0.5, 0.5);
	const FVector2D LeftMiddle(0.0, 0.5);
	const FVector2D RightMiddle(1.0, 0.5);

	FLinearColor EffectColor(const FDartsEffect& E, const FLinearColor& Lane)
	{
		switch (E.Kind)
		{
		case EEffectKind::Bull: return KC::Sun;
		case EEffectKind::Bounty: return Lane;
		case EEffectKind::Miss: return KC::WithAlpha(KC::White, Callout::MissAlpha);
		default: return KC::Mint;
		}
	}

	/** A card's life: in with a pop, out with a fade (0 when not showing). */
	float CardFade(double Age, double Life)
	{
		if (Age < 0.0 || Age > Life)
		{
			return 0.f;
		}
		return FMath::Clamp(static_cast<float>((Life - Age) / VisitCard::FadeSeconds), 0.f, 1.f);
	}

	/** A pop-in scale Age seconds into a pop of Seconds: eased with an overshoot, never quite 0 (it scales a painter). */
	float PopScale(double Age, double Seconds, float Overshoot = DefaultOvershoot)
	{
		return FMath::Max(MinScale, KartUI::Ease::OutBack(FMath::Clamp(static_cast<float>(Age / Seconds), 0.f, 1.f), Overshoot));
	}

	/** The visit the board shows a player on: the one in play, capped at the last. */
	int32 LiveVisit(const FDartsPlayer& Player, const FDartsMatch& Match)
	{
		return FMath::Min((Player.DartsThrown - Match.TurnDarts) / DartsPerRound, RoundCount - 1);
	}

	bool IsBigVisit(const FDartsVisitSummary& V)
	{
		return V.Total >= BigVisitTotal || V.Bounties >= DartsPerRound;
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

	FString NumberText(int32 Value)
	{
		return FString::FromInt(Value);
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
	const float Width = NameW + Slot * DartsPerPlayer + Gap * (RoundCount - 1) + TotalGap * S + TotalWidth * S;
	const FDartsSeat* Seat = G.SeatOf(Player);
	const FLinearColor Color = Seat ? Seat->Color : KC::Sky;
	const bool bUp = Up && Up->Id == Player.Id;
	P.Card(Pos, FVector2D(Width, RowH), bUp ? KC::Paper : KC::WithAlpha(KC::Night, IdleAlpha), Scoreboard::Row.Radius * S, Scoreboard::Row.Shadow * S,
		bUp ? Color : FLinearColor::Transparent, bUp ? Scoreboard::Row.Outline * S : 0.f);
	P.Box(Pos + FVector2D(0.f, TabInset * S), FVector2D(TabWidth * S, RowH - 2.f * TabInset * S), Color, TabRadius * S);
	const FLinearColor Ink = bUp ? KC::Night : KC::White;
	float FittedSize = 0.f;
	const FString& Name = FittedName(Row, Player.Name, S, FittedSize);
	P.Text(Name, KartUI::Font(EFont::Title, FittedSize), Pos + FVector2D(NameX * S, RowH / 2.f), Ink, LeftMiddle);
	const int32 Live = bUp ? LiveVisit(Player, M) : -1;
	if (Live >= 0 && M.Phase == EMatchPhase::Playing)
	{
		const FVector2D Band = Pos + FVector2D(NameW + Live * (Slot * DartsPerRound + Gap) - BandPadX * S, BandInsetY * S);
		P.Box(Band, FVector2D(Slot * DartsPerRound + 2.f * BandPadX * S, RowH - 2.f * BandInsetY * S), KC::WithAlpha(Color, BandAlpha), BandRadius * S);
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
			P.Circle(C, EmptyDartRadius * S, FLinearColor::Transparent, KC::WithAlpha(bUp ? KC::Night : KC::White, EmptyDartAlpha), EmptyDartOutline * S);
			continue;
		}
		const FDartLanding& L = Player.Landings[d];
		const FLinearColor Fill = L.ScoreDelta == 0 ? KC::WithAlpha(KC::NavyDim, MissAlpha) : (L.Hit.Ring == ERing::Bull ? KC::Sun : Color);
		P.Circle(C, DartRadius * S, Fill, L.bBounty ? KC::Sun : KC::WithAlpha(KC::Night, DartOutlineAlpha), (L.bBounty ? BountyOutline : DartOutline) * S);
		P.Text(Text.Darts[d].Get(L.ScoreDelta, PointsText), KartUI::Font(EFont::Label, (L.ScoreDelta >= BigPoints ? BigPointsSize : PointsSize) * S), C,
			KC::TextOn(Fill), Centred);
	}
	P.Text(Text.Score.Get(Player.Score, NumberText), KartUI::Font(EFont::Display, ScoreSize * S), Pos + FVector2D(Width - TotalGap * S, RowH / 2.f), Ink, RightMiddle);
}

void SDartsHud::PaintUp(const FKartPainter& P, const ADartsGame& G, float S) const
{
	// Who's up, the visit, the called shot, the darts in hand.
	using namespace UpPanel;
	const FText& BountyLabel = ARCADE_LABEL("Darts", "Bounty", "BOUNTY");
	static const FString Times2(TEXT("×2"));
	const FDartsMatch& M = G.GetMatch();
	const FDartsPlayer* Up = M.ActivePlayer();
	if (!Up || M.Phase == EMatchPhase::Countdown)
	{
		return;
	}
	const FDartsSeat* Seat = G.SeatOf(*Up);
	const int32 Visit = LiveVisit(*Up, M);
	const FVector2D TopRight(P.Size().X * (1.f - KartUI::Metric::SafeFraction), P.Size().Y * KartUI::Metric::SafeFraction);
	const FString& Round = RoundText.Get(Visit, [](int32 V) { return FText::Format(ARCADE_LABEL("Darts", "RoundOf", "ROUND {0} / {1}"), V + 1, RoundCount).ToString(); });
	P.Text(Round, KartUI::Font(EFont::Display, RoundSize * S, RoundOutline * S), TopRight, KC::White, FVector2D(1.0, 0.0), RoundShadow * S);
	// The bounty chip: the called sector's number in the wedge's gold, "x2".
	const FVector2D Size = ChipSize * S;
	const FVector2D ChipAt(TopRight.X - Size.X, TopRight.Y + ChipBelow * S);
	P.Card(ChipAt, Size, KC::WithAlpha(KC::Night, ChipAlpha), Chip.Radius * S, Chip.Shadow * S, KC::Sun, Chip.Outline * S);
	P.Text(BountyLabel, KartUI::Font(EFont::Label, ChipLabelSize * S), ChipAt + FVector2D(ChipLabelX * S, Size.Y / 2.f), KC::Sun, LeftMiddle);
	P.Text(BountyText.Get(BountyForRound(Visit), NumberText), KartUI::Font(EFont::Display, ChipNumberSize * S),
		ChipAt + FVector2D(Size.X - ChipNumberFromRight * S, Size.Y / 2.f), KC::Sun, RightMiddle);
	P.Text(Times2, KartUI::Font(EFont::Display, ChipTimesSize * S), ChipAt + FVector2D(Size.X - ChipTimesFromRight * S, Size.Y / 2.f), KC::White, RightMiddle);
	// Darts in hand, as three little darts.
	const int32 Left = FMath::Max(0, DartsPerRound - M.TurnDarts);
	for (int32 i = 0; i < DartsPerRound; ++i)
	{
		const FVector2D C(TopRight.X - DartsFromRight * S - i * DartSpacing * S, TopRight.Y + DartsBelow * S);
		const FLinearColor Fill = i < Left ? (Seat ? Seat->Color : KC::Sky) : KC::WithAlpha(KC::White, SpentAlpha);
		Poly.Reset();
		Poly.Add(C + FVector2D(0.f, -DartTip * S));
		Poly.Add(C + FVector2D(DartHalfWidth * S, -DartShoulder * S));
		Poly.Add(C + FVector2D(0.f, DartTip * S));
		Poly.Add(C + FVector2D(-DartHalfWidth * S, -DartShoulder * S));
		P.Polygon(Poly, Fill);
		P.Box(C + FlightOffset * S, FlightSize * S, Fill, FlightRadius * S);
	}
}

void SDartsHud::PaintCountdown(const FKartPainter& P, const ADartsGame& G, float S) const
{
	using namespace Countdown;
	const FText& ThrowLabel = ARCADE_LABEL("Darts", "Throw", "THROW!");
	const FDartsMatch& M = G.GetMatch();
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	if (M.Phase == EMatchPhase::Countdown)
	{
		const double Remaining = M.CountdownRemainingMs / 1000.0;
		const int32 N = FMath::CeilToInt32(Remaining);
		const float Beat = static_cast<float>(1.0 - (N - Remaining));
		const float Pop = KartUI::Ease::OutBack(FMath::Clamp(Beat / BeatPop, 0.f, 1.f));
		const FString& Digit = CountdownText.Get(FMath::Max(1, N), NumberText);
		P.Transformed(Centre, FMath::Max(MinScale, Pop)).Text(Digit, KartUI::Font(EFont::Display, DigitSize * S, DigitOutline * S, KC::Night), Centre,
			KC::Sun, Centred, DigitShadow * S);
		return;
	}
	const double Since = M.ElapsedMs / 1000.0;
	if (M.Phase == EMatchPhase::Playing && Since < ThrowSeconds)
	{
		const float A = FMath::Clamp(static_cast<float>((ThrowSeconds - Since) / ThrowFade), 0.f, 1.f);
		const FVector2D ThrowAt(P.Size().X / 2.f, P.Size().Y * ThrowY);
		P.Faded(A).Text(ThrowLabel, KartUI::Font(EFont::Display, ThrowSize * S, ThrowOutline * S, KC::Night), ThrowAt, KC::Mint, Centred, ThrowShadow * S);
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
		const FKartPainter C = P.Transformed(At, PopScale(Age, PopSeconds, PopOvershoot) * (bBig ? BigScale : 1.f), bBig ? BigTiltDegrees : 0.f).Faded(Out);
		C.Text(Text.Label, KartUI::Font(EFont::Display, LabelSize * S, LabelOutline * S, KC::Night), At, EffectColor(E, Lane), Centred, LabelShadow * S);
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
	const FSlateFontInfo Font = KartUI::Font(EFont::Display, BannerSize * S, BannerOutline * S, KC::Night);
	FCalloutText& Text = Callouts[Slot];
	if (Text.EffectId != E.Id || Text.Scale != S)
	{
		Text.EffectId = E.Id;
		Text.Scale = S;
		Text.Label = FString::Printf(TEXT("%s  ·  %s"), Seat ? *Seat->Name : TEXT(""), *E.Label);
		Text.Width = static_cast<float>(FKartPainter::Measure(Text.Label, Font).X) + BannerPadX * S;
	}
	const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
	const double Life = E.DurationMs / 1000.0;
	const float Out = FMath::Clamp(static_cast<float>((Life - Age) / FadeSeconds), 0.f, 1.f);
	const FVector2D At = At01 * P.Size();
	const FKartPainter B = P.Transformed(At, PopScale(Age, BannerPopSeconds)).Faded(Out);
	const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
	B.Card(At - FVector2D(Text.Width / 2.f, BannerHeight / 2.f * S), FVector2D(Text.Width, BannerHeight * S), Lane, Banner.Radius * S, Banner.Shadow * S, KC::White,
		Banner.Outline * S);
	B.Text(Text.Label, Font, At, KC::White, Centred, BannerShadow * S);
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
	VisitText.Call = V.Call.ToString();
	const FSlateFontInfo CallFont = KartUI::Font(EFont::Display, (IsBigVisit(V) ? BigCallSize : CallSize) * S, CallOutline * S, KC::Night);
	VisitText.CallWidth = static_cast<float>(FKartPainter::Measure(VisitText.Call, CallFont).X) + RibbonPadX * S;
	VisitText.Total = FString::FromInt(V.Total + V.Bonus);
	VisitText.Footer = V.Bonus > 0 ? FText::Format(ARCADE_LABEL("Darts", "VisitBonus", "incl. +{0} bonus"), V.Bonus) : ARCADE_LABEL("Darts", "VisitPoints", "points");
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
	const float A = CardFade(Age, FMath::Min(LifeSeconds, V.EndAt - V.At + LingerSeconds));
	if (A <= 0.f || V.PlayerId.IsEmpty())
	{
		return;
	}
	RefreshVisitText(V, S);
	const FDartsSeat* Seat = G.SeatOf(V.PlayerId);
	const FLinearColor Lane = Seat ? Seat->Color : KC::Sky;
	const bool bBig = IsBigVisit(V);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	const FKartPainter C = P.Transformed(Centre, PopScale(Age, PopSeconds)).Faded(A);
	const FVector2D CardSize = Size * S;
	const FVector2D Pos = Centre - CardSize / 2.f;
	C.Card(Pos, CardSize, KC::WithAlpha(KC::Night, VisitCard::CardAlpha), Card.Radius * S, Card.Shadow * S, bBig ? KC::Sun : Lane, Card.Outline * S);
	const FSlateFontInfo CallFont = KartUI::Font(EFont::Display, (bBig ? BigCallSize : CallSize) * S, CallOutline * S, KC::Night);
	C.Card(FVector2D(Centre.X - VisitText.CallWidth / 2.f, Pos.Y - RibbonHeight / 2.f * S), FVector2D(VisitText.CallWidth, RibbonHeight * S), bBig ? KC::Sun : Lane,
		Ribbon.Radius * S, Ribbon.Shadow * S);
	C.Text(VisitText.Call, CallFont, FVector2D(Centre.X, Pos.Y), KC::White, Centred, CallShadow * S);
	PaintVisitChips(C, V, Pos, Lane, Age, S);
	// The total, and the round's bonus under it.
	const FVector2D TotalAt(Pos.X + CardSize.X - TotalFromRight * S, Pos.Y + TotalY * S);
	C.Text(VisitText.Total, KartUI::Font(EFont::Display, TotalSize * S, TotalOutline * S), TotalAt, bBig ? KC::Sun : KC::White, RightMiddle, TotalShadow * S);
	C.Text(VisitText.Footer, KartUI::Font(EFont::Label, FooterSize * S), TotalAt + FVector2D(0.f, FooterBelow * S), KC::WithAlpha(KC::White, FooterAlpha), RightMiddle);
}

void SDartsHud::PaintVisitChips(const FKartPainter& Card, const FDartsVisitSummary& V, const FVector2D& Pos, const FLinearColor& Lane, double Age, float S) const
{
	// The three darts as chips, landing one after another.
	using namespace VisitCard;
	for (int32 i = 0; i < V.Labels.Num(); ++i)
	{
		const FVector2D ChipAt(Pos.X + ChipsAt.X * S + i * ChipPitch * S, Pos.Y + ChipsAt.Y * S);
		const FKartPainter D = Card.Transformed(ChipAt + ChipPivot * S, PopScale(Age - ChipDelay - i * ChipStagger, ChipPopSeconds));
		const bool bMiss = V.Points[i] == 0;
		const FLinearColor Fill = bMiss ? KC::NavyDim : Lane;
		// A plain single reads once ("18"); a treble, double, bull or bounty shows what it paid ("T20" over "+60").
		const bool bPlain = !bMiss && V.Labels[i] == FString::FromInt(V.Points[i]);
		D.Card(ChipAt, ChipSize * S, bMiss ? KC::WithAlpha(KC::NavyDim, MissChipAlpha) : Lane, Chip.Radius * S, Chip.Shadow * S);
		const float Label = VisitText.LabelSizes[i];
		D.Text(V.Labels[i], KartUI::Font(EFont::Title, bPlain ? Label * PlainLabelScale : Label), ChipAt + FVector2D(ChipPivot.X * S, (bPlain ? PlainLabelY : LabelY) * S),
			KC::TextOn(Fill), Centred);
		if (!bPlain)
		{
			D.Text(VisitText.Points[i], KartUI::Font(EFont::Label, PointsSize * S), ChipAt + FVector2D(ChipPivot.X * S, PointsY * S),
				KC::WithAlpha(KC::TextOn(Fill), PointsAlpha), Centred);
		}
	}
}

void SDartsHud::PaintGauge(const FKartPainter& P, const ADartsGame& G, const FArcadeHudView& View, float S) const
{
	// The aim read: by the reticle, a vertical power gauge while the dart is gripped (the sweet band in mint), and a
	// quiet "hold" hint while it isn't.
	using namespace Gauge;
	const FText& HoldLabel = ARCADE_LABEL("Darts", "GaugeHold", "HOLD TO THROW");
	const FText& NowLabel = ARCADE_LABEL("Darts", "GaugeNow", "NOW!");
	const FText& HighLabel = ARCADE_LABEL("Darts", "GaugeHigh", "HIGH");
	const FText& LowLabel = ARCADE_LABEL("Darts", "GaugeLow", "LOW");
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
			const float Pulse = HintMin + (1.f - HintMin) * FMath::Sin(static_cast<float>(G.GetClock()) * HintPulseRate);
			const FKartPainter H = P.Faded(Pulse);
			const FVector2D PillSize = HintPill * S;
			const FVector2D PillAt = At + FVector2D(-PillSize.X / 2.f, HintBelow * S);
			H.Card(PillAt, PillSize, KC::WithAlpha(KC::Night, HintAlpha), Hint.Radius * S, Hint.Shadow * S, Thrower->Color, Hint.Outline * S);
			H.Text(HoldLabel, KartUI::Font(EFont::Label, HintSize * S), PillAt + PillSize / 2.f, KC::White, Centred);
		}
		return;
	}
	const FVector2D GaugeSize = Size * S;
	const FVector2D Pos = At + FVector2D(OffsetX * S, -GaugeSize.Y / 2.f);
	P.Box(Pos - FVector2D(FramePad, FramePad) * S, GaugeSize + FVector2D(2.f * FramePad, 2.f * FramePad) * S, KC::WithAlpha(KC::Night, FrameAlpha), FrameRadius * S);
	auto YOf = [&](double Power) { return Pos.Y + GaugeSize.Y * static_cast<float>(1.0 - Power); };
	const float SweetShare = static_cast<float>(2.0 * Tuning::SweetBand);
	P.Box(FVector2D(Pos.X, YOf(SweetSpot + Tuning::SweetBand)), FVector2D(GaugeSize.X, GaugeSize.Y * SweetShare), KC::WithAlpha(KC::Mint, SweetAlpha), SweetRadius * S);
	const float Power = static_cast<float>(FMath::Clamp(L.Power, 0.0, 1.0));
	const bool bSweet = Tuning::IsSweet(Power);
	const FLinearColor Fill = bSweet ? KC::Mint : (Power > SweetSpot ? KC::Hot : KC::Sun);
	P.Box(FVector2D(Pos.X, YOf(Power)), FVector2D(GaugeSize.X, GaugeSize.Y * Power), Fill, FillRadius * S);
	// The marker, and what letting go now does.
	P.Box(FVector2D(Pos.X - MarkerOverhang * S, YOf(Power) - MarkerHeight / 2.f * S), FVector2D(GaugeSize.X + 2.f * MarkerOverhang * S, MarkerHeight * S), KC::White,
		MarkerRadius * S);
	const FText& Read = bSweet ? NowLabel : (Power > SweetSpot ? HighLabel : LowLabel);
	P.Text(Read, KartUI::Font(EFont::Display, (bSweet ? SweetReadSize : ReadSize) * S, ReadOutline * S), FVector2D(Pos.X + GaugeSize.X + ReadGap * S, YOf(Power)), Fill,
		LeftMiddle, ReadShadow * S);
}

void SDartsHud::PaintControls(const FKartPainter& P, const ADartsGame& G, float S) const
{
	using namespace Controls;
	const FText& PhoneHint = ARCADE_LABEL("Darts", "PhoneHint", "Aim with your phone, pinch, flick and let go");
	const FText& BotHint = ARCADE_LABEL("Darts", "BotHint", "Lining up...");
	const FText& ReleaseHint = ARCADE_LABEL("Darts", "Release", "Let go in the green  ·  or flick the mouse up");
	const FText& GripHint = ARCADE_LABEL("Darts", "Grip", "Aim: WASD, stick or mouse  ·  Throw: hold SPACE, RT or click");
	const FText& LowLabel = ARCADE_LABEL("Darts", "Low", "LOW");
	const FText& HighLabel = ARCADE_LABEL("Darts", "High", "HIGH");
	const FDartsSeat* Thrower = G.GetThrower();
	const FDartsMatch& M = G.GetMatch();
	if (!Thrower || M.Phase != EMatchPhase::Playing)
	{
		return;
	}
	const FVector2D Centre(P.Size().X * (1.f - KartUI::Metric::SafeFraction) - LeftOfSafeRight * S, P.Size().Y * CentreY);
	const int32 SeatIndex = static_cast<int32>(Thrower - G.GetSeats().GetData());
	const FString& Up = UpText.Get(SeatIndex, [Thrower](int32) { return FText::Format(ARCADE_LABEL("Darts", "Up", "{0} is up"), FText::FromString(Thrower->Name)).ToString(); });
	if (Thrower->Kind != EThrowerKind::Local)
	{
		const FText& Hint = Thrower->Kind == EThrowerKind::Phone ? PhoneHint : BotHint;
		P.Card(Centre - RemoteCard / 2.0 * S, RemoteCard * S, KC::WithAlpha(KC::Night, CardAlpha), Remote.Radius * S, Remote.Shadow * S, Thrower->Color, Remote.Outline * S);
		P.Text(Up, KartUI::Font(EFont::Title, RemoteUpSize * S), Centre + FVector2D(0.f, RemoteUpY * S), KC::White, Centred);
		P.Text(Hint, KartUI::Font(EFont::Body, RemoteHintSize * S), Centre + FVector2D(0.f, RemoteHintY * S), KC::WithAlpha(KC::White, RemoteHintAlpha), Centred);
		return;
	}
	const FLocalThrow& L = G.GetLocal();
	P.Card(Centre - LocalCard / 2.0 * S, LocalCard * S, KC::WithAlpha(KC::Night, CardAlpha), Local.Radius * S, Local.Shadow * S, Thrower->Color, Local.Outline * S);
	P.Text(Up, KartUI::Font(EFont::Title, LocalUpSize * S), Centre + FVector2D(0.f, LocalUpY * S), KC::White, Centred);
	P.Text(L.bGripped ? ReleaseHint : GripHint, KartUI::Font(EFont::Body, LocalHintSize * S), Centre + FVector2D(0.f, LocalHintY * S),
		KC::WithAlpha(KC::White, LocalHintAlpha), Centred);
	// The meter, with the sweet spot (a throw that lands on the aim) marked; under it drops, over it sails.
	const FVector2D BarSize = MeterSize * S;
	const FVector2D BarPos = Centre + FVector2D(-BarSize.X / 2.f, MeterY * S);
	P.Box(BarPos, BarSize, KC::WithAlpha(KC::White, MeterTrackAlpha), MeterRadius * S);
	const float SweetShare = static_cast<float>(2.0 * Tuning::SweetBand);
	P.Box(BarPos + FVector2D(BarSize.X * static_cast<float>(SweetSpot - Tuning::SweetBand), 0.f), FVector2D(BarSize.X * SweetShare, BarSize.Y), KC::WithAlpha(KC::Mint, SweetAlpha),
		MeterRadius * S);
	const float Power = static_cast<float>(L.bGripped ? L.Power : 0.0);
	if (Power > 0.f)
	{
		const FLinearColor To = Tuning::IsSweet(Power) ? KC::Mint : (Power > SweetSpot ? KC::Hot : KC::Sun);
		P.Gradient(BarPos, FVector2D(FMath::Max(BarSize.Y, BarSize.X * Power), BarSize.Y), KC::Sky, To, false, MeterRadius * S);
	}
	P.Text(LowLabel, KartUI::Font(EFont::Label, EndLabelSize * S), BarPos - FVector2D(EndLabelGap * S, -BarSize.Y / 2.f), KC::Sun, RightMiddle);
	P.Text(HighLabel, KartUI::Font(EFont::Label, EndLabelSize * S), BarPos + FVector2D(BarSize.X + EndLabelGap * S, BarSize.Y / 2.f), KC::Hot, LeftMiddle);
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
		{ ARCADE_LABEL("Darts", "CoachAim", "AIM"), ARCADE_LABEL("Darts", "CoachAimKeys", "WASD  ·  left stick  ·  mouse"), TEXT("1") },
		{ ARCADE_LABEL("Darts", "CoachHold", "HOLD"), ARCADE_LABEL("Darts", "CoachHoldKeys", "SPACE  ·  RT  ·  left click"), TEXT("2") },
		{ ARCADE_LABEL("Darts", "CoachLetGo", "LET GO"), ARCADE_LABEL("Darts", "CoachLetGoKeys", "when the meter is in the green"), TEXT("3") },
	};
	enum EStep : int32 { AimStep, HoldStep, LetGoStep };
	const FLocalThrow& L = G.GetLocal();
	const FKartPainter F = P.Faded(A);
	const int32 Current = L.bGripped ? LetGoStep : (L.Aim.IsNearlyZero(AimedAway) ? AimStep : HoldStep);
	const FVector2D Origin(P.Size().X * KartUI::Metric::SafeFraction, P.Size().Y * TopY);
	for (int32 i = 0; i < UE_ARRAY_COUNT(Steps); ++i)
	{
		const bool bOn = i == Current;
		const float Pop = bOn ? 1.f + Pulse * FMath::Sin(static_cast<float>(G.GetClock()) * PulseRate) : 1.f;
		const FVector2D Pos = Origin + FVector2D(0.f, i * StepPitch * S);
		const FKartPainter R = F.Transformed(Pos + FVector2D(0.f, CardSize.Y / 2.0 * S), Pop);
		R.Card(Pos, CardSize * S, bOn ? KC::Paper : KC::WithAlpha(KC::Night, IdleAlpha), Card.Radius * S, Card.Shadow * S, bOn ? KC::Mint : FLinearColor::Transparent,
			bOn ? Card.Outline * S : 0.f);
		const FLinearColor Ink = bOn ? KC::Night : KC::White;
		R.Circle(Pos + NumberAt * S, DiscRadius * S, bOn ? KC::Mint : KC::WithAlpha(KC::White, IdleDiscAlpha));
		R.Text(Steps[i].Number, KartUI::Font(EFont::Display, NumberSize * S), Pos + NumberAt * S, bOn ? KC::Night : KC::White, Centred);
		R.Text(Steps[i].Title, KartUI::Font(EFont::Display, TitleSize * S), Pos + FVector2D(TextX * S, TitleY * S), Ink, LeftMiddle);
		R.Text(Steps[i].Keys, KartUI::Font(EFont::Body, KeysSize * S), Pos + FVector2D(TextX * S, KeysY * S), KC::WithAlpha(Ink, KeysAlpha), LeftMiddle);
	}
}

void SDartsHud::PaintIntro(const FKartPainter& P, const ADartsGame& G, float S) const
{
	using namespace Intro;
	const FText& Title = ARCADE_LABEL("Darts", "Title", "BULLSEYE BASH");
	const FText& Rules = ARCADE_LABEL("Darts", "Rules", "Three darts a visit, three visits. Hit the called sector for double points.");
	const double Age = G.GetStageAge();
	const double Life = ADartsGame::IntroSeconds;
	const float A = FMath::Clamp(static_cast<float>(Age / FadeInSeconds), 0.f, 1.f) * FMath::Clamp(static_cast<float>((Life - Age) / FadeOutSeconds), 0.f, 1.f);
	const FKartPainter F = P.Faded(A);
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * CentreY);
	const FVector2D TitleAt = Centre - FVector2D(0.f, TitleAbove * S);
	F.Transformed(TitleAt, PopScale(Age, PopSeconds)).Text(Title, KartUI::Font(EFont::Display, TitleSize * S, TitleOutline * S), TitleAt, KC::Hot, Centred, TitleShadow * S);
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
	F.Text(Names, KartUI::Font(EFont::Title, NamesSize * S, NamesOutline * S), Centre + FVector2D(0.f, NamesBelow * S), KC::White, Centred, NamesShadow * S);
	F.Text(Rules, KartUI::Font(EFont::Body, RulesSize * S, RulesOutline * S), Centre + FVector2D(0.f, RulesBelow * S), KC::WithAlpha(KC::White, RulesAlpha), Centred,
		RulesShadow * S);
}

void SDartsHud::PaintStandings(const FKartPainter& P, const ADartsGame& G, float S) const
{
	using namespace Standings;
	const FText& FinalTitle = ARCADE_LABEL("Darts", "Final", "FINAL SCORES");
	const FDartsMatch& M = G.GetMatch();
	// Best first, ties in seat order: the rules' Leaderboard order.
	const TArray<int32>& Order = Ranks.Update(M.Players.Num(), [&M](int32 i) { return static_cast<int64>(M.Players[i].Score); });
	StandingText.SetNum(Order.Num(), EAllowShrinking::No);
	const double Age = G.GetFinishedAge();
	const FKartPainter F = P.Faded(FMath::Clamp(static_cast<float>(Age / FadeInSeconds), 0.f, 1.f));
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * TitleY);
	F.Text(FinalTitle, KartUI::Font(EFont::Display, TitleSize * S, TitleOutline * S), Centre, KC::Sun, Centred, TitleShadow * S);
	for (int32 i = 0; i < Order.Num(); ++i)
	{
		const FDartsPlayer& Player = M.Players[Order[i]];
		const FDartsSeat* Seat = G.SeatOf(Player);
		const float In = PopScale(Age - (RowDelay + i * RowStagger), RowPopSeconds);
		const FVector2D Pos(P.Size().X / 2.f - RowSize.X / 2.0 * S, P.Size().Y * TopY + i * RowPitch * S);
		const FKartPainter RowPainter = F.Transformed(Pos + RowSize / 2.0 * S, In);
		const bool bWinner = M.WinnerIds.Contains(Player.Id);
		RowPainter.Card(Pos, RowSize * S, bWinner ? KC::Paper : KC::WithAlpha(KC::Night, IdleAlpha), Row.Radius * S, Row.Shadow * S, Seat ? Seat->Color : KC::Sky,
			Row.Outline * S);
		const FLinearColor Ink = bWinner ? KC::Night : KC::White;
		FStandingText& Text = StandingText[i];
		RowPainter.Text(Text.Place.Get(i + 1, NumberText), KartUI::Font(EFont::Display, PlaceSize * S), Pos + PlaceAt * S, KC::ForPlace(i + 1), Centred, PlaceShadow * S);
		RowPainter.Text(Player.Name, KartUI::Font(EFont::Title, NameSize * S), Pos + NameAt * S, Ink, LeftMiddle);
		// The stats line re-formats only when one of its three numbers moves (packed into one key).
		const int64 StatsKey = (static_cast<int64>(Player.BestRound) << 32) | (static_cast<int64>(Player.Bulls & 0xffff) << 16) | (Player.Bounties & 0xffff);
		const FString& Stats = Text.Stats.Get(StatsKey, [&Player](int64)
		{
			return FText::Format(ARCADE_LABEL("Darts", "StandingStats", "best round {0}  ·  {1} bulls  ·  {2} bounties"), Player.BestRound, Player.Bulls, Player.Bounties).ToString();
		});
		RowPainter.Text(Stats, KartUI::Font(EFont::Body, StatsSize * S), Pos + StatsAt * S, KC::WithAlpha(Ink, StatsAlpha), LeftMiddle);
		RowPainter.Text(Text.Score.Get(Player.Score, NumberText), KartUI::Font(EFont::Display, ScoreSize * S), Pos + TotalAt * S, Ink, RightMiddle);
	}
}

