#include "Misc/AutomationTest.h"
#include "UI/KartUiDebugFlags.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartUiDebugFlagsTest, "MovecadeKart.UI.DebugFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartUiDebugFlagsTest::RunTest(const FString& Parameters)
{
	const FKartUiDebugFlags None = FKartUiDebugFlags::Parse(TEXT("-KartAutoDrive -KartLaps=1"));
	TestFalse(TEXT("no tour without -KartUiShots"), None.bTour);
	TestEqual(TEXT("no steps without -KartUiShots"), None.TourSteps.Num(), 0);
	TestFalse(TEXT("no forced pad without -KartUiPad"), None.bForcePad);

	const FKartUiDebugFlags Tour = FKartUiDebugFlags::Parse(TEXT("-KartUiShots=title,menu,race -KartUiPad -KartBots=3"));
	TestTrue(TEXT("-KartUiShots starts the tour"), Tour.bTour);
	TestEqual(TEXT("every step, in order"), Tour.TourSteps.Num(), 3);
	if (Tour.TourSteps.Num() == 3)
	{
		TestEqual(TEXT("first step"), Tour.TourSteps[0], FName(TEXT("title")));
		TestEqual(TEXT("second step"), Tour.TourSteps[1], FName(TEXT("menu")));
		TestEqual(TEXT("last step"), Tour.TourSteps[2], FName(TEXT("race")));
	}
	TestTrue(TEXT("-KartUiPad forces gamepad glyphs"), Tour.bForcePad);

	TestFalse(TEXT("a null command line reads as no flags"), FKartUiDebugFlags::Parse(nullptr).bTour);
	return true;
}

#endif
