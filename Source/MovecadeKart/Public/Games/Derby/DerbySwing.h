#pragma once

#include "CoreMinimal.h"
#include "Net/RoomProtocol.h"

/**
 * Home Run Derby's swing detector: the phone is the bat. It turns a phone's sensor frames into a swing event: when
 * the swing peaked (the gyro magnitude's maximum, on the detector's own clock of summed frame dt), how hard (the
 * peak angular speed) and where it points (pull or opposite field).
 *
 * The direction comes from the calibrated attitude: the phone's top (+Y) is the barrel. At the peak the barrel's
 * heading on the ground (0 pointing at the screen, the pitcher) is compared with square contact, the barrel across
 * the plate at 90 degrees to the pitch, in the sense the swing turns (its yaw rate about the vertical, which also
 * tells a right-handed swing from a left-handed one). Barrel already past square is early, a pull; not yet there is
 * late, the other way. Without a quaternion the swing still counts, aimed through the middle.
 */
namespace MovecadeKart::Derby
{
	struct FSwingTuning
	{
		/** deg/s: a swing starts above this. */
		double OnsetDps = 240.0;
		/** deg/s: a full-strength swing. */
		double FullDps = 1300.0;
		/** A swing must peak at least this hard to count (a wiggle is not a swing). */
		double MinPeakDps = 420.0;
		/** It ends when the speed falls under this share of its peak (or the onset). */
		double EndFraction = 0.45;
		double MaxSwingMs = 700.0;
		double RefractoryMs = 450.0;
		/** Degrees past square for a full pull (or short of it for a full push the other way). */
		double AimRangeDeg = 55.0;
	};

	struct FSwingEvent
	{
		/** On the detector's clock (ClockMs), ms. */
		double PeakAtMs = 0.0;
		double PeakDps = 0.0;
		/** 0..1. */
		double Strength = 0.0;
		/** -1 left field .. +1 right field. */
		double Direction = 0.0;
		/** +1 a right-handed swing (counter-clockwise from above), -1 left-handed. */
		double Handed = 1.0;
		/** The barrel's heading at the peak, degrees (0 at the pitcher, + toward the right of the screen). */
		double HeadingDeg = 0.0;
		bool bHasAttitude = false;
	};

	class MOVECADEKART_API FSwingDetector
	{
	public:
		explicit FSwingDetector(const FSwingTuning& InTuning = FSwingTuning()) : Tuning(InTuning) {}

		/** One frame. Returns a swing once it has peaked and come down. */
		TOptional<FSwingEvent> Feed(const Net::FSensorFrame& Frame);
		void Reset();

		double ClockMs() const { return Clock; }
		/** 0..1, the live angular speed (for the batter's load and the HUD). */
		double Charge() const { return LiveCharge; }
		bool IsSwinging() const { return bInSwing; }

		/** The barrel's heading (degrees, 0 at the screen) and the vertical yaw rate (deg/s, + counter-clockwise from above). */
		static void Barrel(const Net::FSensorFrame& Frame, double& OutHeadingDeg, double& OutYawRateDps, bool& bOutHasAttitude);

	private:
		FSwingTuning Tuning;
		double Clock = 0.0;
		double LiveCharge = 0.0;
		double Peak = 0.0;
		double PeakAt = 0.0;
		double PeakHeading = 0.0;
		double StartAt = 0.0;
		/** The yaw swept since the onset, signed (the swing's sense). */
		double YawSwept = 0.0;
		double CooldownUntil = -1.0;
		bool bPeakAttitude = false;
		bool bNeedRest = false;
		bool bInSwing = false;
	};

	/** The pull or push of a barrel heading against square contact for a swing turning in Handed's sense, -1..1 field. */
	MOVECADEKART_API double SwingDirection(double HeadingDeg, double Handed, double AimRangeDeg);
}
