#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "UI/KartAccessibility.h"
#include "UI/KartPlayerPrefs.h"
#include "UI/KartUIPainter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags PainterTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartPainterLayersTest, "MovecadeKart.UI.PainterLayers", PainterTestFlags)

bool FKartPainterLayersTest::RunTest(const FString& Parameters)
{
	FSlateWindowElementList Elements(nullptr);
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(1920.f, 1080.f), FSlateLayoutTransform());
	int32 Layer = 10;
	const FKartPainter P(Geometry, Elements, Layer);

	// Consecutive primitives with the same batch parameters share a layer; anything different starts the next.
	P.Circle(FVector2D(100.0, 100.0), 20.f, FLinearColor::Red);
	P.Circle(FVector2D(200.0, 100.0), 20.f, FLinearColor::Green, FLinearColor::Transparent, 0.f);
	P.Circle(FVector2D(300.0, 100.0), 20.f, FLinearColor::Blue);
	TestEqual(TEXT("three equal circles take one layer"), Layer, 11);
	P.Circle(FVector2D(400.0, 100.0), 30.f, FLinearColor::Blue);
	TestEqual(TEXT("a different size starts a new layer"), Layer, 12);
	P.Circle(FVector2D(500.0, 100.0), 20.f, FLinearColor::Blue);
	TestEqual(TEXT("going back to the first size never rejoins its old layer"), Layer, 13);

	// A clip change always starts a new layer.
	P.PushClip(FVector2D::ZeroVector, FVector2D(100.0, 100.0));
	P.Circle(FVector2D(10.0, 10.0), 20.f, FLinearColor::Blue);
	P.PopClip();
	TestEqual(TEXT("clipped primitives get their own layer"), Layer, 14);

	// Item groups: the Nth primitive of every item shares a layer; order within an item is kept.
	{
		const FKartPaintGroup Group(P);
		for (int32 Item = 0; Item < 5; ++Item)
		{
			Group.Next();
			P.Circle(FVector2D(100.0 * Item, 300.0), 20.f, FLinearColor::White);
			P.Circle(FVector2D(100.0 * Item, 300.0), 10.f, FLinearColor::Black);
		}
		{
			// A nested group continues the open item instead of restarting it.
			const FKartPaintGroup Nested(P);
			Nested.Next();
			P.Circle(FVector2D(700.0, 300.0), 5.f, FLinearColor::Black);
		}
	}
	TestEqual(TEXT("five two-part items take three layers (the nested primitive is the last item's third)"), Layer, 17);
	P.Circle(FVector2D(100.0, 500.0), 20.f, FLinearColor::White);
	TestEqual(TEXT("drawing after a group goes on top of it"), Layer, 18);

	// A new painter on the same counter never joins a run left by an earlier one.
	const FKartPainter Again(Geometry, Elements, Layer);
	Again.Circle(FVector2D(100.0, 500.0), 20.f, FLinearColor::White);
	TestEqual(TEXT("a fresh painter starts a fresh layer"), Layer, 19);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAccessibilityTest, "MovecadeKart.UI.Accessibility", PainterTestFlags)

bool FKartAccessibilityTest::RunTest(const FString& Parameters)
{
	UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
	const float SavedScale = Prefs->UIScale;
	const bool bSavedMotion = Prefs->bReduceMotion;
	const uint8 SavedVision = Prefs->ColorVision;
	ON_SCOPE_EXIT
	{
		Prefs->UIScale = SavedScale;
		Prefs->bReduceMotion = bSavedMotion;
		Prefs->ColorVision = SavedVision;
	};

	Prefs->bReduceMotion = false;
	TestEqual(TEXT("full motion by default"), FKartAccessibility::MotionScale(), 1.f);
	Prefs->bReduceMotion = true;
	TestTrue(TEXT("reduce motion tones motion down"), FKartAccessibility::ReduceMotion() && FKartAccessibility::MotionScale() < 0.5f);

	Prefs->UIScale = 9.f;
	TestEqual(TEXT("UI scale is clamped to the largest step"), FKartAccessibility::UIScale(), FKartAccessibility::UIScaleSteps().Last());
	Prefs->UIScale = 0.f;
	TestEqual(TEXT("UI scale is clamped to the smallest step"), FKartAccessibility::UIScale(), FKartAccessibility::UIScaleSteps()[0]);
	TestTrue(TEXT("100% is a step"), FKartAccessibility::UIScaleSteps().Contains(1.f));

	for (int32 Mode = 0; Mode < static_cast<int32>(EKartColorVision::Count); ++Mode)
	{
		Prefs->ColorVision = static_cast<uint8>(Mode);
		const FLinearColor Good = FKartAccessibility::Good(), Bad = FKartAccessibility::Bad();
		// Good and bad must differ in lightness too, not just hue, to survive any colour-vision deficiency.
		TestTrue(*FString::Printf(TEXT("mode %d: good and bad differ in lightness"), Mode), FMath::Abs(Good.GetLuminance() - Bad.GetLuminance()) > 0.05f);
	}
	Prefs->ColorVision = 200;
	TestEqual(TEXT("an out-of-range mode reads as the last one"), static_cast<int32>(FKartAccessibility::ColorVision()), static_cast<int32>(EKartColorVision::Count) - 1);
	return true;
}

#endif
