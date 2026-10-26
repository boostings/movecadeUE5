#include "Games/Fishing/FishingDeviceControls.h"
#include "Games/Fishing/FishingMotion.h"
#include "Input/Tests/DeviceTestHelpers.h"
#include "Net/RoomProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Fishing;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishingDeviceControlsTest, "MovecadeKart.Fishing.DeviceControls", DeviceTestFlags)
bool FFishingDeviceControlsTest::RunTest(const FString&)
{
	// phone-sim.mjs's rod on the device: a forward flick (the cast), an upward jerk (the hook), then the wrist rolled.
	constexpr double Hz = 100.0;
	TArray<FGestureSample> Samples;
	for (int32 k = 0; k < 300; ++k)
	{
		const double T = k / Hz;
		FGestureSample& S = Samples.AddDefaulted_GetRef();
		const double Flick = (T - 0.3) / 0.2;
		const double Jerk = (T - 1.4) / 0.16;
		if (Flick >= 0.0 && Flick <= 1.0) S.Gyro.X = -950.0 * FMath::Sin(UE_DOUBLE_PI * Flick);
		if (Jerk >= 0.0 && Jerk <= 1.0) S.Gyro.X = 750.0 * FMath::Sin(UE_DOUBLE_PI * Jerk);
		if (T >= 2.0) S.Gyro.Y = 420.0;
	}
	FFishingDeviceControls Scheme;
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	TArray<Net::FSensorFrame> PhoneFrames;
	PlayGesture(Scheme, Sink, FDeviceGestures::Upright(), Samples, Hz, PhoneFrames);
	TestSameFrames(*this, TEXT("rod frames"), Sink->Frames, PhoneFrames);

	// The rules' input: FFishingMotion (AFishingGame::FeedPhone) on each stream: the same flicks and the same reel.
	FFishingMotion Device, Phone;
	TArray<FFlick> DeviceFlicks, PhoneFlicks;
	bool bSameReel = true;
	for (int32 i = 0; i < FMath::Min(Sink->Frames.Num(), PhoneFrames.Num()); ++i)
	{
		if (const TOptional<FFlick> F = Device.Feed(FMotionSample::FromFrame(Sink->Frames[i]))) DeviceFlicks.Add(*F);
		if (const TOptional<FFlick> F = Phone.Feed(FMotionSample::FromFrame(PhoneFrames[i]))) PhoneFlicks.Add(*F);
		bSameReel &= FMath::IsNearlyEqual(Device.Reel(), Phone.Reel(), 1e-4);
	}
	TestEqual(TEXT("two flicks each"), DeviceFlicks.Num(), PhoneFlicks.Num());
	if (DeviceFlicks.Num() == 2 && PhoneFlicks.Num() == 2)
	{
		TestTrue(TEXT("a forward cast, then a hook"), DeviceFlicks[0].Direction == EFlick::Forward && DeviceFlicks[1].Direction == EFlick::Back);
		TestNearlyEqual(TEXT("cast power"), DeviceFlicks[0].Power, PhoneFlicks[0].Power, 1e-4);
		TestNearlyEqual(TEXT("hook power"), DeviceFlicks[1].Power, PhoneFlicks[1].Power, 1e-4);
	}
	TestTrue(TEXT("the same reel all along"), bSameReel);
	TestTrue(TEXT("the wrist reels"), Device.Reel() > 0.5);

	// The touch fallback: circling a finger reels, through the same motion detector.
	FFishingDeviceControls Touch;
	FRecordingSink TouchSink;
	FFishingMotion Reel;
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.7, 0.5), 0.0 }, TouchSink);
	for (int32 i = 1; i <= 120; ++i)
	{
		const double T = i / 60.0;
		const double A = 2.0 * UE_DOUBLE_PI * 1.2 * T;
		Touch.OnSurface({ ETouchPhase::Move, 0, FVector2D(0.5 + 0.2 * FMath::Cos(A), 0.5 + 0.2 * FMath::Sin(A)), T }, TouchSink);
		FDeviceMotion M;
		if (Touch.SynthesizeMotion(T, M))
		{
			FMotionSample S;
			S.Gyro = M.GyroDegPerSec;
			S.Accel = M.AccelMs2;
			S.DtMs = 1000.0 / 60.0;
			Reel.Feed(S);
		}
	}
	TestTrue(TEXT("circling reels"), Reel.Reel() > 0.4);
	return true;
}

#endif
