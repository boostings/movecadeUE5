#include "Misc/AutomationTest.h"
#include "Render/KartGameUserSettings.h"
#include "Render/KartMobileLighting.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartRenderMobileBudgetTest, "MovecadeKart.Render.MobileBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartRenderMobileBudgetTest::RunTest(const FString& Parameters)
{
	// A device profile's levels (IOS_High in DefaultDeviceProfiles.ini): High with Medium foliage, native resolution.
	Scalability::FQualityLevels Phone;
	Phone.SetFromSingleQualityLevel(2);
	Phone.FoliageQuality = 1;
	Phone.ResolutionQuality = 100.f;

	const KartRender::FRenderBudget None = KartRender::MobileBudgetFor(Phone, 0);
	TestTrue(TEXT("no guard: the profile's levels"), None.Levels == Phone);
	TestEqual(TEXT("no dynamic resolution: floor is the ceiling"), None.MinScreenPercentage, None.MaxScreenPercentage);

	const KartRender::FRenderBudget Step1 = KartRender::MobileBudgetFor(Phone, 1);
	TestEqual(TEXT("step 1 trims shadows"), Step1.Levels.ShadowQuality, 1);
	TestEqual(TEXT("step 1 trims post"), Step1.Levels.PostProcessQuality, 1);
	TestEqual(TEXT("step 1 trims effects"), Step1.Levels.EffectsQuality, 1);
	TestEqual(TEXT("step 1 keeps view distance"), Step1.Levels.ViewDistanceQuality, 2);
	TestEqual(TEXT("step 1 keeps textures (memory, not frame time)"), Step1.Levels.TextureQuality, 2);
	TestEqual(TEXT("step 1 keeps resolution"), Step1.MaxScreenPercentage, 100.f);

	const KartRender::FRenderBudget Step2 = KartRender::MobileBudgetFor(Phone, 2);
	TestEqual(TEXT("step 2 trims view distance"), Step2.Levels.ViewDistanceQuality, 1);
	TestEqual(TEXT("step 2 trims foliage"), Step2.Levels.FoliageQuality, 0);
	TestEqual(TEXT("step 2 lowers the resolution ceiling"), Step2.MaxScreenPercentage, KartRender::MobileGuardScreenPercentage);
	TestEqual(TEXT("ResolutionQuality follows the ceiling"), Step2.Levels.ResolutionQuality, KartRender::MobileGuardScreenPercentage);

	// Already at Low: the guard never goes below 0.
	Scalability::FQualityLevels Low;
	Low.SetFromSingleQualityLevel(0);
	const KartRender::FRenderBudget LowGuard = KartRender::MobileBudgetFor(Low, KartRender::MaxGuardSteps);
	TestEqual(TEXT("low shadows stay at 0"), LowGuard.Levels.ShadowQuality, 0);
	TestEqual(TEXT("low view distance stays at 0"), LowGuard.Levels.ViewDistanceQuality, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMobileLightingTest, "MovecadeKart.Render.MobileLightingFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartMobileLightingTest::RunTest(const FString& Parameters)
{
	using KartMobileLighting::FDirectionalInfo;
	TestEqual(TEXT("no lights: no key"), KartMobileLighting::PickKeyLight({}), INDEX_NONE);

	// A dim rim registered first must not win the single mobile directional slot.
	const FDirectionalInfo RimFirst[] = { { 1.f, true }, { 6.f, true } };
	TestEqual(TEXT("the brightest is the key"), KartMobileLighting::PickKeyLight(RimFirst), 1);

	// A bright rim (no shadows) loses to the shadow-casting key (Sumo's moon and rim).
	const FDirectionalInfo BrightRim[] = { { 2.f, true, true }, { 5.f, true, false } };
	TestEqual(TEXT("the shadow caster is the key"), KartMobileLighting::PickKeyLight(BrightRim), 0);
	TestEqual(TEXT("half the rim folds into the sky"), KartMobileLighting::SkyScaleFor(RimFirst, 1), 1.f + KartMobileLighting::FoldShare / 6.f);

	const FDirectionalInfo HiddenBright[] = { { 9.f, false }, { 2.f, true } };
	TestEqual(TEXT("hidden lights never win"), KartMobileLighting::PickKeyLight(HiddenBright), 1);
	TestEqual(TEXT("hidden lights don't fold"), KartMobileLighting::SkyScaleFor(HiddenBright, 1), 1.f);

	const FDirectionalInfo Twins[] = { { 4.f, true }, { 4.f, true }, { 4.f, true } };
	TestEqual(TEXT("fold is capped"), KartMobileLighting::SkyScaleFor(Twins, 0), KartMobileLighting::MaxFoldScale);

	const FDirectionalInfo Single[] = { { 3.f, true } };
	TestEqual(TEXT("a single light changes nothing"), KartMobileLighting::SkyScaleFor(Single, 0), 1.f);

	// Indoor ambient tint: neutral without lamps, warm under warm lamps, never fully saturated.
	using KartMobileLighting::FLocalInfo;
	TestTrue(TEXT("no lamps: white"), KartMobileLighting::IndoorAmbientTint({}).Equals(FLinearColor::White));
	const FLocalInfo Warm[] = { { FLinearColor(1.f, 0.6f, 0.3f), 800.f }, { FLinearColor(1.f, 0.6f, 0.3f), 400.f } };
	const FLinearColor WarmTint = KartMobileLighting::IndoorAmbientTint(Warm);
	TestTrue(TEXT("warm lamps tint warm"), WarmTint.R > WarmTint.B);
	TestTrue(TEXT("tint pulled toward white"), WarmTint.B > 0.3f);
	const FLocalInfo Neon[] = { { FLinearColor(0.f, 0.f, 1.f), 10.f }, { FLinearColor::White, 2000.f } };
	const FLinearColor NeonTint = KartMobileLighting::IndoorAmbientTint(Neon);
	TestTrue(TEXT("a dim neon barely tints the room"), FMath::Abs(NeonTint.R - NeonTint.G) < 0.05f && NeonTint.B < 1.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartRenderThermalTest, "MovecadeKart.Render.ThermalPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartRenderThermalTest::RunTest(const FString& Parameters)
{
	using KartRender::EThermalState;
	const KartRender::FThermalResponse Neutral;
	TestTrue(TEXT("good: nothing"), KartRender::ThermalResponseFor(EThermalState::Good, false) == Neutral);
	TestTrue(TEXT("unknown: nothing"), KartRender::ThermalResponseFor(EThermalState::Unknown, false) == Neutral);

	const KartRender::FThermalResponse Bad = KartRender::ThermalResponseFor(EThermalState::Bad, false);
	TestEqual(TEXT("bad keeps 60"), Bad.MaxFps, 0.f);
	TestEqual(TEXT("bad trims one step"), Bad.MinGuardSteps, 1);

	const KartRender::FThermalResponse Serious = KartRender::ThermalResponseFor(EThermalState::Serious, false);
	TestEqual(TEXT("serious caps at 30"), Serious.MaxFps, KartRender::ThermalFallbackFps);

	const KartRender::FThermalResponse Critical = KartRender::ThermalResponseFor(EThermalState::Critical, false);
	TestEqual(TEXT("critical caps at 30"), Critical.MaxFps, KartRender::ThermalFallbackFps);
	TestEqual(TEXT("critical takes every step"), Critical.MinGuardSteps, KartRender::MaxGuardSteps);

	const KartRender::FThermalResponse LowPower = KartRender::ThermalResponseFor(EThermalState::Good, true);
	TestEqual(TEXT("low power caps at 30"), LowPower.MaxFps, KartRender::ThermalFallbackFps);
	TestEqual(TEXT("low power keeps the levels"), LowPower.MinGuardSteps, 0);
	return true;
}

#endif
