#include "Games/Stacker/StackerDeviceControls.h"

#include "Input/DeviceMotionSim.h"

#define LOCTEXT_NAMESPACE "StackerDeviceControls"

using namespace MovecadeKart::Input;

namespace
{
	constexpr double StackerSendHz = 60.0;
	const TCHAR* const DropEvent = TEXT("stacker:drop");

	/** A drag of the whole surface width (or height) leans the palm this far (the local stick's full tilt). */
	constexpr double DragFullTiltDeg = 30.0;
	/** Past this travel a touch is a drag (it leans), not a tap (it drops). */
	constexpr double DragTravel = 0.04;

	// The autoplay: phone-sim.mjs startStacker (the palm rolling 4 degrees either way, DROP every 1.3 s).
	constexpr double SimPeriod = 1.3;
	constexpr double SimRollDeg = 4.0;
	constexpr double SimRollRate = 0.9;
}

FStackerDeviceControls::FStackerDeviceControls()
{
	Layout.FrameKind = EDeviceFrameKind::Attitude;
	Layout.SendHz = StackerSendHz;
	Layout.Buttons = { { EDeviceButton::A, LOCTEXT("Drop", "DROP"), EButtonAnchor::BottomCentre, true } };
	Layout.bSurface = true;
	Layout.Hint = LOCTEXT("Hint", "Balance on your palm · tap to drop");
}

FQuat FStackerDeviceControls::Palm(double RollDeg, double PitchDeg)
{
	// The screen normal leans across by Roll (about y) and away by Pitch (about -x): runtime.ts's roll and pitch.
	return FQuat(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(RollDeg)) * FQuat(FVector(1.0, 0.0, 0.0), -FMath::DegreesToRadians(PitchDeg));
}

void FStackerDeviceControls::OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink)
{
	// StackerController.tsx drops on pointerdown.
	if (Button == EDeviceButton::A && bDown)
	{
		Sink.SendEvent(DropEvent, nullptr);
	}
}

void FStackerDeviceControls::OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink)
{
	if (!Finger.Feed(Touch))
	{
		return;
	}
	bDragging |= Finger.Delta().Size() > DragTravel;
	if (Touch.Phase == ETouchPhase::Up)
	{
		if (!bDragging)
		{
			Sink.SendEvent(DropEvent, nullptr);
		}
		bDragging = false;
	}
}

bool FStackerDeviceControls::SynthesizeMotion(double Now, FDeviceMotion& OutMotion)
{
	if (!Finger.bActive || !bDragging)
	{
		return false;
	}
	const FVector2D Lean = Finger.Delta() * (2.0 * DragFullTiltDeg);
	OutMotion = FDeviceGestures::Posed(FDeviceGestures::Flat(), Palm(Lean.X, -Lean.Y), FVector3d::ZeroVector, FVector3d::ZeroVector);
	return true;
}

TSharedPtr<IDeviceMotionScript> FStackerDeviceControls::MakeAutoplayScript() const
{
	return MakeShared<FTimedDeviceScript>([](double T)
	{
		return FDeviceGestures::Posed(FDeviceGestures::Flat(), Palm(SimRollDeg * FMath::Sin(T * SimRollRate), 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector);
	}, TArray<FTimedDeviceScript::FPress>{
		{ SimPeriod - 0.05, EDeviceButton::A, true },
		{ SimPeriod - 0.01, EDeviceButton::A, false },
	}, SimPeriod);
}

#undef LOCTEXT_NAMESPACE
