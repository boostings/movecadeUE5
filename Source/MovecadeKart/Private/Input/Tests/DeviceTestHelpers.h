#pragma once

// Shared by the on-device controls tests (MovecadeKart.Input.* and each game's DeviceControls test): a sink that
// records the phone traffic, the phone's own frame for a known motion, and a driver that plays a motion through the
// device path (engine reading, FromEngine, calibration, the wire) the way UDeviceControlsSubsystem does.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceMotion.h"
#include "Input/DeviceMotionSim.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MovecadeKart::Input::Tests
{
	constexpr EAutomationTestFlags DeviceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** Every frame and event a scheme sent, in order. */
	class FRecordingSink : public IDeviceInputSink
	{
	public:
		struct FEvent
		{
			FString Name;
			TSharedPtr<FJsonObject> Data;
		};

		virtual void SendFrame(const Net::FSensorFrame& Frame) override { Frames.Add(Frame); }
		virtual void SendEvent(const FString& Event, const TSharedPtr<FJsonObject>& Data) override { Events.Add({ Event, Data }); }

		const FEvent* FindEvent(const FString& Name) const
		{
			return Events.FindByPredicate([&Name](const FEvent& E) { return E.Name == Name; });
		}

		TArray<Net::FSensorFrame> Frames;
		TArray<FEvent> Events;
	};

	/**
	 * What a web controller sends for a motion Base * Rotation (Rotation in the calibrated frame), written out by hand
	 * from the protocol (Docs/Port/05 §2) like phone-sim.mjs's attitudeFrame: calibrated quaternion and its Euler,
	 * gyro, accel, gravity; then the wire's rounding.
	 */
	inline Net::FSensorFrame PhoneAttitudeFrame(const FDeviceMotion& Motion, const FQuat& Rotation, uint8 Sequence, uint16 DtMs)
	{
		using namespace MovecadeKart::Net;
		FSensorFrame F;
		F.Flags = SensorFlagCalibrated | SensorFlagHasGyro | SensorFlagHasAccel | SensorFlagHasGravity | SensorFlagHasQuaternion;
		F.ScreenOrientation = static_cast<uint8>(Motion.Orientation);
		F.Sequence = Sequence;
		F.DtMs = DtMs;
		FQuat Q = Rotation.GetNormalized();
		if (Q.W < 0.0)
		{
			Q = FQuat(-Q.X, -Q.Y, -Q.Z, -Q.W);
		}
		F.bHasQuaternion = true;
		F.Quaternion = FVector4d(Q.X, Q.Y, Q.Z, Q.W);
		EulerFromAttitude(Q, F.Alpha, F.Beta, F.Gamma);
		F.Gyro = Motion.GyroDegPerSec;
		F.Accel = Motion.AccelMs2;
		F.Gravity = Motion.GravityMs2;
		return QuantizeSensorFrame(F);
	}

	/** One sample of a test gesture: the calibrated rotation, the device's rates, and any button change at it. */
	struct FGestureSample
	{
		FQuat Rotation = FQuat::Identity;
		FVector3d Gyro = FVector3d::ZeroVector;
		FVector3d Accel = FVector3d::ZeroVector;
		TOptional<bool> ButtonA;
	};

	/**
	 * Plays Samples (one every 1 / Hz seconds, starting at the calibrated pose) through Controls' device path, and
	 * builds the phone's frames for the same samples. Returns the device's motions (for the phone-side reference).
	 */
	inline void PlayGesture(IDeviceControlScheme& Scheme, TSharedRef<FRecordingSink> Sink, const FDeviceMotion& Base,
		TConstArrayView<FGestureSample> Samples, double Hz, TArray<Net::FSensorFrame>& OutPhoneFrames, TArray<FDeviceMotion>* OutMotions = nullptr)
	{
		const FMotionConvention Convention = FMotionConvention::IOS();
		FLocalDeviceController Controller(TSharedRef<IDeviceControlScheme>(MakeShareable(&Scheme, [](IDeviceControlScheme*) {})), Sink);
		const double T0 = 100.0;
		double PreviousT = T0;
		for (int32 k = 0; k < Samples.Num(); ++k)
		{
			const double T = T0 + k / Hz;
			FDeviceMotion M = FDeviceGestures::Posed(Base, Samples[k].Rotation, Samples[k].Gyro, Samples[k].Accel);
			M.TimeSeconds = T;
			if (OutMotions)
			{
				OutMotions->Add(M);
			}
			if (Samples[k].ButtonA.IsSet() && *Samples[k].ButtonA)
			{
				Controller.PushButton(EDeviceButton::A, true, T);
			}
			Controller.PushMotion(FromEngine(ToEngine(M, Convention), Convention));
			if (Samples[k].ButtonA.IsSet() && !*Samples[k].ButtonA)
			{
				Controller.PushButton(EDeviceButton::A, false, T);
			}
			const uint16 Dt = k == 0 ? 16 : static_cast<uint16>(FMath::RoundToDouble((T - PreviousT) * 1000.0));
			// The phone calibrates on its first pose too.
			OutPhoneFrames.Add(PhoneAttitudeFrame(M, Samples[0].Rotation.Inverse() * Samples[k].Rotation, static_cast<uint8>(k), Dt));
			PreviousT = T;
		}
	}

	/** The two frames carry the same values: equal after the wire, to within one fixed-point step (a rounding tie). */
	inline bool SameFrame(const Net::FSensorFrame& A, const Net::FSensorFrame& B, FString& OutWhy)
	{
		auto Near = [](double X, double Y, double Step) { return FMath::Abs(X - Y) <= Step * 1.0001; };
		auto NearV = [&Near](const FVector3d& X, const FVector3d& Y, double Step) { return Near(X.X, Y.X, Step) && Near(X.Y, Y.Y, Step) && Near(X.Z, Y.Z, Step); };
		if (A.Flags != B.Flags) { OutWhy = TEXT("flags"); return false; }
		if (A.ScreenOrientation != B.ScreenOrientation) { OutWhy = TEXT("orientation"); return false; }
		if (A.Sequence != B.Sequence) { OutWhy = TEXT("sequence"); return false; }
		if (A.DtMs != B.DtMs) { OutWhy = FString::Printf(TEXT("dtMs %u vs %u"), A.DtMs, B.DtMs); return false; }
		// The Euler angles as the rotation they stand for: two triples can name one pose (gamma's +-90 boundary, beta's poles).
		auto EulerPose = [](const Net::FSensorFrame& F) { return AttitudeFromEuler(FMath::DegreesToRadians(F.Beta), FMath::DegreesToRadians(F.Gamma), FMath::DegreesToRadians(F.Alpha)); };
		const FQuat EA = EulerPose(A), EB = EulerPose(B);
		if (!EA.RotateVector(FVector(1, 0, 0)).Equals(EB.RotateVector(FVector(1, 0, 0)), 2e-3) || !EA.RotateVector(FVector(0, 1, 0)).Equals(EB.RotateVector(FVector(0, 1, 0)), 2e-3)) { OutWhy = TEXT("euler"); return false; }
		if (!NearV(A.Gyro, B.Gyro, 0.05)) { OutWhy = TEXT("gyro"); return false; }
		if (!NearV(A.Accel, B.Accel, 0.001) || !NearV(A.Gravity, B.Gravity, 0.001)) { OutWhy = TEXT("accel/gravity"); return false; }
		const FVector4d DQ = A.Quaternion - B.Quaternion;
		const double QuatStep = 1.0001 / 16384.0;
		if (A.bHasQuaternion != B.bHasQuaternion || FMath::Abs(DQ.X) > QuatStep || FMath::Abs(DQ.Y) > QuatStep || FMath::Abs(DQ.Z) > QuatStep || FMath::Abs(DQ.W) > QuatStep)
		{
			OutWhy = TEXT("quaternion");
			return false;
		}
		return true;
	}

	inline void TestSameFrames(FAutomationTestBase& Test, const FString& What, TConstArrayView<Net::FSensorFrame> Device, TConstArrayView<Net::FSensorFrame> Phone)
	{
		Test.TestEqual(What + TEXT(": frame count"), Device.Num(), Phone.Num());
		for (int32 i = 0; i < FMath::Min(Device.Num(), Phone.Num()); ++i)
		{
			FString Why;
			if (!SameFrame(Device[i], Phone[i], Why))
			{
				Test.AddError(FString::Printf(TEXT("%s: frame %d differs from the phone's (%s)"), *What, i, *Why));
				return;
			}
		}
	}
}

#endif
