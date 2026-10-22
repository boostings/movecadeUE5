#pragma once

#include "CoreMinimal.h"
#include "Games/Darts/DartsPub.h"

/** Geometry shared by DartsPub.cpp (the number ring) and DartsPubMeshes.cpp (the board's meshes). */
namespace MovecadeKart::Darts::PubGeometry
{
	/** A point in the board pivot's frame (cm): board radii across and up, Lift metres toward the camera (UE -X). */
	inline FVector BoardLocal(double X, double Y, double Lift = 0.0)
	{
		return FVector(-Lift * 100.0, X * ADartsPub::BoardRadius * 100.0, Y * ADartsPub::BoardRadius * 100.0);
	}
}
