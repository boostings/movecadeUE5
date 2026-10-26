#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceMotion.h"

/**
 * Builds the sensor frames a web controller would send for the device's own motion, so the games see the device
 * exactly as they see a phone (same flags, same calibration, same fixed-point rounding: every frame goes through the
 * wire encoding). Owned by FLocalDeviceController; pure, no UObjects.
 */
namespace MovecadeKart::Input
{
	enum class EDeviceFrameKind : uint8
	{
		/**
		 * Kart's wheel mode (lib/motion.ts, lib/wheel.ts): 30 bytes, CALIBRATED | HAS_GRAVITY, Gamma = the rotation of
		 * gravity in the screen plane atan2(gRight, -gUp) less the calibrated neutral, wrapped to (-180, 180]; held
		 * while the phone lies flat (in-plane gravity <= 1.5 m/s^2).
		 */
		Wheel,
		/**
		 * The motion pad and the arcade controllers: 38 bytes, calibrated attitude quaternion (q_cal^-1 q) and its
		 * Euler angles, gyro, gravity-free acceleration and gravity.
		 */
		Attitude,
	};

	class MOVECADEKART_API FDeviceFrameBuilder
	{
	public:
		/** In-plane gravity at or under this leaves the wheel angle where it was (lib/wheel.ts). */
		static constexpr double FlatWheelGravityMs2 = 1.5;

		explicit FDeviceFrameBuilder(EDeviceFrameKind InKind = EDeviceFrameKind::Attitude) : Kind(InKind) {}

		/** Takes Motion as the neutral pose (the calibrate step: attitude reference and the wheel's level). */
		void Calibrate(const FDeviceMotion& Motion);
		bool IsCalibrated() const { return bCalibrated; }
		/**
		 * Attitude frames only: scales the calibrated rotation's angle and the gyro and acceleration by this (1 is a
		 * phone's frame exactly). Kart applies its sensitivity in the steering transform, like the room host.
		 */
		void SetSensitivity(double InSensitivity) { Sensitivity = FMath::Clamp(InSensitivity, 0.25, 4.0); }
		double GetSensitivity() const { return Sensitivity; }
		EDeviceFrameKind GetKind() const { return Kind; }
		/** The calibrated neutral attitude (identity before calibration). */
		const FQuat& GetReference() const { return Reference; }

		/** The wire frame for Motion, DtMs after the previous one (rounded and clamped like the phone's u16). */
		Net::FSensorFrame Build(const FDeviceMotion& Motion, double DtMs);

		/** The kart wheel angle of a motion before calibration, degrees; false when the phone lies flat. */
		static bool WheelAngle(const FDeviceMotion& Motion, double& OutDegrees);

	private:
		EDeviceFrameKind Kind;
		FQuat Reference = FQuat::Identity;
		double WheelNeutralDeg = 0.0;
		double LastWheelDeg = 0.0;
		double Sensitivity = 1.0;
		uint8 Sequence = 0;
		bool bCalibrated = false;
	};
}
