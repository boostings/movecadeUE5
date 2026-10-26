#include "Mobile/MovecadeOrientation.h"

#include "Misc/Parse.h"

namespace MovecadeKart::Mobile
{
	namespace
	{
		/**
		 * Native pixels and the safe area insets UIKit / Android report, in points (dp). Approximate where the
		 * platform varies by OS version; close enough for layout checks, not for pixel-exact mockups.
		 */
		const FMovecadeDeviceProfile Profiles[] = {
			// iPhone 15 Pro: Dynamic Island 59 pt, home indicator 34 pt upright; 59 pt each side and 21 pt sideways.
			{ TEXT("iphone15pro"), FIntPoint(1179, 2556), 3.f, FMargin(0.f, 59.f, 0.f, 34.f), FMargin(59.f, 0.f, 59.f, 21.f) },
			// iPhone SE (3rd gen): no notch; a 20 pt status bar upright, nothing sideways.
			{ TEXT("iphonese"), FIntPoint(750, 1334), 2.f, FMargin(0.f, 20.f, 0.f, 0.f), FMargin(0.f) },
			// Pixel 8: punch-hole status bar and gesture bar; sideways the cutout sits on one side.
			{ TEXT("pixel8"), FIntPoint(1080, 2400), 2.625f, FMargin(0.f, 45.f, 0.f, 24.f), FMargin(45.f, 0.f, 0.f, 24.f) },
		};

		bool ParsePreviewShape(const FString& Text, EPreviewShape& Out)
		{
			if (Text.IsEmpty() || Text.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
			{
				Out = EPreviewShape::Auto;
			}
			else if (Text.Equals(TEXT("portrait"), ESearchCase::IgnoreCase))
			{
				Out = EPreviewShape::Portrait;
			}
			else if (Text.Equals(TEXT("landscape"), ESearchCase::IgnoreCase))
			{
				Out = EPreviewShape::Landscape;
			}
			else
			{
				return false;
			}
			return true;
		}

		int32 EvenFloor(float Value)
		{
			return FMath::Max(2, FMath::FloorToInt32(Value * 0.5f) * 2);
		}
	}

	const TCHAR* LexToString(EScreenShape Shape)
	{
		return Shape == EScreenShape::Portrait ? TEXT("portrait") : TEXT("landscape");
	}

	EScreenShape ShapeOf(FIntPoint Size, EScreenShape Fallback)
	{
		if (Size.X <= 0 || Size.Y <= 0)
		{
			return Fallback;
		}
		return Size.Y > Size.X ? EScreenShape::Portrait : EScreenShape::Landscape;
	}

	EDeviceScreenOrientation ToDeviceOrientation(EArcadeOrientation Wanted)
	{
		switch (Wanted)
		{
		// Plain Portrait, not PortraitSensor: upside-down portrait puts the camera housing at the bottom on
		// notched iPhones (iOS refuses it there anyway) and Android phones mostly lock it out too.
		case EArcadeOrientation::Portrait: return EDeviceScreenOrientation::Portrait;
		case EArcadeOrientation::Any: return EDeviceScreenOrientation::FullSensor;
		case EArcadeOrientation::Landscape:
		default: return EDeviceScreenOrientation::LandscapeSensor;
		}
	}

	EArcadeOrientation ResolveWanted(const FString& Level, TOptional<EArcadeOrientation> Override)
	{
		return Override.IsSet() ? Override.GetValue() : Arcade::OrientationForLevel(Level);
	}

	FMovecadeSafeArea FMovecadeSafeArea::FromSlateRatio(const FMargin& Ratio)
	{
		auto Side = [](float HalfRatio) { return FMath::Clamp(HalfRatio * 0.5f, 0.f, 0.5f); };
		return FMovecadeSafeArea{ FMargin(Side(Ratio.Left), Side(Ratio.Top), Side(Ratio.Right), Side(Ratio.Bottom)) };
	}

	FMargin FMovecadeSafeArea::ToSlateRatio() const
	{
		return FMargin(Fraction.Left * 2.f, Fraction.Top * 2.f, Fraction.Right * 2.f, Fraction.Bottom * 2.f);
	}

	FMargin FMovecadeSafeArea::Insets(const FVector2D& Size) const
	{
		const float W = static_cast<float>(Size.X);
		const float H = static_cast<float>(Size.Y);
		return FMargin(Fraction.Left * W, Fraction.Top * H, Fraction.Right * W, Fraction.Bottom * H);
	}

	FSlateRect FMovecadeSafeArea::Inset(const FSlateRect& Rect) const
	{
		const FMargin In = Insets(FVector2D(Rect.GetSize()));
		return FSlateRect(Rect.Left + In.Left, Rect.Top + In.Top, Rect.Right - In.Right, Rect.Bottom - In.Bottom);
	}

	FIntPoint FMovecadeDeviceProfile::PixelsFor(EScreenShape Shape) const
	{
		return Shape == EScreenShape::Portrait ? PortraitPixels : FIntPoint(PortraitPixels.Y, PortraitPixels.X);
	}

	FMovecadeSafeArea FMovecadeDeviceProfile::SafeAreaFor(EScreenShape Shape) const
	{
		const FIntPoint Pixels = PixelsFor(Shape);
		const FMargin& Points = Shape == EScreenShape::Portrait ? PortraitInsetPoints : LandscapeInsetPoints;
		const float PerWidth = PointScale / static_cast<float>(Pixels.X);
		const float PerHeight = PointScale / static_cast<float>(Pixels.Y);
		return FMovecadeSafeArea{ FMargin(Points.Left * PerWidth, Points.Top * PerHeight, Points.Right * PerWidth, Points.Bottom * PerHeight) };
	}

	TConstArrayView<FMovecadeDeviceProfile> DeviceProfiles()
	{
		return MakeArrayView(Profiles);
	}

	const FMovecadeDeviceProfile* FindDeviceProfile(const FString& Name)
	{
		for (const FMovecadeDeviceProfile& Profile : Profiles)
		{
			if (Name.Equals(Profile.Name, ESearchCase::IgnoreCase))
			{
				return &Profile;
			}
		}
		return nullptr;
	}

	TOptional<FMobilePreview> FMobilePreview::Parse(const TCHAR* CommandLine)
	{
		FString Value;
		// No stop on separators: the value itself is comma separated.
		const bool bHasValue = FParse::Value(CommandLine, TEXT("MobilePreview="), Value, false);
		if (!bHasValue && !FParse::Param(CommandLine, TEXT("MobilePreview")))
		{
			return {};
		}

		FString ShapeText = Value.TrimStartAndEnd();
		FString DeviceText;
		ShapeText.Split(TEXT(","), &ShapeText, &DeviceText);

		FMobilePreview Preview;
		Preview.Device = &Profiles[0];
		if (!ParsePreviewShape(ShapeText.TrimStartAndEnd(), Preview.Shape))
		{
			return {};
		}
		DeviceText.TrimStartAndEndInline();
		if (!DeviceText.IsEmpty())
		{
			Preview.Device = FindDeviceProfile(DeviceText);
			if (!Preview.Device)
			{
				return {};
			}
		}
		return Preview;
	}

	EScreenShape FMobilePreview::ShapeFor(EArcadeOrientation Wanted, EScreenShape Current) const
	{
		switch (Shape)
		{
		case EPreviewShape::Portrait: return EScreenShape::Portrait;
		case EPreviewShape::Landscape: return EScreenShape::Landscape;
		case EPreviewShape::Auto:
		default:
			if (Wanted == EArcadeOrientation::Portrait)
			{
				return EScreenShape::Portrait;
			}
			return Wanted == EArcadeOrientation::Landscape ? EScreenShape::Landscape : Current;
		}
	}

	FIntPoint FitPreviewWindow(FIntPoint DevicePixels, FIntPoint WorkArea, float Fill)
	{
		if (DevicePixels.X <= 0 || DevicePixels.Y <= 0 || WorkArea.X <= 0 || WorkArea.Y <= 0)
		{
			return DevicePixels;
		}
		const float Scale = FMath::Min3(Fill * WorkArea.X / DevicePixels.X, Fill * WorkArea.Y / DevicePixels.Y, 1.f);
		return FIntPoint(EvenFloor(DevicePixels.X * Scale), EvenFloor(DevicePixels.Y * Scale));
	}
}
