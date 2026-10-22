#pragma once

#include "CoreMinimal.h"
// Callers set the built mesh on their components.
#include "Engine/StaticMesh.h"

class UObject;

namespace MovecadeKart
{
	/**
	 * Meshes modelled in code: indexed triangles per material slot with two UV channels, built into a transient
	 * UStaticMesh from one FMeshDescription (UE cm). Lathes, ellipsoids, limbs, cylinders and boxes cover the props of
	 * every game; winding follows the vertex normals, so shapes can be described in any handedness.
	 *
	 * Material slots are numbered in the order of each slot's first Section() call. Venues build a mesh once and share
	 * it between instances.
	 */
	struct MOVECADEKART_API FMeshBuilder
	{
		struct FVert
		{
			FVector3f P;
			FVector3f N;
			FVector2f UV0;
			FVector2f UV1;
		};
		struct FSection
		{
			FName Slot;
			TArray<FVert> Verts;
			TArray<int32> Indices;
		};
		TArray<FSection> Sections;

		/** The section that draws Slot, added at the end on first use. */
		int32 Section(FName Slot);
		/** Adds a vertex to section S; returns its index. */
		int32 Vert(int32 S, const FVector& P, const FVector& N, const FVector2D& UV0, const FVector2D& UV1);
		/** A triangle wound to face along its vertices' normals (UE's front faces wind against the right-hand rule). */
		void Tri(int32 S, int32 A, int32 B, int32 C);

		/**
		 * A smooth surface of revolution about Axis through Origin. Profile is (radius, height) from pole to pole;
		 * Scale stretches the result per local axis (U, V, Axis) for ellipsoids.
		 */
		void Lathe(FName Slot, const TArray<FVector2D>& Profile, int32 Segments, const FVector& Origin, const FVector& Axis,
			const FVector& Scale, const FVector2D& UV1, double HeightFrom = 0.0, double HeightTo = 1.0);
		/** An ellipsoid with radii R about Centre (axis Z). */
		void Ellipsoid(FName Slot, const FVector& Centre, const FVector& R, int32 Rings, int32 Segments, const FVector2D& UV1);
		/** A rounded limb from A to B (radii RA, RB, hemispherical ends). */
		void Limb(FName Slot, const FVector& A, const FVector& B, double RA, double RB, int32 Segments, const FVector2D& UV1);
		/** An open or capped cylinder from A to B (smooth sides, flat caps). */
		void Cylinder(FName Slot, const FVector& A, const FVector& B, double Radius, int32 Segments, const FVector2D& UV1, bool bCaps = true);
		/**
		 * An axis-aligned box (half extents H about C), flat shaded, UVs per face, then turned by Rotation about Pivot
		 * and bent through Map (positions; normals follow Map's linear part).
		 */
		void Box(FName Slot, const FVector& C, const FVector& H, const FVector2D& UV1, const FQuat& Rotation = FQuat::Identity, const FVector& Pivot = FVector::ZeroVector,
			TFunctionRef<FVector(const FVector&)> Map = [](const FVector& P) { return P; });

		/** A transient static mesh (named Name, unique under Outer) from the sections. */
		UStaticMesh* Build(UObject* Outer, const TCHAR* Name);
	};
}
