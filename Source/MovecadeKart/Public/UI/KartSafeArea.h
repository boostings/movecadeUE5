#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Math/Box2D.h"

struct FGeometry;

/**
 * Screen shape and safe area for the HUDs and menus: which way the screen is held, whether it is a phone, and the
 * rectangle that is clear of the notch, the home bar, the rounded corners and (on a TV) the overscan.
 *
 * FKartDisplay::Get is the one place that asks the platform. Today it reads the viewport's aspect ratio and the
 * engine's safe zone (FDisplayMetrics::TitleSafePaddingSize, which iOS fills from the safe-area insets), plus the
 * -MobilePreview=portrait|landscape window flag, which emulates an iPhone's insets on the desktop. When the mobile
 * orientation subsystem lands, Get reads orientation and insets from it instead; nothing else changes.
 *
 * FKartSafeFrame turns that into a widget's local units: every HUD and menu lays out against SafeMin / SafeMax and
 * Scale instead of the old fixed 5 % TV margin. On a 16:9 PC it reproduces the old layout exactly.
 */
namespace KartSafeArea
{
	/** The display, in viewport pixels. */
	struct MOVECADEKART_API FKartDisplay
	{
		/** Taller than wide. */
		bool bPortrait = false;
		/** A phone-sized touch screen (a mobile platform, or the -MobilePreview emulation): bigger text, insets,
		 *  touch-control zones. */
		bool bPhone = false;
		/** The unsafe border (notch, home bar, rounded corners), in viewport pixels. */
		FMargin InsetsPx;

		/** For a viewport of ViewportPx pixels. */
		static FKartDisplay Get(const FVector2D& ViewportPx);
	};

	/** -MobilePreview: none, a portrait or a landscape phone (parsed once). */
	enum class EPreview : uint8 { None, Portrait, Landscape };
	MOVECADEKART_API EPreview GetPreview();

	/** A full-viewport widget's layout frame, in its local (Slate) units. */
	struct MOVECADEKART_API FKartSafeFrame
	{
		/** The whole widget. */
		FVector2D Size = FVector2D(1920.0, 1080.0);
		/** The rectangle every element stays inside. */
		FVector2D SafeMin = FVector2D::ZeroVector;
		FVector2D SafeMax = FVector2D(1920.0, 1080.0);
		/** Local units per design unit: HUDs are drawn in 1080-tall design units (1080 across in portrait). */
		float Scale = 1.f;
		bool bPortrait = false;
		bool bPhone = false;
		/** The on-screen touch controls' corners (landscape phones), in local units: nothing important goes there.
		 *  Empty (zero size at the bottom corners) elsewhere. */
		FBox2D TouchLeft = FBox2D(ForceInit);
		FBox2D TouchRight = FBox2D(ForceInit);

		FVector2D SafeSize() const { return SafeMax - SafeMin; }
		FVector2D SafeCentre() const { return (SafeMin + SafeMax) * 0.5; }
		/** The safe area's top / bottom edge at the screen's centre line: where centred banners and prompts go. */
		double Top() const { return SafeMin.Y; }
		double Bottom() const { return SafeMax.Y; }
		/** The bottom edge for something centred spanning HalfWidth either side of the centre: lifted above the
		 *  touch zones when it would overlap them. */
		double BottomClearOfTouch(double CentreX, double HalfWidth) const;
		bool HasTouchZones() const { return TouchLeft.bIsValid || TouchRight.bIsValid; }
		/** Scale, or less so DesignWidth (design units) fits across the safe area: for wide banners and cards. */
		float FitWidth(float DesignWidth) const
		{
			return FMath::Min(Scale, static_cast<float>(SafeSize().X) / FMath::Max(DesignWidth, 1.f));
		}

		/**
		 * The frame for a HUD covering the whole viewport (Geometry: its OnPaint geometry). On a PC Scale is
		 * Size.Y / 1080 and the safe area is the 5 % TV margin, as before; on a phone the text is boosted for a
		 * hand-held screen and the margin is the device insets plus a thin pad.
		 */
		static FKartSafeFrame ForHud(const FGeometry& Geometry);
		/** The same from a local size and the viewport pixels per local unit. */
		static FKartSafeFrame Make(const FVector2D& LocalSize, float PixelsPerUnit);
		/** The same for an explicit display (tests). */
		static FKartSafeFrame Make(const FVector2D& LocalSize, float PixelsPerUnit, const FKartDisplay& Display);
	};

	/** The phone's HUD text boost over the TV design (a 6-inch screen at arm's length vs. a TV from the couch). */
	constexpr float PhoneHudBoost = 1.2f;
	/** Design units per screen width in portrait (the HUD is laid out 1080 units across). */
	constexpr float PortraitDesignWidth = 1080.f;
	/** The design height in landscape. */
	constexpr float LandscapeDesignHeight = 1080.f;
	/** The touch zones reserved for the on-screen buttons at the bottom corners of a landscape phone, in design units. */
	constexpr float TouchZoneWidth = 420.f;
	constexpr float TouchZoneHeight = 360.f;
}
