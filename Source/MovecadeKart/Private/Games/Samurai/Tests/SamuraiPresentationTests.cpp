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

#endif
