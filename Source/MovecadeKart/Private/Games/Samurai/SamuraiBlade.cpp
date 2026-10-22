#include "Games/Samurai/SamuraiBlade.h"

namespace MovecadeKart::Samurai
{
	namespace
	{
		/** Web up (y), not UE's. */
		const FVector WebUp(0.0, 1.0, 0.0);

		// bladeContact.ts: a blade that moved less than this (m) can't cut; contact assistance (m).
		constexpr double MinBladeTravel = 0.0005;
		constexpr double ContactAssist = 0.02;

		// bladeIntent.ts: rates in deg/s, acceleration in m/s^2, times in s, travel in degrees.
		/** Below both, the blade is resting; resting this long ends the sweep. */
		constexpr double QuietRate = 45.0;
		constexpr double QuietAccel = 0.8;
		constexpr double QuietSeconds = 0.08;
		/** A sweep only starts past one of these. */
		constexpr double StartRate = 90.0;
		constexpr double StartAccel = 2.5;
		/** A sweep longer than this was an adjustment, not a cut. */
		constexpr double MaxSweepSeconds = 0.55;
		/** A sweep is meant once it has lasted this long and travelled this far (or pushed this hard). */
		constexpr double MinSweepSeconds = 0.08;
		constexpr double MinTravel = 18.0;
		constexpr double MinImpulse = 0.45;

		// handTarget.ts samurai: the hand's rest direction (web).
		const FVector HandRest(0.1, -0.23, -0.28);
		/** BladeDirectionToward: settling iterations and the damping of each. */
		constexpr int32 AimIterations = 24;
		constexpr double AimDamping = 0.6;
	}

	FVector ClosestPointOnTriangle(const FVector& P, const FVector& A, const FVector& B, const FVector& C)
	{
		// Real-Time Collision Detection 5.1.5, as three.js implements it.
		const FVector AB = B - A, AC = C - A, AP = P - A;
		const double D1 = FVector::DotProduct(AB, AP), D2 = FVector::DotProduct(AC, AP);
		if (D1 <= 0.0 && D2 <= 0.0)
		{
			return A;
		}
		const FVector BP = P - B;
		const double D3 = FVector::DotProduct(AB, BP), D4 = FVector::DotProduct(AC, BP);
		if (D3 >= 0.0 && D4 <= D3)
		{
			return B;
		}
		const double VC = D1 * D4 - D3 * D2;
		if (VC <= 0.0 && D1 >= 0.0 && D3 <= 0.0)
		{
			return A + AB * (D1 / (D1 - D3));
		}
		const FVector CP = P - C;
		const double D5 = FVector::DotProduct(AB, CP), D6 = FVector::DotProduct(AC, CP);
		if (D6 >= 0.0 && D5 <= D6)
		{
			return C;
		}
		const double VB = D5 * D2 - D1 * D6;
		if (VB <= 0.0 && D2 >= 0.0 && D6 <= 0.0)
		{
			return A + AC * (D2 / (D2 - D6));
		}
		const double VA = D3 * D6 - D5 * D4;
		if (VA <= 0.0 && (D4 - D3) >= 0.0 && (D5 - D6) >= 0.0)
		{
			return B + (C - B) * ((D4 - D3) / ((D4 - D3) + (D5 - D6)));
		}
		const double Denom = 1.0 / (VA + VB + VC);
		return A + AB * (VB * Denom) + AC * (VC * Denom);
	}

	bool SweptBladeHits(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Centre, double Radius)
	{
		// A blade that hasn't moved can't cut; 2 cm of contact assistance, no speed gate.
		if (FMath::Max(FVector::Dist(A, C), FVector::Dist(B, D)) < MinBladeTravel)
		{
			return false;
		}
		const double Tolerance = FMath::Max(0.0, Radius) + ContactAssist;
		return FVector::Dist(ClosestPointOnTriangle(Centre, A, B, D), Centre) <= Tolerance
			|| FVector::Dist(ClosestPointOnTriangle(Centre, A, D, C), Centre) <= Tolerance;
	}

	void FBladeIntent::Reset()
	{
		Duration = Travel = Impulse = Quiet = 0.0;
		bActive = false;
	}

	bool FBladeIntent::Feed(double Rate, double Accel, double DtMs)
	{
		const double Dt = DtMs / 1000.0;
		if (!(Dt > 0.0 && Dt <= FBladeIntent::MaxFrameMs / 1000.0) || !FMath::IsFinite(Rate + Accel))
		{
			Reset();
			return false;
		}
		if (Rate < QuietRate && Accel < QuietAccel)
		{
			Quiet += Dt;
			if (Quiet >= QuietSeconds)
			{
				Reset();
			}
			return bActive;
		}
		Quiet = 0.0;
		if (Duration == 0.0 && Rate < StartRate && Accel < StartAccel)
		{
			return false;
		}
		Duration += Dt;
		Travel += Rate * Dt;
		Impulse += FMath::Max(0.0, Accel - QuietAccel) * Dt;
		if (Duration > MaxSweepSeconds)
		{
			Reset();
			return false;
		}
		if (Duration >= MinSweepSeconds && (Travel >= MinTravel || Impulse >= MinImpulse))
		{
			bActive = true;
		}
		return bActive;
	}

	FVector ShoulderFor(double LaneX)
	{
		// Facing -z, the swordsman's right is +x.
		return FVector(LaneX + FBladeRig::ShoulderSide, FBladeRig::ShoulderHeight, FBladeRig::StandZ);
	}

	FBlade BladeFor(double LaneX, const FVector& Direction)
	{
		FBlade B;
		B.Shoulder = ShoulderFor(LaneX);
		B.Direction = Direction.GetSafeNormal(UE_SMALL_NUMBER, WebUp);
		// handTarget.ts samurai: the hand rests low and in front and follows the blade by ElbowFollow.
		const FVector Rest = HandRest.GetSafeNormal();
		const FVector Arm = FMath::Lerp(Rest, B.Direction, FBladeRig::ElbowFollow).GetSafeNormal(UE_SMALL_NUMBER, Rest);
		B.Hand = B.Shoulder + Arm * FBladeRig::Reach;
		B.Base = B.Hand + B.Direction * FBladeRig::BladeBase;
		B.Tip = B.Hand + B.Direction * FBladeRig::BladeTip;
		return B;
	}

	FVector BladeDirectionFromQuaternion(const FQuat& Q)
	{
		return Q.GetNormalized().RotateVector(FVector(0.0, 1.0, 0.0));
	}

	FVector BladeDirectionFromEuler(double AlphaDeg, double BetaDeg, double GammaDeg)
	{
		// W3C deviceorientation: R = Rz(alpha) Rx(beta) Ry(gamma).
		const FQuat Z(FVector(0, 0, 1), FMath::DegreesToRadians(AlphaDeg));
		const FQuat X(FVector(1, 0, 0), FMath::DegreesToRadians(BetaDeg));
		const FQuat Y(FVector(0, 1, 0), FMath::DegreesToRadians(GammaDeg));
		return BladeDirectionFromQuaternion(Z * X * Y);
	}

	FVector BladeDirectionToward(double LaneX, const FVector& TipGoal)
	{
		// Aim from the shoulder, then from where that puts the hand, until it settles.
		FVector Dir = (TipGoal - ShoulderFor(LaneX)).GetSafeNormal(UE_SMALL_NUMBER, WebUp);
		for (int32 i = 0; i < AimIterations; ++i)
		{
			const FBlade B = BladeFor(LaneX, Dir);
			// Damped: the hand moves with the blade, so a full step overshoots.
			Dir = FMath::Lerp(Dir, (TipGoal - B.Hand).GetSafeNormal(UE_SMALL_NUMBER, WebUp), AimDamping).GetSafeNormal(UE_SMALL_NUMBER, WebUp);
		}
		return Dir;
	}
}
