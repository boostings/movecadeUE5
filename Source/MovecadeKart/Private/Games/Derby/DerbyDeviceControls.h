#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchGestures.h"

/**
 * Home Run Derby on the device, as the motion pad plays it: the device is the bat (attitude frames; the host's swing
 * detector times and aims the swing), and READY is the pad's A (pad:press / pad:release), which also swings when
 * pressed while the ball is on its way (the no-gyro path). Touch fallback: tap anywhere for the same A.
 */
class FDerbyDeviceControls : public MovecadeKart::Input::IDeviceControlScheme
{
public:
	FDerbyDeviceControls();

	virtual const MovecadeKart::Input::FDeviceLayout& GetLayout() const override { return Layout; }
	virtual void OnButton(MovecadeKart::Input::EDeviceButton Button, bool bDown, double Now, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnSurface(const MovecadeKart::Input::FDeviceTouch& Touch, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> MakeAutoplayScript() const override;

	/** phone-sim.mjs's bat: the calibrated attitude and device-axes gyro at K (0..1) through a swing of Seconds, Aim degrees off square. */
	static MovecadeKart::Input::FDeviceMotion Swing(double K, double Seconds, double AimDeg);

private:
	MovecadeKart::Input::FDeviceLayout Layout;
	MovecadeKart::Input::FTouchTrack Finger;
};
