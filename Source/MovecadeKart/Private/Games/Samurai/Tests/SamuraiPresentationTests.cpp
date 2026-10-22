#include "Games/Samurai/SamuraiBlade.h"
#include "Games/Samurai/SamuraiFx.h"
#include "Games/Samurai/SamuraiTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Samurai;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiDebugFlagsTest, "MovecadeKart.Samurai.DebugFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSamuraiDebugFlagsTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("No switch: no demo"), FSamuraiDebugFlags::Parse(TEXT("-SamuraiPlayers=1")).bPracticeDemo);
	TestFalse(TEXT("No command line: no demo"), FSamuraiDebugFlags::Parse(nullptr).bPracticeDemo);
#if UE_BUILD_SHIPPING
	TestFalse(TEXT("Shipping never reads the switch"), FSamuraiDebugFlags::Parse(TEXT("-SamuraiPracticeDemo")).bPracticeDemo);
#else
	TestTrue(TEXT("-SamuraiPracticeDemo"), FSamuraiDebugFlags::Parse(TEXT("-SamuraiBots=2 -SamuraiPracticeDemo")).bPracticeDemo);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiFxRecipeTest, "MovecadeKart.Samurai.FxRecipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSamuraiFxRecipeTest::RunTest(const FString& Parameters)
{
	TArray<MovecadeKart::Fx::FKartFxEmit> Out;
	// Two juice arcs, the spray, the pulp and the splash; a perfect cut adds the star and the glints.
	AppendCutFx(EVariant::Apple, FVector::ZeroVector, FVector(0, 1, 0), 0.3, /*bPerfect*/ false, Out);
	TestEqual(TEXT("A cut's emits"), Out.Num(), 5);
	Out.Reset();
	AppendCutFx(EVariant::Melon, FVector::ZeroVector, FVector(0, 1, 0), 0.3, /*bPerfect*/ true, Out);
	TestEqual(TEXT("A perfect cut's emits"), Out.Num(), 7);
	TestTrue(TEXT("Perfect arcs throw more juice"), Out[0].Count > 10);
	// The arcs leave the two cut faces, either side of the cut plane.
	TestTrue(TEXT("Arcs leave opposite faces"), Out[0].Position.Y < 0.0 && Out[1].Position.Y > 0.0);
	Out.Reset();
	AppendBombFx(FVector(0.0, 0.0, 100.0), Out);
	TestTrue(TEXT("A bomb: the explosion and the shards"), Out.Num() >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiLowFpsSwingTest, "MovecadeKart.Samurai.LowFpsSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSamuraiLowFpsSwingTest::RunTest(const FString& Parameters)
{
	// At 9 fps a frame is 0.111 s; the game caps it at 0.1f s (a float, a hair over 0.1 in double).
	const float CappedFrame = FMath::Min(1.f / 9.f, 0.1f);
	const double DtSeconds = CappedFrame;
	constexpr double SwingDegPerSecond = 300.0;

	FBladeIntent Uncapped;
	bool bUncappedOpened = false;
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		bUncappedOpened |= Uncapped.Feed(SwingDegPerSecond, 0.0, DtSeconds * 1000.0);
	}
	TestFalse(TEXT("The raw float frame reads as a stall (the bug FrameMs fixes)"), bUncappedOpened);

	FBladeIntent Capped;
	bool bOpened = false;
	for (int32 Frame = 0; Frame < 3; ++Frame)
	{
		bOpened |= Capped.Feed(SwingDegPerSecond, 0.0, FBladeIntent::FrameMs(DtSeconds));
	}
	TestTrue(TEXT("A fast swing at 9 fps opens the intent"), bOpened);

	TestEqual(TEXT("FrameMs caps at the gate's limit"), FBladeIntent::FrameMs(DtSeconds), FBladeIntent::MaxFrameMs);
	TestEqual(TEXT("FrameMs leaves normal frames alone"), FBladeIntent::FrameMs(1.0 / 60.0), (1.0 / 60.0) * 1000.0);
	return true;
}

#endif
