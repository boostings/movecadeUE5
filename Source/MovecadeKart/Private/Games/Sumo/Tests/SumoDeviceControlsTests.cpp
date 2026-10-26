#include "Games/Sumo/SumoDeviceControls.h"
#include "Games/Sumo/SumoInput.h"
#include "Input/Tests/DeviceTestHelpers.h"
#include "Net/RoomProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoDeviceControlsTest, "MovecadeKart.Sumo.DeviceControls", DeviceTestFlags)
bool FSumoDeviceControlsTest::RunTest(const FString&)
{
	// phone-sim.mjs's sumo wander on the device, and DASH.
	constexpr double Hz = 60.0;
	TArray<FGestureSample> Samples;
	for (int32 k = 0; k < 120; ++k)
	{
		const double T = k / Hz;
		const double Pitch = FMath::DegreesToRadians(14.0 * FMath::Sin(T * 0.83) + 8.0 * FMath::Sin(T * 2.1));
		const double Roll = FMath::DegreesToRadians(16.0 * FMath::Sin(T * 0.61 + 1.3) + 6.0 * FMath::Sin(T * 1.7));
		FGestureSample& S = Samples.AddDefaulted_GetRef();
		S.Rotation = k == 0 ? FQuat::Identity : FDeviceGestures::Blade(-Pitch, Roll);
		if (k == 60) S.ButtonA = true;
		if (k == 67) S.ButtonA = false;
	}
	FSumoDeviceControls Scheme;
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	TArray<Net::FSensorFrame> PhoneFrames;
	PlayGesture(Scheme, Sink, FDeviceGestures::Flat(), Samples, Hz, PhoneFrames);
	TestSameFrames(*this, TEXT("tilt pad frames"), Sink->Frames, PhoneFrames);
	bool bSameTilt = Sink->Frames.Num() == PhoneFrames.Num();
	double MaxTilt = 0.0;
	for (int32 i = 0; bSameTilt && i < PhoneFrames.Num(); ++i)
	{
		// The rules' input: Sumo::PhoneTilt, what ASumoGame::FeedPhone hands SetInput.
		const FVector2D D = Sumo::PhoneTilt(Sink->Frames[i]);
		bSameTilt &= D.Equals(Sumo::PhoneTilt(PhoneFrames[i]), 2e-3);
		MaxTilt = FMath::Max(MaxTilt, D.Size());
	}
	TestTrue(TEXT("the same tilt as the phone's, frame for frame"), bSameTilt);
	TestTrue(TEXT("the wander tilts"), MaxTilt > 0.5);
	TCHAR Button = 0;
	const FRecordingSink::FEvent* Press = Sink->FindEvent(Net::PadPressEvent);
	TestTrue(TEXT("DASH is pad:press {button:'a'}"), Press && Press->Data.IsValid() && Net::ReadPadButton(*Press->Data, Button) && Button == TEXT('a'));
	TestTrue(TEXT("and pad:release"), Sink->FindEvent(Net::PadReleaseEvent) != nullptr);

	// The floating stick: a full drag up and right is a full tilt that way (X away, Y right in the rules' plane).
	FSumoDeviceControls Touch;
	FRecordingSink TouchSink;
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.4, 0.6), 1.0 }, TouchSink);
	Touch.OnSurface({ ETouchPhase::Move, 0, FVector2D(0.4 + 0.15, 0.6), 1.1 }, TouchSink);
	FDeviceMotion M;
	TestTrue(TEXT("the stick drives the pad"), Touch.SynthesizeMotion(1.1, M));
	Net::FSensorFrame F;
	F.bHasQuaternion = true;
	F.Quaternion = FVector4d(M.Attitude.X, M.Attitude.Y, M.Attitude.Z, M.Attitude.W);
	const FVector2D Right = Sumo::PhoneTilt(F);
	TestTrue(TEXT("a full drag right is a full roll right"), Right.Y > 0.95 && FMath::Abs(Right.X) < 0.05);
	Touch.OnSurface({ ETouchPhase::Move, 0, FVector2D(0.4, 0.6 - 0.15), 1.2 }, TouchSink);
	Touch.SynthesizeMotion(1.2, M);
	F.Quaternion = FVector4d(M.Attitude.X, M.Attitude.Y, M.Attitude.Z, M.Attitude.W);
	const FVector2D Up = Sumo::PhoneTilt(F);
	TestTrue(TEXT("a full drag up rolls away"), Up.X > 0.95 && FMath::Abs(Up.Y) < 0.05);
	Touch.OnSurface({ ETouchPhase::Up, 0, FVector2D(0.4, 0.45), 1.3 }, TouchSink);
	TestFalse(TEXT("let go, the device tilts again"), Touch.SynthesizeMotion(1.3, M));
	return true;
}

#endif
