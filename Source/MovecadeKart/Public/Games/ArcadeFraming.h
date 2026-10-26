#pragma once

#include "CoreMinimal.h"

class UCameraComponent;
class UWorld;

/**
 * Camera framing for the party games across screen shapes. The games author their shots as the web did: a vertical
 * field of view for a 16:9 screen. These helpers keep a shot's subject in frame on a phone held upright (9:19.5),
 * where a fixed vertical field of view would crop the sides away, and convert to UE's horizontal field of view.
 */
namespace ArcadeFraming
{
	/** The shape every shot was designed for. */
	constexpr double DesignAspect = 16.0 / 9.0;

	/** The game viewport's width / height (DesignAspect without a viewport). */
	MOVECADEKART_API double ViewportAspect(const UWorld* World);

	/** Taller than wide. */
	inline bool IsPortrait(double Aspect) { return Aspect < 1.0; }

	/** A vertical field of view (degrees) as the horizontal one at Aspect. */
	MOVECADEKART_API double HorizontalFov(double VerticalFovDeg, double Aspect);

	/**
	 * Gives Camera a vertical field of view of VerticalFovDeg on any screen. The engine keeps the vertical field of
	 * view (MaintainYFOV) and reads the component's FieldOfView as horizontal at the component's own AspectRatio
	 * (16:9), not the viewport's: converting with the viewport's aspect only works on a 16:9 screen, and on a
	 * phone held upright zooms in almost 4x.
	 */
	MOVECADEKART_API void SetVerticalFov(UCameraComponent& Camera, double VerticalFovDeg);

	/**
	 * The vertical field of view (degrees) that shows a subject HalfWidth either side of the view axis at Distance
	 * across Fill of the screen's width (Fill 0.9: a 5 % border each side), at Aspect.
	 */
	MOVECADEKART_API double VerticalFovForWidth(double HalfWidth, double Distance, double Aspect, double Fill = 1.0);

	/**
	 * DesignFov (vertical, for 16:9) at Aspect, widened when the screen is narrower than KeepShare of the design
	 * frame's width would allow: KeepShare 0.5 keeps the middle half of what a 16:9 screen shows across the screen.
	 * Unchanged on screens at least that wide (every landscape screen for KeepShare <= 0.75).
	 */
	MOVECADEKART_API double KeepWidth(double DesignFov, double Aspect, double KeepShare);
}
