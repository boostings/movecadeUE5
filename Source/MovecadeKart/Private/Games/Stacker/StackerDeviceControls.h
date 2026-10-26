#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchGestures.h"

/**
 * Sky Stacker on the device, as StackerController.tsx plays it: the palm's tilt streams as attitude frames (the host
 * balances the tower on the quaternion's roll and pitch) and a tap drops the block (stacker:drop, no payload). Tap
 * anywhere, or the DROP button. Touch fallback for the palm: drag a finger and the palm leans that way.
 */
class FStackerDeviceControls : public MovecadeKart::Input::IDeviceControlScheme
{
public:
	FStackerDeviceControls();

	virtual const MovecadeKart::Input::FDeviceLayout& GetLayout() const override { return Layout; }
	virtual void OnButton(MovecadeKart::Input::EDeviceButton Button, bool bDown, double Now, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnSurface(const MovecadeKart::Input::FDeviceTouch& Touch, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual bool SynthesizeMotion(double Now, MovecadeKart::Input::FDeviceMotion& OutMotion) override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> MakeAutoplayScript() const override;

	/** The calibrated palm for a lean of Roll and Pitch degrees (PalmFromFrame reads them back). */
	static FQuat Palm(double RollDeg, double PitchDeg);

private:
	MovecadeKart::Input::FDeviceLayout Layout;
	MovecadeKart::Input::FTouchTrack Finger;
	bool bDragging = false;
};
