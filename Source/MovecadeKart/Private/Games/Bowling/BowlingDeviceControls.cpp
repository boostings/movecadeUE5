#include "Games/Bowling/BowlingDeviceControls.h"

#include "Dom/JsonObject.h"
#include "Input/DeviceMotionSim.h"

#define LOCTEXT_NAMESPACE "BowlingDeviceControls"

using namespace MovecadeKart::Input;
using namespace MovecadeKart::Bowling;

namespace
{
	constexpr double BowlingSendHz = 60.0;

	// The swipe fallback (FThrowTuning's ranges: aim and hook +-0.45 m).
	constexpr double MaxAim = 0.45;
	constexpr double MaxHook = 0.45;
	/** Upward finger speed (surface heights a second) for a full-power ball, and the least that bowls at all. */
	constexpr double FullRollSpeed = 2.5;
	constexpr double MinRollSpeed = 0.5;
	constexpr double MinSwipePower = 0.04;
	/** Sideways travel (of the surface width) for a full hook. */
	constexpr double FullHookTravel = 0.3;
	constexpr double SwipeConfidence = 0.5;

	// The autoplay: phone-sim.mjs bowl() (a 0.7 s forward push peaking at 22 m/s^2), every 5 s.
	constexpr double SimPeriod = 5.0;
	constexpr double SimGripAt = 1.0;
	constexpr double SimSwingSeconds = 0.7;
	constexpr double SimPushMs2 = 22.0;
	constexpr double SimSideShare = 0.3;
}

FBowlingDeviceControls::FBowlingDeviceControls()
{
	Layout.FrameKind = EDeviceFrameKind::Attitude;
	Layout.SendHz = BowlingSendHz;
	Layout.Orientation = EDeviceOrientationHint::Portrait;
	Layout.Buttons = { { EDeviceButton::A, LOCTEXT("Grip", "GRIP"), EButtonAnchor::BottomCentre, true } };
	Layout.bSurface = true;
	Layout.Hint = LOCTEXT("Hint", "Hold GRIP, swing, let go · or roll a finger up");
}

void FBowlingDeviceControls::OnSample(const MovecadeKart::Net::FSensorFrame& Frame, IDeviceInputSink& Sink)
{
	// The phone's detector runs on every sample; the button path never auto-commits.
	Detector.Feed(Frame);
}

void FBowlingDeviceControls::OnButton(EDeviceButton Button, bool bDown, double Now, IDeviceInputSink& Sink)
{
	if (Button != EDeviceButton::A)
	{
		return;
	}
	if (bDown)
	{
		Detector.Grip();
	}
	else if (Detector.IsGripped())
	{
		Sink.SendEvent(TEXT("bowling:release"), ReleasePayload(Detector.Release()));
	}
}

void FBowlingDeviceControls::OnSurface(const FDeviceTouch& Touch, IDeviceInputSink& Sink)
{
	if (Swipe.Feed(Touch) && Touch.Phase == ETouchPhase::Up && Swipe.PeakUpSpeed >= MinRollSpeed)
	{
		Sink.SendEvent(TEXT("bowling:release"), ReleasePayload(SwipeRelease(Swipe)));
	}
}

TSharedPtr<FJsonObject> FBowlingDeviceControls::ReleasePayload(const FThrowRelease& Release)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("aim"), Release.Shot.Aim);
	Data->SetNumberField(TEXT("power"), Release.Shot.Power);
	Data->SetNumberField(TEXT("curve"), Release.Shot.Curve);
	Data->SetNumberField(TEXT("loft"), Release.Shot.Loft);
	Data->SetNumberField(TEXT("confidence"), Release.Confidence);
	Data->SetStringField(TEXT("source"), TEXT("accel"));
	Data->SetBoolField(TEXT("hookUnavailable"), Release.bHookUnavailable);
	return Data;
}

FThrowRelease FBowlingDeviceControls::SwipeRelease(const FTouchTrack& S)
{
	FThrowRelease R;
	R.Shot.Aim = FMath::Clamp((S.Start.X - 0.5) * 2.0 * MaxAim, -MaxAim, MaxAim);
	R.Shot.Power = FMath::Clamp(S.PeakUpSpeed / FullRollSpeed, MinSwipePower, 1.0);
	R.Shot.Curve = FMath::Clamp(S.Delta().X / FullHookTravel * MaxHook, -MaxHook, MaxHook);
	R.Shot.Loft = 0.0;
	R.Confidence = SwipeConfidence;
	return R;
}

TSharedPtr<IDeviceMotionScript> FBowlingDeviceControls::MakeAutoplayScript() const
{
	return MakeShared<FTimedDeviceScript>([](double T)
	{
		const double U = (T - SimGripAt) / SimSwingSeconds;
		const double Push = U >= 0.0 && U <= 1.0 ? SimPushMs2 * FMath::Sin(UE_DOUBLE_PI * U) : 0.0;
		return FDeviceGestures::Posed(FDeviceGestures::Upright(), FQuat::Identity, FVector3d::ZeroVector, FVector3d(0.0, Push, Push * SimSideShare));
	}, TArray<FTimedDeviceScript::FPress>{
		{ SimGripAt, EDeviceButton::A, true },
		{ SimGripAt + SimSwingSeconds, EDeviceButton::A, false },
	}, SimPeriod);
}

#undef LOCTEXT_NAMESPACE
