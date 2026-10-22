// Tests for the Sky Stacker helpers outside the golden port: the drop preview the guide shows, and the debug flags.
#include "Games/Stacker/StackerDebugFlags.h"
#include "Games/Stacker/StackerRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Stacker;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackerPreviewTest, "MovecadeKart.Stacker.Preview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStackerPreviewTest::RunTest(const FString& Parameters)
{
	// The preview at the cursor must call every drop the way DropBlock then resolves it, across perfects, trims and
	// misses, on a steady tower and a leaning one.
	for (const double Lean : { 0.0, 7.0 })
	{
		for (double Offset = -3.0; Offset <= 3.0; Offset += 0.125)
		{
			FMatch M = CreateMatch({ { TEXT("p1"), TEXT("P1") } });
			while (M.Phase == EMatchPhase::Countdown)
			{
				StepMatch(M, 100.0);
			}
			FTower& Tower = M.Towers[0];
			Tower.Balance.LeanX = Lean;
			Tower.CursorCenter = Tower.Top().Center + Offset;
			const FDropPreview Preview = PreviewDrop(Tower, Tower.CursorCenter);
			const double WidthBefore = Tower.Top().Width;
			const TOptional<FDropOutcome> Outcome = DropBlock(M, TEXT("p1"));
			if (!TestTrue(TEXT("the drop is allowed"), Outcome.IsSet()))
			{
				return false;
			}
			const FString What = FString::Printf(TEXT("lean %.1f offset %.3f"), Lean, Offset);
			TestEqual(What + TEXT(": perfect"), Preview.bPerfect, Outcome->Kind == EDropKind::Perfect);
			TestEqual(What + TEXT(": miss"), Preview.bMiss, Outcome->Kind == EDropKind::Miss);
			if (Outcome->Kind == EDropKind::Land)
			{
				TestEqual(What + TEXT(": kept width"), Preview.Right - Preview.Left, Outcome->Width);
				TestTrue(What + TEXT(": trimmed"), Outcome->Width < WidthBefore);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackerDebugFlagsTest, "MovecadeKart.Stacker.DebugFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStackerDebugFlagsTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no flags by default"), FStackerDebugFlags::Parse(TEXT("-game -StackerBots=3")).bLeanTest);
	TestFalse(TEXT("no command line"), FStackerDebugFlags::Parse(nullptr).bLeanTest);
#if !UE_BUILD_SHIPPING
	TestTrue(TEXT("-StackerLeanTest"), FStackerDebugFlags::Parse(TEXT("-game -StackerLeanTest -StackerBots=3")).bLeanTest);
#endif
	return true;
}

#endif
