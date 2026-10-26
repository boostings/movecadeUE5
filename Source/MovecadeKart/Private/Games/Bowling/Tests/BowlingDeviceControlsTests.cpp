#include "Games/Bowling/BowlingDeviceControls.h"
#include "Input/Tests/DeviceTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

namespace
{
	double Field(const FJsonObject& Data, const TCHAR* Key)
	{
		double V = 0.0;
		Data.TryGetNumberField(Key, V);
		return V;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingDeviceControlsTest, "MovecadeKart.Bowling.DeviceControls", DeviceTestFlags)
bool FBowlingDeviceControlsTest::RunTest(const FString&)
{
	// phone-sim.mjs's swing on the device: grip, a 0.7 s forward push peaking at 22 m/s^2 with a turn of the wrist, let go.
	constexpr double Hz = 60.0;
	TArray<FGestureSample> Samples;
	for (int32 k = 0; k < 70; ++k)
	{
		FGestureSample& S = Samples.AddDefaulted_GetRef();
		const double T = k / Hz;
		const double U = (T - 0.2) / 0.7;
		const double Push = U >= 0.0 && U <= 1.0 ? 22.0 * FMath::Sin(UE_DOUBLE_PI * U) : 0.0;
		S.Accel = FVector3d(0.0, Push, Push * 0.3);
		S.Gyro = FVector3d(0.0, U >= 0.0 && U <= 1.0 ? 160.0 : 0.0, 40.0);
		S.Rotation = FQuat(FVector(0, 0, 1), U >= 0.0 ? 0.2 * FMath::Min(1.0, U) : 0.0);
		if (k == 12) S.ButtonA = true;
		if (k == 54) S.ButtonA = false;
	}
	FBowlingDeviceControls Scheme;
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	TArray<Net::FSensorFrame> PhoneFrames;
	PlayGesture(Scheme, Sink, FDeviceGestures::Upright(), Samples, Hz, PhoneFrames);
	TestSameFrames(*this, TEXT("bowling frames"), Sink->Frames, PhoneFrames);

	// BowlingController.tsx: the phone's detector, grip on pointer-down, release on pointer-up.
	FThrowDetector Phone;
	TOptional<FThrowRelease> Expected;
	for (int32 k = 0; k < PhoneFrames.Num(); ++k)
	{
		if (Samples[k].ButtonA.IsSet() && *Samples[k].ButtonA) Phone.Grip();
		Phone.Feed(PhoneFrames[k]);
		if (Samples[k].ButtonA.IsSet() && !*Samples[k].ButtonA) Expected = Phone.Release();
	}
	const FRecordingSink::FEvent* Release = Sink->FindEvent(TEXT("bowling:release"));
	if (!TestTrue(TEXT("the device sent bowling:release"), Release && Release->Data.IsValid()) || !Expected.IsSet())
	{
		return false;
	}
	// The rules' input: the payload's shot (ABowlingGameMode's ParseRelease reads these four).
	const FJsonObject& D = *Release->Data;
	TestTrue(TEXT("a real ball"), Field(D, TEXT("power")) > 0.3);
	TestNearlyEqual(TEXT("aim"), Field(D, TEXT("aim")), Expected->Shot.Aim, 1e-6);
	TestNearlyEqual(TEXT("power"), Field(D, TEXT("power")), Expected->Shot.Power, 1e-6);
	TestNearlyEqual(TEXT("curve"), Field(D, TEXT("curve")), Expected->Shot.Curve, 1e-6);
	TestNearlyEqual(TEXT("loft"), Field(D, TEXT("loft")), Expected->Shot.Loft, 1e-6);

	// The touch fallback: a finger rolled up the right side, drifting left, bowls right of centre with a hook.
	FBowlingDeviceControls Touch;
	FRecordingSink TouchSink;
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.7, 0.9), 1.0 }, TouchSink);
	for (int32 i = 1; i <= 10; ++i)
	{
		Touch.OnSurface({ ETouchPhase::Move, 0, FVector2D(0.7 - 0.01 * i, 0.9 - 0.06 * i), 1.0 + 0.02 * i }, TouchSink);
	}
	Touch.OnSurface({ ETouchPhase::Up, 0, FVector2D(0.6, 0.3), 1.2 }, TouchSink);
	const FRecordingSink::FEvent* Roll = TouchSink.FindEvent(TEXT("bowling:release"));
	if (TestTrue(TEXT("a roll bowls"), Roll && Roll->Data.IsValid()))
	{
		TestTrue(TEXT("roll: right of centre, hooking left, real power"), Field(*Roll->Data, TEXT("aim")) > 0.1 && Field(*Roll->Data, TEXT("curve")) < 0.0
			&& Field(*Roll->Data, TEXT("power")) > 0.5);
	}
	return true;
}

#endif
