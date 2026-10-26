#include "Input/DeviceMotion.h"

namespace MovecadeKart::Input
{
	namespace
	{
		// sensor.ts fixed-point scales.
		constexpr double AngleScale = 100.0;
		constexpr double GyroScale = 20.0;
		constexpr double VectorScale = 1000.0;
		constexpr double QuaternionScale = 16384.0;
		/** cos(beta) under this is gimbal lock for the Z-X'-Y'' angles (beta within ~0.0006 degrees of +-90). */
		constexpr double GimbalLockCos = 1e-5;

		/** JS Math.round (halves go up), then the int16 clamp the encoder applies. */
		int16 ToI16(double Value)
		{
			if (!FMath::IsFinite(Value))
			{
				return 0;
			}
			return static_cast<int16>(FMath::Clamp(FMath::FloorToDouble(Value + 0.5), -32768.0, 32767.0));
		}

		void WriteI16(uint8* Out, int32 Offset, double Value)
		{
			const uint16 Bits = static_cast<uint16>(ToI16(Value));
			Out[Offset] = static_cast<uint8>(Bits & 0xff);
			Out[Offset + 1] = static_cast<uint8>(Bits >> 8);
		}

		/** (-180, 180]. */
		double WrapDegrees(double Deg)
		{
			double D = FMath::Fmod(Deg, 360.0);
			if (D > 180.0) D -= 360.0;
			if (D <= -180.0) D += 360.0;
			return D;
		}
	}

	FMotionConvention FMotionConvention::Android()
	{
		FMotionConvention C;
		C.RotationRateSign = -1.0;
		C.TiltSign = -1.0;
		C.AccelerationSign = 1.0;
		C.GravitySign = -1.0;
		return C;
	}

	FMotionConvention FMotionConvention::ForPlatform()
	{
#if PLATFORM_ANDROID
		return Android();
#else
		return IOS();
#endif
	}

	FVector3d DeviceToScreen(const FVector3d& D, EScreenOrientation Orientation)
	{
		switch (Orientation)
		{
		case EScreenOrientation::PortraitUpsideDown: return FVector3d(-D.Z, -D.X, -D.Y);
		case EScreenOrientation::LandscapeRight: return FVector3d(-D.Z, D.Y, -D.X);
		case EScreenOrientation::LandscapeLeft: return FVector3d(-D.Z, -D.Y, D.X);
		default: return FVector3d(-D.Z, D.X, D.Y);
		}
	}

	FVector3d ScreenToDevice(const FVector3d& S, EScreenOrientation Orientation)
	{
		switch (Orientation)
		{
		case EScreenOrientation::PortraitUpsideDown: return FVector3d(-S.Y, -S.Z, -S.X);
		case EScreenOrientation::LandscapeRight: return FVector3d(-S.Z, S.Y, -S.X);
		case EScreenOrientation::LandscapeLeft: return FVector3d(S.Z, -S.Y, -S.X);
		default: return FVector3d(S.Y, S.Z, -S.X);
		}
	}

	FQuat AttitudeFromEuler(double PitchRad, double RollRad, double YawRad)
	{
		const FQuat Z(FVector(0.0, 0.0, 1.0), YawRad);
		const FQuat X(FVector(1.0, 0.0, 0.0), PitchRad);
		const FQuat Y(FVector(0.0, 1.0, 0.0), RollRad);
		return Z * X * Y;
	}

	void EulerFromAttitude(const FQuat& Attitude, double& OutAlphaDeg, double& OutBetaDeg, double& OutGammaDeg)
	{
		// R = Rz(alpha) Rx(beta) Ry(gamma); Rij = (R e_j)_i.
		const FQuat Q = Attitude.GetNormalized();
		const FVector Ex = Q.RotateVector(FVector(1.0, 0.0, 0.0));
		const FVector Ey = Q.RotateVector(FVector(0.0, 1.0, 0.0));
		const FVector Ez = Q.RotateVector(FVector(0.0, 0.0, 1.0));
		double Beta = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Ey.Z, -1.0, 1.0)));
		double Alpha = 0.0;
		double Gamma = 0.0;
		if (FMath::Sqrt(Ey.X * Ey.X + Ey.Y * Ey.Y) < GimbalLockCos)
		{
			// Upright (beta +-90, a portrait phone held to the face): only alpha + gamma is defined. Take gamma 0, so
			// R = Rz(alpha) Rx(beta) and alpha comes from where x points.
			Alpha = FMath::RadiansToDegrees(FMath::Atan2(Ex.Y, Ex.X));
		}
		else
		{
			Alpha = FMath::RadiansToDegrees(FMath::Atan2(-Ey.X, Ey.Y));
			Gamma = FMath::RadiansToDegrees(FMath::Atan2(-Ex.Z, Ez.Z));
		}
		// W3C ranges: gamma within [-90, 90), beta carries the rest.
		if (Gamma > 90.0 || Gamma < -90.0)
		{
			Gamma += Gamma > 0.0 ? -180.0 : 180.0;
			Beta = (Beta >= 0.0 ? 180.0 : -180.0) - Beta;
			Alpha += 180.0;
		}
		OutAlphaDeg = WrapDegrees(Alpha);
		OutBetaDeg = Beta >= 180.0 ? Beta - 360.0 : Beta;
		OutGammaDeg = Gamma;
	}

	EScreenOrientation ResolveOrientation(TOptional<EScreenOrientation> Reported, bool bPortraitShape)
	{
		if (Reported.IsSet())
		{
			const bool bReportedPortrait = *Reported == EScreenOrientation::Portrait || *Reported == EScreenOrientation::PortraitUpsideDown;
			if (bReportedPortrait == bPortraitShape)
			{
				return *Reported;
			}
		}
		return bPortraitShape ? EScreenOrientation::Portrait : EScreenOrientation::LandscapeLeft;
	}

	FDeviceMotion FromEngine(const FEngineMotionReading& Reading, const FMotionConvention& Convention)
	{
		const EScreenOrientation O = Reading.Orientation;
		FDeviceMotion M;
		M.Orientation = O;
		M.TimeSeconds = Reading.TimeSeconds;
		// The engine reorients the Euler triple (pitch, yaw, roll) like a vector: undo that, then rebuild the rotation.
		const FVector3d Euler = ScreenToDevice(FVector3d(Reading.Tilt) * Convention.TiltSign, O);
		M.Attitude = AttitudeFromEuler(Euler.X, Euler.Z, Euler.Y);
		M.GyroDegPerSec = ScreenToDevice(FVector3d(Reading.RotationRate) * Convention.RotationRateSign, O) * (180.0 / UE_DOUBLE_PI);
		M.AccelMs2 = ScreenToDevice(FVector3d(Reading.Acceleration), O) * (StandardGravity * Convention.AccelerationSign);
		M.GravityMs2 = ScreenToDevice(FVector3d(Reading.Gravity), O) * (StandardGravity * Convention.GravitySign);
		return M;
	}

	FEngineMotionReading ToEngine(const FDeviceMotion& Motion, const FMotionConvention& Convention)
	{
		const EScreenOrientation O = Motion.Orientation;
		FEngineMotionReading R;
		R.Orientation = O;
		R.TimeSeconds = Motion.TimeSeconds;
		// Back to Core Motion's Euler: alpha = yaw, beta = pitch, gamma = roll.
		double Alpha = 0.0, Beta = 0.0, Gamma = 0.0;
		EulerFromAttitude(Motion.Attitude, Alpha, Beta, Gamma);
		const FVector3d Euler(FMath::DegreesToRadians(Beta), FMath::DegreesToRadians(Alpha), FMath::DegreesToRadians(Gamma));
		R.Tilt = FVector(DeviceToScreen(Euler, O) / Convention.TiltSign);
		R.RotationRate = FVector(DeviceToScreen(Motion.GyroDegPerSec * (UE_DOUBLE_PI / 180.0), O) / Convention.RotationRateSign);
		R.Acceleration = FVector(DeviceToScreen(Motion.AccelMs2 / (StandardGravity * Convention.AccelerationSign), O));
		R.Gravity = FVector(DeviceToScreen(Motion.GravityMs2 / (StandardGravity * Convention.GravitySign), O));
		return R;
	}

	TArray<uint8, TFixedAllocator<Net::SensorFrameV2Bytes>> EncodeSensorFrame(const Net::FSensorFrame& Frame)
	{
		using namespace MovecadeKart::Net;
		const bool bQuaternion = (Frame.Flags & SensorFlagHasQuaternion) != 0;
		TArray<uint8, TFixedAllocator<SensorFrameV2Bytes>> Bytes;
		Bytes.SetNumZeroed(bQuaternion ? SensorFrameV2Bytes : SensorFrameBytes);
		uint8* Out = Bytes.GetData();
		Out[0] = SensorFrameType;
		Out[1] = Frame.Flags & SensorFlagMask;
		Out[2] = FMath::Min<uint8>(Frame.ScreenOrientation, 3);
		Out[3] = Frame.Sequence;
		WriteI16(Out, 4, Frame.Alpha * AngleScale);
		WriteI16(Out, 6, Frame.Beta * AngleScale);
		WriteI16(Out, 8, Frame.Gamma * AngleScale);
		for (int32 i = 0; i < 3; ++i)
		{
			WriteI16(Out, 10 + i * 2, Frame.Gyro[i] * GyroScale);
			WriteI16(Out, 16 + i * 2, Frame.Accel[i] * VectorScale);
			WriteI16(Out, 22 + i * 2, Frame.Gravity[i] * VectorScale);
		}
		Out[28] = static_cast<uint8>(Frame.DtMs & 0xff);
		Out[29] = static_cast<uint8>(Frame.DtMs >> 8);
		if (bQuaternion)
		{
			WriteI16(Out, 30, Frame.Quaternion.X * QuaternionScale);
			WriteI16(Out, 32, Frame.Quaternion.Y * QuaternionScale);
			WriteI16(Out, 34, Frame.Quaternion.Z * QuaternionScale);
			WriteI16(Out, 36, Frame.Quaternion.W * QuaternionScale);
		}
		return Bytes;
	}

	Net::FSensorFrame QuantizeSensorFrame(const Net::FSensorFrame& Frame)
	{
		const auto Bytes = EncodeSensorFrame(Frame);
		Net::FSensorFrame Out;
		ensure(Net::DecodeSensorFrame(Bytes.GetData(), Bytes.Num(), Out));
		Out.PlayerIndex = Frame.PlayerIndex;
		return Out;
	}
}
