#pragma once

#include "CoreMinimal.h"
#include "Games/Bowling/BowlingThrow.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchGestures.h"

/**
 * Bowling on the device, as BowlingController.tsx plays it (Docs/Port/06-bowling.md §2): hold GRIP, swing the arm,
 * let go; the device's own throw detector measures the release and sends bowling:release, a phone's payload. The
 * frames go to the host too (the lane guide). Touch fallback: roll a finger up the lane: where it starts is the line,
 * its speed the power, its drift sideways on the way up the hook.
 */
class FBowlingDeviceControls : public MovecadeKart::Input::IDeviceControlScheme
{
public:
	FBowlingDeviceControls();

	virtual const MovecadeKart::Input::FDeviceLayout& GetLayout() const override { return Layout; }
	virtual void OnSample(const MovecadeKart::Net::FSensorFrame& Frame, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnButton(MovecadeKart::Input::EDeviceButton Button, bool bDown, double Now, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual void OnSurface(const MovecadeKart::Input::FDeviceTouch& Touch, MovecadeKart::Input::IDeviceInputSink& Sink) override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> MakeAutoplayScript() const override;

	/** bowling:release's payload, as the phone writes it. */
	static TSharedPtr<FJsonObject> ReleasePayload(const MovecadeKart::Bowling::FThrowRelease& Release);
	/** The swipe fallback's release for a finger rolled up the screen (positions 0..1, y down). */
	static MovecadeKart::Bowling::FThrowRelease SwipeRelease(const MovecadeKart::Input::FTouchTrack& Swipe);

private:
	MovecadeKart::Input::FDeviceLayout Layout;
	MovecadeKart::Bowling::FThrowDetector Detector;
	MovecadeKart::Input::FTouchTrack Swipe;
};
