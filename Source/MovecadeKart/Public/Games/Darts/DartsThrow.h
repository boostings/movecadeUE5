#pragma once

#include "CoreMinimal.h"
#include "Games/Darts/DartsRules.h"
#include "Net/RoomProtocol.h"

/**
 * The darts throw detector (movecade-v2 darts/throw.ts, Docs/Port/07-darts.md §3). Aim comes from gravity
 * (elevation against a neutral pose captured while the phone is still) and integrated yaw rate; the release from the
 * user-acceleration envelope, with the aim read from before the arm moved. In play the phone runs it and sends
 * darts:release at thumb-lift (D2); the host runs one per seat only for the live reticle.
 */
namespace MovecadeKart::Darts
{
	struct FThrowTuning
	{
		double OnsetMs2 = 6.0;
		double CommitPeakMs2 = 11.0;
		double ReferenceMs2 = 30.0;
		double ReleaseFraction = 0.4;
		double ReleaseFloorMs2 = 4.0;
		double MinDurationMs = 45.0;
		double MaxDurationMs = 420.0;
		double CooldownMs = 700.0;
		double ForwardDotMinimum = 0.15;
		double Smoothing = 0.5;
		double GyroOnsetDegPerSec = 240.0;
		double GyroCommitPeakDegPerSec = 460.0;
		double GyroReferenceDegPerSec = 900.0;
		double PitchRangeDeg = 26.0;
		double YawRangeDeg = 22.0;
		/** D1: 0, as shipped (the doc's 0.55 breaks the web's own tests). */
		double YawLeakPerSec = 0.0;
		double AimSmoothingSlow = 0.11;
		double AimSmoothingFast = 0.52;
		double AimSpeedReference = 2.1;
		double NeutralGravityToleranceMs2 = 1.2;
		double NeutralHoldMs = 260.0;
		double ReleaseLookbackMs = 140.0;
		double NoGravityConfidence = 0.65;
		double NoGyroConfidence = 0.7;
		double GyroReleaseConfidence = 0.6;
	};

	/** One motion sample (throw.ts ThrowSample); unset channels are the ones the phone doesn't have. */
	struct FThrowSample
	{
		double Alpha = 0.0;
		double Beta = 0.0;
		double Gamma = 0.0;
		/** deg/s */
		TOptional<FVector3d> Gyro;
		/** m/s^2, gravity removed */
		TOptional<FVector3d> Accel;
		TOptional<FVector3d> Gravity;
		/** Since the sender's previous sample; 0 counts as 16. */
		double DtMs = 16.0;

		static FThrowSample FromFrame(const Net::FSensorFrame& Frame);
	};

	struct FAimState
	{
		double X = 0.0;
		double Y = 0.0;
		double Steadiness = 1.0;
		bool bFromGravity = true;
		bool bCentred = false;
	};

	struct FThrowGesture : FDartGesture
	{
		bool bFromGyro = false;
		/** Detector clock, ms. */
		double At = 0.0;
	};

	class MOVECADEKART_API FDartThrowDetector
	{
	public:
		explicit FDartThrowDetector(const FThrowTuning& InTuning = FThrowTuning()) : Tuning(InTuning) {}

		/** One sample; a gesture on the frame an ungripped throw completes. */
		TOptional<FThrowGesture> Feed(const FThrowSample& Sample);
		const FAimState& Aim() const { return AimState; }
		/** 0..1, the envelope against the commit threshold. */
		double Charge() const;
		bool IsAvailable() const { return bAvailable; }
		/** Thumb down: the envelope keeps running but never auto-commits. */
		void Grip() { bGripped = true; }
		/** Thumb up: the throw now; unset without a grip or before any usable sample. */
		TOptional<FThrowGesture> Release();
		bool IsGripped() const { return bGripped; }
		void Recentre();
		void Reset();

	private:
		static constexpr int32 History = 32;

		void UpdateAim(const FThrowSample& Sample, double DtSeconds);
		void PushHistory();
		FVector2D AimAtLookback(double Ms) const;
		FThrowGesture Build(double PeakValue, double Elapsed, double YawRate, double RollRate, bool bAccelChannel, double Reference, double Commit) const;

		FThrowTuning Tuning;
		double Clock = 0.0;
		double Envelope = 0.0;
		bool bThrowing = false;
		double StartedAt = 0.0;
		double Peak = 0.0;
		double PeakYawRate = 0.0;
		double PeakRollRate = 0.0;
		double CooldownUntil = 0.0;
		bool bAvailable = true;
		bool bSawForwardThrust = false;
		bool bGripped = false;
		bool bUsedAccelChannel = true;
		double YawDeg = 0.0;
		double NeutralPitchDeg = 0.0;
		bool bCentred = false;
		double SteadyForMs = 0.0;
		double AimX = 0.0;
		double AimY = 0.0;
		bool bUsedGravity = true;
		bool bUsedGyro = true;
		/** Float, like the web's Float32Array ring. */
		float HistoryX[History] = {};
		float HistoryY[History] = {};
		float HistoryT[History] = {};
		int32 HistoryNext = 0;
		int32 HistoryCount = 0;
		double AimSpeed = 0.0;
		FAimState AimState;
	};
}
