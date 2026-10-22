// AStackerGame: the local builder's coach marks ("drop when aligned", "lean against the wind"); SStackerHud paints them.

#include "Games/Stacker/StackerGame.h"

using namespace MovecadeKart::Stacker;

namespace
{
	/** "Drop when aligned" goes for good after this many landings. */
	constexpr int32 DropLessonLandings = 3;
	/** "Lean against the wind" goes for good once the tower has been held steady in a real wind this long, and the
	 * mark has been on screen at least this long. */
	constexpr double LeanLessonSteadySeconds = 6.0;
	constexpr double LeanLessonShownSeconds = 2.5;
	/** A wind (degrees of push) this strong is worth leaning against. */
	constexpr double RealWind = 1.0;
	/** Past this share of the limit the lean itself (rather than the wind) says which way to push. */
	constexpr double LeanShowsRatio = 0.25;
	/** The marks fade in and out at this rate (per second); a mark this visible counts as shown. */
	constexpr float FadeRate = 8.f;
	constexpr float ShownThreshold = 0.5f;

	/**
	 * The lessons are learnt once per session: a later round (or a rematch) doesn't repeat them. Deliberate process
	 * state: a session is one run of the game, and nothing else reads it.
	 */
	bool GDropLessonDone = false;
	bool GLeanLessonDone = false;
}

void AStackerGame::UpdateCoach(float Dt)
{
	// "Drop when aligned" until a few blocks have landed; "lean against the wind" when the wind starts to matter,
	// until the tower has been held steady in it for a while.
	Coach.Tower = Lanes.IndexOfByPredicate([](const FLane& L) { return L.Kind == EBuilderKind::Local; });
	float WantDrop = 0.f, WantLean = 0.f;
	if (Coach.Tower != INDEX_NONE && Stage == EStackerStage::Match && Match.Phase == EMatchPhase::Playing)
	{
		const FTower& Tower = Match.Towers[Coach.Tower];
		const bool bLive = !Tower.IsDone() && Tower.Balance.RecoveryMs <= 0.0;
		const bool bCursor = bLive && Match.ElapsedMs - Tower.LastDropAtMs >= DropCooldownMs;
		Coach.bAligned = bCursor && PreviewDrop(Tower, Tower.CursorCenter).bPerfect;
		if (CoachLandings >= DropLessonLandings)
		{
			GDropLessonDone = true;
		}
		WantDrop = !GDropLessonDone && bCursor ? 1.f : 0.f;

		const FVector2D Wind = WindAt(Match.ElapsedMs, Tower.Height());
		const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
		const double Ratio = FMath::Abs(Tower.Balance.LeanX) / Limit;
		const bool bWindy = FMath::Abs(Wind.X) > RealWind;
		const bool bLeaning = Ratio > SteadyBalanceRatio || Tower.Balance.DangerMs > 0.0;
		if (bWindy && !bLeaning)
		{
			CoachSteadySeconds += Dt;
		}
		if (CoachSteadySeconds > LeanLessonSteadySeconds && CoachLeanShown > LeanLessonShownSeconds)
		{
			GLeanLessonDone = true;
		}
		WantLean = !GLeanLessonDone && bLive && (bLeaning || (bWindy && CoachLeanShown < LeanLessonShownSeconds)) ? 1.f : 0.f;
		// Push the palm against whatever is tipping the tower: the lean itself once it shows, else the wind.
		const double Push = Ratio > LeanShowsRatio ? -Tower.Balance.LeanX : -Wind.X;
		Coach.LeanSide = Push >= 0.0 ? 1.f : -1.f;
		if (Coach.Lean > ShownThreshold)
		{
			CoachLeanShown += Dt;
		}
	}
	const float K = 1.f - FMath::Exp(-FadeRate * Dt);
	Coach.Drop += (WantDrop - Coach.Drop) * K;
	Coach.Lean += (WantLean - Coach.Lean) * K;
}
