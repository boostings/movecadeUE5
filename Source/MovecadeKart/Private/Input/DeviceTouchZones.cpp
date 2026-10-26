#include "Input/DeviceTouchZones.h"

namespace MovecadeKart::Input
{
	namespace
	{
		// Sizes, as shares of the safe area's short side (all at or above FDeviceTouchZones::MinTargetShare).
		constexpr float MarginShare = 0.05f;
		constexpr float LargeButtonShare = 0.25f;
		constexpr float ButtonShare = 0.19f;
		constexpr float ChipShare = 0.14f;
		/** The pause / settings column starts this far down the safe area. */
		constexpr float ChipColumnTop = 0.3f;
		/** Chip widths, in chip heights. */
		constexpr float CalibrateChipWidth = 2.6f;
		constexpr float SensitivityLabelWidth = 1.7f;
		constexpr float SteeringChipWidth = 3.2f;
		constexpr float ChipGap = 0.25f;
		/** A raised button sits this far in from the edge (of its own diameter) and this much above its neighbour. */
		constexpr float RaisedInset = 0.35f;
		constexpr float RaisedGap = 0.06f;
		/** The gap between a bottom-corner button and the one just inside it. */
		constexpr float InnerGap = 0.05f;

		static_assert(ChipShare >= FDeviceTouchZones::MinTargetShare, "chips must stay finger-sized");
		static_assert(ButtonShare >= FDeviceTouchZones::MinTargetShare, "buttons must stay finger-sized");
	}

	FDeviceTouchZones FDeviceTouchZones::Compute(const FDeviceLayout& Layout, const FVector2D& FullSize, const FMargin& Insets, const FRowOptions& Row)
	{
		FDeviceTouchZones Z;
		// Every control goes inside the notch, the rounded corners and the home indicator.
		const FVector2D Origin(Insets.Left, Insets.Top);
		const FVector2D Size(FMath::Max(1.0, FullSize.X - Insets.Left - Insets.Right), FMath::Max(1.0, FullSize.Y - Insets.Top - Insets.Bottom));
		const float Short = static_cast<float>(FMath::Min(Size.X, Size.Y));
		const float Margin = Short * MarginShare;
		const float Large = Short * LargeButtonShare;

		for (int32 i = 0; i < Layout.Buttons.Num(); ++i)
		{
			const FDeviceButtonSpec& Spec = Layout.Buttons[i];
			FTouchButtonZone& B = Z.Buttons.AddDefaulted_GetRef();
			B.Index = i;
			B.Anchor = Spec.Anchor;
			const float D = Short * (Spec.bLarge ? LargeButtonShare : ButtonShare);
			B.Radius = D * 0.5f;
			const double R = B.Radius;
			const double BottomY = Size.Y - Margin - R;
			const double RaisedY = Size.Y - Margin - Large - Short * RaisedGap - R;
			switch (Spec.Anchor)
			{
			case EButtonAnchor::BottomLeft: B.Centre = FVector2D(Margin + R, BottomY); break;
			case EButtonAnchor::LeftRaised: B.Centre = FVector2D(Margin + R + D * RaisedInset, RaisedY); break;
			case EButtonAnchor::RightRaised: B.Centre = FVector2D(Size.X - Margin - R - D * RaisedInset, RaisedY); break;
			case EButtonAnchor::BottomCentre: B.Centre = FVector2D(Size.X * 0.5, BottomY); break;
			case EButtonAnchor::BottomLeftInner: B.Centre = FVector2D(Margin + Large + Short * InnerGap + R, BottomY); break;
			case EButtonAnchor::BottomRightInner: B.Centre = FVector2D(Size.X - Margin - Large - Short * InnerGap - R, BottomY); break;
			default: B.Centre = FVector2D(Size.X - Margin - R, BottomY); break;
			}
			B.Centre += Origin;
		}

		// Pause and settings stay clear of the HUD's corners and of the thumbs' buttons: in portrait a column at the
		// right edge a little above the middle, in landscape (thumbs in both bottom corners) a pair at the top centre.
		const float H = Short * ChipShare;
		Z.ChipHeight = H;
		const FVector2D Chip(H, H);
		const bool bTopPair = Layout.Chips == EChipPlacement::Auto && Size.X > Size.Y;
		if (bTopPair)
		{
			const double X = Origin.X + Size.X * 0.5 - H * (1.f + ChipGap * 0.5f);
			const double Y = Origin.Y + Margin * 0.5f;
			Z.Chips.Add({ ETouchTarget::Pause, FVector2D(X, Y), Chip });
			Z.Chips.Add({ ETouchTarget::Settings, FVector2D(X + H * (1.f + ChipGap), Y), Chip });
		}
		else
		{
			const double X = Layout.Chips == EChipPlacement::LeftEdge ? Origin.X + Margin : Origin.X + Size.X - Margin - H;
			const double Y = Origin.Y + Size.Y * ChipColumnTop;
			Z.Chips.Add({ ETouchTarget::Pause, FVector2D(X, Y), Chip });
			Z.Chips.Add({ ETouchTarget::Settings, FVector2D(X, Y + H * (1.f + ChipGap)), Chip });
		}
		if (!Row.bSettingsOpen)
		{
			return Z;
		}

		// The settings row: calibrate, the sensitivity - value +, and the kart's steering choice. Under the chip pair
		// in landscape (mid-height it would run into the raised thumb buttons), else mid-screen. On a screen too
		// narrow for one line the steering chip goes on a second line under it.
		const double RowWidth = H * (CalibrateChipWidth + ChipGap + 1.f + SensitivityLabelWidth + 1.f);
		const double SteerWidth = H * SteeringChipWidth;
		const bool bOneLine = !Row.bSteering || RowWidth + H * ChipGap + SteerWidth <= Size.X - 2.0 * Margin;
		const double Total = Row.bSteering && bOneLine ? RowWidth + H * ChipGap + SteerWidth : RowWidth;
		double RowX = Origin.X + (Size.X - Total) * 0.5;
		const double RowY = bTopPair ? Z.Chips[0].Pos.Y + H * (1.f + ChipGap) : Origin.Y + Size.Y * 0.5 - H * 0.5;
		Z.Chips.Add({ ETouchTarget::Calibrate, FVector2D(RowX, RowY), FVector2D(H * CalibrateChipWidth, H) });
		RowX += H * (CalibrateChipWidth + ChipGap);
		Z.Chips.Add({ ETouchTarget::SensitivityDown, FVector2D(RowX, RowY), Chip });
		RowX += H * (1.f + SensitivityLabelWidth);
		Z.Chips.Add({ ETouchTarget::SensitivityUp, FVector2D(RowX, RowY), Chip });
		RowX += H * (1.f + ChipGap);
		if (Row.bSteering)
		{
			const FVector2D SteerPos = bOneLine ? FVector2D(RowX, RowY) : FVector2D(Origin.X + (Size.X - SteerWidth) * 0.5, RowY + H * (1.f + ChipGap));
			Z.Chips.Add({ ETouchTarget::Steering, SteerPos, FVector2D(SteerWidth, H) });
		}
		return Z;
	}

	FBox2D FDeviceTouchZones::PauseAndSettings() const
	{
		FBox2D Box(ForceInit);
		for (const FTouchChipZone& C : Chips)
		{
			if (C.Target == ETouchTarget::Pause || C.Target == ETouchTarget::Settings)
			{
				Box += C.Bounds();
			}
		}
		return Box;
	}

	const FTouchChipZone* FDeviceTouchZones::ChipAt(const FVector2D& Point) const
	{
		return Chips.FindByPredicate([&Point](const FTouchChipZone& C) { return C.Contains(Point); });
	}

	int32 FDeviceTouchZones::ButtonAt(const FVector2D& Point) const
	{
		for (const FTouchButtonZone& B : Buttons)
		{
			if (FVector2D::Distance(Point, B.Centre) <= B.HitRadius())
			{
				return B.Index;
			}
		}
		return INDEX_NONE;
	}
}
