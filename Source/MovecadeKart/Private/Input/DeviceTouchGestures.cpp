#include "Input/DeviceTouchGestures.h"

namespace MovecadeKart::Input
{
	namespace
	{
		/** How much each move pulls the measured velocity toward the latest step. */
		constexpr double VelocityBlend = 0.5;
		/** Steps shorter than this (s) don't measure a speed (two events in one frame). */
		constexpr double MinStepSeconds = 1e-3;
		/** The crank's centre follows the finger this much per move (a running mean of the circle). */
		constexpr double CentreFollow = 0.08;
		/** Too near the centre, the angle is noise. */
		constexpr double MinCrankRadius = 0.02;
	}

	bool FTouchTrack::Feed(const FDeviceTouch& Touch)
	{
		if (Touch.Phase == ETouchPhase::Down)
		{
			if (bActive)
			{
				return false;
			}
			*this = FTouchTrack();
			bActive = true;
			Pointer = Touch.Pointer;
			Start = Last = Previous = Touch.Position;
			StartTime = LastTime = PreviousTime = Touch.Time;
			return true;
		}
		if (!bActive || Touch.Pointer != Pointer)
		{
			return false;
		}
		const double Dt = Touch.Time - LastTime;
		if (Dt >= MinStepSeconds)
		{
			const FVector2D Step = (Touch.Position - Last) / Dt;
			Velocity = FMath::Lerp(Velocity, Step, VelocityBlend);
			PeakUpSpeed = FMath::Max(PeakUpSpeed, -Velocity.Y);
		}
		Previous = Last;
		PreviousTime = LastTime;
		Last = Touch.Position;
		LastTime = Touch.Time;
		if (Touch.Phase == ETouchPhase::Up)
		{
			bActive = false;
		}
		return true;
	}

	void FTouchCrank::Feed(const FVector2D& Position, double Time)
	{
		Centre = FMath::Lerp(Centre, Position, CentreFollow);
		const FVector2D Arm = Position - Centre;
		const double Dt = Time - LastTime;
		if (Arm.Size() < MinCrankRadius)
		{
			LastTime = Time;
			return;
		}
		const double Now = FMath::Atan2(Arm.Y, Arm.X);
		if (bHasAngle && Dt > 0.0)
		{
			const double Rate = FMath::Abs(FMath::UnwindRadians(Now - Angle)) / (2.0 * UE_DOUBLE_PI) / Dt;
			RevPerSec += (Rate - RevPerSec) * (1.0 - FMath::Exp(-Dt / SmoothingSeconds));
		}
		Angle = Now;
		bHasAngle = true;
		LastTime = Time;
	}

	void FTouchCrank::Coast(double Time)
	{
		const double Dt = FMath::Max(0.0, Time - LastTime);
		RevPerSec *= FMath::Exp(-Dt / SmoothingSeconds);
		LastTime = Time;
		bHasAngle = false;
	}
}
