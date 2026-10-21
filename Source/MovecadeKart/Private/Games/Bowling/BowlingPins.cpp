#include "Games/Bowling/BowlingPins.h"

namespace MovecadeKart::Bowling
{
	namespace
	{
		constexpr double FixedStepSeconds = 1.0 / 120.0;
		/** W7: a hitch drops time rather than replaying a second of pin physics in one frame. */
		constexpr double MaxFrameStep = 0.2;
		constexpr double Gravity = 9.81 * VisualUnitsPerMetre;
		constexpr double HalfHeight = VisualPinHeight / 2.0;
		constexpr double StandingY = HalfHeight;
		constexpr double FallenY = VisualDeckY + VisualPinRadius * 1.08;
		constexpr double BaseRadius = VisualPinRadius * 0.426;
		constexpr double BellyBelowCentre = HalfHeight * (1.0 - 2.0 * 0.3);
		constexpr double TipAngle = 0.17;
		constexpr double DownAngle = UE_DOUBLE_HALF_PI;
		constexpr double MaxSurvivorLean = 0.085;
		constexpr double AssistAfter = 0.9;

		double SupportHeight(double Lean)
		{
			const double Up = FMath::Cos(Lean), Out = FMath::Sin(Lean);
			return FMath::Max3(FallenY, HalfHeight * Up + BaseRadius * Out, BellyBelowCentre * Up + VisualPinRadius * Out);
		}

		double StableJitter(double Seed)
		{
			return FMath::Sin(Seed * 12.9898 + 78.233) * 0.5 + 0.5;
		}
	}

	FPinSolver::FPinSolver()
	{
		TArray<int32> All;
		for (int32 Id = 1; Id <= 10; ++Id)
		{
			All.Add(Id);
		}
		Reset(nullptr, All);
	}

	void FPinSolver::Reset(const FShotResult* Shot, const TArray<int32>& Standing)
	{
		ElapsedSeconds = 0.0;
		Accumulator = 0.0;
		bBallDriven = false;
		bArmed = false;
		ArmedAt = 0.0;
		bHasPrevBall = false;
		Bodies.SetNum(10);
		for (int32 i = 0; i < 10; ++i)
		{
			const int32 Id = i + 1;
			FBody& B = Bodies[i];
			B = FBody();
			B.Id = Id;
			const FVector2D P = PinPosition(Id);
			B.BaseX = P.X * VisualUnitsPerMetre;
			B.BaseZ = P.Y * VisualUnitsPerMetre;
			B.X = B.BaseX;
			B.Z = B.BaseZ;
			B.Y = StandingY;
			B.Jitter = StableJitter(Id * 19.0);
			B.ScatterSign = Id % 2 == 0 ? 1 : -1;
			B.bVisible = Standing.Contains(Id);
			B.bFelled = Shot && Shot->Felled.Contains(Id);
			if (!Shot || !B.bVisible)
			{
				continue;
			}
			B.Energy = B.bFelled ? 0.78 : 0.0;
			if (const FPinImpact* Impact = Shot->Impacts.FindByPredicate([Id](const FPinImpact& I) { return I.Id == Id; }))
			{
				B.Energy = Impact->Energy;
				B.ScatterSign = Impact->ScatterSign;
			}
			if (B.bFelled)
			{
				B.FallbackAt = 0.02 + B.BaseZ * 0.045 + B.Jitter * 0.035;
			}
		}
		Publish();
	}

	void FPinSolver::Strike(FBody& Body, double NX, double NZ, double Strength) const
	{
		Body.bStruck = true;
		const double Push = FMath::Clamp(Strength, 0.0, 3.0);
		if (!Body.bFelled)
		{
			// A survivor only rocks: the sim says it stays.
			Body.NudgeX += NX * Push * 0.05;
			Body.NudgeZ += NZ * Push * 0.05;
			Body.Wobble = FMath::Min(0.09, Body.Wobble + Push * 0.05);
			return;
		}
		const double Weight = Body.Lean > 0.02 ? FMath::Clamp(Push / (Push + 1.2), 0.0, 1.0) : 1.0;
		const double MX = Body.LeanX * (1.0 - Weight) + NX * Weight;
		const double MZ = Body.LeanZ * (1.0 - Weight) + NZ * Weight;
		const double Len = FMath::Max(FMath::Sqrt(MX * MX + MZ * MZ), 1e-9);
		Body.LeanX = MX / Len;
		Body.LeanZ = MZ / Len;
		Body.LeanRate += Push * 5.6;
		Body.VX += NX * Push * 0.85;
		Body.VZ += NZ * Push * 0.85;
		Body.VY += FMath::Min(0.5, Push * 0.16);
		Body.Spin += (Body.Jitter - 0.5) * Push * 3.4;
	}

	void FPinSolver::BallContact(double FromX, double FromZ, const FVisualBall& Ball)
	{
		if (!bArmed && Ball.Z > -VisualPinHeight)
		{
			bArmed = true;
			ArmedAt = ElapsedSeconds;
		}
		const double TX = Ball.X - FromX, TZ = Ball.Z - FromZ;
		const double TravelSq = TX * TX + TZ * TZ;
		for (FBody& B : Bodies)
		{
			if (!B.bVisible || B.bBallHit || B.Lean >= DownAngle)
			{
				continue;
			}
			double T = 0.0;
			if (TravelSq > 1e-9)
			{
				T = FMath::Clamp(((B.X - FromX) * TX + (B.Z - FromZ) * TZ) / TravelSq, 0.0, 1.0);
			}
			const double DX = B.X - (FromX + TX * T), DZ = B.Z - (FromZ + TZ * T);
			const double Reach = VisualBallRadius + VisualPinRadius + FMath::Sin(B.Lean) * HalfHeight;
			const double DistSq = DX * DX + DZ * DZ;
			if (DistSq >= Reach * Reach)
			{
				continue;
			}
			const double Dist = FMath::Max(FMath::Sqrt(DistSq), 1e-4);
			const double NX = DX / Dist;
			const double NZ = FMath::Clamp(DZ / Dist + 0.55, -1.0, 1.6);
			const double Len = FMath::Max(FMath::Sqrt(NX * NX + NZ * NZ), 1e-9);
			B.bBallHit = true;
			bArmed = true;
			const double Speed = FMath::IsFinite(Ball.Speed) ? FMath::Clamp(Ball.Speed / 8.0, 0.35, 1.6) : 1.0;
			Strike(B, NX / Len, NZ / Len, (0.62 + B.Energy * 0.72) * Speed);
		}
	}

	void FPinSolver::Integrate(double Dt)
	{
		for (FBody& B : Bodies)
		{
			if (!B.bVisible)
			{
				continue;
			}
			B.Age += Dt;
			if (!B.bFelled)
			{
				const double Settle = 1.0 - FMath::Exp(-3.8 * Dt);
				B.X += (B.BaseX + B.NudgeX - B.X) * Settle;
				B.Z += (B.BaseZ + B.NudgeZ - B.Z) * Settle;
				B.NudgeX *= FMath::Exp(-2.4 * Dt);
				B.NudgeZ *= FMath::Exp(-2.4 * Dt);
				B.Wobble *= FMath::Exp(-1.6 * Dt);
				const double Rock = FMath::Min(MaxSurvivorLean, B.Wobble);
				B.RotX = FMath::Sin(ElapsedSeconds * 17.0 + B.Id * 0.7) * Rock;
				B.RotZ = -FMath::Cos(ElapsedSeconds * 14.0 + B.Id) * Rock * 0.8;
				B.RotY *= FMath::Exp(-4.0 * Dt);
				B.Y = StandingY;
				B.Lean = Rock;
				continue;
			}
			if (!B.bStruck)
			{
				if (!bBallDriven && B.Age >= B.FallbackAt)
				{
					const double JX = (B.Jitter - 0.5) * 0.7;
					const double Len = FMath::Sqrt(JX * JX + 1.0);
					Strike(B, JX / Len, 1.0 / Len, 0.7 + B.Energy * 0.5);
				}
				else
				{
					B.Y = StandingY;
					continue;
				}
			}
			if (B.Lean < DownAngle)
			{
				if (B.Lean > TipAngle)
				{
					B.LeanRate += (Gravity / HalfHeight) * FMath::Sin(B.Lean) * 0.13 * Dt;
				}
				B.Lean += B.LeanRate * Dt;
				if (B.Lean >= DownAngle)
				{
					B.Lean = DownAngle;
					B.LeanRate = 0.0;
				}
			}
			else
			{
				B.LeanRate = 0.0;
			}
			B.X += B.VX * Dt;
			B.Z += B.VZ * Dt;
			B.VY -= Gravity * Dt;
			B.Y += B.VY * Dt;
			const double Hinged = SupportHeight(B.Lean);
			if (B.Y <= Hinged)
			{
				B.Y = Hinged;
				B.VY = B.VY < -1.0 ? -B.VY * 0.18 : 0.0;
			}
			const bool bGrounded = B.Lean >= DownAngle - 0.02;
			const double Friction = FMath::Exp(-(bGrounded ? 5.2 : 1.6) * Dt);
			B.VX *= Friction;
			B.VZ *= Friction;
			B.Spin *= Friction;
			B.RotY += B.Spin * Dt;
			B.RotX = B.Lean * B.LeanZ;
			B.RotZ = -B.Lean * B.LeanX;
		}
	}

	void FPinSolver::PinContact(double Dt)
	{
		auto Moving = [](const FBody& B) { return B.bStruck && (B.Lean > 0.01 || FMath::Sqrt(B.VX * B.VX + B.VZ * B.VZ) > 0.05); };
		auto Radius = [](const FBody& B) { return VisualPinRadius + FMath::Sin(B.Lean) * HalfHeight; };
		for (int32 a = 0; a < Bodies.Num(); ++a)
		{
			FBody& L = Bodies[a];
			if (!L.bVisible)
			{
				continue;
			}
			for (int32 b = a + 1; b < Bodies.Num(); ++b)
			{
				FBody& R = Bodies[b];
				if (!R.bVisible)
				{
					continue;
				}
				const bool bLM = Moving(L), bRM = Moving(R);
				if (!bLM && !bRM)
				{
					continue;
				}
				const double DX = R.X - L.X, DZ = R.Z - L.Z;
				const double Reach = Radius(L) + Radius(R);
				const double DistSq = DX * DX + DZ * DZ;
				if (DistSq >= Reach * Reach)
				{
					continue;
				}
				const double Dist = FMath::Max(FMath::Sqrt(DistSq), 1e-4);
				const double NX = DX / Dist, NZ = DZ / Dist;
				const double Overlap = Reach - Dist;
				const double LS = FMath::Sqrt(L.VX * L.VX + L.VZ * L.VZ) + L.LeanRate * HalfHeight;
				const double RS = FMath::Sqrt(R.VX * R.VX + R.VZ * R.VZ) + R.LeanRate * HalfHeight;
				const double Transfer = FMath::Clamp((LS + RS) * 0.4 + Overlap * 2.2, 0.0, 3.0) * Dt * 60.0 * 0.2;
				if (bLM)
				{
					Strike(R, NX, NZ, Transfer);
				}
				if (bRM)
				{
					Strike(L, -NX, -NZ, Transfer);
				}
				if (L.bFelled && L.bStruck)
				{
					L.X -= NX * Overlap * 0.22;
					L.Z -= NZ * Overlap * 0.22;
				}
				if (R.bFelled && R.bStruck)
				{
					R.X += NX * Overlap * 0.22;
					R.Z += NZ * Overlap * 0.22;
				}
			}
		}
	}

	void FPinSolver::EnforceAuthority()
	{
		// The sim decided: a pin that must fall and hasn't, gets helped down.
		for (FBody& B : Bodies)
		{
			if (!B.bVisible || !B.bFelled || B.Lean >= DownAngle)
			{
				continue;
			}
			const double AssistAt = AssistAfter + B.BaseZ * 0.035 + B.Jitter * 0.14;
			if (!bArmed || ElapsedSeconds - ArmedAt < AssistAt)
			{
				continue;
			}
			if (!B.bStruck)
			{
				const double JX = (B.Jitter - 0.5) * 0.7;
				const double Len = FMath::Sqrt(JX * JX + 1.0);
				Strike(B, JX / Len, 1.0 / Len, 0.9);
			}
			B.LeanRate = FMath::Max(B.LeanRate, 3.4);
		}
	}

	void FPinSolver::FixedStep(double Dt)
	{
		ElapsedSeconds += Dt;
		if (!bBallDriven && !bArmed)
		{
			bArmed = true;
		}
		Integrate(Dt);
		PinContact(Dt);
		EnforceAuthority();
	}

	void FPinSolver::Step(double Dt, const FVisualBall* Ball)
	{
		if (Ball)
		{
			bBallDriven = true;
			BallContact(bHasPrevBall ? PrevBallX : Ball->X, bHasPrevBall ? PrevBallZ : Ball->Z, *Ball);
			PrevBallX = Ball->X;
			PrevBallZ = Ball->Z;
			bHasPrevBall = true;
		}
		else
		{
			bHasPrevBall = false;
		}
		Accumulator += FMath::Min(FMath::Max(0.0, Dt), MaxFrameStep);
		while (Accumulator >= FixedStepSeconds)
		{
			FixedStep(FixedStepSeconds);
			Accumulator -= FixedStepSeconds;
		}
		Publish();
	}

	void FPinSolver::Publish()
	{
		Poses_.SetNum(Bodies.Num());
		for (int32 i = 0; i < Bodies.Num(); ++i)
		{
			const FBody& B = Bodies[i];
			FPinPose& P = Poses_[i];
			P.Id = B.Id;
			P.Position = FVector(B.X, B.Y, B.Z);
			// RotX tips the top toward +z, -RotZ toward +x (three.js Euler about the rack axes).
			P.Tilt = FVector2D(-B.RotZ, B.RotX);
			P.Spin = B.RotY;
			P.bVisible = B.bVisible;
			P.bToppled = B.bFelled && B.Lean > TipAngle;
			P.OffSpot = FMath::Sqrt(FMath::Square(B.X - B.BaseX) + FMath::Square(B.Z - B.BaseZ));
		}
	}
}
