#include "Misc/AutomationTest.h"
#include "Render/KartGameUserSettings.h"
#include "Render/KartRenderBudget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The groups the budget may trim, in a fixed order, for compact comparisons. */
	TArray<int32> Groups(const Scalability::FQualityLevels& L)
	{
		return { L.ViewDistanceQuality, L.AntiAliasingQuality, L.ShadowQuality, L.GlobalIlluminationQuality, L.ReflectionQuality,
			L.PostProcessQuality, L.TextureQuality, L.EffectsQuality, L.FoliageQuality, L.ShadingQuality };
	}

	const EKartGraphicsPreset AllPresets[] = { EKartGraphicsPreset::Performance, EKartGraphicsPreset::Balanced,
		EKartGraphicsPreset::Quality, EKartGraphicsPreset::Ultra };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartRenderPresetsTest, "MovecadeKart.Render.PresetsAscend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartRenderPresetsTest::RunTest(const FString& Parameters)
{
	// Each preset is at least as good as the one below it, in every group and in resolution.
	for (int32 i = 1; i < UE_ARRAY_COUNT(AllPresets); ++i)
	{
		const TArray<int32> Lower = Groups(KartRender::QualityLevelsForPreset(AllPresets[i - 1]));
		const TArray<int32> Higher = Groups(KartRender::QualityLevelsForPreset(AllPresets[i]));
		for (int32 g = 0; g < Lower.Num(); ++g)
		{
			TestTrue(FString::Printf(TEXT("preset %d group %d ascends"), i, g), Higher[g] >= Lower[g]);
		}
		TestTrue(TEXT("resolution ascends"), KartRender::ScreenPercentageForPreset(AllPresets[i]) > KartRender::ScreenPercentageForPreset(AllPresets[i - 1]));
	}
	const Scalability::FQualityLevels Ultra = KartRender::QualityLevelsForPreset(EKartGraphicsPreset::Ultra);
	TestEqual(TEXT("Ultra is native TSR"), Ultra.ResolutionQuality, 100.f);
	TestEqual(TEXT("Ultra has Epic GI (hit-lit reflections)"), Ultra.GlobalIlluminationQuality, 3);
	TestTrue(TEXT("Performance keeps Lumen on (Medium, not Low)"), KartRender::QualityLevelsForPreset(EKartGraphicsPreset::Performance).GlobalIlluminationQuality >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartRenderSplitBudgetTest, "MovecadeKart.Render.SplitScreenBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartRenderSplitBudgetTest::RunTest(const FString& Parameters)
{
	const Scalability::FQualityLevels Ultra = KartRender::QualityLevelsForPreset(EKartGraphicsPreset::Ultra);

	// One view, no guard: exactly the player's choice.
	const KartRender::FRenderBudget Solo = KartRender::BudgetFor(Ultra, 1, 0);
	TestTrue(TEXT("solo budget is the user's levels"), Solo.Levels == Ultra);
	TestEqual(TEXT("solo ceiling"), Solo.MaxScreenPercentage, 100.f);
	TestTrue(TEXT("solo floor below the ceiling"), Solo.MinScreenPercentage < Solo.MaxScreenPercentage && Solo.MinScreenPercentage >= 50.f);

	// Two views: Lumen and shadows capped at High.
	const KartRender::FRenderBudget Two = KartRender::BudgetFor(Ultra, 2, 0);
	TestEqual(TEXT("2 views: GI High"), Two.Levels.GlobalIlluminationQuality, 2);
	TestEqual(TEXT("2 views: reflections High"), Two.Levels.ReflectionQuality, 2);
	TestEqual(TEXT("2 views: shadows High"), Two.Levels.ShadowQuality, 2);
	TestTrue(TEXT("2 views: ceiling never raised"), Two.MaxScreenPercentage <= Solo.MaxScreenPercentage);

	// Four views: also AA and post; the ceiling drops again.
	const KartRender::FRenderBudget Four = KartRender::BudgetFor(Ultra, 4, 0);
	TestEqual(TEXT("4 views: AA High"), Four.Levels.AntiAliasingQuality, 2);
	TestEqual(TEXT("4 views: post High"), Four.Levels.PostProcessQuality, 2);
	TestTrue(TEXT("4 views: lower ceiling"), Four.MaxScreenPercentage < Solo.MaxScreenPercentage);
	TestEqual(TEXT("ResolutionQuality follows the ceiling"), Four.Levels.ResolutionQuality, Four.MaxScreenPercentage);

	// The budget never raises a group the player turned down.
	const Scalability::FQualityLevels Performance = KartRender::QualityLevelsForPreset(EKartGraphicsPreset::Performance);
	const KartRender::FRenderBudget LowFour = KartRender::BudgetFor(Performance, 4, 0);
	const TArray<int32> User = Groups(Performance);
	const TArray<int32> Trimmed = Groups(LowFour.Levels);
	for (int32 g = 0; g < User.Num(); ++g)
	{
		TestTrue(FString::Printf(TEXT("group %d never raised"), g), Trimmed[g] <= User[g]);
	}
	TestTrue(TEXT("ceiling never raised"), LowFour.MaxScreenPercentage <= Performance.ResolutionQuality);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartRenderGuardTest, "MovecadeKart.Render.FrameTimeGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartRenderGuardTest::RunTest(const FString& Parameters)
{
	const Scalability::FQualityLevels Ultra = KartRender::QualityLevelsForPreset(EKartGraphicsPreset::Ultra);
	const KartRender::FRenderBudget Step1 = KartRender::BudgetFor(Ultra, 1, 1);
	TestEqual(TEXT("step 1 trims GI"), Step1.Levels.GlobalIlluminationQuality, 2);
	TestEqual(TEXT("step 1 trims reflections"), Step1.Levels.ReflectionQuality, 2);
	TestEqual(TEXT("step 1 keeps shadows"), Step1.Levels.ShadowQuality, 3);

	const KartRender::FRenderBudget Step2 = KartRender::BudgetFor(Ultra, 1, 2);
	TestEqual(TEXT("step 2 trims shadows"), Step2.Levels.ShadowQuality, 2);

	const KartRender::FRenderBudget Clamped = KartRender::BudgetFor(Ultra, 1, 99);
	TestTrue(TEXT("steps clamp at the maximum"), Clamped.Levels == Step2.Levels);

	// The guard never turns Lumen off (Low), whatever the starting point.
	const Scalability::FQualityLevels Performance = KartRender::QualityLevelsForPreset(EKartGraphicsPreset::Performance);
	const KartRender::FRenderBudget Floor = KartRender::BudgetFor(Performance, 4, KartRender::MaxGuardSteps);
	TestEqual(TEXT("GI floor is Medium"), Floor.Levels.GlobalIlluminationQuality, 1);
	TestEqual(TEXT("reflections floor is Medium"), Floor.Levels.ReflectionQuality, 1);

	TestEqual(TEXT("min screen percentage floor"), KartRender::MinScreenPercentageFor(66.7f), 50.f);
	TestEqual(TEXT("min screen percentage is 60 % of the ceiling"), KartRender::MinScreenPercentageFor(100.f), 60.f);
	return true;
}

#endif
