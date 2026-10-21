#pragma once

#include "CoreMinimal.h"

/**
 * Bullseye Bash's rules (movecade-v2 darts/{board,engine}.ts, Docs/Port/07-darts.md §1-§2): a real clock board,
 * the landing model that turns a throw gesture into a board point (no RNG anywhere), and the match: players share
 * one board, three darts a visit, three rounds each, a called "bounty" sector per round worth double, and round
 * bonuses. Pure and deterministic: no UObjects, no clock.
 */
namespace MovecadeKart::Darts
{
	// Board (board.ts) ----------------------------------------------------------------------------------------

	/** Sector values clockwise from 12 o'clock. */
	constexpr int32 SectorCount = 20;
	MOVECADEKART_API const int32* Sectors();
	constexpr double SectorArc = UE_DOUBLE_PI * 2.0 / SectorCount;

	/** Radii in units of the double ring's outer edge (mm / 170). */
	constexpr double BullRadius = 6.35 / 170.0;
	constexpr double OuterBullRadius = 15.9 / 170.0;
	constexpr double TripleInnerRadius = 99.0 / 170.0;
	constexpr double TripleOuterRadius = 107.0 / 170.0;
	constexpr double DoubleInnerRadius = 162.0 / 170.0;
	constexpr double DoubleOuterRadius = 1.0;
	/** Darts stick in the surround out to here (they score nothing past the double ring). */
	constexpr double CatchRadius = 1.32;

	enum class ERing : uint8
	{
		Bull,
		OuterBull,
		Triple,
		Double,
		Single,
		Miss,
	};

	struct FBoardHit
	{
		int32 Score = 0;
		ERing Ring = ERing::Miss;
		/** The sector struck; 0 in the bulls and on a miss. */
		int32 Sector = 0;
		/** 0 on a miss and in the bulls, else 1/2/3. */
		int32 Multiplier = 0;
		double Radius = TNumericLimits<double>::Max();
		/** "T20", "D16", "Bull", "Bullseye", "Miss". */
		FString Label = TEXT("Miss");
	};

	/** Which sector (0..19, board order) a point lies in; +x right, +y up. */
	MOVECADEKART_API int32 SectorIndexAt(double X, double Y);
	/** A sector's centre angle, in the same maths convention. */
	MOVECADEKART_API double SectorCenterAngle(int32 Index);
	MOVECADEKART_API FBoardHit ScoreAt(double X, double Y);

	// Rules (engine.ts) ---------------------------------------------------------------------------------------

	constexpr int32 DartsPerRound = 3;
	constexpr int32 RoundCount = 3;
	constexpr int32 DartsPerPlayer = DartsPerRound * RoundCount;
	constexpr double CountdownMs = 3000.0;
	constexpr double MatchDurationMs = 180000.0;
	constexpr double FlightMs = 620.0;
	constexpr double CalloutMs = 1100.0;
	constexpr double AimReach = 1.12;
	constexpr double SweetSpot = 0.55;
	constexpr int32 BountyMultiplier = 2;
	constexpr int32 TidyRoundBonus = 20;
	constexpr int32 TonThreshold = 100;
	constexpr int32 TonBonus = 40;
	constexpr int32 MaxEffects = 20;

	MOVECADEKART_API double MatchDurationForPlayers(int32 PlayerCount);
	/** The called shot: 19, 16, 20 by round (19 for a negative round). */
	MOVECADEKART_API int32 BountyForRound(int32 Round);

	/** What a throw is (throw.ts ThrowGesture): the phone's detector, or local inputs, produce one. */
	struct FDartGesture
	{
		/** The aim before the arm moved, -1..1 each way. */
		double AimX = 0.0;
		double AimY = 0.0;
		/** 0..1; SweetSpot lands on the aim point. */
		double Power = SweetSpot;
		/** Twist through the release, deg/s; positive pulls right. */
		double SpinDegPerSec = 0.0;
		double RollDegPerSec = 0.0;
		double Confidence = 1.0;
		/** 0..1 how still the aim was; an unsteady hand carries the dart along its sweep. */
		double Steadiness = 1.0;
		double PeakMs2 = 20.0;
		double DurationMs = 120.0;
	};

	/** Where a gesture puts the dart, in board radii (never beyond CatchRadius). */
	MOVECADEKART_API FVector2D LandingPoint(const FDartGesture& Gesture);

	enum class EMatchPhase : uint8
	{
		Countdown,
		Playing,
		Ended,
	};

	enum class EEffectKind : uint8
	{
		Hit,
		Bull,
		Bounty,
		Miss,
		Round,
		Finished,
	};

	struct FDartLanding
	{
		int32 Id = 0;
		FString PlayerId;
		int32 LaneIndex = 0;
		/** Board radii, +x right, +y up. */
		double X = 0.0;
		double Y = 0.0;
		FBoardHit Hit;
		/** Points credited, bounty included. */
		int32 ScoreDelta = 0;
		bool bBounty = false;
		int32 Round = 0;
		/** 0-based within the round. */
		int32 DartIndex = 0;
		/** Since it stuck (not while its flight is in the air), capped near 2000. */
		double AgeMs = 0.0;
	};

	struct FDartFlight
	{
		int32 Id = 0;
		FString PlayerId;
		int32 LaneIndex = 0;
		double X = 0.0;
		double Y = 0.0;
		/** 0..1 through the 620 ms flight. */
		double Progress = 0.0;
		double Power = 0.0;
		/** Cosmetic lateral curve from the twist, -1..1. */
		double Curve = 0.0;
		FDartLanding Landing;
	};

	struct FDartsEffect
	{
		int32 Id = 0;
		FString PlayerId;
		int32 LaneIndex = 0;
		EEffectKind Kind = EEffectKind::Hit;
		double TtlMs = 0.0;
		double DurationMs = 0.0;
		double Intensity = 1.0;
		int32 ScoreDelta = 0;
		FString Label;
		double X = 0.0;
		double Y = 0.0;
	};

	struct FDartsPlayer
	{
		FString Id;
		FString Name;
		int32 LaneIndex = 0;
		int32 Score = 0;
		int32 DartsThrown = 0;
		/** 0-based; RoundCount once finished. */
		int32 Round = 0;
		int32 RoundScore = 0;
		int32 RoundHits = 0;
		int32 BestRound = 0;
		int32 Bulls = 0;
		int32 Trebles = 0;
		int32 Bounties = 0;
		TArray<FDartLanding> Landings;
		bool bFinished = false;
	};

	struct FDartsMatch
	{
		EMatchPhase Phase = EMatchPhase::Countdown;
		double ElapsedMs = 0.0;
		double CountdownRemainingMs = CountdownMs;
		double TimeRemainingMs = MatchDurationMs;
		TArray<FDartsPlayer> Players;
		TArray<FDartFlight> Flights;
		TArray<FDartsEffect> Effects;
		TArray<FString> WinnerIds;
		int32 NextId = 1;
		/** Whose three-dart visit owns the board. */
		int32 CurrentPlayerIndex = 0;
		/** Darts released this visit; reset only once its flights have landed. */
		int32 TurnDarts = 0;

		FDartsPlayer* FindPlayer(const FString& PlayerId);
		const FDartsPlayer* FindPlayer(const FString& PlayerId) const;
		/** The player who may throw now (still them while their third dart settles), or null once ended. */
		const FDartsPlayer* ActivePlayer() const;
	};

	struct FMatchSeed
	{
		FString Id;
		FString Name;
	};

	MOVECADEKART_API FDartsMatch CreateMatch(const TArray<FMatchSeed>& Players);

	MOVECADEKART_API int32 DartsLeftInRound(const FDartsPlayer& Player);

	/** Advances the match (dt clamped to [0, 100] ms): countdown, clock, effects, flights, turn passing, the end. */
	MOVECADEKART_API void StepMatch(FDartsMatch& Match, double DtMs);

	struct FThrowOutcome
	{
		FDartLanding Landing;
		/** Bonuses paid when this dart closed a round. */
		int32 RoundBonus = 0;
		bool bRoundComplete = false;
		bool bFinished = false;
		/** "Miss", "Bounty T19!" or the hit's label. */
		FString Label;
	};

	/** One throw from the active player: the only place a score moves. Unset when it isn't allowed. */
	MOVECADEKART_API TOptional<FThrowOutcome> ThrowDart(FDartsMatch& Match, const FString& PlayerId, const FDartGesture& Gesture);

	/** Players by score, best first (stable). */
	MOVECADEKART_API TArray<const FDartsPlayer*> Leaderboard(const FDartsMatch& Match);
}
