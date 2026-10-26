#include "Games/ArcadeCatalog.h"
#include "Misc/AutomationTest.h"
#include "Mobile/MovecadeOrientation.h"
#include "Mobile/MovecadeOrientationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using Arcade::EArcadeOrientation;
using Mobile::EPreviewShape;
using Mobile::EScreenShape;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileCatalogOrientationTest, "MovecadeKart.Mobile.CatalogOrientation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileCatalogOrientationTest::RunTest(const FString& Parameters)
{
	const TMap<FString, EArcadeOrientation> Expected = {
		{ TEXT("bowling"), EArcadeOrientation::Portrait },
		{ TEXT("darts"), EArcadeOrientation::Portrait },
		{ TEXT("samurai"), EArcadeOrientation::Portrait },
		{ TEXT("stacker"), EArcadeOrientation::Landscape },
		{ TEXT("sumo"), EArcadeOrientation::Landscape },
		{ TEXT("derby"), EArcadeOrientation::Landscape },
		{ TEXT("fishing"), EArcadeOrientation::Landscape },
	};
	for (const Arcade::FGameInfo& Game : Arcade::Games())
	{
		const EArcadeOrientation* Want = Expected.Find(Game.Id);
		if (!TestNotNull(*FString::Printf(TEXT("%s has an expected orientation (a new game picks one here)"), *Game.Id), Want))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s orientation"), *Game.Id), Arcade::LexToString(Game.Orientation), Arcade::LexToString(*Want));
		TestEqual(*FString::Printf(TEXT("%s level orientation"), *Game.Id), Arcade::OrientationForLevel(Game.Map), Game.Orientation);
	}
	TestEqual(TEXT("every expected game is in the catalog"), Arcade::Games().Num(), Expected.Num());

	TestEqual(TEXT("kart races are landscape"), Arcade::KartOrientation, EArcadeOrientation::Landscape);
	TestEqual(TEXT("the menus and lobby are landscape"), Arcade::MenuOrientation, EArcadeOrientation::Landscape);
	TestEqual(TEXT("a track level (front end, races)"), Arcade::OrientationForLevel(TEXT("/Game/Maps/L_SunsetHarbor")), EArcadeOrientation::Landscape);
	TestEqual(TEXT("another track"), Arcade::OrientationForLevel(TEXT("L_NeonCity")), EArcadeOrientation::Landscape);
	TestEqual(TEXT("a game level with options and a path"), Arcade::OrientationForLevel(TEXT("/Game/Maps/L_Darts.L_Darts?Players=1?Bots=2")), EArcadeOrientation::Portrait);
	TestEqual(TEXT("no level yet"), Arcade::OrientationForLevel(FString()), Arcade::MenuOrientation);

	EArcadeOrientation Parsed = EArcadeOrientation::Landscape;
	TestTrue(TEXT("parses portrait"), Arcade::LexFromString(Parsed, TEXT("Portrait")) && Parsed == EArcadeOrientation::Portrait);
	TestTrue(TEXT("parses any"), Arcade::LexFromString(Parsed, TEXT("any")) && Parsed == EArcadeOrientation::Any);
	TestFalse(TEXT("rejects junk"), Arcade::LexFromString(Parsed, TEXT("sideways")));
	TestEqual(TEXT("junk leaves the value"), Parsed, EArcadeOrientation::Any);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileOrientationDecisionsTest, "MovecadeKart.Mobile.OrientationDecisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileOrientationDecisionsTest::RunTest(const FString& Parameters)
{
	// The device lock per wanted orientation.
	TestTrue(TEXT("landscape follows the sensor between both sides"), Mobile::ToDeviceOrientation(EArcadeOrientation::Landscape) == EDeviceScreenOrientation::LandscapeSensor);
	TestTrue(TEXT("portrait is upright only"), Mobile::ToDeviceOrientation(EArcadeOrientation::Portrait) == EDeviceScreenOrientation::Portrait);
	TestTrue(TEXT("any is unlocked"), Mobile::ToDeviceOrientation(EArcadeOrientation::Any) == EDeviceScreenOrientation::FullSensor);

	// Level and override.
	TestEqual(TEXT("game level"), Mobile::ResolveWanted(TEXT("L_Samurai"), {}), EArcadeOrientation::Portrait);
	TestEqual(TEXT("returning to the menus"), Mobile::ResolveWanted(TEXT("L_SunsetHarbor"), {}), EArcadeOrientation::Landscape);
	TestEqual(TEXT("an override wins"), Mobile::ResolveWanted(TEXT("L_Samurai"), EArcadeOrientation::Any), EArcadeOrientation::Any);

	// The logical orientation is the viewport's shape.
	TestEqual(TEXT("tall"), Mobile::ShapeOf(FIntPoint(1179, 2556)), EScreenShape::Portrait);
	TestEqual(TEXT("wide"), Mobile::ShapeOf(FIntPoint(2556, 1179)), EScreenShape::Landscape);
	TestEqual(TEXT("square is landscape"), Mobile::ShapeOf(FIntPoint(800, 800)), EScreenShape::Landscape);
	TestEqual(TEXT("unknown size uses the fallback"), Mobile::ShapeOf(FIntPoint::ZeroValue, EScreenShape::Portrait), EScreenShape::Portrait);

	// Screen state comparisons drive the change delegate.
	FMovecadeScreenState A;
	FMovecadeScreenState B = A;
	TestTrue(TEXT("same state"), A == B);
	B.SafeArea.Fraction.Top = 0.05f;
	TestTrue(TEXT("a safe area change is a change"), A != B);
	B = A;
	B.ViewportSize = FIntPoint(10, 20);
	TestTrue(TEXT("a resize is a change"), A != B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobileSafeAreaTest, "MovecadeKart.Mobile.SafeArea",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobileSafeAreaTest::RunTest(const FString& Parameters)
{
	// Slate's ratio is per side of the half size: 0.2 of half the height is 0.1 of the height.
	const Mobile::FMovecadeSafeArea Area = Mobile::FMovecadeSafeArea::FromSlateRatio(FMargin(0.f, 0.2f, 0.f, 0.1f));
	TestEqual(TEXT("top fraction"), Area.Fraction.Top, 0.1f);
	TestEqual(TEXT("bottom fraction"), Area.Fraction.Bottom, 0.05f);
	TestTrue(TEXT("round trip"), Area.ToSlateRatio() == FMargin(0.f, 0.2f, 0.f, 0.1f));
	const FMargin In = Area.Insets(FVector2D(1000.0, 2000.0));
	TestEqual(TEXT("top inset"), In.Top, 200.f);
	TestEqual(TEXT("bottom inset"), In.Bottom, 100.f);
	const FSlateRect Safe = Area.Inset(FSlateRect(0.f, 0.f, 1000.f, 2000.f));
	TestEqual(TEXT("safe rect top"), Safe.Top, 200.f);
	TestEqual(TEXT("safe rect bottom"), Safe.Bottom, 1900.f);
	TestTrue(TEXT("zero"), Mobile::FMovecadeSafeArea().IsZero() && !Area.IsZero());

	// iPhone 15 Pro: 59 pt island + 34 pt home indicator at 3x on 2556 px upright; 59 pt each side sideways.
	const Mobile::FMovecadeDeviceProfile* Phone = Mobile::FindDeviceProfile(TEXT("iPhone15Pro"));
	if (TestNotNull(TEXT("iphone15pro profile"), Phone))
	{
		TestEqual(TEXT("portrait pixels"), Phone->PixelsFor(EScreenShape::Portrait), FIntPoint(1179, 2556));
		TestEqual(TEXT("landscape pixels"), Phone->PixelsFor(EScreenShape::Landscape), FIntPoint(2556, 1179));
		const Mobile::FMovecadeSafeArea Up = Phone->SafeAreaFor(EScreenShape::Portrait);
		TestTrue(TEXT("upright top"), FMath::IsNearlyEqual(Up.Fraction.Top, 177.f / 2556.f));
		TestTrue(TEXT("upright bottom"), FMath::IsNearlyEqual(Up.Fraction.Bottom, 102.f / 2556.f));
		TestEqual(TEXT("upright sides"), Up.Fraction.Left + Up.Fraction.Right, 0.f);
		const Mobile::FMovecadeSafeArea Side = Phone->SafeAreaFor(EScreenShape::Landscape);
		TestTrue(TEXT("sideways left"), FMath::IsNearlyEqual(Side.Fraction.Left, 177.f / 2556.f));
		TestTrue(TEXT("sideways right"), FMath::IsNearlyEqual(Side.Fraction.Right, 177.f / 2556.f));
		TestTrue(TEXT("sideways bottom"), FMath::IsNearlyEqual(Side.Fraction.Bottom, 63.f / 1179.f));
		TestEqual(TEXT("sideways top"), Side.Fraction.Top, 0.f);
	}
	TestNull(TEXT("unknown phone"), Mobile::FindDeviceProfile(TEXT("nokia3310")));
	TestTrue(TEXT("three profiles"), Mobile::DeviceProfiles().Num() == 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobilePreviewFlagTest, "MovecadeKart.Mobile.PreviewFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMobilePreviewFlagTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no flag, no preview"), Mobile::FMobilePreview::Parse(TEXT("-game -windowed")).IsSet());

	const TOptional<Mobile::FMobilePreview> Portrait = Mobile::FMobilePreview::Parse(TEXT("-game -MobilePreview=portrait -KartShots=5"));
	if (TestTrue(TEXT("portrait"), Portrait.IsSet()))
	{
		TestTrue(TEXT("portrait shape"), Portrait->Shape == EPreviewShape::Portrait);
		TestEqual(TEXT("default phone"), FString(Portrait->Device->Name), FString(TEXT("iphone15pro")));
	}
	const TOptional<Mobile::FMobilePreview> Pixel = Mobile::FMobilePreview::Parse(TEXT("-MobilePreview=Landscape,pixel8"));
	if (TestTrue(TEXT("landscape on a pixel"), Pixel.IsSet()))
	{
		TestTrue(TEXT("landscape shape"), Pixel->Shape == EPreviewShape::Landscape);
		TestEqual(TEXT("pixel8"), FString(Pixel->Device->Name), FString(TEXT("pixel8")));
	}
	const TOptional<Mobile::FMobilePreview> Bare = Mobile::FMobilePreview::Parse(TEXT("-game -MobilePreview -nosound"));
	TestTrue(TEXT("bare flag is auto"), Bare.IsSet() && Bare->Shape == EPreviewShape::Auto);
	TestFalse(TEXT("unknown shape"), Mobile::FMobilePreview::Parse(TEXT("-MobilePreview=sideways")).IsSet());
	TestFalse(TEXT("unknown phone"), Mobile::FMobilePreview::Parse(TEXT("-MobilePreview=portrait,brick")).IsSet());

	// Auto follows the level; Any keeps whatever the window is.
	Mobile::FMobilePreview Auto;
	Auto.Shape = EPreviewShape::Auto;
	TestEqual(TEXT("auto, portrait game"), Auto.ShapeFor(EArcadeOrientation::Portrait, EScreenShape::Landscape), EScreenShape::Portrait);
	TestEqual(TEXT("auto, landscape level"), Auto.ShapeFor(EArcadeOrientation::Landscape, EScreenShape::Portrait), EScreenShape::Landscape);
	TestEqual(TEXT("auto, any keeps the shape"), Auto.ShapeFor(EArcadeOrientation::Any, EScreenShape::Portrait), EScreenShape::Portrait);
	Mobile::FMobilePreview Fixed;
	Fixed.Shape = EPreviewShape::Portrait;
	TestEqual(TEXT("fixed portrait ignores the level"), Fixed.ShapeFor(EArcadeOrientation::Landscape, EScreenShape::Landscape), EScreenShape::Portrait);

	// Window fit: the phone's aspect inside 92 % of the work area, never upscaled, even sizes.
	const FIntPoint Tall = Mobile::FitPreviewWindow(FIntPoint(1179, 2556), FIntPoint(2560, 1392));
	TestEqual(TEXT("tall window height"), Tall.Y, 1280);
	TestTrue(TEXT("tall window keeps the aspect"), FMath::IsNearlyEqual(float(Tall.X) / Tall.Y, 1179.f / 2556.f, 0.01f));
	TestTrue(TEXT("even"), Tall.X % 2 == 0 && Tall.Y % 2 == 0);
	const FIntPoint Wide = Mobile::FitPreviewWindow(FIntPoint(2556, 1179), FIntPoint(1920, 1032));
	TestTrue(TEXT("wide window fits"), Wide.X <= 1766 && Wide.Y <= 950);
	TestTrue(TEXT("wide window keeps the aspect"), FMath::IsNearlyEqual(float(Wide.X) / Wide.Y, 2556.f / 1179.f, 0.01f));
	TestEqual(TEXT("never larger than the phone"), Mobile::FitPreviewWindow(FIntPoint(750, 1334), FIntPoint(7680, 4320)), FIntPoint(750, 1334));
	return true;
}

#endif
