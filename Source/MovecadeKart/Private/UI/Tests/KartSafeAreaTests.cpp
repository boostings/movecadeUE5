#include "Games/ArcadeFraming.h"
#include "Input/DeviceControls.h"
#include "Input/KartDeviceControls.h"
#include "Input/SDeviceTouchControls.h"
#include "Misc/AutomationTest.h"
#include "UI/KartSafeArea.h"
#include "UI/KartUIStyle.h"

#if WITH_DEV_AUTOMATION_TESTS

using KartSafeArea::FKartDisplay;
using KartSafeArea::FKartSafeFrame;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartSafeAreaFrameTest, "MovecadeKart.UI.SafeArea.Frame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartSafeAreaFrameTest::RunTest(const FString& Parameters)
{
	// A 16:9 PC: exactly the old layout (Size.Y / 1080 and the 5 % TV margin), no touch zones.
	const FKartSafeFrame Pc = FKartSafeFrame::Make(FVector2D(1920.0, 1080.0), 1.f, FKartDisplay());
	TestEqual(TEXT("PC scale is Size.Y / 1080"), Pc.Scale, 1.f);
	TestEqual(TEXT("PC margin is the TV safe fraction"), Pc.SafeMin, FVector2D(1920.0, 1080.0) * KartUI::Metric::SafeFraction);
	TestEqual(TEXT("PC far margin mirrors it"), Pc.SafeMax, FVector2D(1920.0, 1080.0) * (1.0 - KartUI::Metric::SafeFraction));
	TestFalse(TEXT("no touch zones without device controls"), Pc.HasTouchZones() || Pc.TouchCentre.bIsValid || Pc.Chips.bIsValid);
	TestEqual(TEXT("FitWidth leaves PC cards alone"), Pc.FitWidth(1120.f), 1.f);

	// A landscape phone at 2x: the insets (pixels) become local units, plus the phone's pad, and the text grows.
	FKartDisplay Phone;
	Phone.bPhone = true;
	Phone.InsetsPx = FMargin(118.f, 0.f, 118.f, 42.f);
	const FKartSafeFrame Wide = FKartSafeFrame::Make(FVector2D(2340.0, 1080.0), 2.f, Phone);
	TestEqual(TEXT("phone scale is boosted"), Wide.Scale, KartSafeArea::PhoneHudBoost);
	TestTrue(TEXT("left edge clears the notch"), Wide.SafeMin.X > 59.0);
	TestTrue(TEXT("right edge clears the notch"), Wide.SafeMax.X < 2340.0 - 59.0);
	TestTrue(TEXT("bottom clears the home bar"), Wide.SafeMax.Y < 1080.0 - 21.0);

	// Held upright: laid out 1080 units across.
	FKartDisplay Upright = Phone;
	Upright.bPortrait = true;
	Upright.InsetsPx = FMargin(0.f, 177.f, 0.f, 102.f);
	const FKartSafeFrame Tall = FKartSafeFrame::Make(FVector2D(1080.0, 2340.0), 1.f, Upright);
	TestTrue(TEXT("portrait"), Tall.bPortrait);
	TestEqual(TEXT("portrait scale is Size.X / 1080, boosted"), Tall.Scale, KartSafeArea::PhoneHudBoost);
	TestTrue(TEXT("top clears the island"), Tall.SafeMin.Y > 177.0);
	TestTrue(TEXT("a wide card is fitted to the width"), Tall.FitWidth(1120.f) * 1120.f <= Tall.SafeSize().X + 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartSafeAreaTouchZonesTest, "MovecadeKart.UI.SafeArea.TouchZones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartSafeAreaTouchZonesTest::RunTest(const FString& Parameters)
{
	using namespace MovecadeKart::Input;
	// The zones must cover every button exactly where the touch overlay draws and hits it.
	auto Check = [this](const TCHAR* Name, const FDeviceLayout& Layout, const FVector2D& Size)
	{
		FKartDisplay Display;
		Display.bPhone = true;
		Display.bPortrait = Size.Y > Size.X;
		Display.Controls = &Layout;
		const FKartSafeFrame F = FKartSafeFrame::Make(Size, 1.f, Display);
		const TSharedRef<SDeviceTouchControls> Overlay = SNew(SDeviceTouchControls).Layout(Layout);
		for (int32 i = 0; i < Layout.Buttons.Num(); ++i)
		{
			FVector2D Centre;
			float Radius = 0.f;
			Overlay->ButtonCircle(i, Size, Centre, Radius);
			const FBox2D Circle(Centre - FVector2D(Radius, Radius), Centre + FVector2D(Radius, Radius));
			const bool bCovered = (F.TouchLeft.bIsValid && F.TouchLeft.IsInside(Circle)) || (F.TouchRight.bIsValid && F.TouchRight.IsInside(Circle))
				|| (F.TouchCentre.bIsValid && F.TouchCentre.IsInside(Circle));
			TestTrue(FString::Printf(TEXT("%s: button %d is inside a touch zone"), Name, i), bCovered);
			TestTrue(FString::Printf(TEXT("%s: a centred card stops above button %d"), Name, i),
				F.BottomClearOfTouch(Centre.X, 1.0) <= Centre.Y - Radius);
		}
		TestTrue(FString::Printf(TEXT("%s: the chips are reserved"), Name), F.Chips.bIsValid);
	};
	const FKartDeviceControls Kart;
	Check(TEXT("kart, landscape"), Kart.GetLayout(), FVector2D(2340.0, 1080.0));

	FDeviceLayout Portrait;
	Portrait.Buttons = { { EDeviceButton::A, FText::GetEmpty(), EButtonAnchor::BottomCentre, true } };
	Check(TEXT("one big button, portrait"), Portrait, FVector2D(1080.0, 2340.0));

	FDeviceLayout Pad;
	Pad.Buttons = { { EDeviceButton::A, FText::GetEmpty(), EButtonAnchor::BottomRight, true } };
	Pad.Chips = EChipPlacement::LeftEdge;
	Check(TEXT("one thumb button, landscape"), Pad, FVector2D(2340.0, 1080.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartSafeAreaTopPanelsTest, "MovecadeKart.UI.SafeArea.TopPanels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartSafeAreaTopPanelsTest::RunTest(const FString& Parameters)
{
	FKartDisplay Phone;
	Phone.bPhone = true;
	const FKartSafeFrame F = FKartSafeFrame::Make(FVector2D(2340.0, 1080.0), 1.f, Phone);
	for (const float MinWidth : { 0.f, 1000.f })
	{
		const FKartSafeFrame::FTopPanels Panels = F.TopPanels(4, 460.f, MinWidth, 150.f, 20.f, 400.f);
		TestEqual(TEXT("stacked only when side by side is too narrow"), Panels.bStacked, MinWidth > 0.f);
		for (int32 i = 0; i < 4; ++i)
		{
			const FBox2D Box(Panels.Pos(i), Panels.Pos(i) + Panels.Size());
			TestTrue(FString::Printf(TEXT("panel %d is inside the safe area"), i),
				Box.Min.X >= F.SafeMin.X - 0.01 && Box.Max.X <= F.SafeMax.X + 0.01 && Box.Min.Y >= F.SafeMin.Y - 0.01);
			TestFalse(FString::Printf(TEXT("panel %d leaves the centre gap"), i), Box.Min.X < F.Size.X * 0.5 + 200.0 && Box.Max.X > F.Size.X * 0.5 - 200.0);
			for (int32 j = 0; j < i; ++j)
			{
				const FBox2D Other(Panels.Pos(j), Panels.Pos(j) + Panels.Size());
				const FVector2D Overlap(FMath::Min(Box.Max.X, Other.Max.X) - FMath::Max(Box.Min.X, Other.Min.X), FMath::Min(Box.Max.Y, Other.Max.Y) - FMath::Max(Box.Min.Y, Other.Min.Y));
				TestFalse(FString::Printf(TEXT("panels %d and %d do not overlap"), j, i), Overlap.X > 1.0 && Overlap.Y > 1.0);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcadeFramingTest, "MovecadeKart.Arcade.Framing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArcadeFramingTest::RunTest(const FString& Parameters)
{
	using namespace ArcadeFraming;
	TestEqual(TEXT("landscape keeps the design FOV"), KeepWidth(44.0, DesignAspect, 0.5), 44.0);
	TestEqual(TEXT("a wider phone keeps it too"), KeepWidth(44.0, 19.5 / 9.0, 0.75), 44.0);
	const double Upright = KeepWidth(44.0, 9.0 / 19.5, 0.5);
	TestTrue(TEXT("upright, the lens opens"), Upright > 44.0);
	// What the 16:9 frame showed across half its width still spans the narrow screen.
	const double DesignHalfTan = FMath::Tan(FMath::DegreesToRadians(44.0 / 2.0)) * DesignAspect * 0.5;
	TestTrue(TEXT("the kept share fits across"), FMath::IsNearlyEqual(FMath::Tan(FMath::DegreesToRadians(Upright / 2.0)) * 9.0 / 19.5, DesignHalfTan, 1e-6));
	// A subject HalfWidth across at Distance spans the width exactly.
	const double Fov = VerticalFovForWidth(0.7, 6.0, 0.5);
	TestTrue(TEXT("fit to width"), FMath::IsNearlyEqual(FMath::Tan(FMath::DegreesToRadians(Fov / 2.0)) * 0.5 * 6.0, 0.7, 1e-6));
	TestTrue(TEXT("16:9 horizontal FOV round trip"), FMath::IsNearlyEqual(HorizontalFov(44.0, DesignAspect),
		2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(22.0)) * DesignAspect)), 1e-9));
	return true;
}

#endif
