#pragma once

#include "CoreMinimal.h"
#include "Games/Bowling/BowlingSim.h"
#include "Net/RoomProtocol.h"

/**
 * The bowling throw detector (movecade-v2 bowling/throw.ts, Docs/Port/06-bowling.md §2): turns a phone's sensor
 * frames into a release {aim, power, curve, loft}. In the web build the phone runs it and sends the result; the
 * host runs one per seat for the live lane guide. The port runs it on the host for local play too (a pad or mouse
 * synthesises frames), and accepts the phone's own release as the authority, like the web.
 */
namespace MovecadeKart::Bowling
{
	struct FThrowTuning
	{
		double Onset = 7.0;          // m/s^2
		double FullPower = 26.0;
		double ReleaseFraction = 0.45;
		double RefractoryMs = 900.0;
		double AimWindowMs = 220.0;
		double AimRange = 0.45;      // m
		double MaxHook = 0.45;       // m
		double FullTurnDegPerSec = 400.0;
		double FullLoftDeg = 75.0;
		double MinPower = 0.04;
	};

	struct FThrowPreview
	{
		double Aim = 0.0;
		double Curve = 0.0;
		double Loft = 0.0;
		/** 0..1, how hard the arm is moving right now. */
		double Charge = 0.0;
	};

	struct FThrowRelease
	{
		FShot Shot;
		/** 1 with accel and gravity, 0.75 accel only, 0.5 gyro only; never used by the sim. */
		double Confidence = 0.5;
		bool bHookUnavailable = false;
	};

	class MOVECADEKART_API FThrowDetector
	{
	public:
		explicit FThrowDetector(const FThrowTuning& InTuning = FThrowTuning()) : Tuning(InTuning) {}

		/** One frame. Returns a release when an ungripped swing commits on its own (the button path never does). */
		TOptional<FThrowRelease> Feed(const Net::FSensorFrame& Frame);
		/** The grip button goes down: aim, hook and loft are measured from here. */
		void Grip();
		/** The grip button comes up: the throw, from the strongest push in the last AimWindowMs. */
		FThrowRelease Release();
		void Reset();

		const FThrowPreview& Preview() const { return Current; }
		bool IsGripped() const { return bGripped; }
		double ClockMs() const { return Clock; }

	private:
		struct FSample
		{
			double At = 0.0;
			double Yaw = 0.0;
			double Roll = 0.0;
			double Elevation = 0.0;
			double Thrust = 0.0;
		};
		FThrowRelease Build(double Power, const FSample& Pose) const;
		FThrowPreview PreviewFor(const FSample& Pose, double Charge) const;

		FThrowTuning Tuning;
		/** The last AimWindowMs of samples are History[HistoryHead..]; older ones wait to be dropped in bulk. */
		TArray<FSample> History;
		int32 HistoryHead = 0;
		FThrowPreview Current;
		double Clock = 0.0;
		double Yaw = 0.0;
		double GripElevation = 0.0;
		double LastElevation = 0.0;
		double Peak = 0.0;
		double PeakAt = 0.0;
		double CooldownUntil = -1.0;
		bool bInSwing = false;
		bool bGripped = false;
		bool bHasGyro = false;
		bool bHasAccel = false;
		bool bHasGravity = false;
	};
}
