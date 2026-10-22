#include "Games/Bowling/BowlingThrow.h"

namespace MovecadeKart::Bowling
{
	void FThrowDetector::Reset()
	{
		History.Reset();
		HistoryHead = 0;
		Current = FThrowPreview();
		Yaw = 0.0;
		Peak = 0.0;
		bInSwing = false;
		bGripped = false;
	}

	FThrowPreview FThrowDetector::PreviewFor(const FSample& Pose, double Charge) const
	{
		FThrowPreview P;
		// 45 degrees of yaw is a full aim, and turning the phone left aims left (the sign flips).
		P.Aim = FMath::Clamp(-(Pose.Yaw / 45.0) * Tuning.AimRange, -Tuning.AimRange, Tuning.AimRange);
		P.Curve = FMath::Clamp(Pose.Roll / Tuning.FullTurnDegPerSec * Tuning.MaxHook, -Tuning.MaxHook, Tuning.MaxHook);
		P.Loft = FMath::Clamp((Pose.Elevation - GripElevation) / Tuning.FullLoftDeg, 0.0, 1.0);
		P.Charge = Charge;
		return P;
	}

	FThrowRelease FThrowDetector::Build(double Power, const FSample& Pose) const
	{
		const FThrowPreview P = PreviewFor(Pose, 0.0);
		FThrowRelease R;
		R.Shot.Aim = P.Aim;
		R.Shot.Curve = bHasGyro ? P.Curve : 0.0;
		R.Shot.Loft = P.Loft;
		R.Shot.Power = FMath::Clamp(Power, Tuning.MinPower, 1.0);
		R.Confidence = bHasAccel ? (bHasGravity ? 1.0 : 0.75) : 0.5;
		if (!bHasGyro)
		{
			R.Confidence = FMath::Min(R.Confidence, 0.6);
		}
		R.bHookUnavailable = !bHasGyro;
		return R;
	}

	TOptional<FThrowRelease> FThrowDetector::Feed(const Net::FSensorFrame& Frame)
	{
		const double Dt = Frame.DtMs > 0 ? FMath::Min<double>(Frame.DtMs, 250.0) : 16.0;
		Clock += Dt;
		bHasGyro = (Frame.Flags & Net::SensorFlagHasGyro) != 0;
		bHasAccel = (Frame.Flags & Net::SensorFlagHasAccel) != 0;
		bHasGravity = (Frame.Flags & Net::SensorFlagHasGravity) != 0;

		FSample S;
		S.At = Clock;
		const double G = Frame.Gravity.Size();
		const bool bGravity = bHasGravity && G > 1e-6;
		S.Elevation = bGravity ? -FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Frame.Gravity.Y / G, -1.0, 1.0))) : 0.0;
		// Yaw: turn about gravity, integrated since the grip; roll: the wrist's twist right now.
		const double YawRate = bGravity ? FVector3d::DotProduct(Frame.Gyro, Frame.Gravity / G) : Frame.Gyro.Z;
		Yaw += YawRate * Dt / 1000.0;
		S.Yaw = Yaw;
		S.Roll = Frame.Gyro.Y;
		LastElevation = S.Elevation;
		S.Thrust = bHasAccel ? Frame.Accel.Size() : Frame.Gyro.Size() / 25.0;
		History.Add(S);
		// A sliding window: the head index moves on instead of shifting the array every frame, and the expired
		// samples are dropped in one go once they make up most of it.
		while (HistoryHead < History.Num() && History[HistoryHead].At < Clock - Tuning.AimWindowMs)
		{
			++HistoryHead;
		}
		if (HistoryHead >= 32 && HistoryHead * 2 >= History.Num())
		{
			History.RemoveAt(0, HistoryHead, EAllowShrinking::No);
			HistoryHead = 0;
		}
		const double Span = Tuning.FullPower - Tuning.Onset;
		const double Charge = FMath::Clamp((S.Thrust - Tuning.Onset) / Span, 0.0, 1.0);
		Current = PreviewFor(S, Charge);

		if (Clock < CooldownUntil)
		{
			return TOptional<FThrowRelease>();
		}
		if (!bInSwing)
		{
			if (S.Thrust >= Tuning.Onset)
			{
				bInSwing = true;
				Peak = S.Thrust;
				PeakAt = Clock;
			}
			return TOptional<FThrowRelease>();
		}
		if (S.Thrust > Peak)
		{
			Peak = S.Thrust;
			PeakAt = Clock;
		}
		if (!bGripped && S.Thrust < FMath::Max(Tuning.Onset * 0.8, Tuning.ReleaseFraction * Peak))
		{
			bInSwing = false;
			CooldownUntil = Clock + Tuning.RefractoryMs;
			const FSample* Pose = nullptr;
			for (int32 i = HistoryHead; i < History.Num() && !Pose; ++i)
			{
				Pose = History[i].At >= PeakAt ? &History[i] : nullptr;
			}
			return Build(FMath::Clamp((Peak - Tuning.Onset) / Span, 0.0, 1.0), Pose ? *Pose : S);
		}
		return TOptional<FThrowRelease>();
	}

	void FThrowDetector::Grip()
	{
		bGripped = true;
		Yaw = 0.0;
		History.Reset();
		HistoryHead = 0;
		Current = FThrowPreview();
		bInSwing = false;
		Peak = 0.0;
		// Loft is measured from the arm's elevation at the grip.
		GripElevation = LastElevation;
	}

	FThrowRelease FThrowDetector::Release()
	{
		const double Span = Tuning.FullPower - Tuning.Onset;
		double Best = Current.Charge * Span + Tuning.Onset;
		for (int32 i = HistoryHead; i < History.Num(); ++i)
		{
			if (History[i].At >= Clock - Tuning.AimWindowMs)
			{
				Best = FMath::Max(Best, History[i].Thrust);
			}
		}
		const FSample Pose = HistoryHead < History.Num() ? History.Last() : FSample();
		const FThrowRelease R = Build((Best - Tuning.Onset) / Span, Pose);
		bGripped = false;
		bInSwing = false;
		CooldownUntil = Clock + Tuning.RefractoryMs;
		return R;
	}
}
