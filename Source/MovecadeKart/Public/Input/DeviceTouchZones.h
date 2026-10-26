#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Layout/Margin.h"
#include "Math/Box2D.h"

/**
 * Where the on-device touch controls sit on screen: the one geometry SDeviceTouchControls draws and hit-tests with,
 * public so the HUDs and menus (UI/KartSafeArea) can keep clear of the same rectangles instead of copying the sizes.
 * Pure: a scheme's FDeviceLayout, a widget size and the safe-area insets in, rectangles out (all in the widget's
 * local units). UDeviceControlsSubsystem::GetTouchZones answers it for the running scheme.
 */
namespace MovecadeKart::Input
{
	/** What a touch on the overlay lands on. */
	enum class ETouchTarget : uint8
	{
		None,
		Button,
		Surface,
		Pause,
		Settings,
		Calibrate,
		SensitivityDown,
		SensitivityUp,
		/** Kart: tilt or on-screen steering (the in-game settings row). */
		Steering,
	};

	struct FTouchButtonZone
	{
		/** Index into FDeviceLayout::Buttons. */
		int32 Index = INDEX_NONE;
		EButtonAnchor Anchor = EButtonAnchor::BottomRight;
		FVector2D Centre = FVector2D::ZeroVector;
		float Radius = 0.f;

		/** Fingers are wide: a button answers this far outside its circle (as a share of the radius). */
		static constexpr float HitSlop = 1.15f;
		float HitRadius() const { return Radius * HitSlop; }
		/** The square around the button's hit circle, grown by Pad on every side. */
		FBox2D HitBounds(double Pad = 0.0) const
		{
			const double Reach = HitRadius() + Pad;
			return FBox2D(Centre - FVector2D(Reach, Reach), Centre + FVector2D(Reach, Reach));
		}
	};

	struct FTouchChipZone
	{
		ETouchTarget Target = ETouchTarget::None;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;

		FBox2D Bounds() const { return FBox2D(Pos, Pos + Size); }
		bool Contains(const FVector2D& Point) const
		{
			return Point.X >= Pos.X && Point.Y >= Pos.Y && Point.X <= Pos.X + Size.X && Point.Y <= Pos.Y + Size.Y;
		}
	};

	/** Every on-screen control of a scheme for one widget size. */
	struct MOVECADEKART_API FDeviceTouchZones
	{
		/** The scheme's buttons, in FDeviceLayout::Buttons order. */
		TArray<FTouchButtonZone, TInlineAllocator<8>> Buttons;
		/** The pause and settings chips, then (while the settings row is open) its chips. */
		TArray<FTouchChipZone, TInlineAllocator<8>> Chips;
		/** The chip height (every chip is this tall). */
		float ChipHeight = 0.f;

		/** The settings row's options beyond calibrate and sensitivity. */
		struct FRowOptions
		{
			bool bSettingsOpen = false;
			/** The scheme offers a steering choice (kart: tilt or on-screen buttons). */
			bool bSteering = false;
		};

		/**
		 * The zones for Layout on a widget of Size whose unsafe border (notch, home bar) is Insets, in the same units.
		 * Every target is at least MinTargetShare of the safe area's short side.
		 */
		static FDeviceTouchZones Compute(const FDeviceLayout& Layout, const FVector2D& Size, const FMargin& Insets, const FRowOptions& Row);
		static FDeviceTouchZones Compute(const FDeviceLayout& Layout, const FVector2D& Size, const FMargin& Insets)
		{
			return Compute(Layout, Size, Insets, FRowOptions());
		}

		/** The pause and settings chips together (always the first two chips). */
		FBox2D PauseAndSettings() const;
		/** The chip under Point, or null. */
		const FTouchChipZone* ChipAt(const FVector2D& Point) const;
		/** The button whose hit circle holds Point, or INDEX_NONE. */
		int32 ButtonAt(const FVector2D& Point) const;

		/** The smallest target, as a share of the safe area's short side: 44 pt on a 320 pt screen. */
		static constexpr float MinTargetShare = 44.f / 320.f;
	};
}
