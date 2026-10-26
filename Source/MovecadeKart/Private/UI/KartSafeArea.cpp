#include "UI/KartSafeArea.h"

#include "Layout/Geometry.h"
#include "Mobile/MovecadeOrientationSubsystem.h"
#include "UI/KartUIStyle.h"

namespace KartSafeArea
{
	namespace
	{
		/** A phone's own margin inside its insets, in design units (no TV overscan to allow for). */
		constexpr float PhonePad = 22.f;
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
		return Display;
	}

	double FKartSafeFrame::BottomClearOfTouch(double CentreX, double HalfWidth) const
	{
		double Bottom = SafeMax.Y;
		for (const FBox2D& Zone : { TouchLeft, TouchRight })
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
		const float Side = static_cast<float>(SafeSize().X) / 2.f - CentreGap / 2.f - Gap;
		Panels.Width = FMath::Max(1.f, FMath::Min(MaxWidth, (Side - (PerSide - 1) * Gap) / PerSide));
		if (Panels.Width < MinWidth && PerSide > 1)
		{
			Panels.bStacked = true;
			Panels.Width = FMath::Max(1.f, FMath::Min(MaxWidth, Side));
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
		if (!F.bPhone)
		{
			// The TV's action-safe margin, as every HUD used before.
			const FVector2D Margin = F.Size * KartUI::Metric::SafeFraction;
			F.SafeMin = Margin;
			F.SafeMax = F.Size - Margin;
			return F;
		}
		const float ToLocal = 1.f / FMath::Max(PixelsPerUnit, UE_KINDA_SMALL_NUMBER);
		const FMargin& In = Display.InsetsPx;
		const double Pad = PhonePad * F.Scale;
		F.SafeMin = FVector2D(In.Left * ToLocal + Pad, In.Top * ToLocal + Pad);
		F.SafeMax = F.Size - FVector2D(In.Right * ToLocal + Pad, In.Bottom * ToLocal + Pad);
		if (!F.bPortrait)
		{
			const FVector2D Zone(TouchZoneWidth * F.Scale, TouchZoneHeight * F.Scale);
			F.TouchLeft = FBox2D(FVector2D(0.0, F.Size.Y - Zone.Y), FVector2D(In.Left * ToLocal + Zone.X, F.Size.Y));
			F.TouchRight = FBox2D(FVector2D(F.Size.X - In.Right * ToLocal - Zone.X, F.Size.Y - Zone.Y), F.Size);
		}
		return F;
	}
}
