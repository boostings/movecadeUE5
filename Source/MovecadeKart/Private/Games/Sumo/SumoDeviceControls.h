#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchGestures.h"

/**
 * Sumo Bumpers on the device, as the motion pad plays it: the device held flat is a tilt pad (attitude frames; the
 * host reads the lean with Sumo::PhoneTilt) and DASH is the pad's A (pad:press / pad:release {button:"a"}). Touch
 * fallback: a floating stick, put a finger down anywhere and drag; it leans the pad the way a palm would.
 */
class FSumoDeviceControls : public MovecadeKart::Input::IDeviceControlScheme
{
public:
	FSumoDeviceControls();

	virtual const MovecadeKart::Input::FDeviceLayout& GetLayout() const override { return Layout; }
	virtual void OnButton(MovecadeKart::Input::EDeviceButton Button, bool bDown, double Now, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnSurface(const MovecadeKart::Input::FDeviceTouch& Touch, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual bool SynthesizeMotion(double Now, MovecadeKart::Input::FDeviceMotion& OutMotion) override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> MakeAutoplayScript() const override;

	/** The calibrated pad for a stick (-1..1, x right, y away from the player). */
	static FQuat PadForStick(const FVector2D& Stick);

private:
	MovecadeKart::Input::FDeviceLayout Layout;
	MovecadeKart::Input::FTouchTrack Finger;
};
