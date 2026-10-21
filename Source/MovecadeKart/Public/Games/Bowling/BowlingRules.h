#pragma once

#include "CoreMinimal.h"

/**
 * Bowling's rules (movecade-v2 bowling/engine.ts, Docs/Port/06-bowling.md §1): ten-pin scoring for 1-4 players
 * who take a frame each in turn. Pure and deterministic: no UObjects, no clock.
 */
namespace MovecadeKart::Bowling
{
	constexpr int32 Frames = 10;
	constexpr int32 Pins = 10;

	enum class EFrameKind : uint8
	{
		None,
		Open,
		Spare,
		Strike,
	};

	struct FFrameScore
	{
		int32 Number = 1;
		TArray<int32> Rolls;
		EFrameKind Kind = EFrameKind::None;
		/** The running total through this frame; unset while its bonus balls are still to come (and after). */
		TOptional<int32> Cumulative;
		bool bComplete = false;
	};

	struct FCard
	{
		FString PlayerId;
		TArray<int32> Rolls;
		TArray<FFrameScore> FrameList;
		int32 Score = 0;
		bool bDone = false;
	};

	/** Frames, kinds and running totals from a flat list of rolls (engine.ts score()). */
	MOVECADEKART_API FCard ScoreRolls(const FString& PlayerId, const TArray<int32>& Rolls);

	struct FMatch
	{
		/** Turn order, fixed at kickoff. */
		TArray<FString> Order;
		TArray<FCard> Cards;
		/** Index into Order of the bowler up, or -1 when the match is over. */
		int32 TurnIndex = 0;
		int32 Standing = Pins;
		/** 1, 2 or 3 (the 10th frame's fill ball). */
		int32 RollInFrame = 1;
		bool bComplete = false;

		const FCard* CardOf(const FString& PlayerId) const;
		const FString* Bowler() const { return Order.IsValidIndex(TurnIndex) ? &Order[TurnIndex] : nullptr; }
	};

	MOVECADEKART_API FMatch CreateMatch(const TArray<FString>& Order);

	struct FRollResult
	{
		/** The pins counted: clamp(floor(PinsDown), 0, standing). */
		int32 Felled = 0;
		bool bTurnEnded = false;
		/** A fresh rack goes up (the turn ended, or the 10th frame cleared the deck). */
		bool bRackReset = false;
	};

	/** The bowler up knocks PinsDown pins (engine.ts roll()). */
	MOVECADEKART_API FRollResult Roll(FMatch& Match, double PinsDown);

	/** The frame the card is in, 1..10. */
	MOVECADEKART_API int32 FrameNumberFor(const FCard& Card);

	/** Player ids by score, highest first; ties keep turn order. */
	MOVECADEKART_API TArray<FString> Standings(const FMatch& Match);

	/** A player leaves. A bystander keeps the bowler up; if it was their turn, the rack resets and play moves on. */
	MOVECADEKART_API void DropPlayer(FMatch& Match, const FString& PlayerId);

	/** The score-sheet marks for a frame ("X", "/", "–", digits), the 10th frame marked ball by ball. */
	MOVECADEKART_API TArray<FString> FrameMarks(const FFrameScore& Frame);
}
