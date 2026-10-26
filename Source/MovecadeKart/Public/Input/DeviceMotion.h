#pragma once

#include "CoreMinimal.h"
#include "Net/RoomProtocol.h"

/**
 * The device's own motion, as the on-device controls read it (Docs: Input/ in Architecture). Two spaces meet here:
 *
 *  - The engine's: FMotionEvent from Slate (FIOSInputInterface / FAndroidInputInterface with ios/android.UnifyMotionSpace
 *    = 1, the default since 5.5). Vectors are screen-relative and left-handed: X into the screen, Y to the screen's
 *    right, Z up the screen, for whatever way the screen is turned. Tilt is the attitude's Euler triple (pitch, yaw,
 *    roll, radians) pushed through the same reorientation; rotation rate is rad/s; gravity and user acceleration are
 *    in g.
 *  - The phone's: what a web controller puts in a sensor frame (sensor.ts, Docs/Port/04 §6.1, 05 §2). W3C device axes
 *    (x to the right of the screen in portrait, y up the long side, z out of the screen), gyro deg/s about x, y, z,
 *    acceleration (gravity removed) m/s^2, gravity m/s^2 pointing down (an upright portrait phone reads (0, -9.81, 0)),
 *    and the attitude as a W3C Z-X'-Y'' rotation.
 *
 * FromEngine turns the first into the second; ToEngine is its exact inverse (the desktop simulator and the tests use
 * it to produce what the engine would deliver for a known motion). The platforms' sign conventions differ and live in
 * one place, FMotionConvention. Pure functions: no UObjects.
 */
namespace MovecadeKart::Input
{
	/** The wire's screenOrientation byte: 0 portrait, 1 landscape-left, 2 upside-down, 3 landscape-right. */
	enum class EScreenOrientation : uint8
	{
		Portrait = 0,
		LandscapeLeft = 1,
		PortraitUpsideDown = 2,
		LandscapeRight = 3,
	};

	/** Standard gravity: WebKit's kGravity, what a Safari phone multiplies Core Motion's g by. */
	constexpr double StandardGravity = 9.80665;

	/** One FMotionEvent, in the engine's unified motion space (see above). */
	struct FEngineMotionReading
	{
		FVector Tilt = FVector::ZeroVector;
		FVector RotationRate = FVector::ZeroVector;
		FVector Gravity = FVector::ZeroVector;
		FVector Acceleration = FVector::ZeroVector;
		EScreenOrientation Orientation = EScreenOrientation::Portrait;
		double TimeSeconds = 0.0;
	};

	/** The same motion as a phone measures it (W3C device axes and units). */
	struct FDeviceMotion
	{
		/** Device to earth, W3C Z-X'-Y'' (alpha about z, then beta about x, then gamma about y). */
		FQuat Attitude = FQuat::Identity;
		FVector3d GyroDegPerSec = FVector3d::ZeroVector;
		/** Gravity removed. */
		FVector3d AccelMs2 = FVector3d::ZeroVector;
		/** Pointing down: (0, -9.81, 0) for an upright portrait phone. */
		FVector3d GravityMs2 = FVector3d(0.0, -StandardGravity, 0.0);
		EScreenOrientation Orientation = EScreenOrientation::Portrait;
		double TimeSeconds = 0.0;
	};

	/**
	 * How a platform's unified readings differ from Core Motion's own sign conventions. iOS (the engine passes Core
	 * Motion through, reoriented): Core Motion's userAcceleration has the opposite sign of the W3C acceleration WebKit
	 * reports, its gravity and right-handed rotation rate the same. Android (FAndroidInputInterface::QueueMotionData
	 * negates the rate and the tilt): the defaults below follow the engine source and still need a device check.
	 */
	struct MOVECADEKART_API FMotionConvention
	{
		double RotationRateSign = 1.0;
		double TiltSign = 1.0;
		double AccelerationSign = -1.0;
		double GravitySign = 1.0;

		static FMotionConvention IOS() { return FMotionConvention(); }
		static FMotionConvention Android();
		/** The running platform's (iOS everywhere else, so the desktop simulator round-trips like an iPhone). */
		static FMotionConvention ForPlatform();
	};

	/** A device-axes vector as the engine's unified space reorients it for Orientation (IOSInputInterface.cpp). */
	MOVECADEKART_API FVector3d DeviceToScreen(const FVector3d& Device, EScreenOrientation Orientation);
	/** The inverse of DeviceToScreen. */
	MOVECADEKART_API FVector3d ScreenToDevice(const FVector3d& Screen, EScreenOrientation Orientation);

	/** The W3C attitude for Euler angles in radians (Core Motion's pitch about x, roll about y, yaw about z). */
	MOVECADEKART_API FQuat AttitudeFromEuler(double PitchRad, double RollRad, double YawRad);
	/** The W3C deviceorientation angles of an attitude, degrees: alpha (-180, 180], beta [-180, 180), gamma [-90, 90]. */
	MOVECADEKART_API void EulerFromAttitude(const FQuat& Attitude, double& OutAlphaDeg, double& OutBetaDeg, double& OutGammaDeg);

	MOVECADEKART_API FDeviceMotion FromEngine(const FEngineMotionReading& Reading, const FMotionConvention& Convention);
	MOVECADEKART_API FEngineMotionReading ToEngine(const FDeviceMotion& Motion, const FMotionConvention& Convention);

	/**
	 * protocol/src/sensor.ts encodeSensorFrame: 30 bytes, or 38 with the quaternion flag, little-endian, each value
	 * rounded the way the phone rounds (Math.round) and clamped to int16. The frame's PlayerIndex is not written.
	 */
	MOVECADEKART_API TArray<uint8, TFixedAllocator<Net::SensorFrameV2Bytes>> EncodeSensorFrame(const Net::FSensorFrame& Frame);
	/** The frame the host receives for Frame after the wire: encoded, then decoded (bit-identical to a phone's). */
	MOVECADEKART_API Net::FSensorFrame QuantizeSensorFrame(const Net::FSensorFrame& Frame);
}
