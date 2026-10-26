#include "Games/Fishing/FishingDeviceControls.h"

#include "Input/DeviceMotionSim.h"
#include "Net/RoomProtocol.h"

#define LOCTEXT_NAMESPACE "FishingDeviceControls"

using namespace MovecadeKart::Input;

namespace
{
	/** phone-sim.mjs's fishing profile streams at 100 Hz. */
	constexpr double FishingSendHz = 100.0;
	/** Under this the crank is at rest (the touch reel lets the device's own motion back in). */
	constexpr double CrankRestRevPerSec = 0.05;

	// The autoplay: phone-sim.mjs fishingPose on a timer (it can't see the prompts): hold A to cast, a jerk to hook,
	// then the wrist rolled in bursts to reel, every 9 s.
	constexpr double SimPeriod = 9.0;
	constexpr double SimCastAt = 0.5;
	constexpr double SimCastHold = 0.7;
	constexpr double SimJerkAt = 3.5;
	constexpr double SimJerkSeconds = 0.16;
	constexpr double SimJerkDegPerSec = 750.0;
	constexpr double SimReelFrom = 4.0;
	constexpr double SimReelBurst = 1.6;
	constexpr double SimReelOnShare = 0.75;
	constexpr double SimReelDegPerSec = 420.0;
	constexpr double SimHookAt = 3.6;
}

FFishingDeviceControls::FFishingDeviceControls()
{
	Layout.FrameKind = EDeviceFrameKind::Attitude;
	Layout.SendHz = FishingSendHz;
	Layout.Buttons = { { EDeviceButton::A, LOCTEXT("Cast", "CAST"), EButtonAnchor::BottomRight, true } };
	Layout.bSurface = true;
	// A landscape HUD with its clock at the top centre.
	Layout.Chips = EChipPlacement::LeftEdge;
	Layout.Hint = LOCTEXT("Hint", "Flick to cast, jerk up to hook, roll the wrist to reel · or circle a finger");
}

void FFishingDeviceControls::OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink)
{
	if (Button == EDeviceButton::A)
	{
		Sink.SendEvent(bDown ? MovecadeKart::Net::PadPressEvent : MovecadeKart::Net::PadReleaseEvent, MakePadButtonData(TEXT('a')));
	}
}

void FFishingDeviceControls::OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink)
{
	if (Finger.Feed(Touch) && Touch.Phase != ETouchPhase::Up)
	{
		Crank.Feed(Touch.Position, Touch.Time);
	}
}

bool FFishingDeviceControls::SynthesizeMotion(double Now, FDeviceMotion& OutMotion)
{
	if (!Finger.bActive)
	{
		Crank.Coast(Now);
	}
	if (!Finger.bActive && Crank.RevPerSec < CrankRestRevPerSec)
	{
		return false;
	}
	OutMotion = FDeviceGestures::Posed(FDeviceGestures::Upright(), FQuat::Identity, FVector3d(0.0, WristRollFor(Crank.RevPerSec), 0.0), FVector3d::ZeroVector);
	return true;
}

TSharedPtr<IDeviceMotionScript> FFishingDeviceControls::MakeAutoplayScript() const
{
	return MakeShared<FTimedDeviceScript>([](double Time)
	{
		const double T = FMath::Fmod(Time, SimPeriod);
		FVector3d Gyro = FVector3d::ZeroVector;
		const double J = (T - SimJerkAt) / SimJerkSeconds;
		if (J >= 0.0 && J <= 1.0)
		{
			Gyro.X = SimJerkDegPerSec * FMath::Sin(UE_DOUBLE_PI * J);
		}
		if (T >= SimReelFrom && FMath::Fmod((T - SimReelFrom) / SimReelBurst, 1.0) < SimReelOnShare)
		{
			Gyro.Y = SimReelDegPerSec;
		}
		return FDeviceGestures::Posed(FDeviceGestures::Upright(), FQuat::Identity, Gyro, FVector3d::ZeroVector);
	}, TArray<FTimedDeviceScript::FPress>{
		{ SimCastAt, EDeviceButton::A, true },
		{ SimCastAt + SimCastHold, EDeviceButton::A, false },
		// A press hooks too, in case the jerk came early.
		{ SimHookAt, EDeviceButton::A, true },
		{ SimHookAt + 0.05, EDeviceButton::A, false },
	}, SimPeriod);
}

#undef LOCTEXT_NAMESPACE
