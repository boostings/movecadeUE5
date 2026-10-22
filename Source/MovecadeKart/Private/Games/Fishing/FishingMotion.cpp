#include "Games/Fishing/FishingMotion.h"

#include "Net/RoomProtocol.h"

namespace MovecadeKart::Fishing
{
	FMotionSample FMotionSample::FromFrame(const Net::FSensorFrame& Frame)
	{
		FMotionSample S;
		if (Frame.Flags & Net::SensorFlagHasGyro)
		{
			S.Gyro = Frame.Gyro;
		}
		if (Frame.Flags & Net::SensorFlagHasAccel)
		{
			S.Accel = Frame.Accel;
		}
		S.DtMs = Frame.DtMs;
		return S;
	}

	double FFishingMotion::Reel() const
	{
		const double Span = FMath::Max(0.01, Tuning.ReelFullRevPerSec - Tuning.ReelDeadRevPerSec);
		return FMath::Clamp((Rev - Tuning.ReelDeadRevPerSec) / Span, 0.0, Tuning.MaxReel);
	}

	double FFishingMotion::Charge() const
	{
		return FMath::Clamp(Envelope / (bAccelChannel ? Tuning.AccelCommitMs2 : Tuning.CommitDegPerSec), 0.0, 1.0);
	}

	TOptional<FFlick> FFishingMotion::Feed(const FMotionSample& Sample)
	{
		const double DtMs = Sample.DtMs > 0.0 ? FMath::Min(Sample.DtMs, 250.0) : 16.0;
		Now += DtMs;
		const double Dt = DtMs / 1000.0;

		// The reel: wrist roll about the long axis, or the sideways acceleration going round (a circling phone).
		double Instant = 0.0;
		if (Sample.Gyro.IsSet())
		{
			Instant = FMath::Abs(Sample.Gyro->Y) / 360.0;
		}
		if (Sample.Accel.IsSet())
		{
			const FVector3d& A = Sample.Accel.GetValue();
			const double Side = FMath::Sqrt(A.X * A.X + A.Z * A.Z);
			if (Side >= Tuning.CircleMinMs2)
			{
				const double Angle = FMath::Atan2(A.X, A.Z);
				if (bHasCircleAngle)
				{
					const double Turn = FMath::Abs(FMath::UnwindRadians(Angle - CircleAngle));
					Instant = FMath::Max(Instant, Turn / Dt / (2.0 * UE_DOUBLE_PI));
				}
				CircleAngle = Angle;
				bHasCircleAngle = true;
			}
			else
			{
				bHasCircleAngle = false;
			}
		}
		if (!FMath::IsFinite(Instant))
		{
			Instant = 0.0;
		}
		Rev += (FMath::Min(Instant, 4.0) - Rev) * (1.0 - FMath::Exp(-DtMs / Tuning.ReelSmoothingMs));

		// The flick: the pitch rate's envelope (or, with no gyro, the acceleration's).
		const bool bGyro = Sample.Gyro.IsSet();
		if (!bGyro && !Sample.Accel.IsSet())
		{
			Envelope = 0.0;
			bActive = false;
			return {};
		}
		bAccelChannel = !bGyro;
		const double Signed = bGyro ? Sample.Gyro->X : Sample.Accel->Size();
		const double Value = FMath::IsFinite(Signed) ? FMath::Abs(Signed) : 0.0;
		Envelope += (Value - Envelope) * 0.6;
		const double Onset = bGyro ? Tuning.OnsetDegPerSec : Tuning.AccelOnsetMs2;
		const double Commit = bGyro ? Tuning.CommitDegPerSec : Tuning.AccelCommitMs2;
		const double Reference = bGyro ? Tuning.ReferenceDegPerSec : Tuning.AccelReferenceMs2;
		if (!bActive)
		{
			if (Now < CooldownUntil || Envelope < Onset)
			{
				return {};
			}
			bActive = true;
			StartedAt = Now;
			Peak = 0.0;
			PeakAbs = 0.0;
		}
		if (Value > PeakAbs)
		{
			PeakAbs = Value;
			Peak = Signed;
		}
		const double Elapsed = Now - StartedAt;
		const double ReleaseAt = FMath::Max(PeakAbs * Tuning.ReleaseFraction, Onset * 0.6);
		if (Envelope > ReleaseAt && Elapsed <= Tuning.MaxDurationMs)
		{
			return {};
		}
		bActive = false;
		// A refractory window after any candidate, so the arm's follow-through can't read as a second flick.
		CooldownUntil = Now + Tuning.CooldownMs;
		if (PeakAbs < Commit || Elapsed < Tuning.MinDurationMs || Elapsed > Tuning.MaxDurationMs)
		{
			return {};
		}
		FFlick F;
		F.Direction = !bGyro ? EFlick::Unknown : (Peak < 0.0 ? EFlick::Forward : EFlick::Back);
		const double Floor = Commit * 0.6;
		F.Power = FMath::Clamp((PeakAbs - Floor) / FMath::Max(1.0, Reference - Floor), 0.05, 1.0);
		F.Peak = Peak;
		F.DurationMs = Elapsed;
		F.At = Now;
		return F;
	}
}
