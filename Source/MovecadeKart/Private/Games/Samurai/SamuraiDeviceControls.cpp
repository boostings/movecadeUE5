#include "Games/Samurai/SamuraiDeviceControls.h"

#include "Input/DeviceMotionSim.h"

#define LOCTEXT_NAMESPACE "SamuraiDeviceControls"

using namespace MovecadeKart::Input;

namespace
{
	/** The samurai controller's rate (Docs/Port/08-samurai.md: sendHz 100). */
	constexpr double SamuraiSendHz = 100.0;

	// The touch blade, in phone-sim.mjs's bladeQuaternion terms (radians): the top of the screen holds the guard up,
	// the bottom has cut through (its sweep is 0.5 -> -1.7), the sides turn it.
	constexpr double FingerPitchTop = 0.5;
	constexpr double FingerPitchBottom = -1.7;
	constexpr double FingerYawRange = 0.7;
	/** After the finger lifts, the touch blade holds for this long, then the device's own motion takes over. */
	constexpr double TouchHoldSeconds = 0.4;

	// The autoplay: phone-sim.mjs startSamurai (a 0.3 s cut every 0.9 s at a random yaw, gyro 420 through it).
	constexpr double SimPeriod = 0.9;
	constexpr double SimCutAt = 0.3;
	constexpr double SimCutSeconds = 0.3;
	constexpr double SimRestPitch = 0.25;
	constexpr double SimCutGyro = 420.0;
	constexpr double SimYawSpread = 0.7;
}

FSamuraiDeviceControls::FSamuraiDeviceControls()
{
	Layout.FrameKind = EDeviceFrameKind::Attitude;
	Layout.SendHz = SamuraiSendHz;
	Layout.bSurface = true;
	Layout.Hint = LOCTEXT("Hint", "Hold the phone up like a katana and slash · or swipe");
}

FQuat FSamuraiDeviceControls::BladeForFinger(const FVector2D& Position)
{
	const double Pitch = FMath::Lerp(FingerPitchTop, FingerPitchBottom, FMath::Clamp(Position.Y, 0.0, 1.0));
	const double Yaw = -(FMath::Clamp(Position.X, 0.0, 1.0) - 0.5) * 2.0 * FingerYawRange;
	return FDeviceGestures::Blade(Pitch, Yaw);
}

void FSamuraiDeviceControls::OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink)
{
	if (Finger.Feed(Touch) && Touch.Phase == ETouchPhase::Up)
	{
		HoldUntil = Touch.Time + TouchHoldSeconds;
	}
}

bool FSamuraiDeviceControls::SynthesizeMotion(double Now, FDeviceMotion& OutMotion)
{
	if (!Finger.bActive && Now > HoldUntil)
	{
		LastAt = -1.0;
		return false;
	}
	const FQuat Blade = BladeForFinger(Finger.Last);
	// The swing's gyro is the blade's own turn rate: a slow drag stays under the intent gate, a slash opens it.
	double RateDegPerSec = 0.0;
	if (LastAt >= 0.0 && Now > LastAt)
	{
		RateDegPerSec = FMath::RadiansToDegrees(LastBlade.AngularDistance(Blade)) / (Now - LastAt);
	}
	LastBlade = Blade;
	LastAt = Now;
	OutMotion = FDeviceGestures::Posed(FDeviceGestures::Upright(), Blade, FVector3d(RateDegPerSec, 0.0, 0.0), FVector3d::ZeroVector);
	return true;
}

TSharedPtr<IDeviceMotionScript> FSamuraiDeviceControls::MakeAutoplayScript() const
{
	TSharedRef<FRandomStream> Rng = MakeShared<FRandomStream>(0x5a3c);
	TSharedRef<double> Yaw = MakeShared<double>(0.0);
	return MakeShared<FTimedDeviceScript>([Rng, Yaw](double Time)
	{
		const double T = FMath::Fmod(Time, SimPeriod);
		const double K = FMath::Clamp((T - SimCutAt) / SimCutSeconds, 0.0, 1.0);
		const bool bCutting = T >= SimCutAt && T <= SimCutAt + SimCutSeconds;
		if (T < SimCutAt)
		{
			*Yaw = (Rng->FRand() - 0.5) * SimYawSpread;
		}
		const double Pitch = bCutting ? FingerPitchTop - (FingerPitchTop - FingerPitchBottom) * K : SimRestPitch;
		return FDeviceGestures::Posed(FDeviceGestures::Upright(), FDeviceGestures::Blade(Pitch, *Yaw),
			FVector3d(bCutting ? SimCutGyro : 0.0, 0.0, 0.0), FVector3d::ZeroVector);
	}, TArray<FTimedDeviceScript::FPress>(), SimPeriod);
}

#undef LOCTEXT_NAMESPACE
