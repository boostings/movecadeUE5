#include "Race/KartBotDriver.h"

namespace MovecadeKart
{
	FVector2D FKartBotDriver::LookAhead(const FVector2D& Position, double Yaw, const TArray<FVector2D>& Path, double Distance)
	{
		const FVector2D Facing(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
		const int32 N = Path.Num();
		double Best = TNumericLimits<double>::Max();
		int32 Segment = 0;
		double Along = 0.0;
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
				Segment = i;
				Along = T;
			}
		}
		FVector2D P = Path[Segment] + (Path[(Segment + 1) % N] - Path[Segment]) * Along;
		double Remaining = Distance;
		for (int32 Step = 0; Step <= N; ++Step)
		{
			const FVector2D Next = Path[(Segment + 1 + Step) % N];
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

	double FKartBotDriver::TurnDemand(const FVector2D& Position, double Yaw, const TArray<FVector2D>& Path, double LookAheadDistance, double Gain)
	{
		if (Path.Num() < 2)
		{
			return 0.0;
		}
		const FVector2D Target = LookAhead(Position, Yaw, Path, LookAheadDistance);
		double Error = FMath::Atan2(-(Target.X - Position.X), -(Target.Y - Position.Y)) - Yaw;
		while (Error > UE_DOUBLE_PI) Error -= UE_DOUBLE_TWO_PI;
		while (Error < -UE_DOUBLE_PI) Error += UE_DOUBLE_TWO_PI;
		const double Limit = 1.0 + DriftTurn;
		return FMath::Clamp(Error * Gain, -Limit, Limit);
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

	FKartInput FKartBotDriver::Update(const FKartState& State, const TArray<FVector2D>& Path, bool bRacing, double Now)
	{
		if (Now < NextTick)
		{
			return Held;
		}
		NextTick = Now + Tuning.TickSeconds;
		++Ticks;

		double WheelSteering = 0.0;
		int32 WheelDrift = 0;
		bool bGas = bRacing;
		if (bRacing)
		{
			const double Demand = TurnDemand(State.Pos, State.Yaw, Path, Tuning.LookAheadDistance, Tuning.Gain);
			Wheel(Demand, Drift, WheelSteering, WheelDrift, Tuning.DriftEnter);
			// A cautious bot lifts in the tight bends, unless it's drifting (a drift needs the gas).
			const bool bLift = Tuning.LiftDemand > 0.0 && WheelDrift == 0 && FMath::Abs(Demand) > Tuning.LiftDemand && FMath::Abs(State.Speed) > 22.0;
			const bool bCoast = Tuning.CoastEvery > 0 && WheelDrift == 0 && Ticks % Tuning.CoastEvery == 0 && FMath::Abs(State.Speed) > 28.0;
			bGas = !bLift && !bCoast;
		}
		if (bGas != bAccelerating || WheelDrift != Drift || FMath::Abs(WheelSteering - Steering) > 0.04 || Now - SentAt >= RefreshSeconds)
		{
			Steering = WheelSteering;
			Drift = WheelDrift;
			bAccelerating = bGas;
			SentAt = Now;
			// kart-input {isAccelerating, isDrifting, steeringInput} (legacyBridge.ts syncKartInput).
			Held = FKartInput();
			Held.bForward = bAccelerating;
			Held.bJump = Drift != 0;
			Held.SteeringX = Steering;
		}
		return Held;
	}
}
