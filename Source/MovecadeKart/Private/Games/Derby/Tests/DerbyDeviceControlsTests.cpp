#include "Games/Derby/DerbyDeviceControls.h"
#include "Games/Derby/DerbySwing.h"
#include "Input/Tests/DeviceTestHelpers.h"
#include "Net/RoomProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Derby;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDerbyDeviceControlsTest, "MovecadeKart.Derby.DeviceControls", DeviceTestFlags)
bool FDerbyDeviceControlsTest::RunTest(const FString&)
{
	// phone-sim.mjs's bat on the device: held still, then a 0.3 s swing 10 degrees early, then held through.
	constexpr double Hz = 60.0;
	constexpr double SwingSeconds = 0.3;
	const FQuat Neutral = FDeviceGestures::Upright().Attitude;
	TArray<FGestureSample> Samples;
	for (int32 k = 0; k < 75; ++k)
	{
		const double T = k / Hz;
		const double K = (T - 0.4) / SwingSeconds;
		FGestureSample& S = Samples.AddDefaulted_GetRef();
		if (K >= 0.0 && K <= 1.0)
		{
			const FDeviceMotion Bat = FDerbyDeviceControls::Swing(K, SwingSeconds, 10.0);
			S.Rotation = Neutral.Inverse() * Bat.Attitude;
			S.Gyro = Bat.GyroDegPerSec;
		}
	}
	FDerbyDeviceControls Scheme;
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	TArray<Net::FSensorFrame> PhoneFrames;
	PlayGesture(Scheme, Sink, FDeviceGestures::Upright(), Samples, Hz, PhoneFrames);
	TestSameFrames(*this, TEXT("bat frames"), Sink->Frames, PhoneFrames);

	// The rules' input: the host's swing detector (ADerbyGame::FeedPhone) on each stream.
	FSwingDetector DeviceDetector, PhoneDetector;
	TOptional<FSwingEvent> DeviceSwing, PhoneSwing;
	for (int32 i = 0; i < FMath::Min(Sink->Frames.Num(), PhoneFrames.Num()); ++i)
	{
		if (const TOptional<FSwingEvent> E = DeviceDetector.Feed(Sink->Frames[i])) DeviceSwing = E;
		if (const TOptional<FSwingEvent> E = PhoneDetector.Feed(PhoneFrames[i])) PhoneSwing = E;
	}
	if (!TestTrue(TEXT("both swing"), DeviceSwing.IsSet() && PhoneSwing.IsSet()))
	{
		return false;
	}
	TestNearlyEqual(TEXT("peak time"), DeviceSwing->PeakAtMs, PhoneSwing->PeakAtMs, 1e-6);
	TestNearlyEqual(TEXT("strength"), DeviceSwing->Strength, PhoneSwing->Strength, 1e-3);
	TestNearlyEqual(TEXT("direction"), DeviceSwing->Direction, PhoneSwing->Direction, 1e-3);
	TestEqual(TEXT("handedness"), DeviceSwing->Handed, PhoneSwing->Handed);
	TestTrue(TEXT("a real swing with an attitude"), DeviceSwing->bHasAttitude && DeviceSwing->Strength > 0.3);

	// READY and the touch fallback are the pad's A.
	FDerbyDeviceControls Touch;
	FRecordingSink TouchSink;
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.5, 0.5), 1.0 }, TouchSink);
	Touch.OnSurface({ ETouchPhase::Up, 0, FVector2D(0.5, 0.5), 1.1 }, TouchSink);
	TCHAR Button = 0;
	TestTrue(TEXT("a tap is pad:press a"), TouchSink.Events.Num() == 2 && TouchSink.Events[0].Name == Net::PadPressEvent
		&& Net::ReadPadButton(*TouchSink.Events[0].Data, Button) && Button == TEXT('a') && TouchSink.Events[1].Name == Net::PadReleaseEvent);
	return true;
}

#endif
