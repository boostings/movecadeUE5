#pragma once

#include "CoreMinimal.h"
#include "Games/Darts/DartsThrow.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchGestures.h"

/**
 * Bullseye Bash on the device, as DartsController.tsx plays it (Docs/Port/07-darts.md §3): the device runs the throw
 * detector on its own motion; holding THROW grips, the arm throws, and letting go sends darts:release with the
 * detector's gesture, the payload a phone sends. The frames go to the host too (the reticle). Touch fallback: flick a
 * finger up the screen (the mouse flick's feel): where it ends aims, its speed is the power, its sideways drift spins.
 */
class FDartsDeviceControls : public MovecadeKart::Input::IDeviceControlScheme
{
public:
	FDartsDeviceControls();

	virtual const MovecadeKart::Input::FDeviceLayout& GetLayout() const override { return Layout; }
	virtual void OnSample(const MovecadeKart::Net::FSensorFrame& Frame, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnButton(MovecadeKart::Input::EDeviceButton Button, bool bDown, double Now, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnSurface(const MovecadeKart::Input::FDeviceTouch& Touch, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnCalibrated() override { Detector.Recentre(); }
	virtual TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> MakeAutoplayScript() const override;

	/** darts:release's payload for a gesture, as the phone writes it. */
	static TSharedPtr<FJsonObject> ReleasePayload(const MovecadeKart::Darts::FThrowGesture& Gesture);
	/** The swipe fallback's gesture for a finger flick (positions 0..1, y down). */
	static MovecadeKart::Darts::FDartGesture SwipeGesture(const MovecadeKart::Input::FTouchTrack& Swipe);

private:
	MovecadeKart::Input::FDeviceLayout Layout;
	MovecadeKart::Darts::FDartThrowDetector Detector;
	MovecadeKart::Input::FTouchTrack Swipe;
};
