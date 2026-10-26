#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"

/**
 * Finger gestures the touch fallbacks share: one finger followed on the touch surface (positions 0..1 of the surface,
 * y down). Pure trackers: the schemes decide what a swipe, a stick or a crank means in their game.
 */
namespace MovecadeKart::Input
{
	/** The first finger on the surface, from down to up. */
	struct MOVECADEKART_API FTouchTrack
	{
		bool bActive = false;
		int32 Pointer = INDEX_NONE;
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D Last = FVector2D::ZeroVector;
		FVector2D Previous = FVector2D::ZeroVector;
		double StartTime = 0.0;
		double LastTime = 0.0;
		double PreviousTime = 0.0;
		/** Surface heights per second, smoothed over the last moves (y down). */
		FVector2D Velocity = FVector2D::ZeroVector;
		/** The fastest upward speed seen (surface heights per second). */
		double PeakUpSpeed = 0.0;

		/** Feeds one touch; true if it was this track's finger (a second finger is ignored). */
		bool Feed(const FDeviceTouch& Touch);
		FVector2D Delta() const { return Last - Start; }
		double Duration() const { return LastTime - StartTime; }
		/** A short touch that barely moved. */
		bool IsTap(double MaxSeconds = 0.3, double MaxTravel = 0.04) const { return Duration() <= MaxSeconds && Delta().Size() <= MaxTravel; }
	};

	/**
	 * Fingers going round in circles as a crank: revolutions a second, smoothed. The angle is measured about the
	 * circle's running centre (the mean of the recent points), so any size of circle anywhere on the surface counts.
	 */
	struct MOVECADEKART_API FTouchCrank
	{
		double RevPerSec = 0.0;
		double SmoothingSeconds = 0.2;

		void Feed(const FVector2D& Position, double Time);
		/** No finger: the crank runs down. */
		void Coast(double Time);
		void Reset() { *this = FTouchCrank(); }

	private:
		FVector2D Centre = FVector2D(0.5, 0.5);
		double Angle = 0.0;
		double LastTime = 0.0;
		bool bHasAngle = false;
	};
}
