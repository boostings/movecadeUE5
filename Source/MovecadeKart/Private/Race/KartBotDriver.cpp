#include "Race/KartBotDriver.h"

namespace MovecadeKart
{
	FKartBotDriver::FPathCursor FKartBotDriver::Locate(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path)
	{
		const FVector2D Facing(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
		const int32 N = Path.Num();
		double Best = TNumericLimits<double>::Max();
		FPathCursor Cursor;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D A = Path[i];
			const FVector2D D = Path[(i + 1) % N] - A;
			double Length = D.Size();
			if (Length == 0.0)
			{
				Length = 1.0;
			}
			const double T = FMath::Clamp(FVector2D::DotProduct(Position - A, D) / (Length * Length), 0.0, 1.0);
			const double Cost = FVector2D::Distance(A + D * T, Position) + AlignmentCost * (1.0 - FVector2D::DotProduct(Facing, D) / Length);
			if (Cost < Best)
			{
				Best = Cost;
				Cursor.Segment = i;
				Cursor.Along = T;
			}
		}
		return Cursor;
	}

	FVector2D FKartBotDriver::Advance(TConstArrayView<FVector2D> Path, const FPathCursor& Cursor, double Distance)
	{
		const int32 N = Path.Num();
		FVector2D P = Path[Cursor.Segment] + (Path[(Cursor.Segment + 1) % N] - Path[Cursor.Segment]) * Cursor.Along;
		double Remaining = Distance;
		for (int32 Step = 0; Step <= N; ++Step)
		{
			const FVector2D Next = Path[(Cursor.Segment + 1 + Step) % N];
			const double Gap = FVector2D::Distance(Next, P);
			if (Gap >= Remaining)
			{
				return P + (Next - P) * (Remaining / Gap);
			}
			Remaining -= Gap;
			P = Next;
		}
		return P;
	}

	FVector2D FKartBotDriver::LookAhead(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, double Distance)
	{
		return Advance(Path, Locate(Position, Yaw, Path), Distance);
	}

	double FKartBotDriver::TurnDemand(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, double LookAheadDistance, double Gain)
	{
		if (Path.Num() < 2)
		{
			return 0.0;
		}
		return TurnDemandTo(Position, Yaw, LookAhead(Position, Yaw, Path, LookAheadDistance), Gain);
	}

	double FKartBotDriver::TurnDemandTo(const FVector2D& Position, double Yaw, const FVector2D& Target, double Gain)
	{
		double Error = FMath::Atan2(-(Target.X - Position.X), -(Target.Y - Position.Y)) - Yaw;
		while (Error > UE_DOUBLE_PI) Error -= UE_DOUBLE_TWO_PI;
		while (Error < -UE_DOUBLE_PI) Error += UE_DOUBLE_TWO_PI;
		const double Limit = 1.0 + DriftTurn;
		return FMath::Clamp(Error * Gain, -Limit, Limit);
	}

	bool FKartBotDriver::PickBox(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, TConstArrayView<FVector2D> Boxes, FVector2D& OutBox)
	{
		return Path.Num() >= 2 && PickBox(Position, Yaw, Path, Locate(Position, Yaw, Path), Boxes, OutBox);
	}

	bool FKartBotDriver::PickBox(const FVector2D& Position, double Yaw, TConstArrayView<FVector2D> Path, const FPathCursor& Cursor,
		TConstArrayView<FVector2D> Boxes, FVector2D& OutBox)
	{
		const FVector2D Facing(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
		double BestOffset = 7.0;
		bool bFound = false;
		for (const FVector2D& Box : Boxes)
		{
			const double Ahead = FVector2D::DotProduct(Box - Position, Facing);
			if (Ahead < 4.0 || Ahead > 28.0)
			{
				continue;
			}
			// How far the box sits from where the racing line will be at that distance.
			const double Offset = FVector2D::Distance(Box, Advance(Path, Cursor, Ahead));
			if (Offset < BestOffset)
			{
				BestOffset = Offset;
				OutBox = Box;
				bFound = true;
			}
		}
		return bFound;
	}

	void FKartBotDriver::Wheel(double Demand, int32 HeldDrift, double& OutSteering, int32& OutDrift, double DriftEnter)
	{
		if (HeldDrift == 0 && FMath::Abs(Demand) > DriftEnter)
		{
			const int32 Side = Demand > 0.0 ? 1 : -1;
			OutSteering = Side;
			OutDrift = Side;
			return;
		}
		const int32 Holding = HeldDrift != 0 && Demand * HeldDrift >= DriftExit ? HeldDrift : 0;
		OutSteering = FMath::Clamp(Demand - DriftTurn * Holding, -1.0, 1.0);
		OutDrift = Holding;
	}

	FKartInput FKartBotDriver::Update(const FKartState& State, TConstArrayView<FVector2D> Path, bool bRacing, double Now, TConstArrayView<FVector2D> Boxes)
	{
		const int64 Step = FMath::RoundToInt64(Now / FKartSim::StepSeconds);
		if (Step < NextTickStep)
		{
			return Held;
		}
		NextTickStep = Step + FMath::Max<int64>(1, FMath::RoundToInt64(Tuning.TickSeconds / FKartSim::StepSeconds));
		++Ticks;

		double WheelSteering = 0.0;
		int32 WheelDrift = 0;
		bool bGas = bRacing;
		if (bRacing)
		{
			// Locate once: the box check and the line both start from the same point on the path.
			double Demand = 0.0;
			if (Path.Num() >= 2)
			{
				const FPathCursor Cursor = Locate(State.Pos, State.Yaw, Path);
				FVector2D Box;
				const FVector2D Target = Tuning.bSeekItems && PickBox(State.Pos, State.Yaw, Path, Cursor, Boxes, Box)
					? Box
					: Advance(Path, Cursor, Tuning.LookAheadDistance);
				Demand = TurnDemandTo(State.Pos, State.Yaw, Target, Tuning.Gain);
			}
			Wheel(Demand, Drift, WheelSteering, WheelDrift, Tuning.DriftEnter);
			// A cautious bot lifts in the tight bends, unless it's drifting (a drift needs the gas).
			const bool bLift = Tuning.LiftDemand > 0.0 && WheelDrift == 0 && FMath::Abs(Demand) > Tuning.LiftDemand && FMath::Abs(State.Speed) > 22.0;
			const bool bCoast = Tuning.CoastEvery > 0 && WheelDrift == 0 && Ticks % Tuning.CoastEvery == 0 && FMath::Abs(State.Speed) > 28.0;
			bGas = !bLift && !bCoast;
		}
		if (bGas != bAccelerating || WheelDrift != Drift || FMath::Abs(WheelSteering - Steering) > 0.04 || Step - SentAtStep >= FMath::RoundToInt64(RefreshSeconds / FKartSim::StepSeconds))
		{
			Steering = WheelSteering;
			Drift = WheelDrift;
			bAccelerating = bGas;
			SentAtStep = Step;
			// kart-input {isAccelerating, isDrifting, steeringInput} (legacyBridge.ts syncKartInput).
			Held = FKartInput();
			Held.bForward = bAccelerating;
			Held.bJump = Drift != 0;
			Held.SteeringX = Steering;
		}
		return Held;
	}
}
