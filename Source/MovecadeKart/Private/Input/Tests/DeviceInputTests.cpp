#include "Input/Tests/DeviceTestHelpers.h"

#include "Input/DeviceControlsFlags.h"
#include "Input/DeviceControlsSettings.h"
#include "Input/DeviceFrameBuilder.h"
#include "Input/DeviceTouchGestures.h"
#include "Input/DeviceTouchZones.h"
#include "Input/KartDeviceControls.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/RoomProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/KartScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

namespace
{
	const EScreenOrientation AllOrientations[] = { EScreenOrientation::Portrait, EScreenOrientation::LandscapeLeft,
		EScreenOrientation::PortraitUpsideDown, EScreenOrientation::LandscapeRight };

	FDeviceMotion RandomMotion(FRandomStream& Rng, EScreenOrientation Orientation)
	{
		// Keep beta inside (-90, 90): the Euler triple the engine carries is ambiguous at the poles.
		const FQuat Attitude = AttitudeFromEuler(Rng.FRandRange(-1.3f, 1.3f), Rng.FRandRange(-1.4f, 1.4f), Rng.FRandRange(-3.1f, 3.1f));
		FDeviceMotion M = FDeviceGestures::Posed(FDeviceGestures::Flat(), Attitude, FVector3d(Rng.FRandRange(-500.f, 500.f), Rng.FRandRange(-500.f, 500.f),
			Rng.FRandRange(-500.f, 500.f)), FVector3d(Rng.FRandRange(-20.f, 20.f), Rng.FRandRange(-20.f, 20.f), Rng.FRandRange(-20.f, 20.f)));
		M.Orientation = Orientation;
		return M;
	}

	bool SameMotion(const FDeviceMotion& A, const FDeviceMotion& B)
	{
		// Compare the rotations by what they do (acos-based angles lose half the digits near zero).
		const bool bSameRotation = A.Attitude.RotateVector(FVector(1, 0, 0)).Equals(B.Attitude.RotateVector(FVector(1, 0, 0)), 1e-9)
			&& A.Attitude.RotateVector(FVector(0, 1, 0)).Equals(B.Attitude.RotateVector(FVector(0, 1, 0)), 1e-9);
		return bSameRotation && (A.GyroDegPerSec - B.GyroDegPerSec).Length() < 1e-9
			&& (A.AccelMs2 - B.AccelMs2).Length() < 1e-9 && (A.GravityMs2 - B.GravityMs2).Length() < 1e-9 && A.Orientation == B.Orientation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceEncodeFixturesTest, "MovecadeKart.Input.EncodeMatchesPhoneFixtures", DeviceTestFlags)
bool FDeviceEncodeFixturesTest::RunTest(const FString&)
{
	// movecade-v2's own encoder wrote these frames (Data/Net/protocol-fixtures.json): ours must write the same bytes.
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Net/protocol-fixtures.json"))))
	{
		AddError(TEXT("Missing Data/Net/protocol-fixtures.json"));
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
	if (!TestTrue(TEXT("fixtures parse"), Root.IsValid()))
	{
		return false;
	}
	int32 Checked = 0;
	for (const TSharedPtr<FJsonValue>& Value : Root->GetObjectField(TEXT("sensor"))->GetArrayField(TEXT("valid")))
	{
		const FString Hex = Value->AsObject()->GetStringField(TEXT("hex"));
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Hex.Len() / 2);
		HexToBytes(Hex, Bytes.GetData());
		Net::FSensorFrame Frame;
		if (!Net::DecodeSensorFrame(Bytes.GetData(), Bytes.Num(), Frame))
		{
			continue;
		}
		const auto Encoded = EncodeSensorFrame(Frame);
		const int32 Body = Frame.bHasQuaternion ? Net::SensorFrameV2Bytes : Net::SensorFrameBytes;
		TestTrue(Value->AsObject()->GetStringField(TEXT("name")) + TEXT(": same bytes"), Encoded.Num() == Body && FMemory::Memcmp(Encoded.GetData(), Bytes.GetData(), Body) == 0);
		++Checked;
	}
	TestTrue(TEXT("some fixtures checked"), Checked > 0);
	// Rounding is the phone's (Math.round: halves up) and int16 clamps.
	Net::FSensorFrame F;
	F.Gamma = 0.005;
	F.Alpha = -0.005;
	F.Gyro = FVector3d(5000.0, -5000.0, 0.0);
	const Net::FSensorFrame Q = QuantizeSensorFrame(F);
	TestEqual(TEXT("0.5 rounds up"), Q.Gamma, 0.01);
	TestEqual(TEXT("-0.5 rounds up too"), Q.Alpha, 0.0);
	TestEqual(TEXT("clamps high"), Q.Gyro.X, 32767.0 / 20.0);
	TestEqual(TEXT("clamps low"), Q.Gyro.Y, -32768.0 / 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceEngineSpaceTest, "MovecadeKart.Input.EngineMotionSpace", DeviceTestFlags)
bool FDeviceEngineSpaceTest::RunTest(const FString&)
{
	// IOSInputInterface.cpp's unified space (ios.UnifyMotionSpace 1): portrait (x, y, z) -> (-z, x, y), and so on.
	TestTrue(TEXT("portrait: device x is screen right"), DeviceToScreen(FVector3d(1, 0, 0), EScreenOrientation::Portrait).Equals(FVector3d(0, 1, 0)));
	TestTrue(TEXT("portrait: device y is screen up"), DeviceToScreen(FVector3d(0, 1, 0), EScreenOrientation::Portrait).Equals(FVector3d(0, 0, 1)));
	TestTrue(TEXT("portrait: out of the screen is -X"), DeviceToScreen(FVector3d(0, 0, 1), EScreenOrientation::Portrait).Equals(FVector3d(-1, 0, 0)));
	TestTrue(TEXT("landscape-left: device x is screen up"), DeviceToScreen(FVector3d(1, 0, 0), EScreenOrientation::LandscapeLeft).Equals(FVector3d(0, 0, 1)));
	TestTrue(TEXT("landscape-right: device y is screen right"), DeviceToScreen(FVector3d(0, 1, 0), EScreenOrientation::LandscapeRight).Equals(FVector3d(0, 1, 0)));
	TestTrue(TEXT("upside down: device x is screen left"), DeviceToScreen(FVector3d(1, 0, 0), EScreenOrientation::PortraitUpsideDown).Equals(FVector3d(0, -1, 0)));

	// The shape decides portrait or landscape; the device report only picks the side.
	TestEqual(TEXT("landscape report in landscape"), ResolveOrientation(EScreenOrientation::LandscapeRight, false), EScreenOrientation::LandscapeRight);
	TestEqual(TEXT("portrait report in a landscape viewport"), ResolveOrientation(EScreenOrientation::Portrait, false), EScreenOrientation::LandscapeLeft);
	TestEqual(TEXT("face up in portrait"), ResolveOrientation({}, true), EScreenOrientation::Portrait);

	FRandomStream Rng(1234);
	for (const FMotionConvention& Convention : { FMotionConvention::IOS(), FMotionConvention::Android() })
	{
		for (const EScreenOrientation O : AllOrientations)
		{
			for (int32 i = 0; i < 50; ++i)
			{
				const FDeviceMotion M = RandomMotion(Rng, O);
				const FDeviceMotion Back = FromEngine(ToEngine(M, Convention), Convention);
				if (!SameMotion(M, Back))
				{
					AddError(FString::Printf(TEXT("FromEngine(ToEngine(m)) != m (orientation %d, sample %d)"), static_cast<int32>(O), i));
					break;
				}
			}
		}
	}
	// Core Motion's numbers for an upright portrait iPhone: gravity (0, -1, 0) g reads as the phone's (0, -9.81, 0).
	FEngineMotionReading Upright;
	Upright.Gravity = FVector(DeviceToScreen(FVector3d(0, -1, 0), EScreenOrientation::Portrait));
	TestTrue(TEXT("iOS gravity"), FromEngine(Upright, FMotionConvention::IOS()).GravityMs2.Equals(FVector3d(0, -StandardGravity, 0), 1e-9));
	// userAcceleration is opposite to W3C acceleration: Core Motion -1 g on y is a +9.81 m/s^2 push up the phone.
	FEngineMotionReading Push;
	Push.Acceleration = FVector(DeviceToScreen(FVector3d(0, -1, 0), EScreenOrientation::Portrait));
	TestTrue(TEXT("iOS acceleration sign"), FromEngine(Push, FMotionConvention::IOS()).AccelMs2.Equals(FVector3d(0, StandardGravity, 0), 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceFrameBuilderTest, "MovecadeKart.Input.FrameBuilder", DeviceTestFlags)
bool FDeviceFrameBuilderTest::RunTest(const FString&)
{
	// Wheel mode: level at calibration, + clockwise, held while flat.
	FDeviceFrameBuilder Wheel(EDeviceFrameKind::Wheel);
	Wheel.Calibrate(FDeviceGestures::Wheel(0.0));
	const Net::FSensorFrame Level = Wheel.Build(FDeviceGestures::Wheel(0.0), 33.0);
	TestEqual(TEXT("wheel: 30-byte flags"), Level.Flags, static_cast<uint8>(Net::SensorFlagCalibrated | Net::SensorFlagHasGravity));
	TestEqual(TEXT("wheel: landscape-left"), Level.ScreenOrientation, static_cast<uint8>(1));
	TestNearlyEqual(TEXT("wheel: level is 0"), Level.Gamma, 0.0, 0.011);
	TestNearlyEqual(TEXT("wheel: 30 clockwise is +30"), Wheel.Build(FDeviceGestures::Wheel(30.0), 33.0).Gamma, 30.0, 0.011);
	TestNearlyEqual(TEXT("wheel: 45 anticlockwise is -45"), Wheel.Build(FDeviceGestures::Wheel(-45.0), 33.0).Gamma, -45.0, 0.011);
	TestNearlyEqual(TEXT("wheel: flat holds the last angle"), Wheel.Build(FDeviceGestures::Flat(), 33.0).Gamma, -45.0, 0.011);
	TestTrue(TEXT("wheel: steering follows the room host"), Net::SteeringFromGamma(Wheel.Build(FDeviceGestures::Wheel(30.0), 33.0).Gamma) < -0.3);

	// Attitude mode: calibrated to the pose at calibration, any pose.
	FDeviceFrameBuilder Pad(EDeviceFrameKind::Attitude);
	const FDeviceMotion Tilted = FDeviceGestures::Posed(FDeviceGestures::Upright(), FQuat(FVector(1, 0, 0), 0.4), FVector3d::ZeroVector, FVector3d::ZeroVector);
	Pad.Calibrate(Tilted);
	const Net::FSensorFrame Still = Pad.Build(Tilted, 16.0);
	TestTrue(TEXT("attitude: the calibrated pose is identity"), Still.bHasQuaternion && FMath::IsNearlyEqual(Still.Quaternion.W, 1.0, 1e-4));
	const FQuat Turn(FVector(0, 1, 0), 0.3);
	const Net::FSensorFrame Turned = Pad.Build(FDeviceGestures::Posed(Tilted, Turn, FVector3d::ZeroVector, FVector3d::ZeroVector), 16.0);
	TestNearlyEqual(TEXT("attitude: a turn after calibration"), FVector::Distance(FQuat(Turned.Quaternion.X, Turned.Quaternion.Y, Turned.Quaternion.Z, Turned.Quaternion.W).GetNormalized().RotateVector(FVector(1, 0, 0)), Turn.RotateVector(FVector(1, 0, 0))), 0.0, 2e-4);
	TestNearlyEqual(TEXT("attitude: Euler gamma is the roll about y"), Turned.Gamma, FMath::RadiansToDegrees(0.3), 0.011);
	TestEqual(TEXT("attitude: dt rounds"), Turned.DtMs, static_cast<uint16>(16));
	Pad.SetSensitivity(2.0);
	const Net::FSensorFrame Doubled = Pad.Build(FDeviceGestures::Posed(Tilted, Turn, FVector3d(10, 0, 0), FVector3d::ZeroVector), 16.0);
	TestNearlyEqual(TEXT("sensitivity 2 doubles the turn"), FQuat(Doubled.Quaternion.X, Doubled.Quaternion.Y, Doubled.Quaternion.Z, Doubled.Quaternion.W).GetAngle(), 0.6, 2e-4);
	TestNearlyEqual(TEXT("sensitivity 2 doubles the gyro"), Doubled.Gyro.X, 20.0, 0.051);
	return true;
}

namespace
{
	/** A scheme with only a layout: the controller's throttle and calibration under test. */
	class FPlainScheme : public IDeviceControlScheme
	{
	public:
		explicit FPlainScheme(double Hz) { Layout.SendHz = Hz; }
		virtual const FDeviceLayout& GetLayout() const override { return Layout; }
		virtual void OnSample(const Net::FSensorFrame& Frame, IDeviceInputSink& Sink) override { ++Samples; }
		virtual void OnCalibrated() override { ++Calibrations; }
		FDeviceLayout Layout;
		int32 Samples = 0;
		int32 Calibrations = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceControllerTest, "MovecadeKart.Input.ControllerThrottle", DeviceTestFlags)
bool FDeviceControllerTest::RunTest(const FString&)
{
	// The device samples at 120 Hz; a 30 Hz scheme sends every fourth sample, numbered and timed like the phone's.
	TSharedRef<FPlainScheme> Scheme = MakeShared<FPlainScheme>(30.0);
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	FLocalDeviceController Controller(Scheme, Sink);
	for (int32 i = 0; i < 120; ++i)
	{
		FDeviceMotion M = FDeviceGestures::Upright();
		M.TimeSeconds = 10.0 + i / 120.0;
		Controller.PushMotion(M);
	}
	TestEqual(TEXT("every sample reaches the scheme"), Scheme->Samples, 120);
	TestTrue(TEXT("about 30 frames a second go out"), Sink->Frames.Num() >= 29 && Sink->Frames.Num() <= 31);
	TestEqual(TEXT("calibrated once, on the first sample"), Scheme->Calibrations, 1);
	bool bSequential = true;
	for (int32 i = 1; i < Sink->Frames.Num(); ++i)
	{
		bSequential &= Sink->Frames[i].Sequence == static_cast<uint8>(Sink->Frames[i - 1].Sequence + 1);
		bSequential &= Sink->Frames[i].DtMs == 33 || Sink->Frames[i].DtMs == 34;
	}
	TestTrue(TEXT("sequence and dt like the phone's throttle"), bSequential);
	Controller.RequestCalibration();
	FDeviceMotion M = FDeviceGestures::Flat();
	M.TimeSeconds = 12.0;
	Controller.PushMotion(M);
	TestEqual(TEXT("the calibrate button recalibrates"), Scheme->Calibrations, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceRecordingTest, "MovecadeKart.Input.RecordingRoundTrip", DeviceTestFlags)
bool FDeviceRecordingTest::RunTest(const FString&)
{
	FDeviceRecording R;
	FDeviceRecordedEvent& M = R.Events.AddDefaulted_GetRef();
	M.Time = 0.25;
	M.Reading = ToEngine(FDeviceGestures::Wheel(20.0), FMotionConvention::IOS());
	FDeviceRecordedEvent& B = R.Events.AddDefaulted_GetRef();
	B.Kind = FDeviceRecordedEvent::EKind::Button;
	B.Time = 0.5;
	B.Button = EDeviceButton::Item;
	B.bDown = true;
	FDeviceRecordedEvent& T = R.Events.AddDefaulted_GetRef();
	T.Kind = FDeviceRecordedEvent::EKind::Touch;
	T.Time = 0.75;
	T.Touch.Phase = ETouchPhase::Move;
	T.Touch.Pointer = 2;
	T.Touch.Position = FVector2D(0.25, 0.5);
	FDeviceRecording Back;
	TestTrue(TEXT("parses"), FDeviceRecording::FromText(R.ToText(), Back));
	TestEqual(TEXT("three events"), Back.Events.Num(), 3);
	if (Back.Events.Num() == 3)
	{
		TestTrue(TEXT("motion"), Back.Events[0].Reading.Gravity.Equals(M.Reading.Gravity, 1e-6) && Back.Events[0].Reading.Orientation == EScreenOrientation::LandscapeLeft);
		TestTrue(TEXT("button"), Back.Events[1].Button == EDeviceButton::Item && Back.Events[1].bDown);
		TestTrue(TEXT("touch"), Back.Events[2].Touch.Pointer == 2 && Back.Events[2].Touch.Position.Equals(FVector2D(0.25, 0.5), 1e-6));
	}
	FDeviceRecording Bad;
	TestFalse(TEXT("a malformed line is refused"), FDeviceRecording::FromText(TEXT("m,0,1,2\n"), Bad));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceFlagsTest, "MovecadeKart.Input.Flags", DeviceTestFlags)
bool FDeviceFlagsTest::RunTest(const FString&)
{
	TestTrue(TEXT("on by default on a phone"), FDeviceControlsFlags::Parse(TEXT(""), true).bEnabled);
	TestFalse(TEXT("off by default on the desktop"), FDeviceControlsFlags::Parse(TEXT(""), false).bEnabled);
	TestFalse(TEXT("-NoDeviceControls"), FDeviceControlsFlags::Parse(TEXT("-NoDeviceControls"), true).bEnabled);
#if !UE_BUILD_SHIPPING
	TestTrue(TEXT("-DeviceControls"), FDeviceControlsFlags::Parse(TEXT("-DeviceControls"), false).bEnabled);
	const FDeviceControlsFlags Sim = FDeviceControlsFlags::Parse(TEXT("-DeviceMotion=sim"), false);
	TestTrue(TEXT("-DeviceMotion=sim plays the device"), Sim.bEnabled && Sim.Source == EDeviceMotionSource::Simulated);
	const FDeviceControlsFlags Replay = FDeviceControlsFlags::Parse(TEXT("-DeviceMotionReplay=take1.csv"), false);
	TestTrue(TEXT("-DeviceMotionReplay under Saved"), Replay.Source == EDeviceMotionSource::Replay && Replay.ReplayPath.EndsWith(TEXT("take1.csv")));
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceTouchTargetsTest, "MovecadeKart.Input.TouchTargets", DeviceTestFlags)
bool FDeviceTouchTargetsTest::RunTest(const FString&)
{
	// Menus: a finger near a small button still gets it; between two, the nearer wins; far away, nothing.
	TArray<FKartHitRegion> Regions;
	Regions.Add({ 1, FVector2D(100, 100), FVector2D(200, 60) });
	Regions.Add({ 2, FVector2D(100, 180), FVector2D(200, 60) });
	const double Min = 1080.0 * SKartScreenStack::MinTouchTargetShare;
	TestTrue(TEXT("44 pt on a 320 pt screen is ~148 canvas units"), Min > 140.0 && Min < 150.0);
	TestEqual(TEXT("an exact hit"), SKartScreenStack::TouchHitAmong(Regions, FVector2D(150, 130), Min), 1);
	TestEqual(TEXT("just above the first"), SKartScreenStack::TouchHitAmong(Regions, FVector2D(150, 80), Min), 1);
	TestEqual(TEXT("in the gap, nearer the second"), SKartScreenStack::TouchHitAmong(Regions, FVector2D(150, 172), Min), 2);
	TestEqual(TEXT("far away"), SKartScreenStack::TouchHitAmong(Regions, FVector2D(900, 900), Min), INDEX_NONE);

	// Swipes and cranks.
	FTouchTrack Swipe;
	Swipe.Feed({ ETouchPhase::Down, 0, FVector2D(0.5, 0.8), 1.0 });
	for (int32 i = 1; i <= 10; ++i)
	{
		Swipe.Feed({ ETouchPhase::Move, 0, FVector2D(0.5, 0.8 - 0.05 * i), 1.0 + 0.01 * i });
	}
	Swipe.Feed({ ETouchPhase::Up, 0, FVector2D(0.5, 0.3), 1.11 });
	TestTrue(TEXT("a flick up is fast upward"), Swipe.PeakUpSpeed > 4.0 && !Swipe.bActive && !Swipe.IsTap());
	FTouchCrank Crank;
	for (int32 i = 0; i <= 120; ++i)
	{
		const double T = i / 60.0;
		const double A = 2.0 * UE_DOUBLE_PI * 1.0 * T;
		Crank.Feed(FVector2D(0.5 + 0.2 * FMath::Cos(A), 0.5 + 0.2 * FMath::Sin(A)), T);
	}
	TestNearlyEqual(TEXT("one turn a second cranks at ~1 rev/s"), Crank.RevPerSec, 1.0, 0.15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartDeviceControlsTest, "MovecadeKart.Input.KartDeviceControls", DeviceTestFlags)
bool FKartDeviceControlsTest::RunTest(const FString&)
{
	// The device's wheel and buttons become the same FKartInput the room host makes of a phone's frames and kart-input.
	double Now = 50.0;
	TSharedRef<FKartDeviceSeat> Seat = MakeShared<FKartDeviceSeat>([&Now]() { return Now; });
	FKartDeviceControls Scheme;
	TestEqual(TEXT("30 Hz like the web kart controller"), Scheme.GetLayout().SendHz, 30.0);
	FLocalDeviceController Controller(TSharedRef<IDeviceControlScheme>(MakeShareable(&Scheme, [](IDeviceControlScheme*) {})), Seat);
	const FMotionConvention Convention = FMotionConvention::IOS();
	auto Drive = [&](double Degrees)
	{
		Now += 1.0 / 30.0;
		FDeviceMotion M = FDeviceGestures::Wheel(Degrees);
		M.TimeSeconds = Now;
		Controller.PushMotion(FromEngine(ToEngine(M, Convention), Convention));
	};
	Drive(0.0);
	Drive(35.0);
	// A phone turned 35 degrees clockwise: its wheel-mode frame, written out as phone-sim.mjs does.
	Net::FSensorFrame Phone;
	Phone.Flags = Net::SensorFlagCalibrated | Net::SensorFlagHasGravity;
	Phone.ScreenOrientation = 1;
	Phone.Gamma = 35.0;
	Phone = QuantizeSensorFrame(Phone);
	const FKartInput Read = Seat->Read();
	TestNearlyEqual(TEXT("steering: SteeringFromGamma of the phone's frame"), Read.SteeringX, Net::SteeringFromGamma(Phone.Gamma), 1e-3);
	TestFalse(TEXT("no pedals yet"), Read.bForward || Read.bBackward || Read.bJump || Read.bUseItem);

	Controller.PushButton(EDeviceButton::Gas, true, Now);
	Controller.PushButton(EDeviceButton::Drift, true, Now);
	Controller.PushButton(EDeviceButton::Item, true, Now);
	Controller.PushButton(EDeviceButton::Item, false, Now);
	FKartInput Pressed = Seat->Read();
	TestTrue(TEXT("GAS, DRIFT and ITEM"), Pressed.bForward && Pressed.bJump && Pressed.bUseItem && !Pressed.bBackward);
	TestFalse(TEXT("ITEM is one press"), Seat->Read().bUseItem);
	Controller.PushButton(EDeviceButton::Item, true, Now + 0.1);
	TestFalse(TEXT("ITEM is debounced (200 ms)"), Seat->Read().bUseItem);
	Controller.PushButton(EDeviceButton::Gas, false, Now);
	Controller.PushButton(EDeviceButton::Brake, true, Now);
	Pressed = Seat->Read();
	TestTrue(TEXT("GAS up, BRAKE down"), !Pressed.bForward && Pressed.bBackward);
	Now += Net::SensorStaleSeconds + 0.1;
	TestEqual(TEXT("stale tilt steers straight"), Seat->Read().SteeringX, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTouchSteeringTest, "MovecadeKart.Input.KartTouchSteering", DeviceTestFlags)
bool FKartTouchSteeringTest::RunTest(const FString&)
{
	// Without tilt (no sensors, or turned off) the kart steers with on-screen arrows sent as button steering.
	double Now = 20.0;
	TSharedRef<FKartDeviceSeat> Seat = MakeShared<FKartDeviceSeat>([&Now]() { return Now; });
	FKartDeviceControls Scheme;
	FLocalDeviceController Controller(TSharedRef<IDeviceControlScheme>(MakeShareable(&Scheme, [](IDeviceControlScheme*) {})), Seat);
	const FMotionConvention Convention = FMotionConvention::IOS();
	auto Step = [&](double Seconds, bool bMotion)
	{
		for (double End = Now + Seconds; Now < End;)
		{
			Now += 1.0 / 60.0;
			if (bMotion)
			{
				FDeviceMotion M = FDeviceGestures::Wheel(20.0);
				M.TimeSeconds = Now;
				Controller.PushMotion(FromEngine(ToEngine(M, Convention), Convention));
			}
			Controller.Tick(Now);
		}
	};
	auto HasArrows = [&Scheme]()
	{
		return Scheme.GetLayout().Buttons.ContainsByPredicate([](const FDeviceButtonSpec& B) { return B.Button == EDeviceButton::SteerLeft; });
	};
	const uint32 TiltRevision = Scheme.GetLayoutRevision();
	Step(0.5, false);
	TestFalse(TEXT("the sensors get a moment to start"), Controller.IsTouchSteering() || HasArrows());
	Step(FLocalDeviceController::FirstMotionGraceSeconds, false);
	TestTrue(TEXT("no sensors: the arrows come up"), Controller.IsTouchSteering() && HasArrows());
	TestNotEqual(TEXT("the overlay sees a new layout"), Scheme.GetLayoutRevision(), TiltRevision);

	Controller.PushButton(EDeviceButton::SteerRight, true, Now);
	Step(FKartDeviceControls::SteerRampSeconds * 0.5, false);
	const double Half = Seat->Read().SteeringX;
	TestTrue(TEXT("a short press turns part way, to the right (negative)"), Half < -0.2 && Half > -0.8);
	Step(FKartDeviceControls::SteerRampSeconds, false);
	TestEqual(TEXT("held, it reaches full right lock"), Seat->Read().SteeringX, -1.0);
	Controller.PushButton(EDeviceButton::SteerRight, false, Now);
	Step(FKartDeviceControls::SteerRampSeconds * 1.5, false);
	TestEqual(TEXT("let go, it centres"), Seat->Read().SteeringX, 0.0);
	Controller.PushButton(EDeviceButton::SteerLeft, true, Now);
	Step(Net::SensorStaleSeconds + 0.5, false);
	TestEqual(TEXT("left is positive, and button steering never goes stale"), Seat->Read().SteeringX, 1.0);
	Controller.PushButton(EDeviceButton::SteerLeft, false, Now);

	// The sensors wake up: tilt takes over again and the arrows go.
	Step(0.5, true);
	TestTrue(TEXT("motion back: tilt again"), !Controller.IsTouchSteering() && !HasArrows());
	const int32 FramesWithTilt = Seat->GetState().SensorFrames;
	TestTrue(TEXT("tilt frames flow"), FramesWithTilt > 0);

	// The player turns tilt off: the arrows show and the device's frames stop, even though it still moves.
	Controller.SetMotionSteeringWanted(false);
	Step(0.5, true);
	TestTrue(TEXT("tilt off: the arrows"), Controller.IsTouchSteering() && HasArrows());
	TestEqual(TEXT("no more tilt frames"), Seat->GetState().SensorFrames, 0);
	TestEqual(TEXT("the leftover tilt is gone"), Seat->Read().SteeringX, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeviceTouchZonesTest, "MovecadeKart.Input.TouchZones", DeviceTestFlags)
bool FDeviceTouchZonesTest::RunTest(const FString&)
{
	// Every scheme's controls stay inside the safe area, finger-sized, and apart, on the largest and smallest phones.
	FKartDeviceControls Kart;
	FKartDeviceSeat Seat([]() { return 0.0; });
	const FDeviceLayout Tilt = Kart.GetLayout();
	Kart.SetTouchSteering(true, 0.0, Seat);
	const FDeviceLayout Arrows = Kart.GetLayout();
	struct FScreen { const TCHAR* Name; FVector2D Size; FMargin Insets; };
	const FScreen Screens[] = {
		{ TEXT("iPhone 15 Pro"), FVector2D(2556.0, 1179.0), FMargin(177.f, 0.f, 177.f, 63.f) },
		{ TEXT("iPhone SE"), FVector2D(1334.0, 750.0), FMargin(0.f) },
	};
	for (const FScreen& Screen : Screens)
	{
		for (const FDeviceLayout* Layout : { &Tilt, &Arrows })
		{
			FDeviceTouchZones::FRowOptions Row;
			Row.bSettingsOpen = true;
			Row.bSteering = true;
			const FDeviceTouchZones Z = FDeviceTouchZones::Compute(*Layout, Screen.Size, Screen.Insets, Row);
			const FBox2D Safe(FVector2D(Screen.Insets.Left, Screen.Insets.Top), Screen.Size - FVector2D(Screen.Insets.Right, Screen.Insets.Bottom));
			const double MinTarget = FMath::Min(Safe.GetSize().X, Safe.GetSize().Y) * FDeviceTouchZones::MinTargetShare - 0.01;
			const FString What = FString::Printf(TEXT("%s, %s"), Screen.Name, Layout == &Tilt ? TEXT("tilt") : TEXT("arrows"));
			TestEqual(What + TEXT(": one zone per button"), Z.Buttons.Num(), Layout->Buttons.Num());
			for (const FTouchButtonZone& B : Z.Buttons)
			{
				TestTrue(FString::Printf(TEXT("%s: button %d is inside the safe area"), *What, B.Index), Safe.IsInside(FBox2D(B.Centre - FVector2D(B.Radius, B.Radius), B.Centre + FVector2D(B.Radius, B.Radius))));
				TestTrue(FString::Printf(TEXT("%s: button %d is finger-sized"), *What, B.Index), B.Radius * 2.0 >= MinTarget);
				TestEqual(FString::Printf(TEXT("%s: button %d hits itself"), *What, B.Index), Z.ButtonAt(B.Centre), B.Index);
				for (const FTouchButtonZone& Other : Z.Buttons)
				{
					TestTrue(FString::Printf(TEXT("%s: buttons %d and %d do not overlap"), *What, B.Index, Other.Index),
						Other.Index == B.Index || FVector2D::Distance(B.Centre, Other.Centre) >= B.Radius + Other.Radius);
				}
			}
			TestTrue(What + TEXT(": the settings row has calibrate, - , + and steering"), Z.Chips.Num() == 6 && Z.Chips.Last().Target == ETouchTarget::Steering);
			for (const FTouchChipZone& Chip : Z.Chips)
			{
				TestTrue(FString::Printf(TEXT("%s: chip %d is inside the safe area"), *What, static_cast<int32>(Chip.Target)), Safe.IsInside(Chip.Bounds()));
				TestTrue(FString::Printf(TEXT("%s: chip %d is finger-sized"), *What, static_cast<int32>(Chip.Target)), Chip.Size.Y >= MinTarget);
				TestTrue(FString::Printf(TEXT("%s: chip %d is clear of the buttons"), *What, static_cast<int32>(Chip.Target)),
					!Z.Buttons.ContainsByPredicate([&Chip](const FTouchButtonZone& B) { return Chip.Bounds().Intersect(B.HitBounds()); }));
			}
			TestTrue(What + TEXT(": the pause chip answers"), Z.ChipAt(Z.Chips[0].Pos + Z.Chips[0].Size * 0.5) == &Z.Chips[0]);
		}
	}

	// A portrait game with one big bottom button and the chips in the bottom corners (darts): beside it, not on it.
	FDeviceLayout Corners;
	Corners.Buttons = { { EDeviceButton::A, FText::GetEmpty(), EButtonAnchor::BottomCentre, true } };
	Corners.Chips = EChipPlacement::BottomCorners;
	for (const FVector2D& Size : { FVector2D(1179.0, 2556.0), FVector2D(750.0, 1334.0) })
	{
		const FDeviceTouchZones Z = FDeviceTouchZones::Compute(Corners, Size, FMargin());
		const FBox2D Button = Z.Buttons[0].HitBounds();
		for (const FTouchChipZone& Chip : Z.Chips)
		{
			TestFalse(TEXT("corner chip clear of the big button"), Chip.Bounds().Intersect(Button));
			TestTrue(TEXT("corner chip level with the big button"), Chip.Pos.Y < Z.Buttons[0].Centre.Y && Chip.Pos.Y + Chip.Size.Y > Z.Buttons[0].Centre.Y);
			TestTrue(TEXT("corner chip inside the screen"), FBox2D(FVector2D::ZeroVector, Size).IsInside(Chip.Bounds()));
		}
		TestTrue(TEXT("pause left, settings right"), Z.Chips[0].Pos.X < Z.Buttons[0].Centre.X && Z.Chips[1].Pos.X > Z.Buttons[0].Centre.X);
	}
	return true;
}

#endif
