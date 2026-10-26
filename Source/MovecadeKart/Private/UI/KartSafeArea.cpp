#include "UI/KartSafeArea.h"

#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "Layout/Geometry.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UI/KartUIStyle.h"

namespace KartSafeArea
{
	namespace
	{
		/** The emulated phone (-MobilePreview): an iPhone 15 Pro's safe-area insets in points, on its 393 x 852 point
		 *  screen. Portrait: the Dynamic Island on top, the home bar below; landscape: the island on one side (iOS
		 *  insets both sides alike, so the island can be on either), the home bar below. */
		constexpr double PhoneShortPt = 393.0;
		constexpr double PhoneLongPt = 852.0;
		constexpr double PortraitTopPt = 59.0;
		constexpr double PortraitBottomPt = 34.0;
		constexpr double LandscapeSidePt = 59.0;
		constexpr double LandscapeBottomPt = 21.0;

		/** A phone's own margin inside its insets, in design units (no TV overscan to allow for). */
		constexpr float PhonePad = 22.f;
#if PLATFORM_IOS || PLATFORM_ANDROID
		/** The device insets are re-read at most this often (seconds): they only move on a rotation. */
		constexpr double InsetsRefreshSeconds = 0.5;

		/** The engine's safe zone, cached: FDisplayMetrics carries the monitor list, so reading it allocates. A
		 *  deliberate process-wide cache, refreshed on a timer and when the viewport changes size. */
		struct FInsetsCache
		{
			FVector2D ViewportPx = FVector2D::ZeroVector;
			double ReadAt = -1.0;
			FMargin InsetsPx;
		};

		FMargin DeviceInsets(const FVector2D& ViewportPx)
		{
			static FInsetsCache Cache;
			const double Now = FPlatformTime::Seconds();
			if (Cache.ViewportPx != ViewportPx || Now - Cache.ReadAt > InsetsRefreshSeconds)
			{
				Cache.ViewportPx = ViewportPx;
				Cache.ReadAt = Now;
				Cache.InsetsPx = FMargin();
				if (FSlateApplication::IsInitialized())
				{
					FDisplayMetrics Metrics;
					FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
					const FVector4 Pad = Metrics.TitleSafePaddingSize; // left, top, right, bottom (pixels)
					Cache.InsetsPx = FMargin(static_cast<float>(Pad.X), static_cast<float>(Pad.Y), static_cast<float>(Pad.Z), static_cast<float>(Pad.W));
				}
			}
			return Cache.InsetsPx;
		}
#endif

		FMargin PreviewInsets(bool bPortrait, const FVector2D& ViewportPx)
		{
			if (bPortrait)
			{
				const double PerPt = ViewportPx.Y / PhoneLongPt;
				return FMargin(0.f, static_cast<float>(PortraitTopPt * PerPt), 0.f, static_cast<float>(PortraitBottomPt * PerPt));
			}
			const double PerPt = ViewportPx.Y / PhoneShortPt;
			const float Side = static_cast<float>(LandscapeSidePt * PerPt);
			return FMargin(Side, 0.f, Side, static_cast<float>(LandscapeBottomPt * PerPt));
		}
	}

	EPreview GetPreview()
	{
		static const EPreview Preview = []()
		{
			FString Value;
			if (FParse::Value(FCommandLine::Get(), TEXT("MobilePreview="), Value))
			{
				return Value.Equals(TEXT("portrait"), ESearchCase::IgnoreCase) ? EPreview::Portrait : EPreview::Landscape;
			}
			return EPreview::None;
		}();
		return Preview;
	}

	FKartDisplay FKartDisplay::Get(const FVector2D& ViewportPx)
	{
		FKartDisplay Display;
		Display.bPortrait = ViewportPx.Y > ViewportPx.X;
		if (GetPreview() != EPreview::None)
		{
			Display.bPhone = true;
			Display.InsetsPx = PreviewInsets(Display.bPortrait, ViewportPx);
			return Display;
		}
#if PLATFORM_IOS || PLATFORM_ANDROID
		Display.bPhone = true;
		Display.InsetsPx = DeviceInsets(ViewportPx);
#endif
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
