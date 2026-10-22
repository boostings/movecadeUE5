#include "Games/Fishing/FishingDebugFlags.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishingDebugFlagsTest, "MovecadeKart.Fishing.DebugFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFishingDebugFlagsTest::RunTest(const FString& Parameters)
{
	const FFishingDebugFlags None = FFishingDebugFlags::Parse(TEXT("-game -FishingPlayers=0"));
	TestFalse(TEXT("no demo by default"), None.bDemo);
	TestEqual(TEXT("the rules' round length by default"), None.RoundSeconds, 0.0);
	TestFalse(TEXT("a fresh seed by default"), None.Seed.IsSet());

	const FFishingDebugFlags All = FFishingDebugFlags::Parse(TEXT("-game -FishingDemo -FishingClock=8 -FishingSeed=42"));
	TestTrue(TEXT("-FishingDemo"), All.bDemo);
	TestEqual(TEXT("-FishingClock=8"), All.RoundSeconds, 8.0);
	TestEqual(TEXT("-FishingSeed=42"), All.Seed.Get(0u), 42u);

	const FFishingDebugFlags Bad = FFishingDebugFlags::Parse(TEXT("-FishingClock=-3"));
	TestEqual(TEXT("a non-positive clock keeps the full round"), Bad.RoundSeconds, 0.0);
	TestFalse(TEXT("a null command line is the defaults"), FFishingDebugFlags::Parse(nullptr).bDemo);
	return true;
}

#endif
