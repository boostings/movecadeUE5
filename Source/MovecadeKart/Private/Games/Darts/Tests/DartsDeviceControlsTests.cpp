#include "Games/Darts/DartsDeviceControls.h"
#include "Games/Darts/DartsRelease.h"
#include "Input/Tests/DeviceTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Darts;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsDeviceControlsTest, "MovecadeKart.Darts.DeviceControls", DeviceTestFlags)
bool FDartsDeviceControlsTest::RunTest(const FString&)
{
	// A throw on the device: still (the detector's neutral), grip, a 160 ms forward thrust with a little yaw, let go.
	constexpr double Hz = 60.0;
	TArray<FGestureSample> Samples;
	for (int32 k = 0; k < 60; ++k)
	{
		FGestureSample& S = Samples.AddDefaulted_GetRef();
		const double T = k / Hz;
		const double U = (T - 0.55) / 0.16;
		const double Push = U >= 0.0 && U <= 1.0 ? 18.0 * FMath::Sin(UE_DOUBLE_PI * U) : 0.0;
		S.Rotation = FQuat(FVector(1, 0, 0), T > 0.45 ? -0.15 * FMath::Min(1.0, (T - 0.45) * 4.0) : 0.0);
		S.Gyro = FVector3d(T > 0.45 && T < 0.7 ? -60.0 : 0.0, 0.0, 25.0);
		S.Accel = FVector3d(0.0, Push, Push * 0.2);
		if (k == 30) S.ButtonA = true;
		if (k == 44) S.ButtonA = false;
	}
	FDartsDeviceControls Scheme;
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	TArray<Net::FSensorFrame> PhoneFrames;
	PlayGesture(Scheme, Sink, FDeviceGestures::Upright(), Samples, Hz, PhoneFrames);
	TestSameFrames(*this, TEXT("darts frames"), Sink->Frames, PhoneFrames);

	// The phone: its own detector on its frames, grip on pointer-down, release on pointer-up (DartsController.tsx).
	FDartThrowDetector Phone;
	TOptional<FThrowGesture> PhoneGesture;
	for (int32 k = 0; k < PhoneFrames.Num(); ++k)
	{
		if (Samples[k].ButtonA.IsSet() && *Samples[k].ButtonA) Phone.Grip();
		Phone.Feed(FThrowSample::FromFrame(PhoneFrames[k]));
		if (Samples[k].ButtonA.IsSet() && !*Samples[k].ButtonA) PhoneGesture = Phone.Release();
	}
	const FRecordingSink::FEvent* Release = Sink->FindEvent(TEXT("darts:release"));
	if (!TestTrue(TEXT("the device sent darts:release"), Release && Release->Data.IsValid()) || !TestTrue(TEXT("the phone threw"), PhoneGesture.IsSet()))
	{
		return false;
	}
	// The rules' input: what ADartsGameMode makes of each payload (GestureFromRelease, then SubmitRelease's BoundRelease).
	const FDartGesture Device = BoundRelease(*GestureFromRelease(*Release->Data));
	const FDartGesture Expected = BoundRelease(*GestureFromRelease(*FDartsDeviceControls::ReleasePayload(*PhoneGesture)));
	TestTrue(TEXT("a real throw"), Device.Power > 0.3);
	TestNearlyEqual(TEXT("aim x"), Device.AimX, Expected.AimX, 1e-6);
	TestNearlyEqual(TEXT("aim y"), Device.AimY, Expected.AimY, 1e-6);
	TestNearlyEqual(TEXT("power"), Device.Power, Expected.Power, 1e-6);
	TestNearlyEqual(TEXT("spin"), Device.SpinDegPerSec, Expected.SpinDegPerSec, 1e-6);
	TestNearlyEqual(TEXT("steadiness"), Device.Steadiness, Expected.Steadiness, 1e-6);

	// The touch fallback: a flick up from above the middle throws high, hard.
	FDartsDeviceControls Touch;
	FRecordingSink TouchSink;
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.5, 0.4), 1.0 }, TouchSink);
	for (int32 i = 1; i <= 8; ++i)
	{
		Touch.OnSurface({ ETouchPhase::Move, 0, FVector2D(0.5, 0.4 - 0.04 * i), 1.0 + 0.012 * i }, TouchSink);
	}
	Touch.OnSurface({ ETouchPhase::Up, 0, FVector2D(0.5, 0.08), 1.1 }, TouchSink);
	const FRecordingSink::FEvent* Swipe = TouchSink.FindEvent(TEXT("darts:release"));
	if (TestTrue(TEXT("a flick throws"), Swipe && Swipe->Data.IsValid()))
	{
		const FDartGesture G = *GestureFromRelease(*Swipe->Data);
		TestTrue(TEXT("flick: aimed up, real power"), G.AimY > 0.1 && G.Power > 0.5);
	}
	return true;
}

#endif
