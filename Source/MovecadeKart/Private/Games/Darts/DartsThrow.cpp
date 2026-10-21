#include "Games/Darts/DartsThrow.h"

namespace MovecadeKart::Darts
{
	namespace
	{
		double ClampSigned(double Value)
		{
			return FMath::IsFinite(Value) ? FMath::Clamp(Value, -1.0, 1.0) : 0.0;
		}

		double Clamp01(double Value)
		{
			return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0, 1.0) : 0.0;
		}
	}

	FThrowSample FThrowSample::FromFrame(const Net::FSensorFrame& Frame)
	{
		FThrowSample S;
		S.Alpha = Frame.Alpha;
		S.Beta = Frame.Beta;
		S.Gamma = Frame.Gamma;
		if (Frame.Flags & Net::SensorFlagHasGyro)
		{
			S.Gyro = Frame.Gyro;
		}
		if (Frame.Flags & Net::SensorFlagHasAccel)
		{
			S.Accel = Frame.Accel;
		}
		if (Frame.Flags & Net::SensorFlagHasGravity)
		{
			S.Gravity = Frame.Gravity;
		}
		S.DtMs = Frame.DtMs;
		return S;
	}

	double FDartThrowDetector::Charge() const
	{
		return Clamp01(Envelope / (bUsedAccelChannel ? Tuning.CommitPeakMs2 : Tuning.GyroCommitPeakDegPerSec));
	}

	void FDartThrowDetector::PushHistory()
	{
		HistoryX[HistoryNext] = static_cast<float>(AimX);
		HistoryY[HistoryNext] = static_cast<float>(AimY);
		HistoryT[HistoryNext] = static_cast<float>(Clock);
		HistoryNext = (HistoryNext + 1) % History;
		HistoryCount = FMath::Min(HistoryCount + 1, History);
	}

	FVector2D FDartThrowDetector::AimAtLookback(double Ms) const
	{
		// The newest sample that is old enough, else the oldest held.
		const double Want = Clock - Ms;
		int32 Best = INDEX_NONE;
		double BestTime = -TNumericLimits<double>::Max();
		for (int32 i = 0; i < HistoryCount; ++i)
		{
			if (HistoryT[i] <= Want && HistoryT[i] > BestTime)
			{
				BestTime = HistoryT[i];
				Best = i;
			}
		}
		if (Best == INDEX_NONE)
		{
			double OldestTime = TNumericLimits<double>::Max();
			for (int32 i = 0; i < HistoryCount; ++i)
			{
				if (HistoryT[i] < OldestTime)
				{
					OldestTime = HistoryT[i];
					Best = i;
				}
			}
			if (Best == INDEX_NONE)
			{
				return FVector2D(AimX, AimY);
			}
		}
		return FVector2D(HistoryX[Best], HistoryY[Best]);
	}

	void FDartThrowDetector::UpdateAim(const FThrowSample& Sample, double DtSeconds)
	{
		double TargetX = 0.0, TargetY = 0.0;
		if (Sample.Gravity.IsSet() && Sample.Gravity->Size() > 1.0)
		{
			bUsedGravity = true;
			AimState.bFromGravity = true;
			const FVector3d G = Sample.Gravity.GetValue();
			const double GMag = G.Size();
			const FVector3d U = G / GMag;
			// The device's +y points forward; nose up drives U.Y negative, so negate for a positive elevation.
			const double PitchDeg = -FMath::Asin(ClampSigned(U.Y)) * 180.0 / UE_DOUBLE_PI;
			const double UserMag = Sample.Accel.IsSet() ? Sample.Accel->Size() : 0.0;
			const bool bStill = FMath::Abs(GMag - 9.81) < Tuning.NeutralGravityToleranceMs2 && UserMag < 1.6;
			if (bStill && !bThrowing)
			{
				SteadyForMs += DtSeconds * 1000.0;
				if (!bCentred && SteadyForMs >= Tuning.NeutralHoldMs)
				{
					NeutralPitchDeg = PitchDeg;
					bCentred = true;
					YawDeg = 0.0;
				}
			}
			else
			{
				SteadyForMs = 0.0;
			}
			TargetY = ClampSigned((PitchDeg - NeutralPitchDeg) / Tuning.PitchRangeDeg);
			if (Sample.Gyro.IsSet())
			{
				bUsedGyro = true;
				// Yaw about the gravity axis is what gravity can't see, so it is the part worth integrating.
				const double YawRate = FVector3d::DotProduct(Sample.Gyro.GetValue(), U);
				YawDeg += YawRate * DtSeconds;
				YawDeg *= FMath::Max(0.0, 1.0 - Tuning.YawLeakPerSec * DtSeconds);
				TargetX = ClampSigned(YawDeg / Tuning.YawRangeDeg);
			}
			else
			{
				bUsedGyro = false;
				TargetX = ClampSigned(Sample.Gamma / Tuning.YawRangeDeg);
			}
		}
		else
		{
			bUsedGravity = false;
			bUsedGyro = Sample.Gyro.IsSet();
			AimState.bFromGravity = false;
			if (!bCentred)
			{
				NeutralPitchDeg = Sample.Beta;
				bCentred = true;
			}
			TargetY = ClampSigned((Sample.Beta - NeutralPitchDeg) / Tuning.PitchRangeDeg);
			TargetX = ClampSigned(Sample.Gamma / Tuning.YawRangeDeg);
		}
		// Adaptive smoothing: heavy at rest, light while sweeping.
		const double RawSpeed = FMath::Sqrt(FMath::Square(TargetX - AimX) + FMath::Square(TargetY - AimY)) / FMath::Max(0.001, DtSeconds);
		AimSpeed += (RawSpeed - AimSpeed) * 0.3;
		const double Blend = Tuning.AimSmoothingSlow + (Tuning.AimSmoothingFast - Tuning.AimSmoothingSlow) * Clamp01(AimSpeed / Tuning.AimSpeedReference);
		AimX += (TargetX - AimX) * Blend;
		AimY += (TargetY - AimY) * Blend;
		AimState.X = AimX;
		AimState.Y = AimY;
		AimState.Steadiness = Clamp01(1.0 - AimSpeed / Tuning.AimSpeedReference);
		AimState.bCentred = bCentred;
		PushHistory();
	}

	FThrowGesture FDartThrowDetector::Build(double PeakValue, double Elapsed, double YawRate, double RollRate, bool bAccelChannel, double Reference, double Commit) const
	{
		const FVector2D Released = AimAtLookback(Tuning.ReleaseLookbackMs + Elapsed);
		const double Overshoot = Clamp01((PeakValue - Commit) / FMath::Max(1.0, Reference - Commit));
		double Confidence = Clamp01(0.5 + 0.3 * Overshoot + 0.2 * AimState.Steadiness);
		if (!bAccelChannel) Confidence = FMath::Min(Confidence, Tuning.GyroReleaseConfidence);
		if (!bUsedGravity) Confidence = FMath::Min(Confidence, Tuning.NoGravityConfidence);
		if (!bUsedGyro) Confidence = FMath::Min(Confidence, Tuning.NoGyroConfidence);
		FThrowGesture G;
		G.AimX = Released.X;
		G.AimY = Released.Y;
		G.SpinDegPerSec = YawRate;
		G.RollDegPerSec = RollRate;
		G.Confidence = Confidence;
		G.Steadiness = AimState.Steadiness;
		G.PeakMs2 = PeakValue;
		G.DurationMs = Elapsed;
		G.bFromGyro = !bAccelChannel;
		G.At = Clock;
		return G;
	}

	TOptional<FThrowGesture> FDartThrowDetector::Release()
	{
		const bool bWasGripped = bGripped;
		bGripped = false;
		if (!bWasGripped || !bAvailable || HistoryCount == 0)
		{
			return {};
		}
		const double Elapsed = bThrowing ? Clock - StartedAt : 0.0;
		const double PeakValue = bThrowing ? FMath::Max(Peak, Envelope) : Envelope;
		const double YawRate = bThrowing ? PeakYawRate : 0.0;
		const double RollRate = bThrowing ? PeakRollRate : 0.0;
		bThrowing = false;
		Peak = 0.0;
		CooldownUntil = Clock + Tuning.CooldownMs;
		const double Reference = bUsedAccelChannel ? Tuning.ReferenceMs2 : Tuning.GyroReferenceDegPerSec;
		const double Commit = bUsedAccelChannel ? Tuning.CommitPeakMs2 : Tuning.GyroCommitPeakDegPerSec;
		// A lift with no flick still throws: the weakest legal dart.
		FThrowGesture G = Build(PeakValue, Elapsed, YawRate, RollRate, bUsedAccelChannel, Reference, Commit);
		G.Power = FMath::Max(0.05, Clamp01(PeakValue / Reference));
		return G;
	}

	void FDartThrowDetector::Recentre()
	{
		bCentred = false;
		SteadyForMs = 0.0;
		YawDeg = 0.0;
		AimState.bCentred = false;
	}

	void FDartThrowDetector::Reset()
	{
		*this = FDartThrowDetector(Tuning);
	}

	TOptional<FThrowGesture> FDartThrowDetector::Feed(const FThrowSample& Sample)
	{
		const double DtMs = Sample.DtMs > 0.0 ? FMath::Min(Sample.DtMs, 250.0) : 16.0;
		Clock += DtMs;
		const double DtSeconds = DtMs / 1000.0;
		bAvailable = Sample.Accel.IsSet() || Sample.Gyro.IsSet();
		UpdateAim(Sample, DtSeconds);
		if (!bAvailable)
		{
			Envelope = 0.0;
			bThrowing = false;
			return {};
		}
		const bool bUseAccel = Sample.Accel.IsSet();
		bUsedAccelChannel = bUseAccel;
		const FVector3d V = bUseAccel ? Sample.Accel.GetValue() : Sample.Gyro.GetValue();
		const double Onset = bUseAccel ? Tuning.OnsetMs2 : Tuning.GyroOnsetDegPerSec;
		const double Commit = bUseAccel ? Tuning.CommitPeakMs2 : Tuning.GyroCommitPeakDegPerSec;
		const double Reference = bUseAccel ? Tuning.ReferenceMs2 : Tuning.GyroReferenceDegPerSec;
		const double Floor = bUseAccel ? Tuning.ReleaseFloorMs2 : Tuning.GyroOnsetDegPerSec * 0.6;

		const double Instant = V.Size();
		Envelope += (Instant - Envelope) * Tuning.Smoothing;
		// A throw pushes along the device's +y; lifting or setting the phone down doesn't.
		const double ForwardDot = Instant > 0.001 ? V.Y / Instant : 0.0;

		if (!bThrowing)
		{
			if (Clock < CooldownUntil || Envelope < Onset)
			{
				return {};
			}
			bThrowing = true;
			StartedAt = Clock;
			Peak = 0.0;
			PeakYawRate = 0.0;
			PeakRollRate = 0.0;
			bSawForwardThrust = false;
		}
		if (Envelope > Peak)
		{
			Peak = Envelope;
			if (Sample.Gyro.IsSet())
			{
				const FVector3d W = Sample.Gyro.GetValue();
				const double Length = Sample.Gravity.IsSet() ? Sample.Gravity->Size() : 0.0;
				PeakYawRate = Length > 1.0 ? -FVector3d::DotProduct(W, Sample.Gravity.GetValue()) / Length : W.Z;
				PeakRollRate = W.Y;
			}
		}
		if (bUseAccel && ForwardDot >= Tuning.ForwardDotMinimum)
		{
			bSawForwardThrust = true;
		}
		if (bGripped)
		{
			return {};
		}
		const double ReleaseAt = FMath::Max(Peak * Tuning.ReleaseFraction, Floor);
		const double Elapsed = Clock - StartedAt;
		if (Envelope > ReleaseAt && Elapsed <= Tuning.MaxDurationMs)
		{
			return {};
		}
		bThrowing = false;
		const double PeakValue = Peak;
		Peak = 0.0;
		// The refractory window opens on any candidate ending, so a rejected one can't re-arm on its own tail.
		CooldownUntil = Clock + Tuning.CooldownMs;
		if (PeakValue < Commit || Elapsed < Tuning.MinDurationMs || Elapsed > Tuning.MaxDurationMs || (bUseAccel && !bSawForwardThrust))
		{
			return {};
		}
		FThrowGesture G = Build(PeakValue, Elapsed, PeakYawRate, PeakRollRate, bUseAccel, Reference, Commit);
		G.Power = Clamp01(PeakValue / Reference);
		return G;
	}
}
