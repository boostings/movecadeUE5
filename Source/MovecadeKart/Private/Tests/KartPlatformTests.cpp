#include "Capture/KartCaptureSubsystem.h"
#include "KartGameViewportClient.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartCaptureSettingsTest, "MovecadeKart.Platform.CaptureSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartCaptureSettingsTest::RunTest(const FString& Parameters)
{
	// The engine's quoting: the value is quoted, not the whole switch (FParse skips quoted text when it looks for one).
	const FKartCaptureSettings Given = FKartCaptureSettings::Parse(TEXT("-game -KartCapture=\"D:/Shots/My Run\" -KartCaptureFrom=4.5 -KartCaptureFor=2 -KartCaptureNoUI"));
	TestEqual(TEXT("a quoted folder keeps its spaces"), Given.Folder, FString(TEXT("D:/Shots/My Run")));
	TestEqual(TEXT("from"), Given.From, 4.5);
	TestEqual(TEXT("for"), Given.For, 2.0);
	TestFalse(TEXT("no UI"), Given.bShowUI);
	TestTrue(TEXT("the stretch starts at From"), Given.Covers(4.5) && !Given.Covers(4.49));
	TestTrue(TEXT("and ends before From + For"), Given.Covers(6.49) && !Given.Covers(6.5));
	TestEqual(TEXT("frame files"), Given.FramePath(7), Given.Folder + TEXT("/frame_00007.png"));

	const FKartCaptureSettings Defaults = FKartCaptureSettings::Parse(TEXT("-game -KartCapture"));
	TestEqual(TEXT("default folder"), Defaults.Folder, FPaths::ProjectSavedDir() / TEXT("Capture"));
	TestEqual(TEXT("default length"), Defaults.For, 10.0);
	TestEqual(TEXT("from the start"), Defaults.From, 0.0);
	TestTrue(TEXT("with the UI"), Defaults.bShowUI);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartSplitscreenTest, "MovecadeKart.Platform.SplitscreenLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartSplitscreenTest::RunTest(const FString& Parameters)
{
	// RaceUI.tsx: three players are two views on top and one centred below, each a quarter of the screen.
	const FSplitscreenData& Three = GetDefault<UKartGameViewportClient>()->SplitscreenInfo[ESplitScreenType::ThreePlayer_FavorBottom];
	if (!TestEqual(TEXT("three views"), Three.PlayerData.Num(), 3))
	{
		return false;
	}
	for (const FPerPlayerSplitscreenData& View : Three.PlayerData)
	{
		TestTrue(TEXT("quarter-screen views"), FMath::IsNearlyEqual(View.SizeX, 0.5f) && FMath::IsNearlyEqual(View.SizeY, 0.5f));
	}
	TestTrue(TEXT("top left"), Three.PlayerData[0].OriginX == 0.f && Three.PlayerData[0].OriginY == 0.f);
	TestTrue(TEXT("top right"), Three.PlayerData[1].OriginX == 0.5f && Three.PlayerData[1].OriginY == 0.f);
	TestTrue(TEXT("bottom, centred"), Three.PlayerData[2].OriginX == 0.25f && Three.PlayerData[2].OriginY == 0.5f);
	return true;
}

#endif
