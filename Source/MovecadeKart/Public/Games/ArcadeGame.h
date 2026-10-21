#pragma once

#include "CoreMinimal.h"

/** One seat's line in a finished game, for the party table. */
struct FArcadeStanding
{
	FString Name;
	int32 Score = 0;
	/** 1-based; ties share a place. */
	int32 Place = 1;
	FLinearColor Color = FLinearColor::White;
	/** A person (local or phone), not a house bot. */
	bool bHuman = true;
};

/**
 * What the shared arcade layer (SArcadeOverlay: pause, results, play again, next game) needs from a party game's
 * actor. Each game keeps its own rules, HUD and standings card; this is only the loop around it.
 */
class IArcadeGame
{
public:
	virtual ~IArcadeGame() = default;

	/** The catalog id ("bowling", ...). */
	virtual FString GetArcadeId() const = 0;
	/** Seconds since the final standings came up, or < 0 while the game runs. */
	virtual double GetArcadeFinishedAge() const = 0;
	/** The local pause (the room has its own). */
	virtual void SetArcadePaused(bool bPaused) = 0;
	virtual bool IsArcadePaused() const = 0;
	/** Starts the same game over with the same seats. */
	virtual void ArcadeRestart() = 0;
	virtual void GetArcadeStandings(TArray<FArcadeStanding>& Out) const = 0;
	/** Makes sure the room has the final scores (a game that holds them back for a showcase posts them now). */
	virtual void ArcadeCommitResults() {}

	/** Places from scores (higher is better), ties sharing a place. */
	static void RankByScore(TArray<FArcadeStanding>& Rows)
	{
		Rows.StableSort([](const FArcadeStanding& A, const FArcadeStanding& B) { return A.Score > B.Score; });
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			Rows[i].Place = i > 0 && Rows[i].Score == Rows[i - 1].Score ? Rows[i - 1].Place : i + 1;
		}
	}
};
