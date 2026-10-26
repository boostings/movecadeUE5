#include "Games/Sumo/SumoDeviceControls.h"

#include "Games/Sumo/SumoInput.h"
#include "Input/DeviceMotionSim.h"
#include "Net/RoomProtocol.h"

#define LOCTEXT_NAMESPACE "SumoDeviceControls"

using namespace MovecadeKart::Input;

namespace
{
	constexpr double SumoSendHz = 60.0;
	/** A drag of this much of the surface is a full stick. */
	constexpr double StickReach = 0.15;
	/** The stick leans the pad a little past PhoneTiltFullDeg, so a full stick is a full tilt. */
	constexpr double StickTiltDeg = MovecadeKart::Sumo::PhoneTiltFullDeg * 1.05;

	// The autoplay: phone-sim.mjs's sumo profile (wander: two slow sines per axis, A every 1.7 s held 0.12 s).
	constexpr double SimPeriod = 1.7;
	constexpr double SimHold = 0.12;
}

FSumoDeviceControls::FSumoDeviceControls()
{
	Layout.FrameKind = EDeviceFrameKind::Attitude;
	Layout.SendHz = SumoSendHz;
	Layout.Buttons = { { EDeviceButton::A, LOCTEXT("Dash", "DASH"), EButtonAnchor::BottomRight, true } };
	Layout.bSurface = true;
	Layout.Hint = LOCTEXT("Hint", "Hold flat and tilt to roll · or drag a finger");
}

FQuat FSumoDeviceControls::PadForStick(const FVector2D& Stick)
{
	// Sumo::PhoneTilt reads (pitch, roll) off the screen normal: roll about y leans it right, pitch about -x away.
	const FVector2D S = Stick.GetSafeNormal() * FMath::Min(1.0, Stick.Size());
	return FQuat(FVector(0.0, 1.0, 0.0), FMath::DegreesToRadians(S.X * StickTiltDeg)) * FQuat(FVector(1.0, 0.0, 0.0), -FMath::DegreesToRadians(S.Y * StickTiltDeg));
}

void FSumoDeviceControls::OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink)
{
	if (Button == EDeviceButton::A)
	{
		Sink.SendEvent(bDown ? MovecadeKart::Net::PadPressEvent : MovecadeKart::Net::PadReleaseEvent, MakePadButtonData(TEXT('a')));
	}
}

void FSumoDeviceControls::OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink)
{
	Finger.Feed(Touch);
}

bool FSumoDeviceControls::SynthesizeMotion(double Now, FDeviceMotion& OutMotion)
{
	if (!Finger.bActive)
	{
		return false;
	}
	const FVector2D Drag = Finger.Delta() / StickReach;
	OutMotion = FDeviceGestures::Posed(FDeviceGestures::Flat(), PadForStick(FVector2D(Drag.X, -Drag.Y)), FVector3d::ZeroVector, FVector3d::ZeroVector);
	return true;
}

TSharedPtr<IDeviceMotionScript> FSumoDeviceControls::MakeAutoplayScript() const
{
	return MakeShared<FTimedDeviceScript>([](double T)
	{
		// phone-sim.mjs wander(t), with the script's own (unwrapped) time.
		const double Pitch = FMath::DegreesToRadians(14.0 * FMath::Sin(T * 0.83) + 8.0 * FMath::Sin(T * 2.1));
		const double Roll = FMath::DegreesToRadians(16.0 * FMath::Sin(T * 0.61 + 1.3) + 6.0 * FMath::Sin(T * 1.7));
		return FDeviceGestures::Posed(FDeviceGestures::Flat(), FDeviceGestures::Blade(-Pitch, Roll), FVector3d::ZeroVector, FVector3d::ZeroVector);
	}, TArray<FTimedDeviceScript::FPress>{
		{ SimPeriod - SimHold, EDeviceButton::A, true },
		{ SimPeriod, EDeviceButton::A, false },
	}, SimPeriod);
}

#undef LOCTEXT_NAMESPACE
