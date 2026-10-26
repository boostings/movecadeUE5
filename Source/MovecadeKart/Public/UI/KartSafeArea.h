#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Math/Box2D.h"

struct FGeometry;
namespace MovecadeKart::Input { struct FDeviceLayout; }

/**
 * Screen shape and safe area for the HUDs and menus: which way the screen is held, whether it is a phone, and the
 * rectangle that is clear of the notch, the home bar, the rounded corners and (on a TV) the overscan.
 *
 * FKartDisplay::Get is the one place that asks: the shape is the viewport's, the safe area comes from
 * UMovecadeOrientationSubsystem (the device's insets, or the -MobilePreview phone's on the desktop), and a phone is a
 * mobile platform or that preview.
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
		/** A phone-sized touch screen (a mobile platform, or the -MobilePreview phone): bigger text, insets,
		 *  touch-control zones. */
		bool bPhone = false;
		/** The unsafe border (notch, home bar, rounded corners), in viewport pixels. */
		FMargin InsetsPx;
		/** The on-device controls showing over the game (UDeviceControlsSubsystem's scheme), or null. */
		const MovecadeKart::Input::FDeviceLayout* Controls = nullptr;

		/** For a viewport of ViewportPx pixels. */
		static FKartDisplay Get(const FVector2D& ViewportPx);
	};

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
		/**
		 * Where the on-device controls sit (UDeviceControlsSubsystem's touch overlay), in local units: nothing
		 * important goes there. TouchLeft / TouchRight bound the thumb buttons in each bottom corner, TouchCentre a
		 * portrait game's one big button, Chips the pause and settings chips. Invalid (bIsValid false) when absent.
		 */
		FBox2D TouchLeft = FBox2D(ForceInit);
		FBox2D TouchRight = FBox2D(ForceInit);
		FBox2D TouchCentre = FBox2D(ForceInit);
		FBox2D Chips = FBox2D(ForceInit);

		FVector2D SafeSize() const { return SafeMax - SafeMin; }
		FVector2D SafeCentre() const { return (SafeMin + SafeMax) * 0.5; }
		/** The safe area's top / bottom edge at the screen's centre line: where centred banners and prompts go. */
		double Top() const { return SafeMin.Y; }
		double Bottom() const { return SafeMax.Y; }
		/** The bottom edge for something centred spanning HalfWidth either side of CentreX: lifted above the touch
		 *  buttons it would overlap. */
		double BottomClearOfTouch(double CentreX, double HalfWidth) const;
		/** The bottom edge for something at the left (or right) edge: above that corner's thumb buttons. */
		double BottomLeft() const { return TouchLeft.bIsValid ? FMath::Min(SafeMax.Y, TouchLeft.Min.Y) : SafeMax.Y; }
		double BottomRight() const { return TouchRight.bIsValid ? FMath::Min(SafeMax.Y, TouchRight.Min.Y) : SafeMax.Y; }
		bool HasTouchZones() const { return TouchLeft.bIsValid || TouchRight.bIsValid; }
		/**
		 * Where the per-player panels go on a landscape phone, whose bottom corners belong to the touch controls: along
		 * the top of the safe area, half either side of a CentreGap (local units) left for the clock or round card.
		 * Side by side when each can be at least MinWidth wide, else stacked down each side.
		 */
		struct FTopPanels
		{
			float Width = 0.f;
			float Height = 0.f;
			float Gap = 0.f;
			int32 Count = 0;
			bool bStacked = false;
			/** The block's outer edges: the safe area's, moved in past the pause and settings chips when they sit at
			 *  that edge. */
			FVector2D SafeMin = FVector2D::ZeroVector;
			FVector2D SafeMax = FVector2D::ZeroVector;

			/** Panel Index's top-left corner: the first half on the left, filling inwards; the rest on the right. */
			FVector2D Pos(int32 Index) const;
			FVector2D Size() const { return FVector2D(Width, Height); }
			/** The bottom of the panels' block. */
			double Bottom() const;
		};
		FTopPanels TopPanels(int32 Count, float MaxWidth, float MinWidth, float Height, float Gap, float CentreGap) const;

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

	/**
	 * Menu text on a phone. The menus keep their 1080-unit canvas (so every layout still fits), which puts a 1080-unit
	 * short side on ~390 pt of glass: 1 canvas unit is ~0.36 pt, and the TV design's 18-24 unit detail text would be
	 * 7-9 pt. On a phone the menu stack (and the lobby, as it builds) runs inside an FPhoneMenuTextScope, and
	 * KartUI::Font sizes the reading faces with PhoneMenuTextSize: the HUD's 1.2x boost everywhere, Body text never
	 * under PhoneMenuMinBodySize (~11 pt on a 6-inch screen), and Button-face labels capped at PhoneMenuLabelCap (or
	 * their own size when bigger) so captions keep fitting their pills. Label (small caps, key caps) gets the boost
	 * only; Display and Title are big already and stay as designed. Every mapping is monotonic, so a screen's own
	 * shrink-to-fit still shrinks. Nothing changes on a PC.
	 */
	constexpr float PhoneMenuMinBodySize = 30.f;
	constexpr float PhoneMenuLabelCap = 34.f;
	/** Size for Face at the design Size on a phone's menus (see above); pure, for the tests. Face: KartUI::EFont. */
	MOVECADEKART_API float PhoneMenuTextSize(uint8 Face, float Size);

	/** While one is alive (on the game thread), KartUI::Font gives phone menu sizes. bPhone false makes it a no-op. */
	struct MOVECADEKART_API FPhoneMenuTextScope
	{
		explicit FPhoneMenuTextScope(bool bPhone);
		~FPhoneMenuTextScope();
		FPhoneMenuTextScope(const FPhoneMenuTextScope&) = delete;
		FPhoneMenuTextScope& operator=(const FPhoneMenuTextScope&) = delete;

		static bool IsActive();

	private:
		bool bActive = false;
	};
	/** Design units per screen width in portrait (the HUD is laid out 1080 units across). */
	constexpr float PortraitDesignWidth = 1080.f;
	/** The design height in landscape. */
	constexpr float LandscapeDesignHeight = 1080.f;
}
