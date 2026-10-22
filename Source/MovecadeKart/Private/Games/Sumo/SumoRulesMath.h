#pragma once

#include "CoreMinimal.h"

/** Plane helpers shared by Sumo Bumpers' rules (SumoRules.cpp) and house bots (SumoBots.cpp). Pure and inline. */
namespace MovecadeKart::Sumo
{
	/** V's direction, or Fallback for a (near) zero vector. */
	inline FVector2D SafeNormal(const FVector2D& V, const FVector2D& Fallback = FVector2D(1.0, 0.0))
	{
		const double L = V.Size();
		return L > 1e-9 ? V / L : Fallback;
	}

	/** V clamped to the unit disc. */
	inline FVector2D ClampDisc(const FVector2D& V)
	{
		const double L = V.Size();
		return L > 1.0 ? V / L : V;
	}

	/** V turned a quarter left. */
	inline FVector2D Perp(const FVector2D& V)
	{
		return FVector2D(-V.Y, V.X);
	}
}
