#pragma once

#include "CoreMinimal.h"
#include "Sim/KartSim.h"

/**
 * Race order and what each kart sees of the others, as pure functions of the karts' state. AKartRaceManager ranks
 * and builds contexts with these every step, and the determinism tests call the same code, so the ranked branches
 * of the sim (slipstream, rubber-banding) are exercised exactly as they are in a race.
 */
namespace MovecadeKart
{
	/** What the standings need from one racer. Entry i is seat i. */
	struct FKartStanding
	{
		FVector2D Pos = FVector2D::ZeroVector;
		int32 LapsCompleted = 0;
		double ProgressPercent = 0.0;
		bool bFinished = false;
		double FinishMs = 0.0;
		/** 1 = leader. Written by RankRacers. */
		int32 Place = 1;
	};

	/** laps * 100 + lap progress %: what unfinished karts are ranked and rubber-banded on. */
	[[nodiscard]] inline double ProgressScore(int32 LapsCompleted, double ProgressPercent)
	{
		return LapsCompleted * 100.0 + FMath::Clamp(ProgressPercent, 0.0, 99.9);
	}

	[[nodiscard]] inline double ProgressScore(const FKartStanding& Racer)
	{
		return ProgressScore(Racer.LapsCompleted, Racer.ProgressPercent);
	}

	/** Finished karts by finish time first, then laps and lap progress (RaceUI rankKarts). Order gets the seats in
	 *  race order; each racer's Place is set. Stable, so ties keep seat order. */
	inline void RankRacers(TArrayView<FKartStanding> Racers, TArray<int32>& Order)
	{
		Order.Reset();
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			Order.Add(Seat);
		}
		Order.StableSort([Racers](int32 A, int32 B)
		{
			const FKartStanding& RA = Racers[A];
			const FKartStanding& RB = Racers[B];
			if (RA.bFinished != RB.bFinished)
			{
				return RA.bFinished;
			}
			if (RA.bFinished)
			{
				return RA.FinishMs < RB.FinishMs;
			}
			return ProgressScore(RA) > ProgressScore(RB);
		});
		for (int32 i = 0; i < Order.Num(); ++i)
		{
			Racers[Order[i]].Place = i + 1;
		}
	}

	/** What seat Self sees this step: its place, its gap to the leader, everyone's position, and the nearest kart
	 *  ahead in race order (store.ts getNearestPlayerAhead). Racers must already be ranked. */
	[[nodiscard]] inline FKartRaceContext MakeRaceContext(TConstArrayView<FKartStanding> Racers, int32 Self, bool bRacing, bool bKartContact)
	{
		const FKartStanding& Me = Racers[Self];
		FKartRaceContext C;
		C.PlayerCount = Racers.Num();
		C.RacePosition = Me.Place;
		C.bRacing = bRacing;
		C.SelfSeat = Self;
		C.bKartContact = bKartContact;

		double LeaderScore = 0.0;
		for (const FKartStanding& Other : Racers)
		{
			LeaderScore = FMath::Max(LeaderScore, ProgressScore(Other));
		}
		C.LeaderGapRatio = FMath::Min(1.0, FMath::Max(0.0, LeaderScore - ProgressScore(Me)) / 140.0);

		double NearestAhead = TNumericLimits<double>::Max();
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			if (Seat == Self)
			{
				continue;
			}
			const FKartStanding& Other = Racers[Seat];
			C.Others.Add({ Seat, Other.Pos });
			const double Distance = FVector2D::Distance(Me.Pos, Other.Pos);
			if (Other.Place < Me.Place && Distance < NearestAhead)
			{
				NearestAhead = Distance;
				C.bHasAhead = true;
				C.AheadPosition = Other.Pos;
			}
		}
		return C;
	}
}
