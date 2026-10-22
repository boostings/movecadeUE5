// Port-side checks for Bowling's presentation helpers (not golden: the rules' goldens live in BowlingTests.cpp).
#include "Games/Bowling/BowlingGame.h"
#include "Games/Bowling/BowlingSim.h"
#include "Games/Bowling/BowlingThrow.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Bowling;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingShowcaseTest, "MovecadeKart.BowlingPort.Showcase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowlingShowcaseTest::RunTest(const FString& Parameters)
{
	// The strike the -BowlShowcase hook throws (screenshots of the celebration) must be a strike.
	const TArray<int32> Rack = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	TestEqual(TEXT("the showcase shot strikes"), ResolveShot(ABowlingGame::ShowcaseShot(), Rack).Felled.Num(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingLineReadTest, "MovecadeKart.BowlingPort.LineRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowlingLineReadTest::RunTest(const FString& Parameters)
{
	const TArray<int32> Full = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	TestEqual(TEXT("pocket"), static_cast<int32>(ABowlingGame::ReadLine(0.07, false, Full)), static_cast<int32>(EBowlingLine::Pocket));
	TestEqual(TEXT("brooklyn"), static_cast<int32>(ABowlingGame::ReadLine(-0.07, false, Full)), static_cast<int32>(EBowlingLine::Brooklyn));
	TestEqual(TEXT("head on"), static_cast<int32>(ABowlingGame::ReadLine(0.0, false, Full)), static_cast<int32>(EBowlingLine::HeadOn));
	TestEqual(TEXT("gutter"), static_cast<int32>(ABowlingGame::ReadLine(0.07, true, Full)), static_cast<int32>(EBowlingLine::Gutter));
	TestEqual(TEXT("spare on target"), static_cast<int32>(ABowlingGame::ReadLine(-0.45, false, { 7 })), static_cast<int32>(EBowlingLine::OnTarget));
	TestEqual(TEXT("spare missed"), static_cast<int32>(ABowlingGame::ReadLine(0.3, false, { 7 })), static_cast<int32>(EBowlingLine::Light));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingWindowTest, "MovecadeKart.BowlingPort.ThrowWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowlingWindowTest::RunTest(const FString& Parameters)
{
	// The detector's sliding window survives long streams (thousands of frames, many bulk drops) and still releases.
	FThrowDetector D;
	MovecadeKart::Net::FSensorFrame Frame;
	Frame.DtMs = 4;
	Frame.Flags = MovecadeKart::Net::SensorFlagHasGyro | MovecadeKart::Net::SensorFlagHasAccel | MovecadeKart::Net::SensorFlagHasGravity;
	Frame.Gravity = FVector3d(0.0, -9.8, 0.0);
	for (int32 i = 0; i < 5000; ++i)
	{
		Frame.Accel = FVector3d(0.0, 0.0, 1.0);
		D.Feed(Frame);
	}
	D.Grip();
	for (int32 i = 0; i < 60; ++i)
	{
		Frame.Accel = FVector3d(0.0, 0.0, i < 40 ? 20.0 : 2.0);
		D.Feed(Frame);
	}
	const FThrowRelease R = D.Release();
	TestTrue(TEXT("a hard push reads as power"), R.Shot.Power > 0.3);
	return true;
}

#endif
