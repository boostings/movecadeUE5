#pragma once

#include "CoreMinimal.h"

#include <limits>

/**
 * Sky Stacker's rules (movecade-v2 stacker/{engine,balance}.ts, Docs/Port/09-stacker.md §1-§2): each player builds
 * a tower on their own lane. A block sweeps over the top of the tower and drops on the DROP button; whatever hangs
 * over the block below is trimmed away, a drop inside the perfect window regrows the width. The player balances the
 * tower with their palm (the phone's tilt) against a wind that grows with height; leaning past the limit for too
 * long topples it. No RNG: every player faces the same sweep and wind. Pure: no UObjects, no clock.
 */
namespace MovecadeKart::Stacker
{
	constexpr double RoundDurationMs = 90000.0;
	constexpr double CountdownMs = 3000.0;
	constexpr int32 StartingLives = 3;
	constexpr double TrackHalfWidth = 3.2;
	constexpr double SweepTrackNarrowingPerRow = 0.0125;
	constexpr double MinSweepTrackHalfWidth = 2.4;
	constexpr double BaseWidth = 3.6;
	constexpr double MinWidth = 0.42;
	constexpr double PerfectTolerance = 0.18;
	constexpr double SteadyPerfectTolerance = 0.4;
	constexpr double PerfectRegrow = 0.4;
	constexpr double DropCooldownMs = 420.0;
	constexpr int32 MaxTowerHeight = 60;
	constexpr int32 TowerCompleteBonus = 300;
	constexpr int32 TowerCompleteBonusPerLife = 100;
	constexpr int32 MaxEffects = 24;
	constexpr int32 MaxShards = 32;
	constexpr double BalanceStaleMs = 900.0;
	constexpr double BalanceFailureMs = 2200.0;
	constexpr double SteadyBalanceRatio = 0.5;

	// Balance (balance.ts, degrees) -----------------------------------------------------------------------------

	struct FBalance
	{
		/** The palm's tilt, degrees: roll leans the tower across the lane, pitch toward and away. */
		double Roll = 0.0;
		double Pitch = 0.0;
		/** Match time of the last input; -infinity until the first (the web's quirk: not "missing" then). */
		double LastInputMs = -std::numeric_limits<double>::infinity();
		double WindX = 0.0;
		double WindY = 0.0;
		double LeanX = 0.0;
		double LeanY = 0.0;
		double VelocityX = 0.0;
		double VelocityY = 0.0;
		double DangerMs = 0.0;
		double RecoveryMs = 0.0;
	};

	/** The wind at a match time and height: the same for every tower. */
	MOVECADEKART_API FVector2D WindAt(double ElapsedMs, double Height);
	/** How far the tower may lean (degrees) at a height and top width. */
	MOVECADEKART_API double BalanceLimit(double Height, double Width = BaseWidth);
	MOVECADEKART_API bool IsBalanceSteady(const FBalance& S, double Limit);
	/** How far a lean shifts the support under the next block (world units). */
	MOVECADEKART_API double BalanceDisplacement(double Lean);
	MOVECADEKART_API bool BalanceInputMissing(const FBalance& S, double ElapsedMs);
	MOVECADEKART_API void ApplyLandingImpulse(FBalance& S, double Offset, double Width, double Height);
	/** Integrates the lean against palm and wind; true on a topple (danger held past the failure time). */
	MOVECADEKART_API bool StepBalance(FBalance& S, double ElapsedMs, double Height, double DtMs, double Width = BaseWidth);

	// Engine (engine.ts) -------------------------------------------------------------------------------------

	enum class EDropKind : uint8 { Land, Perfect, Miss };
	enum class EEffectKind : uint8 { Land, Perfect, Miss, Topout, Complete };
	enum class EMatchPhase : uint8 { Countdown, Playing, Ended };

	struct FBlock
	{
		int32 Index = 0;
		/** Unswayed centre across the lane. */
		double Center = 0.0;
		double Width = BaseWidth;
		double AgeMs = 0.0;
		bool bPerfect = false;
	};

	struct FShard
	{
		int32 Id = 0;
		int32 LaneIndex = 0;
		int32 Row = 0;
		double X = 0.0;
		double Width = 0.0;
		double Vx = 0.0;
		double Vy = 0.0;
		double Y = 0.0;
		double Spin = 0.0;
		double TtlMs = 1400.0;
		double DurationMs = 1400.0;
	};

	struct FEffect
	{
		int32 Id = 0;
		FString PlayerId;
		int32 LaneIndex = 0;
		EEffectKind Kind = EEffectKind::Land;
		int32 Row = 0;
		double TtlMs = 0.0;
		double DurationMs = 0.0;
		double Intensity = 1.0;
		int32 ScoreDelta = 0;
		FString Label;
	};

	struct FTower
	{
		FString PlayerId;
		FString Name;
		int32 LaneIndex = 0;
		TArray<FBlock> Blocks;
		double CursorCenter = 0.0;
		int32 CursorDirection = 1;
		int32 Score = 0;
		int32 Combo = 0;
		int32 BestCombo = 0;
		int32 Perfects = 0;
		int32 Drops = 0;
		int32 Lives = StartingLives;
		bool bEliminated = false;
		bool bCompleted = false;
		double LastDropAtMs = -DropCooldownMs;
		FBalance Balance;

		const FBlock& Top() const { return Blocks.Last(); }
		/** Rows on the foundation (row 0 is the foundation). */
		int32 Height() const { return Blocks.Num() - 1; }
		bool IsDone() const { return bEliminated || bCompleted; }
	};

	struct FMatch
	{
		EMatchPhase Phase = EMatchPhase::Countdown;
		double ElapsedMs = 0.0;
		double CountdownRemainingMs = CountdownMs;
		double TimeRemainingMs = RoundDurationMs;
		TArray<FTower> Towers;
		TArray<FEffect> Effects;
		TArray<FShard> Shards;
		TArray<FString> WinnerIds;
		int32 NextId = 1;

		FTower* FindTower(const FString& PlayerId);
		const FTower* FindTower(const FString& PlayerId) const;
	};

	struct FSeed
	{
		FString Id;
		FString Name;
	};

	struct FDropOutcome
	{
		EDropKind Kind = EDropKind::Land;
		int32 ScoreDelta = 0;
		int32 Combo = 0;
		int32 Height = 0;
		double Width = 0.0;
		double Offset = 0.0;
		FString Label;
	};

	MOVECADEKART_API double SweepTrackHalfWidth(double Height);
	MOVECADEKART_API double SweepLimit(double Width, double Height = 0.0);
	MOVECADEKART_API double SweepSpeed(double Height);

	struct FSweepPose
	{
		double Center = 0.0;
		int32 Direction = 1;
	};

	/** A reflecting ping-pong between +-Limit; exactly invertible (a negative dt runs it backwards). */
	MOVECADEKART_API FSweepPose Sweep(double Center, int32 Direction, double Speed, double DtMs, double Limit);
	/** Where the sweeping block was Ms ago. */
	MOVECADEKART_API double RewoundCenter(const FTower& Tower, double Ms);

	MOVECADEKART_API FMatch CreateMatch(const TArray<FSeed>& Players);
	MOVECADEKART_API void StepMatch(FMatch& Match, double DtMs);
	/** The DROP button: unset when it isn't allowed (not playing, done, balance missing or recovering, cooldown). */
	MOVECADEKART_API TOptional<FDropOutcome> DropBlock(FMatch& Match, const FString& PlayerId, double RewindMs = 0.0);
	MOVECADEKART_API TArray<const FTower*> Leaderboard(const FMatch& Match);

	/** JS Math.round: halves round up. */
	inline double JsRound(double X) { return FMath::FloorToDouble(X + 0.5); }
}
