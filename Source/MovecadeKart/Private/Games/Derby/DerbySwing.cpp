#include "Games/Derby/DerbySwing.h"

namespace MovecadeKart::Derby
{
	double SwingDirection(double HeadingDeg, double Handed, double AimRangeDeg)
	{
		const double S = Handed >= 0.0 ? 1.0 : -1.0;
		// Degrees past square: a right-handed barrel sweeps from behind (180) through square (+90) to the pitcher (0).
		double Past = 90.0 - S * HeadingDeg;
		Past = FMath::Fmod(Past + 540.0, 360.0) - 180.0;
		const double Pull = FMath::Clamp(Past / FMath::Max(1.0, AimRangeDeg), -1.0, 1.0);
		// A right-hander pulls to left field (-1), a left-hander to right field.
		return -S * Pull;
	}

	void FSwingDetector::Barrel(const Net::FSensorFrame& Frame, double& OutHeadingDeg, double& OutYawRateDps, bool& bOutHasAttitude)
	{
		bOutHasAttitude = Frame.bHasQuaternion;
		if (Frame.bHasQuaternion)
		{
			const FQuat Q = FQuat(Frame.Quaternion.X, Frame.Quaternion.Y, Frame.Quaternion.Z, Frame.Quaternion.W).GetNormalized();
			const FVector D = Q.RotateVector(FVector(0.0, 1.0, 0.0));
			OutHeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(D.X, -D.Z));
			OutYawRateDps = Q.RotateVector(FVector(Frame.Gyro)).Y;
			return;
		}
		OutHeadingDeg = 90.0;
		// No attitude: the turn about gravity if there is one, else about the screen's normal.
		const double G = Frame.Gravity.Size();
		OutYawRateDps = G > 1e-6 ? -FVector3d::DotProduct(Frame.Gyro, Frame.Gravity / G) : Frame.Gyro.Z;
	}

	void FSwingDetector::Reset()
	{
		LiveCharge = 0.0;
		Peak = 0.0;
		YawSwept = 0.0;
		bInSwing = false;
		bNeedRest = false;
	}

	TOptional<FSwingEvent> FSwingDetector::Feed(const Net::FSensorFrame& Frame)
	{
		const double Dt = Frame.DtMs > 0 ? FMath::Min<double>(Frame.DtMs, 250.0) : 16.0;
		Clock += Dt;
		const double Speed = FVector3d(Frame.Gyro).Size();
		LiveCharge = FMath::Clamp((Speed - Tuning.OnsetDps) / (Tuning.FullDps - Tuning.OnsetDps), 0.0, 1.0);
		double Heading = 0.0, YawRate = 0.0;
		bool bAttitude = false;
		Barrel(Frame, Heading, YawRate, bAttitude);

		// After a swing: the refractory window, and then the bat must come to rest (the follow-through, a rebound,
		// another swing begun inside the window never count); only a fresh swing from rest does.
		if (bNeedRest && Speed < Tuning.OnsetDps && Clock >= CooldownUntil)
		{
			bNeedRest = false;
		}
		if (bNeedRest)
		{
			return TOptional<FSwingEvent>();
		}
		if (!bInSwing)
		{
			if (Speed >= Tuning.OnsetDps)
			{
				bInSwing = true;
				StartAt = Clock;
				Peak = Speed;
				PeakAt = Clock;
				PeakHeading = Heading;
				bPeakAttitude = bAttitude;
				YawSwept = YawRate * Dt / 1000.0;
			}
			return TOptional<FSwingEvent>();
		}
		YawSwept += YawRate * Dt / 1000.0;
		if (Speed > Peak)
		{
			Peak = Speed;
			PeakAt = Clock;
			PeakHeading = Heading;
			bPeakAttitude = bAttitude;
		}
		const bool bEnded = Speed < FMath::Max(Tuning.OnsetDps, Tuning.EndFraction * Peak);
		if (!bEnded && Clock - StartAt < Tuning.MaxSwingMs)
		{
			return TOptional<FSwingEvent>();
		}
		bInSwing = false;
		if (Peak < Tuning.MinPeakDps)
		{
			return TOptional<FSwingEvent>();
		}
		CooldownUntil = Clock + Tuning.RefractoryMs;
		bNeedRest = true;
		FSwingEvent E;
		E.PeakAtMs = PeakAt;
		E.PeakDps = Peak;
		E.Strength = FMath::Clamp((Peak - Tuning.MinPeakDps * 0.5) / (Tuning.FullDps - Tuning.MinPeakDps * 0.5), 0.0, 1.0);
		E.Handed = YawSwept >= 0.0 ? 1.0 : -1.0;
		E.HeadingDeg = PeakHeading;
		E.bHasAttitude = bPeakAttitude;
		E.Direction = bPeakAttitude ? SwingDirection(PeakHeading, E.Handed, Tuning.AimRangeDeg) : 0.0;
		return E;
	}
}
