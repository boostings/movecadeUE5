#pragma once

#include "CoreMinimal.h"
#include "Sim/KartSim.h"

namespace MovecadeKart
{
	/**
	 * How a bot drives. Normal is the web's lobby bot exactly; Easy reacts slowly, takes wide lines, lifts in the
	 * tight bends and only drifts the hairpins; Hard reacts twice as fast, holds a tighter line and drifts more
	 * corners for mini-turbos. Rubber-banding (in the sim) applies on top of every level.
	 */
	struct FKartBotTuning
	{
		double TickSeconds = 0.1;
		double LookAheadDistance = 24.0;
		double Gain = 5.0;
		double DriftEnter = 1.4;
		/** Lift off the gas for a tick when the demand is above this (0 = never lift). */
		double LiftDemand = 0.0;
		/** At speed, coast one decision tick in every this many (0 = never): an easy bot doesn't keep its foot down. */
		int32 CoastEvery = 0;
		/** Steer through an item box near the line when empty-handed. The web's bot didn't, so on Violet Loop,
		 *  whose box row sits beside its racing line, bots never had items. */
		bool bSeekItems = true;

		static FKartBotTuning Easy() { return { 0.2, 30.0, 4.0, 2.2, 1.1, 6, false }; }
		static FKartBotTuning Normal() { return {}; }
		static FKartBotTuning Hard() { return { 0.05, 26.0, 5.5, 1.2, 0.0, 0, true }; }
	};

	/**
	 * A lobby bot at the wheel, ported from movecade-v2 app/src/host/bots/kartPilot.ts. It looks 24 m
	 * down the racing line, steers with gain 5, drifts through bends tighter than full lock, and
	 * re-decides every 100 ms, holding its last input in between (send on change, refresh every 1 s).
	 */
	class MOVECADEKART_API FKartBotDriver
	{
	public:
		static constexpr double AlignmentCost = 20.0;
		static constexpr double DriftTurn = 1.4;
		static constexpr double DriftExit = 0.9;
		static constexpr double RefreshSeconds = 1.0;

		/** The input the bot holds at time Now (seconds). Boxes: the item boxes this kart could collect right now. */
		FKartInput Update(const FKartState& State, TConstArrayView<FVector2D> Path, bool bRacing, double Now, TConstArrayView<FVector2D> Boxes = {});
		/** Back to the grid state; the tuning stays. */
		void Reset() { const FKartBotTuning Keep = Tuning; *this = FKartBotDriver(); Tuning = Keep; }

		FKartBotTuning Tuning;

		/** Where a kart sits on the closed line: the alignment-weighted nearest segment and how far along it. */
		struct FPathCursor
		{
			int32 Segment = 0;
			double Along = 0.0;
		};
		/** Finds the kart on the line (O(path) with a sqrt per segment), once per decision. Path needs 2+ points. */
		[[nodiscard]] static FPathCursor Locate(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path);
		/** The point Distance further along the closed line from Cursor. */
		[[nodiscard]] static FVector2D Advance(TConstArrayView<FVector2D> Path, const FPathCursor& Cursor, double Distance);
		/** The point Distance further along the closed line from where the kart is (alignment-weighted). */
		[[nodiscard]] static FVector2D LookAhead(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, double Distance);
		/** Steering units needed to hold the line: +-1 is full lock; up to +-2.4 needs a drift. */
		[[nodiscard]] static double TurnDemand(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, double LookAheadDistance = 24.0, double Gain = 5.0);
		/** Steering units to point at Target. */
		[[nodiscard]] static double TurnDemandTo(const FVector2D& Position, double Yaw, const FVector2D& Target, double Gain = 5.0);
		/** The box worth a detour: 4-28 m ahead and within 7 m of the racing line there. False if none. */
		[[nodiscard]] static bool PickBox(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, TConstArrayView<FVector2D> Boxes, FVector2D& OutBox);
		/** PickBox with the kart already located on Path (which has 2+ points). */
		[[nodiscard]] static bool PickBox(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, const FPathCursor& Cursor,
			TConstArrayView<FVector2D> Boxes, FVector2D& OutBox);
		/** kartWheel: the wheel and drift side (-1, 0, 1) for a demand, given the drift held. */
		static void Wheel(double Demand, int32 HeldDrift, double& OutSteering, int32& OutDrift, double DriftEnter = 1.4);

	private:
		/** Decisions run on whole sim steps (Now * 60), so a 100 ms cadence is exactly 6 steps every time. */
		int64 NextTickStep = 0;
		int64 SentAtStep = -1000000;
		double Steering = 0.0;
		int32 Drift = 0;
		int32 Ticks = 0;
		bool bAccelerating = false;
		FKartInput Held;
	};
}
