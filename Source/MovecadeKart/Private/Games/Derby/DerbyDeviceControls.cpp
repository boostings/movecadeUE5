#include "Games/Derby/DerbyDeviceControls.h"

#include "Input/DeviceMotionSim.h"
#include "Net/RoomProtocol.h"

#define LOCTEXT_NAMESPACE "DerbyDeviceControls"

using namespace MovecadeKart::Input;

namespace
{
	constexpr double DerbySendHz = 60.0;

	// phone-sim.mjs startDerby: the barrel sweeps 270 degrees from behind round to the follow-through, square contact
	// (heading 90) near the peak of a sine-bell of speed.
	constexpr double SwingStartHeadingDeg = 225.0;
	constexpr double SwingSweepDeg = 270.0;

	// The autoplay: READY, then a 0.3 s swing every 2.5 s (the sim can't see the pitch; the host takes what lands).
	constexpr double SimPeriod = 2.5;
	constexpr double SimReadyAt = 0.4;
	constexpr double SimSwingAt = 1.6;
	constexpr double SimSwingSeconds = 0.3;
	constexpr double SimPressSeconds = 0.1;
}

FDerbyDeviceControls::FDerbyDeviceControls()
{
	Layout.FrameKind = EDeviceFrameKind::Attitude;
	Layout.SendHz = DerbySendHz;
	Layout.Buttons = { { EDeviceButton::A, LOCTEXT("Ready", "READY"), EButtonAnchor::BottomRight, true } };
	Layout.bSurface = true;
	Layout.Hint = LOCTEXT("Hint", "Hold it by the bottom, swing as the ball arrives · or tap");
}

void FDerbyDeviceControls::OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink)
{
	if (Button == EDeviceButton::A)
	{
		Sink.SendEvent(bDown ? MovecadeKart::Net::PadPressEvent : MovecadeKart::Net::PadReleaseEvent, MakePadButtonData(TEXT('a')));
	}
}

void FDerbyDeviceControls::OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink)
{
	// A tap anywhere is the big button (READY, or the button swing): down on down, so the timing is the finger's.
	if (!Finger.Feed(Touch))
	{
		return;
	}
	if (Touch.Phase == ETouchPhase::Down)
	{
		Sink.SendEvent(MovecadeKart::Net::PadPressEvent, MakePadButtonData(TEXT('a')));
	}
	else if (Touch.Phase == ETouchPhase::Up)
	{
		Sink.SendEvent(MovecadeKart::Net::PadReleaseEvent, MakePadButtonData(TEXT('a')));
	}
}

FDeviceMotion FDerbyDeviceControls::Swing(double K, double Seconds, double AimDeg)
{
	const double Heading = SwingStartHeadingDeg + AimDeg - SwingSweepDeg * (1.0 - FMath::Cos(UE_DOUBLE_PI * K)) / 2.0;
	const double RateDegPerSec = SwingSweepDeg * UE_DOUBLE_HALF_PI * FMath::Sin(UE_DOUBLE_PI * K) / Seconds;
	const FQuat Bat = FQuat(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(-Heading)) * FQuat(FVector(1.0, 0.0, 0.0), -UE_DOUBLE_HALF_PI);
	// The swing turns about the vertical: in the device's axes, that's the world rate seen from the bat.
	const FVector3d Gyro(Bat.UnrotateVector(FVector(0.0, RateDegPerSec, 0.0)));
	return FDeviceGestures::Posed(FDeviceGestures::Upright(), Bat, Gyro, FVector3d::ZeroVector);
}

TSharedPtr<IDeviceMotionScript> FDerbyDeviceControls::MakeAutoplayScript() const
{
	return MakeShared<FTimedDeviceScript>([](double Time)
	{
		const double K = (FMath::Fmod(Time, SimPeriod) - SimSwingAt) / SimSwingSeconds;
		return K >= 0.0 && K <= 1.0 ? Swing(K, SimSwingSeconds, 0.0) : FDeviceGestures::Upright();
	}, TArray<FTimedDeviceScript::FPress>{
		{ SimReadyAt, EDeviceButton::A, true },
		{ SimReadyAt + SimPressSeconds, EDeviceButton::A, false },
	}, SimPeriod);
}

#undef LOCTEXT_NAMESPACE
