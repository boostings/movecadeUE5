#pragma once

#include "CoreMinimal.h"

/**
 * Small maths helpers shared by the kart sim and the item world. Each mirrors the three.js call the web build
 * made, operation for operation, so ported formulas stay bit-exact.
 */
namespace MovecadeKart::SimMath
{
	/** three.js MathUtils.damp: frame-rate independent exponential approach of X toward Y. */
	inline double Damp(double X, double Y, double Lambda, double Dt)
	{
		return X + (Y - X) * (1.0 - FMath::Exp(-Lambda * Dt));
	}

	/** The ground-plane direction a three.js object with rotation.y = Yaw faces: yaw 0 faces -Z. */
	inline FVector2D Forward(double Yaw)
	{
		return FVector2D(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
	}

	/** three.js Vector3.normalize(): a zero vector stays zero (FVector2D::GetSafeNormal uses a tolerance). */
	inline FVector2D NormalizeOrZero(const FVector2D& V)
	{
		const double Length = V.Size();
		return Length > 0.0 ? V / Length : FVector2D::ZeroVector;
	}

	/** gsap "power2.out" easing over [0, 1]. */
	inline double PowerTwoOut(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return 1.0 - (1.0 - X) * (1.0 - X);
	}
}
