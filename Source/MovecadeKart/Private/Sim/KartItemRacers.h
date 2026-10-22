#pragma once

#include "CoreMinimal.h"
#include "Sim/KartItemTuning.h"
#include "Sim/KartItems.h"

/** Race-order queries over the item world's racers (store.ts), shared by the item phases. */
namespace MovecadeKart::ItemRacers
{
	using ItemTuning::ScorePerLap;
	using ItemTuning::FinishedScoreBonus;

	/** Race score for item odds: laps * 100 + progress %. */
	inline double RaceScore(const FKartItemRacer& Racer)
	{
		return Racer.Laps * ScorePerLap + Racer.Progress;
	}

	/** RaceScore with finishers always ahead, for ranking. */
	inline double RankScore(const FKartItemRacer& Racer)
	{
		return (Racer.bFinished ? FinishedScoreBonus : 0.0) + Racer.Laps * ScorePerLap + Racer.Progress;
	}

	/** store.ts ordering: more laps, or the same laps and more progress. */
	inline bool IsAheadOf(const FKartItemRacer& Racer, const FKartItemRacer& Other)
	{
		return Racer.Laps > Other.Laps || (Racer.Laps == Other.Laps && Racer.Progress > Other.Progress);
	}

	inline const FVector2D& PositionOf(const FKartItemRacer& Racer)
	{
		return Racer.Sim->State().Pos;
	}

	/** 1-based race position by RankScore; ties share the better place. */
	inline int32 RacePosition(int32 Seat, const TArray<FKartItemRacer>& Racers)
	{
		int32 Ahead = 0;
		for (int32 Other = 0; Other < Racers.Num(); ++Other)
		{
			Ahead += (Other != Seat && RankScore(Racers[Other]) > RankScore(Racers[Seat])) ? 1 : 0;
		}
		return Ahead + 1;
	}

	/** store.ts getNearestPlayerAhead:1943-1980. INDEX_NONE when nobody is ahead. */
	inline int32 NearestAhead(int32 Seat, const TArray<FKartItemRacer>& Racers)
	{
		int32 Best = INDEX_NONE;
		double BestDistance = TNumericLimits<double>::Max();
		const FKartItemRacer& Me = Racers[Seat];
		for (int32 Other = 0; Other < Racers.Num(); ++Other)
		{
			if (Other == Seat || !IsAheadOf(Racers[Other], Me))
			{
				continue;
			}
			const double Distance = FVector2D::Distance(PositionOf(Racers[Other]), PositionOf(Me));
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = Other;
			}
		}
		return Best;
	}

	/** store.ts getFirstPlacePlayer:1923-1941 (laps, then progress; the lowest seat wins ties). */
	inline int32 Leader(const TArray<FKartItemRacer>& Racers)
	{
		int32 Best = 0;
		for (int32 Seat = 1; Seat < Racers.Num(); ++Seat)
		{
			if (IsAheadOf(Racers[Seat], Racers[Best]))
			{
				Best = Seat;
			}
		}
		return Best;
	}
}
