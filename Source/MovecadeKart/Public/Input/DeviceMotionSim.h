#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceMotion.h"

/**
 * The desktop test path for the on-device controls (not in Shipping builds' gameplay; the types compile everywhere so
 * the tests can use them): scripted gestures, and recordings of real device readings.
 *
 * A script is a function of time that says what the device does: its physical motion (FDeviceMotion, the way a
 * phone would measure it) and its button presses. The subsystem turns the motion into the readings the engine would
 * deliver (ToEngine) and back (FromEngine), so the simulator exercises the same conversion the device does.
 * FDeviceGestures has the motions the phone simulator (Tools/net-sim/phone-sim.mjs) plays, one per game.
 */
namespace MovecadeKart::Input
{
	class IDeviceMotionScript
	{
	public:
		virtual ~IDeviceMotionScript() = default;
		/** The device at T seconds since the script started; Press(button, down) for any button change due by T. */
		virtual FDeviceMotion Step(double T, TFunctionRef<void(EDeviceButton, bool)> Press) = 0;
	};

	/** A script from a motion function and a list of timed presses (repeating every Period seconds when > 0). */
	class MOVECADEKART_API FTimedDeviceScript : public IDeviceMotionScript
	{
	public:
		struct FPress
		{
			double At = 0.0;
			EDeviceButton Button = EDeviceButton::A;
			bool bDown = true;
		};

		FTimedDeviceScript(TFunction<FDeviceMotion(double)> InMotion, TArray<FPress> InPresses, double InPeriod);
		virtual FDeviceMotion Step(double T, TFunctionRef<void(EDeviceButton, bool)> Press) override;

	private:
		TFunction<FDeviceMotion(double)> Motion;
		TArray<FPress> Presses;
		double Period = 0.0;
		int32 NextPress = 0;
		int32 Cycle = 0;
	};

	/** Physical motions, in W3C device axes (see DeviceMotion.h). Time in seconds from the gesture's start. */
	namespace FDeviceGestures
	{
		/** An upright portrait phone, still. */
		MOVECADEKART_API FDeviceMotion Upright(EScreenOrientation Orientation = EScreenOrientation::Portrait);
		/** A phone lying flat on a palm, screen up. */
		MOVECADEKART_API FDeviceMotion Flat();
		/** The attitude Rotation (in the calibrated frame) on top of Base, with its rates and gravity worked out. */
		MOVECADEKART_API FDeviceMotion Posed(const FDeviceMotion& Base, const FQuat& Rotation, const FVector3d& GyroDegPerSec, const FVector3d& AccelMs2);
		/** Kart: a landscape phone turned like a wheel by Degrees (clockwise positive). */
		MOVECADEKART_API FDeviceMotion Wheel(double Degrees);
		/** phone-sim.mjs bladeQuaternion: pitched by Pitch (about x) then turned by Yaw (about y), radians. */
		MOVECADEKART_API FQuat Blade(double Pitch, double Yaw);
	}

	/** One line of a recording: an engine reading, a button change or a touch. */
	struct FDeviceRecordedEvent
	{
		enum class EKind : uint8 { Motion, Button, Touch };
		EKind Kind = EKind::Motion;
		double Time = 0.0;
		FEngineMotionReading Reading;
		EDeviceButton Button = EDeviceButton::A;
		bool bDown = false;
		FDeviceTouch Touch;
	};

	/**
	 * A recording as text, one event a line (times in seconds from the start):
	 *   m,<t>,<tilt x y z>,<rate x y z>,<gravity x y z>,<accel x y z>,<orientation 0-3>
	 *   b,<t>,<button 0-5>,<0|1>
	 *   t,<t>,<phase 0 down 1 move 2 up>,<pointer>,<x>,<y>
	 * Lines starting with # are comments.
	 */
	struct MOVECADEKART_API FDeviceRecording
	{
		TArray<FDeviceRecordedEvent> Events;

		FString ToText() const;
		/** False (and Out untouched) if any line is malformed. */
		static bool FromText(const FString& Text, FDeviceRecording& Out);
	};
}
