#pragma once

#include "CoreMinimal.h"

/**
 * Web (three.js: right-handed, Y up, metres) <-> UE (left-handed, Z up, cm). Docs/Port/03 §1:
 *   UE.X = -z*100, UE.Y = x*100, UE.Z = (y + rebase)*100, UE.Yaw = -yaw in degrees.
 * The sim's 2D vectors are (x, z) on the three.js ground plane.
 */
namespace MovecadeKart::Coords
{
	inline FVector ToUE(const FVector2D& XZ, double Y = 0.0)
	{
		return FVector(-XZ.Y * 100.0, XZ.X * 100.0, Y * 100.0);
	}

	inline FVector ToUE(const FVector& ThreeXYZ)
	{
		return FVector(-ThreeXYZ.Z * 100.0, ThreeXYZ.X * 100.0, ThreeXYZ.Y * 100.0);
	}

	/** A direction (no scaling). */
	inline FVector DirToUE(const FVector& ThreeXYZ)
	{
		return FVector(-ThreeXYZ.Z, ThreeXYZ.X, ThreeXYZ.Y);
	}

	inline FVector2D FromUE(const FVector& UE)
	{
		return FVector2D(UE.Y / 100.0, -UE.X / 100.0);
	}

	inline double YawToUE(double ThreeYaw)
	{
		return -FMath::RadiansToDegrees(ThreeYaw);
	}
}
