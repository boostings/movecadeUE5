#include "Feel/KartFeel.h"

namespace MovecadeKart
{
	namespace
	{
		constexpr double Gravity = 9.81;
		constexpr double MaxSubstep = 1.0 / 240.0;
		/** A frame longer than this (a hitch, a breakpoint) is treated as this long. */
		constexpr double MaxFrame = 0.1;
		constexpr double TopSpeed = 40.0;
		constexpr double HopEpsilon = 0.002;

		/** Frame-rate independent exponential smoothing toward Target at Rate per second. */
		double Smooth(double Current, double Target, double Rate, double Dt)
		{
			return FMath::Lerp(Current, Target, 1.0 - FMath::Exp(-Rate * Dt));
		}
	}

	void FFeelSpring::Step(double Target, double Frequency, double Damping, double DeltaSeconds)
	{
		const double Omega = 2.0 * UE_DOUBLE_PI * Frequency;
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaSeconds / MaxSubstep));
		const double H = DeltaSeconds / Steps;
		for (int32 i = 0; i < Steps; ++i)
		{
			// Semi-implicit Euler: velocity first, then position with the new velocity (stable for these stiffnesses).
			Velocity += (Omega * Omega * (Target - Value) - 2.0 * Damping * Omega * Velocity) * H;
			Value += Velocity * H;
		}
	}

	void FKartFeel::Reset()
	{
		for (FFeelSpring* Spring : { &Pitch, &Roll, &Heave, &Squash, &Stretch, &Fov, &PullBack, &Side, &LookYaw, &Bank, &Dip })
		{
			Spring->Reset();
		}
		bPrimed = false;
		LongAccel = 0.0;
		LateralG = 0.0;
		HopVelocity = 0.0;
		LastBoost = 0.0;
		Out = FKartFeelPose();
	}

	void FKartFeel::Punch(double FovDegrees, double PullBackMetres)
	{
		if (!bEnabled)
		{
			return;
		}
		// An impulse v on a spring of angular frequency w peaks near v / w (lightly damped).
		Fov.Kick(FovDegrees * 2.0 * UE_DOUBLE_PI * 3.0);
		PullBack.Kick(PullBackMetres * 2.0 * UE_DOUBLE_PI * 2.0);
	}

	const FKartFeelPose& FKartFeel::Update(const FKartFeelInput& In)
	{
		const double Dt = FMath::Min(In.DeltaSeconds, MaxFrame);
		if (!bEnabled)
		{
			Out = FKartFeelPose();
			return Out;
		}
		// First frame, or the kart was teleported: start at rest instead of springing across the map.
		if (!bPrimed || In.TeleportCount != LastTeleportCount)
		{
			Reset();
			bPrimed = true;
			LastSpeed = In.Speed;
			LastYaw = In.Yaw;
			LastHop = In.HopOffset;
			LastDriftBoostCount = In.DriftBoostCount;
			LastTeleportCount = In.TeleportCount;
			return Out;
		}
		if (Dt <= 0.0)
		{
			return Out;
		}
		const FKartFeelTuning& T = Tuning;

		// Loads, from the frame-to-frame change of the sim's speed and heading.
		const double RawLongAccel = (In.Speed - LastSpeed) / Dt;
		const double YawRate = FMath::FindDeltaAngleRadians(LastYaw, In.Yaw) / Dt;
		// Turning left (yaw rate > 0) at forward speed loads the body toward the right.
		const double RawLateralG = In.Speed * YawRate / Gravity;
		LongAccel = Smooth(LongAccel, RawLongAccel, 10.0, Dt);
		LateralG = Smooth(LateralG, FMath::Clamp(RawLateralG, -3.0, 3.0), 10.0, Dt);

		// Hop take-off and landing.
		const double NewHopVelocity = (In.HopOffset - LastHop) / Dt;
		const bool bTakeOff = LastHop <= HopEpsilon && In.HopOffset > HopEpsilon;
		const bool bLanded = LastHop > HopEpsilon && In.HopOffset <= HopEpsilon;
		const double LandingSpeed = bLanded ? FMath::Max(0.0, -HopVelocity) : 0.0;
		HopVelocity = NewHopVelocity;

		// Boost strength 0..1: a mini-turbo (x1.18) reads clearly, an ultra or rocket start (x1.55+) peaks.
		const double Boost = In.BoostTimeRemaining > 0.0 ? FMath::Clamp((In.BoostMultiplier - 1.0) / 0.55, 0.0, 1.0) : 0.0;
		const bool bNewDriftBoost = In.DriftBoostCount != LastDriftBoostCount;

		// Impulses.
		if (bLanded)
		{
			Heave.Kick(T.LandingHeaveKick * LandingSpeed);
			Squash.Kick(T.LandingSquash * LandingSpeed);
			Dip.Kick(-T.LandingDip * LandingSpeed);
		}
		if (bTakeOff)
		{
			Squash.Kick(-0.6); // a quick stretch up as the kart leaves the road
		}
		if (bNewDriftBoost || (Boost > 0.0 && LastBoost <= 0.0))
		{
			Stretch.Kick(T.BoostStretch * 30.0 * FMath::Max(Boost, 0.4));
			Pitch.Kick(25.0 * FMath::Max(Boost, 0.4)); // the nose lifts as the boost bites
		}
		const double Impact = FMath::Max(In.WallImpact, In.BumpImpact * 0.6);
		if (Impact > 0.0)
		{
			Heave.Kick(T.WallHeaveKick * Impact);
			// The hit side lifts: a wall on the right rolls the body left (right side up).
			const double HitSide = In.WallSide != 0.0 ? In.WallSide : (YawRate >= 0.0 ? -1.0 : 1.0);
			Roll.Kick(-T.WallRollKick * Impact * HitSide);
			Squash.Kick(0.5 * Impact);
		}

		// Chassis.
		Pitch.Step(FMath::Clamp(LongAccel * T.PitchPerAccel, -T.MaxPitch, T.MaxPitch), T.ChassisFrequency, T.ChassisDamping, Dt);
		Roll.Step(FMath::Clamp(LateralG * T.RollPerLateralG, -T.MaxRoll, T.MaxRoll), T.ChassisFrequency, T.ChassisDamping, Dt);
		Heave.Step(0.0, T.ChassisFrequency * 1.3, T.ChassisDamping, Dt);
		Squash.Step(0.0, T.SquashFrequency, T.SquashDamping, Dt);
		Stretch.Step(0.0, T.SquashFrequency * 0.8, T.SquashDamping * 1.4, Dt);

		// Camera.
		const double DriftSign = FMath::Sign(In.DriftDirection);
		const double DriftCharge = FMath::Clamp(In.DriftPower / 10.0, 0.0, 1.0);
		Fov.Step(T.BoostFovKick * Boost, 3.0, 0.8, Dt);
		PullBack.Step(T.BoostPullBack * Boost, 2.0, 0.9, Dt);
		// Drifting left: the eye swings out to the right and the view turns into the corner.
		Side.Step(T.DriftSide * DriftSign, 1.1, 1.0, Dt);
		LookYaw.Step(-T.DriftYaw * DriftSign * (0.6 + 0.4 * DriftCharge), 1.1, 1.0, Dt);
		Bank.Step(FMath::Clamp(-LateralG * T.TurnRoll, -4.0, 4.0), 1.6, 0.9, Dt);
		Dip.Step(0.0, 3.0, 0.55, Dt);

		const double SpeedRatio = FMath::Clamp(FMath::Abs(In.Speed) / TopSpeed, 0.0, 1.0);
		Out.ChassisPitch = FMath::Clamp(Pitch.Value, -T.MaxPitch * 1.5, T.MaxPitch * 1.5);
		Out.ChassisRoll = FMath::Clamp(Roll.Value, -T.MaxRoll * 1.5, T.MaxRoll * 1.5);
		Out.ChassisHeave = FMath::Clamp(Heave.Value, -0.12, 0.08);
		Out.SquashUp = FMath::Clamp(1.0 - Squash.Value, 0.8, 1.15);
		Out.StretchForward = FMath::Clamp(1.0 + Stretch.Value, 0.9, 1.2);
		Out.CameraFovKick = Fov.Value;
		Out.CameraPullBack = PullBack.Value;
		Out.CameraSide = Side.Value;
		Out.CameraYaw = LookYaw.Value;
		Out.CameraRoll = Bank.Value;
		Out.CameraDip = FMath::Clamp(Dip.Value, -0.4, 0.2);
		Out.CameraRumble = T.RumbleAtTopSpeed * SpeedRatio * SpeedRatio
			+ (In.bOnDirt ? T.DirtRumble * FMath::Clamp(FMath::Abs(In.Speed) / 20.0, 0.0, 1.0) : 0.0);

		LastSpeed = In.Speed;
		LastYaw = In.Yaw;
		LastHop = In.HopOffset;
		LastDriftBoostCount = In.DriftBoostCount;
		LastBoost = Boost;
		return Out;
	}
}
