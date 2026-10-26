#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchGestures.h"

/**
 * Samurai Slice on the device: the device is the katana. Its calibrated attitude streams as 100 Hz frames (the host
 * resolves the cuts from the blade, bladeIntent.ts gating on the gyro), exactly as the phone controller sends them.
 * Touch fallback: a finger on the screen holds the blade; where it is aims the tip (the top of the screen raises the
 * guard, the bottom cuts down through the fruit, the sides turn it) and how fast it moves is the swing's gyro.
 */
class FSamuraiDeviceControls : public MovecadeKart::Input::IDeviceControlScheme
{
public:
	FSamuraiDeviceControls();

	virtual const MovecadeKart::Input::FDeviceLayout& GetLayout() const override { return Layout; }
	virtual void OnSurface(const MovecadeKart::Input::FDeviceTouch& Touch, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual bool SynthesizeMotion(double Now, MovecadeKart::Input::FDeviceMotion& OutMotion) override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> MakeAutoplayScript() const override;

	/** The calibrated blade attitude for a finger at Position (0..1 of the screen, y down). */
	static FQuat BladeForFinger(const FVector2D& Position);

private:
	MovecadeKart::Input::FDeviceLayout Layout;
	MovecadeKart::Input::FTouchTrack Finger;
	/** The touch blade keeps the motion until this (it lowers back to the guard after the finger lifts). */
	double HoldUntil = -1.0;
	FQuat LastBlade = FQuat::Identity;
	double LastAt = -1.0;
};
