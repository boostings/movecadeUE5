// SSamuraiHud: the full-screen moments (the intro card and its seat chips, the practice coach mark and swipe, the
// final standings). The round's panels are in SSamuraiHud.cpp.

#include "Games/Samurai/SSamuraiHud.h"

#include "Games/Samurai/SamuraiGame.h"
#include "Games/Samurai/SSamuraiHudStyle.h"

using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Samurai::HudStyle;

void SSamuraiHud::PaintIntro(const FKartPainter& P, const ASamuraiGame& G, float S) const
{
	const FText& Title = ARCADE_LABEL("Samurai", "Title", "SAMURAI SLICE");
	const FText& Rules = ARCADE_LABEL("Samurai", "Rules", "Cut the fruit. Never the bombs. Three precise cuts: Focus, double points.");
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
	const FText& CpuLabel = ARCADE_LABEL("Samurai", "Cpu", "CPU");
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
	const FText& ChipLabel = ARCADE_LABEL("Samurai", "PracticeChip", "PRACTICE");
	const FText& SkipLabel = ARCADE_LABEL("Samurai", "PracticeSkip", "SPACE / (A)  skip");
	const FText& SlowLine = ARCADE_LABEL("Samurai", "PracticeSlow", "Faster! A cut is a quick, sharp swing.");
	const FText& SlowSub = ARCADE_LABEL("Samurai", "PracticeSlowSub", "Flick the mouse or stick right through the apple.");
	const FText& DoneLine = ARCADE_LABEL("Samurai", "PracticeDone", "Clean cut! That's the stroke.");
	const FText& DoneSub = ARCADE_LABEL("Samurai", "PracticeDoneSub", "Cut at the top of each arc for Perfect. Here comes the fruit...");
	const FText& AskLine = ARCADE_LABEL("Samurai", "PracticeLine", "Slice the practice apple!");
	const FText& AskSub = ARCADE_LABEL("Samurai", "PracticeSub", "Mouse or right stick aims the blade. Swing fast through the fruit to cut.");
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
	const FText& Title = ARCADE_LABEL("Samurai", "Final", "FINAL SCORES");
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
