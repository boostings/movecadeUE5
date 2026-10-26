#include "Games/Samurai/SamuraiBlade.h"
#include "Games/Samurai/SamuraiDeviceControls.h"
#include "Input/Tests/DeviceTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

namespace
{
	/** What ASamuraiGame::FeedPhone makes of a frame: the blade's direction and the intent gate (bladeIntent.ts). */
	struct FBladeReading
	{
		FVector Direction;
		bool bIntent = false;
	};

	TArray<FBladeReading> ReadBlade(TConstArrayView<Net::FSensorFrame> Frames)
	{
		FBladeIntent Intent;
		TArray<FBladeReading> Out;
		for (const Net::FSensorFrame& F : Frames)
		{
			FBladeReading& R = Out.AddDefaulted_GetRef();
			R.Direction = BladeDirectionFromQuaternion(FQuat(F.Quaternion.X, F.Quaternion.Y, F.Quaternion.Z, F.Quaternion.W));
			R.bIntent = Intent.Feed(F.Gyro.Size(), F.Accel.Size(), F.DtMs > 0 ? F.DtMs : 10.0);
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiDeviceControlsTest, "MovecadeKart.Samurai.DeviceControls", DeviceTestFlags)
bool FSamuraiDeviceControlsTest::RunTest(const FString&)
{
	// phone-sim.mjs's katana on the device: held up, then a 0.3 s downward cut at a yaw, gyro high through the sweep.
	constexpr double Hz = 100.0;
	TArray<FGestureSample> Samples;
	for (int32 k = 0; k < 90; ++k)
	{
		const double T = k / Hz;
		const double K = FMath::Clamp((T - 0.4) / 0.3, 0.0, 1.0);
		const bool bCutting = T >= 0.4 && T <= 0.7;
		FGestureSample& S = Samples.AddDefaulted_GetRef();
		S.Rotation = FDeviceGestures::Blade(k == 0 ? 0.0 : (bCutting ? 0.5 - 2.2 * K : 0.25), 0.2);
		S.Gyro = FVector3d(bCutting ? 420.0 : 0.0, 0.0, 0.0);
	}
	FSamuraiDeviceControls Scheme;
	TestEqual(TEXT("100 Hz like the samurai controller"), Scheme.GetLayout().SendHz, Hz);
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	TArray<Net::FSensorFrame> PhoneFrames;
	PlayGesture(Scheme, Sink, FDeviceGestures::Upright(), Samples, Hz, PhoneFrames);
	TestSameFrames(*this, TEXT("katana frames"), Sink->Frames, PhoneFrames);

	const TArray<FBladeReading> Device = ReadBlade(Sink->Frames);
	const TArray<FBladeReading> Phone = ReadBlade(PhoneFrames);
	bool bSame = Device.Num() == Phone.Num();
	bool bCut = false;
	for (int32 i = 0; bSame && i < Device.Num(); ++i)
	{
		bSame &= Device[i].bIntent == Phone[i].bIntent && Device[i].Direction.Equals(Phone[i].Direction, 2e-4);
		bCut |= Device[i].bIntent;
	}
	TestTrue(TEXT("the same blade and intent as the phone's, frame for frame"), bSame);
	TestTrue(TEXT("the cut opens the intent gate"), bCut);

	// The touch fallback: a finger slashed down the screen moves the blade fast enough to cut; a still finger doesn't.
	FSamuraiDeviceControls Touch;
	FRecordingSink TouchSink;
	FBladeIntent Intent;
	bool bTouchCut = false;
	bool bStillCut = false;
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.5, 0.1), 0.0 }, TouchSink);
	for (int32 i = 1; i <= 40; ++i)
	{
		const double T = i / 60.0;
		const bool bSlash = i > 20;
		Touch.OnSurface({ ETouchPhase::Move, 0, FVector2D(0.5, bSlash ? 0.1 + 0.04 * (i - 20) : 0.1), T }, TouchSink);
		FDeviceMotion M;
		if (TestTrue(TEXT("the finger holds the blade"), Touch.SynthesizeMotion(T, M)))
		{
			const bool bOpen = Intent.Feed(M.GyroDegPerSec.Size(), 0.0, 1000.0 / 60.0);
			(bSlash ? bTouchCut : bStillCut) |= bOpen;
		}
	}
	TestTrue(TEXT("a slash cuts"), bTouchCut);
	TestFalse(TEXT("a still finger doesn't"), bStillCut);
	return true;
}

#endif
