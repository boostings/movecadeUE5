#include "Games/Darts/DartsDeviceControls.h"

#include "Dom/JsonObject.h"
#include "Input/DeviceMotionSim.h"

#define LOCTEXT_NAMESPACE "DartsDeviceControls"

using namespace MovecadeKart::Input;
using namespace MovecadeKart::Darts;

namespace
{
	/** The phone's frame rate for darts (the motion pad's default). */
	constexpr double DartsSendHz = 60.0;

	// The swipe fallback.
	/** Upward finger speed (surface heights per second) that throws at full power, and the least that throws at all. */
	constexpr double FullFlickSpeed = 3.0;
	constexpr double MinFlickSpeed = 0.6;
	constexpr double MinSwipePower = 0.05;
	/** Spin per unit of sideways drift a second, deg/s, and its bound (the mouse flick's). */
	constexpr double SwipeSpinPerSpeed = 300.0;
	constexpr double MaxSwipeSpin = 400.0;
	constexpr double SwipeSteadiness = 0.9;
	constexpr double SwipeConfidence = 0.5;
	/** The power's equivalent thrust (the detector's reference), for the payload's peakMs2. */
	constexpr double ReferenceMs2 = 30.0;

	// The autoplay (phone-sim.mjs throws every 1.6 s; here a real thrust through the detector).
	constexpr double SimPeriod = 2.2;
	constexpr double SimGripAt = 0.8;
	constexpr double SimThrustAt = 1.0;
	constexpr double SimThrustSeconds = 0.16;
	constexpr double SimThrustPeakMs2 = 18.0;
	constexpr double SimReleaseAt = 1.2;
}

FDartsDeviceControls::FDartsDeviceControls()
{
	Layout.FrameKind = EDeviceFrameKind::Attitude;
	Layout.SendHz = DartsSendHz;
	Layout.Buttons = { { EDeviceButton::A, LOCTEXT("Throw", "HOLD"), EButtonAnchor::BottomCentre, true } };
	Layout.bSurface = true;
	// The board fills a portrait screen's width: pause and settings flank the HOLD button instead of its rim.
	Layout.Chips = EChipPlacement::BottomCorners;
	Layout.Hint = LOCTEXT("Hint", "Hold, throw, let go · or flick a finger up");
}

void FDartsDeviceControls::OnSample(const MovecadeKart::Net::FSensorFrame& Frame, IDeviceInputSink& Sink)
{
	// The phone's detector runs on every sample; only the grip's release throws (the auto-commit is never sent).
	Detector.Feed(FThrowSample::FromFrame(Frame));
}

void FDartsDeviceControls::OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink)
{
	if (Button != EDeviceButton::A)
	{
		return;
	}
	if (bDown)
	{
		Detector.Grip();
		return;
	}
	if (const TOptional<FThrowGesture> Gesture = Detector.Release())
	{
		Sink.SendEvent(TEXT("darts:release"), ReleasePayload(*Gesture));
	}
}

void FDartsDeviceControls::OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink)
{
	if (!Swipe.Feed(Touch) || Touch.Phase != ETouchPhase::Up || Swipe.PeakUpSpeed < MinFlickSpeed)
	{
		return;
	}
	FThrowGesture Gesture;
	static_cast<FDartGesture&>(Gesture) = SwipeGesture(Swipe);
	Sink.SendEvent(TEXT("darts:release"), ReleasePayload(Gesture));
}

TSharedPtr<FJsonObject> FDartsDeviceControls::ReleasePayload(const FThrowGesture& G)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("aimX"), G.AimX);
	Data->SetNumberField(TEXT("aimY"), G.AimY);
	Data->SetNumberField(TEXT("power"), G.Power);
	Data->SetNumberField(TEXT("spinDegPerSec"), G.SpinDegPerSec);
	Data->SetNumberField(TEXT("rollDegPerSec"), G.RollDegPerSec);
	Data->SetNumberField(TEXT("confidence"), G.Confidence);
	Data->SetNumberField(TEXT("steadiness"), G.Steadiness);
	Data->SetNumberField(TEXT("peakMs2"), G.PeakMs2);
	Data->SetNumberField(TEXT("durationMs"), G.DurationMs);
	Data->SetStringField(TEXT("source"), G.bFromGyro ? TEXT("gyro") : TEXT("accel"));
	return Data;
}

FDartGesture FDartsDeviceControls::SwipeGesture(const FTouchTrack& S)
{
	// Touch down where you aim (the board's middle is the screen's), flick up to throw.
	FDartGesture G;
	G.AimX = FMath::Clamp((S.Start.X - 0.5) * 2.0, -1.0, 1.0);
	G.AimY = FMath::Clamp((0.5 - S.Start.Y) * 2.0, -1.0, 1.0);
	G.Power = FMath::Clamp(S.PeakUpSpeed / FullFlickSpeed, MinSwipePower, 1.0);
	const double Seconds = FMath::Max(S.Duration(), 1e-3);
	G.SpinDegPerSec = FMath::Clamp(S.Delta().X / Seconds * SwipeSpinPerSpeed, -MaxSwipeSpin, MaxSwipeSpin);
	G.Steadiness = SwipeSteadiness;
	G.Confidence = SwipeConfidence;
	G.PeakMs2 = G.Power * ReferenceMs2;
	G.DurationMs = Seconds * 1000.0;
	return G;
}

TSharedPtr<IDeviceMotionScript> FDartsDeviceControls::MakeAutoplayScript() const
{
	return MakeShared<FTimedDeviceScript>([](double Time)
	{
		const double T = FMath::Fmod(Time, SimPeriod);
		const double U = (T - SimThrustAt) / SimThrustSeconds;
		const double Push = U >= 0.0 && U <= 1.0 ? SimThrustPeakMs2 * FMath::Sin(UE_DOUBLE_PI * U) : 0.0;
		return FDeviceGestures::Posed(FDeviceGestures::Upright(), FQuat::Identity, FVector3d::ZeroVector, FVector3d(0.0, Push, 0.0));
	}, TArray<FTimedDeviceScript::FPress>{
		{ SimGripAt, EDeviceButton::A, true },
		{ SimReleaseAt, EDeviceButton::A, false },
	}, SimPeriod);
}

#undef LOCTEXT_NAMESPACE
