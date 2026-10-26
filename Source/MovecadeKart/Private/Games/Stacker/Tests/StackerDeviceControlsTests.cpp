#include "Games/Stacker/StackerDeviceControls.h"
#include "Input/Tests/DeviceTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Input;
using namespace MovecadeKart::Input::Tests;

namespace
{
	/** runtime.ts 501-508, as StackerGameInput.cpp's PalmFromFrame reads a frame: the palm's roll and pitch, degrees. */
	FVector2D Palm(const Net::FSensorFrame& F)
	{
		const double X = F.Quaternion.X, Y = F.Quaternion.Y, Z = F.Quaternion.Z, W = F.Quaternion.W;
		const double Nx = 2.0 * (X * Z + W * Y);
		const double Ny = 2.0 * (Y * Z - W * X);
		const double Nz = 1.0 - 2.0 * (X * X + Y * Y);
		return FVector2D(FMath::RadiansToDegrees(FMath::Atan2(Nx, Nz)), FMath::RadiansToDegrees(FMath::Atan2(Ny, Nz)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackerDeviceControlsTest, "MovecadeKart.Stacker.DeviceControls", DeviceTestFlags)
bool FStackerDeviceControlsTest::RunTest(const FString&)
{
	// A palm rocking on the device (phone-sim.mjs: a few degrees of roll), plus a lean away.
	constexpr double Hz = 60.0;
	TArray<FGestureSample> Samples;
	for (int32 k = 0; k < 60; ++k)
	{
		const double T = k / Hz;
		FGestureSample& S = Samples.AddDefaulted_GetRef();
		S.Rotation = FStackerDeviceControls::Palm(12.0 * FMath::Sin(T * 3.0), 6.0 * T);
		if (k == 30) S.ButtonA = true;
		if (k == 31) S.ButtonA = false;
	}
	FStackerDeviceControls Scheme;
	TSharedRef<FRecordingSink> Sink = MakeShared<FRecordingSink>();
	TArray<Net::FSensorFrame> PhoneFrames;
	PlayGesture(Scheme, Sink, FDeviceGestures::Flat(), Samples, Hz, PhoneFrames);
	TestSameFrames(*this, TEXT("palm frames"), Sink->Frames, PhoneFrames);
	bool bSamePalm = Sink->Frames.Num() == PhoneFrames.Num();
	for (int32 i = 0; bSamePalm && i < PhoneFrames.Num(); ++i)
	{
		bSamePalm &= Palm(Sink->Frames[i]).Equals(Palm(PhoneFrames[i]), 0.02);
	}
	TestTrue(TEXT("the tower balances on the same roll and pitch as the phone's"), bSamePalm);
	const FVector2D Last = Palm(Sink->Frames.Last());
	TestNearlyEqual(TEXT("roll reads back"), Last.X, 12.0 * FMath::Sin(59.0 / Hz * 3.0), 0.05);
	TestNearlyEqual(TEXT("pitch reads back"), Last.Y, 6.0 * 59.0 / Hz, 0.05);
	const int32 Drops = Sink->Events.FilterByPredicate([](const FRecordingSink::FEvent& E) { return E.Name == TEXT("stacker:drop"); }).Num();
	TestEqual(TEXT("DROP sends stacker:drop once, with no payload"), Drops, 1);
	TestFalse(TEXT("no payload"), Sink->FindEvent(TEXT("stacker:drop")) && Sink->FindEvent(TEXT("stacker:drop"))->Data.IsValid());

	// Touch: a tap anywhere drops; a drag leans the palm and doesn't.
	FStackerDeviceControls Touch;
	FRecordingSink TouchSink;
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.3, 0.6), 1.0 }, TouchSink);
	Touch.OnSurface({ ETouchPhase::Up, 0, FVector2D(0.3, 0.6), 1.08 }, TouchSink);
	TestEqual(TEXT("a tap drops"), TouchSink.Events.Num(), 1);
	Touch.OnSurface({ ETouchPhase::Down, 0, FVector2D(0.5, 0.5), 2.0 }, TouchSink);
	Touch.OnSurface({ ETouchPhase::Move, 0, FVector2D(0.7, 0.5), 2.2 }, TouchSink);
	FDeviceMotion M;
	TestTrue(TEXT("a drag leans the palm"), Touch.SynthesizeMotion(2.2, M));
	Net::FSensorFrame Leaned;
	const FQuat Q = M.Attitude;
	Leaned.Quaternion = FVector4d(Q.X, Q.Y, Q.Z, Q.W);
	TestTrue(TEXT("a drag right rolls right"), Palm(Leaned).X > 10.0);
	Touch.OnSurface({ ETouchPhase::Up, 0, FVector2D(0.7, 0.5), 2.3 }, TouchSink);
	TestEqual(TEXT("a drag doesn't drop"), TouchSink.Events.Num(), 1);
	return true;
}

#endif
