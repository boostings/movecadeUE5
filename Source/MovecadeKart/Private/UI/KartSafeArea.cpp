#include "UI/KartSafeArea.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceControlsSubsystem.h"
#include "Input/DeviceTouchZones.h"
#include "Layout/Geometry.h"
#include "Mobile/MovecadeOrientationSubsystem.h"
#include "UI/KartUIStyle.h"

namespace KartSafeArea
{
	namespace
	{
		/** A phone's own margin inside its insets, in design units (no TV overscan to allow for). */
		constexpr float PhonePad = 22.f;
		/** Room kept between a HUD element and a touch control, in design units. */
		constexpr float TouchClearance = 14.f;

		/** The running scheme's layout, when the device controls are up in the game viewport's world. */
		const MovecadeKart::Input::FDeviceLayout* ActiveControls()
		{
			const UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport : nullptr;
			const UDeviceControlsSubsystem* Controls = Viewport ? UDeviceControlsSubsystem::Get(Viewport->GetWorld()) : nullptr;
			const MovecadeKart::Input::IDeviceControlScheme* Scheme = Controls && Controls->IsActive() ? Controls->GetScheme() : nullptr;
			return Scheme ? &Scheme->GetLayout() : nullptr;
		}
	}

	/**
	 * The device controls' buttons and chips as zones of F (InsetsLocal: the notch and home bar, local units), grown
	 * by the clearance. The rectangles are Input's own (FDeviceTouchZones, what the touch overlay draws and hits).
	 */
	static void AddTouchZones(FKartSafeFrame& F, const MovecadeKart::Input::FDeviceLayout& Layout, const FMargin& InsetsLocal)
	{
		using namespace MovecadeKart::Input;
		const FDeviceTouchZones Zones = FDeviceTouchZones::Compute(Layout, F.Size, InsetsLocal);
		const double Clear = TouchClearance * F.Scale;
		for (const FTouchButtonZone& Button : Zones.Buttons)
		{
			const FBox2D Box = Button.HitBounds(Clear);
			FBox2D& Zone = Button.Anchor == EButtonAnchor::BottomCentre ? F.TouchCentre : (Button.Centre.X < F.Size.X * 0.5 ? F.TouchLeft : F.TouchRight);
			Zone = Zone.bIsValid ? Zone + Box : Box;
		}
		const FBox2D Chips = Zones.PauseAndSettings();
		if (Chips.bIsValid)
		{
			F.Chips = Chips.ExpandBy(Clear);
		}
	}

	FKartDisplay FKartDisplay::Get(const FVector2D& ViewportPx)
	{
		FKartDisplay Display;
		Display.bPortrait = ViewportPx.Y > ViewportPx.X;
#if PLATFORM_IOS || PLATFORM_ANDROID
		Display.bPhone = true;
#endif
		// The orientation subsystem owns the screen's shape and safe area (the device's, or the -MobilePreview phone's).
		if (const UMovecadeOrientationSubsystem* Screen = UMovecadeOrientationSubsystem::Get())
		{
			const FMovecadeScreenState& State = Screen->GetState();
			Display.bPhone |= State.bPreview;
			Display.InsetsPx = State.SafeInsets(ViewportPx);
		}
		Display.Controls = ActiveControls();
		return Display;
	}

	double FKartSafeFrame::BottomClearOfTouch(double CentreX, double HalfWidth) const
	{
		double Bottom = SafeMax.Y;
		for (const FBox2D& Zone : { TouchLeft, TouchRight, TouchCentre })
		{
			if (Zone.bIsValid && CentreX - HalfWidth < Zone.Max.X && CentreX + HalfWidth > Zone.Min.X)
			{
				Bottom = FMath::Min(Bottom, Zone.Min.Y);
			}
		}
		return Bottom;
	}

	FKartSafeFrame::FTopPanels FKartSafeFrame::TopPanels(int32 Count, float MaxWidth, float MinWidth, float Height, float Gap, float CentreGap) const
	{
		FTopPanels Panels;
		Panels.Count = Count;
		Panels.Height = Height;
		Panels.Gap = Gap;
		Panels.SafeMin = SafeMin;
		Panels.SafeMax = SafeMax;
		const int32 PerSide = FMath::Max(1, (Count + 1) / 2);
		const double Mid = Size.X * 0.5;
		auto Layout = [&]()
		{
			// The narrower side decides the width, so both halves match.
			const double Side = FMath::Min(Mid - Panels.SafeMin.X, Panels.SafeMax.X - Mid) - CentreGap / 2.0 - Gap;
			const float SideBySide = static_cast<float>((Side - (PerSide - 1) * Gap) / PerSide);
			Panels.bStacked = SideBySide < MinWidth && PerSide > 1;
			Panels.Width = FMath::Max(1.f, FMath::Min(MaxWidth, Panels.bStacked ? static_cast<float>(Side) : SideBySide));
		};
		Layout();
		// The pause and settings chips in a column at a side edge: the panels start past them where they meet.
		if (Chips.bIsValid && Chips.Min.Y < Panels.Bottom() && Chips.Max.Y > SafeMin.Y)
		{
			if (Chips.Max.X < Mid - CentreGap / 2.0)
			{
				Panels.SafeMin.X = FMath::Max(Panels.SafeMin.X, Chips.Max.X);
				Layout();
			}
			else if (Chips.Min.X > Mid + CentreGap / 2.0)
			{
				Panels.SafeMax.X = FMath::Min(Panels.SafeMax.X, Chips.Min.X);
				Layout();
			}
		}
		return Panels;
	}

	FVector2D FKartSafeFrame::FTopPanels::Pos(int32 Index) const
	{
		const int32 Left = (Count + 1) / 2;
		const bool bLeft = Index < Left;
		const int32 Slot = bLeft ? Index : Index - Left;
		const int32 OnSide = bLeft ? Left : Count - Left;
		if (bStacked)
		{
			const double Y = SafeMin.Y + Slot * (Height + Gap);
			return FVector2D(bLeft ? SafeMin.X : SafeMax.X - Width, Y);
		}
		// Side by side, filling inwards from the safe edges.
		return bLeft ? FVector2D(SafeMin.X + Slot * (Width + Gap), SafeMin.Y) : FVector2D(SafeMax.X - (OnSide - Slot) * (Width + Gap) + Gap, SafeMin.Y);
	}

	double FKartSafeFrame::FTopPanels::Bottom() const
	{
		const int32 Rows = bStacked ? (Count + 1) / 2 : 1;
		return SafeMin.Y + Rows * Height + (Rows - 1) * Gap;
	}

	FKartSafeFrame FKartSafeFrame::ForHud(const FGeometry& Geometry)
	{
		const float PixelsPerUnit = Geometry.GetAccumulatedLayoutTransform().GetScale();
		return Make(FVector2D(Geometry.GetLocalSize()), PixelsPerUnit);
	}

	FKartSafeFrame FKartSafeFrame::Make(const FVector2D& LocalSize, float PixelsPerUnit)
	{
		return Make(LocalSize, PixelsPerUnit, FKartDisplay::Get(LocalSize * FMath::Max(PixelsPerUnit, UE_KINDA_SMALL_NUMBER)));
	}

	FKartSafeFrame FKartSafeFrame::Make(const FVector2D& LocalSize, float PixelsPerUnit, const FKartDisplay& Display)
	{
		FKartSafeFrame F;
		F.Size = FVector2D(FMath::Max(LocalSize.X, 1.0), FMath::Max(LocalSize.Y, 1.0));
		F.bPortrait = Display.bPortrait;
		F.bPhone = Display.bPhone;
		const float Base = static_cast<float>(F.bPortrait ? F.Size.X / PortraitDesignWidth : F.Size.Y / LandscapeDesignHeight);
		F.Scale = Base * (F.bPhone ? PhoneHudBoost : 1.f);
		const float ToLocal = 1.f / FMath::Max(PixelsPerUnit, UE_KINDA_SMALL_NUMBER);
		const FMargin& In = Display.InsetsPx;
		const FMargin InsetsLocal(In.Left * ToLocal, In.Top * ToLocal, In.Right * ToLocal, In.Bottom * ToLocal);
		if (!F.bPhone)
		{
			// The TV's action-safe margin, as every HUD used before.
			const FVector2D Margin = F.Size * KartUI::Metric::SafeFraction;
			F.SafeMin = Margin;
			F.SafeMax = F.Size - Margin;
		}
		else
		{
			const double Pad = PhonePad * F.Scale;
			F.SafeMin = FVector2D(InsetsLocal.Left + Pad, InsetsLocal.Top + Pad);
			F.SafeMax = F.Size - FVector2D(InsetsLocal.Right + Pad, InsetsLocal.Bottom + Pad);
		}
		if (Display.Controls)
		{
			AddTouchZones(F, *Display.Controls, InsetsLocal);
		}
		return F;
	}
}
