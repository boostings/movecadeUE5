#pragma once

#include "CoreMinimal.h"
#include "Sim/KartSim.h"

namespace MovecadeKart
{
	/**
	 * A lobby bot at the wheel, ported from movecade-v2 app/src/host/bots/kartPilot.ts. It looks 24 m
	 * down the racing line, steers with gain 5, drifts through bends tighter than full lock, and
	 * re-decides every 100 ms, holding its last input in between (send on change, refresh every 1 s).
	 */
	class MOVECADEKART_API FKartBotDriver
	{
	public:
		static constexpr double LookAheadDistance = 24.0;
		static constexpr double Gain = 5.0;
		static constexpr double AlignmentCost = 20.0;
		static constexpr double DriftTurn = 1.4;
		static constexpr double DriftEnter = 1.4;
		static constexpr double DriftExit = 0.9;
		static constexpr double TickSeconds = 0.1;
		static constexpr double RefreshSeconds = 1.0;

		/** The input the bot holds at time Now (seconds). */
		FKartInput Update(const FKartState& State, const TArray<FVector2D>& Path, bool bRacing, double Now);
		void Reset() { *this = FKartBotDriver(); }

		/** The point Distance further along the closed line from where the kart is (alignment-weighted). */
		static FVector2D LookAhead(const FVector2D& Position, double Yaw, const TArray<FVector2D>& Path, double Distance);
		/** Steering units needed to hold the line: +-1 is full lock; up to +-2.4 needs a drift. */
		static double TurnDemand(const FVector2D& Position, double Yaw, const TArray<FVector2D>& Path);
		/** kartWheel: the wheel and drift side (-1, 0, 1) for a demand, given the drift held. */
		static void Wheel(double Demand, int32 HeldDrift, double& OutSteering, int32& OutDrift);

	private:
		double NextTick = 0.0;
		double SentAt = -1.0e9;
		double Steering = 0.0;
		int32 Drift = 0;
		bool bAccelerating = false;
		FKartInput Held;
	};
}
