#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchGestures.h"

/**
 * Fishing Frenzy on the device, as the motion pad plays it: attitude frames at 100 Hz (the host's FFishingMotion
 * reads the cast flick, the hooking jerk and the reel's wrist roll from the gyro), and the pad's A to hold-and-cast
 * or to hook (pad:press / pad:release). Touch fallback: circle a finger on the screen to reel (the crank turns into
 * the wrist roll a phone would send).
 */
class FFishingDeviceControls : public MovecadeKart::Input::IDeviceControlScheme
{
public:
	FFishingDeviceControls();

	virtual const MovecadeKart::Input::FDeviceLayout& GetLayout() const override { return Layout; }
	virtual void OnButton(MovecadeKart::Input::EDeviceButton Button, bool bDown, double Now, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnSurface(const MovecadeKart::Input::FDeviceTouch& Touch, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual bool SynthesizeMotion(double Now, MovecadeKart::Input::FDeviceMotion& OutMotion) override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> MakeAutoplayScript() const override;

	/** The wrist roll (deg/s about the phone's long axis) a crank of RevPerSec stands for. */
	static double WristRollFor(double RevPerSec) { return RevPerSec * 360.0; }

private:
	MovecadeKart::Input::FDeviceLayout Layout;
	MovecadeKart::Input::FTouchTrack Finger;
	MovecadeKart::Input::FTouchCrank Crank;
};
