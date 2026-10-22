#pragma once

#include "CoreMinimal.h"

namespace MovecadeKart::Net { struct FSensorFrame; }

/**
 * Fishing Frenzy's motion detector, on the phone's sensor frames (the generic motion pad streams them at 60-100 Hz).
 * Device axes are the W3C ones: x to the right of the screen, y up the screen (the phone's long axis), z out of it.
 *
 *  - A flick is a sharp pitch (rotation about x). Tipping the top of the phone away (the rod snapping forward,
 *    negative x rate) is a forward flick: the cast, its peak rate the power. Snapping the top back toward you
 *    (positive) is the hook's upward jerk. A phone with no gyro falls back to the acceleration envelope, with the
 *    direction unknown (the game takes it for whichever the moment wants).
 *  - The reel is the rotation rate around the long axis (rolling the wrist, y rate), or circling the phone (the
 *    direction of its sideways acceleration going round), smoothed and scaled so 1.6 revolutions a second is a
 *    full-speed crank (1.0).
 *
 * Pure and deterministic: samples in, gestures out; the tests feed it synthetic streams.
 */
namespace MovecadeKart::Fishing
{
	struct FMotionTuning
	{
		double OnsetDegPerSec = 160.0;
		double CommitDegPerSec = 260.0;
		/** A flick this fast is full power. */
		double ReferenceDegPerSec = 1100.0;
		double ReleaseFraction = 0.35;
		double MinDurationMs = 40.0;
		double MaxDurationMs = 450.0;
		double CooldownMs = 550.0;
		/** The accel-only fallback (no gyro): onset, commit and full power, m/s^2. */
		double AccelOnsetMs2 = 7.0;
		double AccelCommitMs2 = 12.0;
		double AccelReferenceMs2 = 32.0;
		/** Revolutions a second for a full-speed crank, and the dead band under it. */
		double ReelFullRevPerSec = 1.6;
		double ReelDeadRevPerSec = 0.12;
		double ReelSmoothingMs = 180.0;
		/** Circling only counts with this much sideways acceleration (m/s^2). */
		double CircleMinMs2 = 1.2;
		double MaxReel = 1.25;
	};

	struct FMotionSample
	{
		/** deg/s, unset without a gyro. */
		TOptional<FVector3d> Gyro;
		/** m/s^2, gravity removed. */
		TOptional<FVector3d> Accel;
		/** Since the previous sample; 0 counts as 16. */
		double DtMs = 16.0;

		static FMotionSample FromFrame(const Net::FSensorFrame& Frame);
	};

	enum class EFlick : uint8 { Forward, Back, Unknown };

	struct FFlick
	{
		EFlick Direction = EFlick::Unknown;
		/** 0..1 */
		double Power = 0.0;
		double Peak = 0.0;
		double DurationMs = 0.0;
		/** Detector clock, ms. */
		double At = 0.0;
	};

	class MOVECADEKART_API FFishingMotion
	{
	public:
		explicit FFishingMotion(const FMotionTuning& InTuning = FMotionTuning()) : Tuning(InTuning) {}

		/** One sample; a flick on the frame one completes. */
		TOptional<FFlick> Feed(const FMotionSample& Sample);
		/** The reel, 0..MaxReel. */
		double Reel() const;
		double RevPerSec() const { return Rev; }
		/** 0..1, the pitch envelope against the commit threshold (the phone's wind-up, for a meter). */
		double Charge() const;
		double Clock() const { return Now; }
		void Reset() { *this = FFishingMotion(Tuning); }

	private:
		FMotionTuning Tuning;
		double Now = 0.0;
		double Envelope = 0.0;
		bool bActive = false;
		bool bAccelChannel = false;
		double StartedAt = 0.0;
		double Peak = 0.0;
		double PeakAbs = 0.0;
		double CooldownUntil = 0.0;
		double Rev = 0.0;
		double CircleAngle = 0.0;
		bool bHasCircleAngle = false;
	};
}
