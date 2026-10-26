#include "Input/DeviceFrameBuilder.h"

namespace MovecadeKart::Input
{
	namespace
	{
		double Wrap180(double Deg)
		{
			double D = FMath::Fmod(Deg, 360.0);
			if (D > 180.0) D -= 360.0;
			if (D <= -180.0) D += 360.0;
			return D;
		}

		/** The rotation Q with its angle scaled by Factor (about the same axis). */
		FQuat ScaleRotation(const FQuat& Q, double Factor)
		{
			if (Factor == 1.0)
			{
				return Q;
			}
			FVector Axis;
			double Angle = 0.0;
			Q.GetNormalized().ToAxisAndAngle(Axis, Angle);
			if (Angle > UE_DOUBLE_PI)
			{
				Angle -= 2.0 * UE_DOUBLE_PI;
			}
			return FQuat(Axis, Angle * Factor);
		}
	}

	bool FDeviceFrameBuilder::WheelAngle(const FDeviceMotion& Motion, double& OutDegrees)
	{
		// The engine's screen space has Y to the screen's right and Z up it, whichever way the screen is turned.
		const FVector3d Screen = DeviceToScreen(Motion.GravityMs2, Motion.Orientation);
		const double Right = Screen.Y;
		const double Up = Screen.Z;
		if (FMath::Sqrt(Right * Right + Up * Up) <= FlatWheelGravityMs2)
		{
			return false;
		}
		OutDegrees = FMath::RadiansToDegrees(FMath::Atan2(Right, -Up));
		return true;
	}

	void FDeviceFrameBuilder::Calibrate(const FDeviceMotion& Motion)
	{
		Reference = Motion.Attitude.GetNormalized();
		double Wheel = 0.0;
		WheelNeutralDeg = WheelAngle(Motion, Wheel) ? Wheel : 0.0;
		LastWheelDeg = 0.0;
		bCalibrated = true;
	}

	Net::FSensorFrame FDeviceFrameBuilder::Build(const FDeviceMotion& Motion, double DtMs)
	{
		using namespace MovecadeKart::Net;
		FSensorFrame F;
		F.ScreenOrientation = static_cast<uint8>(Motion.Orientation);
		F.Sequence = Sequence++;
		F.DtMs = static_cast<uint16>(FMath::Clamp(FMath::RoundToDouble(FMath::IsFinite(DtMs) ? DtMs : 0.0), 0.0, 65535.0));
		F.Gravity = Motion.GravityMs2;
		if (Kind == EDeviceFrameKind::Wheel)
		{
			F.Flags = SensorFlagCalibrated | SensorFlagHasGravity;
			double Wheel = 0.0;
			if (WheelAngle(Motion, Wheel))
			{
				LastWheelDeg = Wrap180(Wheel - WheelNeutralDeg);
			}
			F.Gamma = LastWheelDeg;
			return QuantizeSensorFrame(F);
		}
		F.Flags = SensorFlagCalibrated | SensorFlagHasGyro | SensorFlagHasAccel | SensorFlagHasGravity | SensorFlagHasQuaternion;
		FQuat Calibrated = ScaleRotation(Reference.Inverse() * Motion.Attitude.GetNormalized(), Sensitivity).GetNormalized();
		// One hemisphere (w >= 0): q and -q are the same rotation, and every reader is sign-blind, but a steady sign
		// keeps the int16 stream continuous.
		if (Calibrated.W < 0.0)
		{
			Calibrated = FQuat(-Calibrated.X, -Calibrated.Y, -Calibrated.Z, -Calibrated.W);
		}
		F.bHasQuaternion = true;
		F.Quaternion = FVector4d(Calibrated.X, Calibrated.Y, Calibrated.Z, Calibrated.W);
		EulerFromAttitude(Calibrated, F.Alpha, F.Beta, F.Gamma);
		F.Gyro = Motion.GyroDegPerSec * Sensitivity;
		F.Accel = Motion.AccelMs2 * Sensitivity;
		return QuantizeSensorFrame(F);
	}
}
