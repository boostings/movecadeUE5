#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeCatalog.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Layout/Margin.h"
#include "Layout/SlateRect.h"

/**
 * The pure half of the phone orientation support: the decisions UMovecadeOrientationSubsystem makes, kept free of
 * engine state so the tests can pin them (MovecadeKart.Mobile.*). HUD and UI code normally only needs
 * FMovecadeScreenState (from the subsystem) and FMovecadeSafeArea.
 */
namespace MovecadeKart::Mobile
{
	using Arcade::EArcadeOrientation;

	/** The shape of the screen right now (a desktop window is whatever it is sized to). */
	enum class EScreenShape : uint8
	{
		Landscape,
		Portrait,
	};

	MOVECADEKART_API const TCHAR* LexToString(EScreenShape Shape);

	/** Portrait when taller than wide; Fallback while the size is unknown (zero). A square counts as landscape. */
	MOVECADEKART_API EScreenShape ShapeOf(FIntPoint Size, EScreenShape Fallback = EScreenShape::Landscape);

	/** The allowed-orientation mask for FPlatformMisc::SetAllowedDeviceOrientation. */
	MOVECADEKART_API EDeviceScreenOrientation ToDeviceOrientation(EArcadeOrientation Wanted);

	/** What the level (or an override) asks for: the override when set, else the catalog's OrientationForLevel. */
	MOVECADEKART_API EArcadeOrientation ResolveWanted(const FString& Level, TOptional<EArcadeOrientation> Override);

	/**
	 * The parts of the screen not covered by a notch, rounded corners or the home indicator, as fractions of the
	 * screen per side (0 = nothing covered; Left/Right of the width, Top/Bottom of the height). Fractions, so they
	 * apply to any surface that covers the whole viewport: a HUD's local size, the viewport in pixels, a Slate
	 * geometry.
	 */
	struct MOVECADEKART_API FMovecadeSafeArea
	{
		FMargin Fraction;

		/** From Slate's safe zone ratio (FSlateApplication::GetSafeZoneRatio), which is per side of the half size. */
		static FMovecadeSafeArea FromSlateRatio(const FMargin& Ratio);
		/** Back to Slate's ratio (for FSlateApplication::SetCustomSafeZone). */
		FMargin ToSlateRatio() const;

		/** The insets in the units of Size (a viewport in pixels, or a widget's local size). */
		FMargin Insets(const FVector2D& Size) const;
		/** Rect shrunk to its safe part (Rect covering the whole viewport). */
		FSlateRect Inset(const FSlateRect& Rect) const;
		bool IsZero() const { return Fraction == FMargin(0.f); }

		bool operator==(const FMovecadeSafeArea& Other) const { return Fraction == Other.Fraction; }
		bool operator!=(const FMovecadeSafeArea& Other) const { return !(*this == Other); }
	};

	/** A phone the desktop preview imitates: its native pixels held upright, and its safe area in points. */
	struct MOVECADEKART_API FMovecadeDeviceProfile
	{
		const TCHAR* Name;
		/** Native resolution, portrait (width < height). */
		FIntPoint PortraitPixels;
		/** Pixels per point (iOS) or per dp (Android). */
		float PointScale;
		/** Safe area insets in points, held upright (Left, Top, Right, Bottom). */
		FMargin PortraitInsetPoints;
		/** Safe area insets in points, held sideways. */
		FMargin LandscapeInsetPoints;

		FIntPoint PixelsFor(EScreenShape Shape) const;
		FMovecadeSafeArea SafeAreaFor(EScreenShape Shape) const;
	};

	/** iphone15pro (the default), iphonese, pixel8. */
	MOVECADEKART_API TConstArrayView<FMovecadeDeviceProfile> DeviceProfiles();
	/** By name, any case; null if unknown. */
	MOVECADEKART_API const FMovecadeDeviceProfile* FindDeviceProfile(const FString& Name);

	/** Which way the desktop preview window is held. */
	enum class EPreviewShape : uint8
	{
		Landscape,
		Portrait,
		/** Rotates with each level's wanted orientation, as a phone with the app's rotation lock would. */
		Auto,
	};

	/**
	 * -MobilePreview=portrait|landscape|auto[,device]: sizes the desktop window to a phone's aspect and simulates its
	 * safe area, so phone layouts can be checked and screenshotted on Windows. Bare -MobilePreview is auto on the
	 * default phone. Read only outside Shipping (UMovecadeOrientationSubsystem); Parse itself is pure.
	 */
	struct MOVECADEKART_API FMobilePreview
	{
		EPreviewShape Shape = EPreviewShape::Auto;
		const FMovecadeDeviceProfile* Device = nullptr;

		/** Unset without the flag, or when its value is malformed (logged by the caller). */
		static TOptional<FMobilePreview> Parse(const TCHAR* CommandLine);

		/** The screen shape to show for a level wanting Wanted; Current is kept for Any under Auto. */
		EScreenShape ShapeFor(EArcadeOrientation Wanted, EScreenShape Current) const;
	};

	/**
	 * The window size showing DevicePixels' aspect inside WorkArea (the desktop minus taskbars and the title bar),
	 * using at most Fill of it, never larger than the device itself. Width and height are even.
	 */
	MOVECADEKART_API FIntPoint FitPreviewWindow(FIntPoint DevicePixels, FIntPoint WorkArea, float Fill = 0.92f);
}
