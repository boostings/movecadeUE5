#include "Games/Samurai/SSamuraiHud.h"

#include "Games/Samurai/SamuraiGame.h"
#include "Games/Samurai/SSamuraiHudStyle.h"

using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Samurai::HudStyle;

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
	Safe = KartSafeArea::FKartSafeFrame::ForHud(AllottedGeometry);
	const float S = Safe.Scale;
	View = FArcadeHudView::Capture(G->GetViewer());
	switch (G->GetStage())
	{
	case ESamuraiStage::Intro:
		// The intro card and the practice coach are wide: shrunk to fit a narrow screen.
		PaintNotes(P, *G, S);
		PaintIntro(P, *G, Safe.FitWidth(Portrait::IntroWidth));
		PaintPractice(P, *G, Safe.FitWidth(Practice::CardSize.X));
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
		PaintBanner(P, *G, Safe.FitWidth(Banner::BandWidth));
		PaintControls(P, *G, Safe.FitWidth(Controls::CardSize.X));
		PaintCountdown(P, *G, S);
		break;
	}
	return Layer;
}

void SSamuraiHud::PaintClock(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FMatch& M = G.GetMatch();
	const FVector2D Top(P.Size().X / 2.f, Safe.SafeMin.Y);
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
	float Width = FMath::Min(Lane::MaxWidth * S, (P.Size().X * Lane::ScreenShare) / FMath::Max(1, Count) - Lane::Gap * S);
	if (Safe.bPortrait)
	{
		// Two columns under the clock.
		const int32 Columns = FMath::Min(Count, Portrait::LaneColumns);
		Width = FMath::Min(Lane::MaxWidth * S, (static_cast<float>(Safe.SafeSize().X) - (Columns - 1) * Lane::Gap * S) / FMath::Max(1, Columns));
	}
	LaneTexts.SetNum(Count);
	for (const FPlayer& Player : M.Players)
	{
		PaintLanePanel(P, G, Player, Width, S);
	}
}

void SSamuraiHud::PaintLanePanel(const FKartPainter& P, const ASamuraiGame& G, const FPlayer& Player, float Width, float S) const
{
	const FText& OutLabel = ARCADE_LABEL("Samurai", "Out", "OUT");
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
	FVector2D Pos(At01.X * P.Size().X - Width / 2.f, Safe.SafeMax.Y - Height);
	if (Safe.bPortrait)
	{
		// Scores on top: a grid under the clock, in lane order (each panel carries its swordsman's colour).
		const int32 Columns = FMath::Min(Count, Portrait::LaneColumns);
		const int32 Row = LaneIndex / Portrait::LaneColumns, Column = LaneIndex % Portrait::LaneColumns;
		const int32 InRow = FMath::Min(Columns, Count - Row * Portrait::LaneColumns);
		const float RowWidth = InRow * Width + (InRow - 1) * Lane::Gap * S;
		Pos = FVector2D(P.Size().X / 2.f - RowWidth / 2.f + Column * (Width + Lane::Gap * S),
			Safe.SafeMin.Y + (Clock::CardSize.Y + Portrait::LanesBelowClock + Row * (Lane::Height + Lane::Gap)) * S);
	}
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
	const FText& BarLabel = ARCADE_LABEL("Samurai", "Focus", "FOCUS x2");
	const FText& ChargeLabel = ARCADE_LABEL("Samurai", "FocusCharge", "FOCUS");
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
	const FVector2D Centre(P.Size().X / 2.f, P.Size().Y * (Safe.bPortrait ? Portrait::BannerY01 : Banner::Y01));
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
	const FText& GoLabel = ARCADE_LABEL("Samurai", "Go", "SLICE!");
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
	const FText& Hint = ARCADE_LABEL("Samurai", "Controls", "Mouse or stick: swing fast through the fruit  ·  never the bombs");
	if (!G.HasLocal() || G.GetMatch().ElapsedMs > Controls::ShowMs)
	{
		return;
	}
	const float A = FMath::Clamp(static_cast<float>((Controls::ShowMs - G.GetMatch().ElapsedMs) / Controls::FadeMs), 0.f, 1.f);
	const FVector2D Centre(P.Size().X / 2.f, Safe.bPortrait ? Safe.SafeMax.Y - Controls::CardSize.Y / 2.f * S : P.Size().Y * Controls::Y01);
	const FKartPainter F = P.Faded(A);
	F.Card(Centre - Controls::CardSize / 2.f * S, Controls::CardSize * S, KC::WithAlpha(KC::Night, Controls::CardAlpha), Controls::Card.Radius * S, Controls::Card.Shadow * S,
		KC::Sun, Controls::Card.OutlineWidth * S);
	F.Text(Hint, Font(Controls::Text, S), Centre, KC::White, Centred);
}
