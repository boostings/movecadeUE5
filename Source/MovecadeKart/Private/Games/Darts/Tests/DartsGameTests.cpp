// Tests for Bullseye Bash's game-side pure helpers: the phone release payload, the debug flags and the shared
// presentation helpers (the rules' golden tests are in DartsTests.cpp).
#include "Dom/JsonObject.h"
#include "Games/Darts/DartsDebugFlags.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/DartsRelease.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Darts;

namespace
{
	constexpr EAutomationTestFlags DartsGameTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsReleasePayloadTest, "MovecadeKart.Darts.ReleasePayload", DartsGameTestFlags)

bool FDartsReleasePayloadTest::RunTest(const FString& Parameters)
{
	FJsonObject Empty;
	TestFalse(TEXT("no power, no throw"), GestureFromRelease(Empty).IsSet());

	FJsonObject Minimal;
	Minimal.SetNumberField(TEXT("power"), 0.6);
	const TOptional<FDartGesture> Defaults = GestureFromRelease(Minimal);
	if (!TestTrue(TEXT("power alone is a throw"), Defaults.IsSet()))
	{
		return false;
	}
	TestEqual(TEXT("power read"), Defaults->Power, 0.6);
	TestEqual(TEXT("aim defaults to the centre"), Defaults->AimX, 0.0);
	TestEqual(TEXT("confidence defaults to 0.5"), Defaults->Confidence, 0.5);
	TestEqual(TEXT("steadiness defaults to 0.5"), Defaults->Steadiness, 0.5);

	FJsonObject Full;
	Full.SetNumberField(TEXT("power"), 0.4);
	Full.SetNumberField(TEXT("aimX"), -0.25);
	Full.SetNumberField(TEXT("aimY"), 0.75);
	Full.SetNumberField(TEXT("spinDegPerSec"), 120.0);
	Full.SetNumberField(TEXT("durationMs"), 180.0);
	const TOptional<FDartGesture> Read = GestureFromRelease(Full);
	TestTrue(TEXT("every field read"), Read.IsSet() && Read->AimX == -0.25 && Read->AimY == 0.75 && Read->SpinDegPerSec == 120.0 && Read->DurationMs == 180.0);

	FDartGesture Wild;
	Wild.AimX = 9.0;
	Wild.AimY = std::numeric_limits<double>::quiet_NaN();
	Wild.Power = -1.0;
	Wild.SpinDegPerSec = 1e9;
	Wild.Confidence = std::numeric_limits<double>::infinity();
	Wild.Steadiness = std::numeric_limits<double>::quiet_NaN();
	Wild.PeakMs2 = 500.0;
	Wild.DurationMs = -5.0;
	const FDartGesture Bounded = BoundRelease(Wild);
	TestEqual(TEXT("aim clamps"), Bounded.AimX, 1.5);
	TestEqual(TEXT("a NaN aim falls back to the centre"), Bounded.AimY, 0.0);
	TestEqual(TEXT("power clamps to 0"), Bounded.Power, 0.0);
	TestEqual(TEXT("spin clamps"), Bounded.SpinDegPerSec, 2000.0);
	TestEqual(TEXT("an infinite confidence falls back to 0.5"), Bounded.Confidence, 0.5);
	TestEqual(TEXT("a NaN steadiness falls back to 0.5"), Bounded.Steadiness, 0.5);
	TestEqual(TEXT("peak clamps"), Bounded.PeakMs2, 120.0);
	TestEqual(TEXT("duration clamps to 0"), Bounded.DurationMs, 0.0);

	FDartGesture Fine;
	Fine.AimX = 0.3;
	Fine.Power = 0.55;
	const FDartGesture Untouched = BoundRelease(Fine);
	TestTrue(TEXT("an in-range release is untouched"), Untouched.AimX == Fine.AimX && Untouched.Power == Fine.Power && Untouched.Confidence == Fine.Confidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsDebugFlagsTest, "MovecadeKart.Darts.DebugFlags", DartsGameTestFlags)

bool FDartsDebugFlagsTest::RunTest(const FString& Parameters)
{
	const FDartsDebugFlags None = FDartsDebugFlags::Parse(TEXT("-game -DartsPlayers=1"));
	TestFalse(TEXT("no local auto by default"), None.bLocalAuto);
	TestFalse(TEXT("no big-hit shots by default"), None.bShotOnBigHit);
	TestFalse(TEXT("a null command line is no flags"), FDartsDebugFlags::Parse(nullptr).bLocalAuto);
	// -DartsAuto is the base's (every seat a bot); it must not read as -DartsLocalAuto.
	TestFalse(TEXT("-DartsAuto is not -DartsLocalAuto"), FDartsDebugFlags::Parse(TEXT("-DartsAuto")).bLocalAuto);
#if !UE_BUILD_SHIPPING
	const FDartsDebugFlags Both = FDartsDebugFlags::Parse(TEXT("-game -DartsLocalAuto -DartsShotBig"));
	TestTrue(TEXT("-DartsLocalAuto"), Both.bLocalAuto);
	TestTrue(TEXT("-DartsShotBig"), Both.bShotOnBigHit);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsPresentationHelpersTest, "MovecadeKart.Darts.PresentationHelpers", DartsGameTestFlags)

bool FDartsPresentationHelpersTest::RunTest(const FString& Parameters)
{
	for (int32 i = 0; i < SectorCount; ++i)
	{
		TestEqual(*FString::Printf(TEXT("sector %d found by its value"), Sectors()[i]), Tuning::SectorIndexOfValue(Sectors()[i]), i);
	}
	TestEqual(TEXT("a value off the board is SectorCount"), Tuning::SectorIndexOfValue(25), SectorCount);
	TestTrue(TEXT("the sweet spot is sweet"), Tuning::IsSweet(SweetSpot));
	TestTrue(TEXT("just inside the band"), Tuning::IsSweet(SweetSpot + Tuning::SweetBand * 0.9));
	TestFalse(TEXT("just outside the band"), Tuning::IsSweet(SweetSpot - Tuning::SweetBand * 1.1));
	TestEqual(TEXT("smooth starts at 0"), Tuning::Smooth(0.0, 1.0, -1.0), 0.0);
	TestEqual(TEXT("smooth ends at 1"), Tuning::Smooth(0.0, 1.0, 2.0), 1.0);
	TestEqual(TEXT("smooth is half way at the middle"), Tuning::Smooth(0.0, 1.0, 0.5), 0.5);
	TestEqual(TEXT("smootherstep is half way at the middle"), Tuning::SmootherStep(0.5), 0.5);
	return true;
}

#endif
