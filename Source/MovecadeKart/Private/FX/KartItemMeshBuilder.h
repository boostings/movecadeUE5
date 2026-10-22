#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/UObjectGlobals.h"

/** The geometry kit behind FX/KartItemMeshes: lathes, tubes, boxes, facets and beams, built into a transient mesh. */
namespace MovecadeKart::Fx::Detail
{
	/** Collects triangles per material slot, then builds a UStaticMesh from one FMeshDescription. */
	struct FItemMeshBuilder
	{
		struct FVert
		{
			FVector3f P;
			FVector3f N;
			FVector2f UV;
		};
		struct FSection
		{
			FName Slot;
			TArray<FVert> Verts; // three per triangle
		};
		TArray<FSection> Sections;

		int32 Section(FName Slot)
		{
			for (int32 i = 0; i < Sections.Num(); ++i)
			{
				if (Sections[i].Slot == Slot)
				{
					return i;
				}
			}
			Sections.Add({ Slot, {} });
			return Sections.Num() - 1;
		}

		/** Adds a triangle; the winding is chosen so it faces along the average of its normals. */
		void Tri(int32 S, const FVert& A, const FVert& B, const FVert& C)
		{
			const FVector3f Face = FVector3f::CrossProduct(B.P - A.P, C.P - A.P);
			const FVector3f Want = A.N + B.N + C.N;
			// UE's front faces wind the other way from the right-hand rule.
			if (FVector3f::DotProduct(Face, Want) > 0.f)
			{
				Sections[S].Verts.Append({ A, C, B });
			}
			else
			{
				Sections[S].Verts.Append({ A, B, C });
			}
		}

		void Quad(int32 S, const FVert& A, const FVert& B, const FVert& C, const FVert& D)
		{
			Tri(S, A, B, C);
			Tri(S, A, C, D);
		}

		/**
		 * A surface of revolution about Axis (unit), through Origin. Profile is (radius, height) from one pole to the
		 * other; Segments around. Normals are smooth along the profile.
		 */
		void Lathe(FName Slot, TConstArrayView<FVector2D> Profile, int32 Segments, const FVector& Origin = FVector::ZeroVector, const FVector& Axis = FVector::UpVector)
		{
			const int32 S = Section(Slot);
			const FVector Up = Axis.GetSafeNormal();
			const FVector Ref = FMath::Abs(Up.Z) > 0.9 ? FVector::ForwardVector : FVector::UpVector;
			const FVector U = FVector::CrossProduct(Up, Ref).GetSafeNormal();
			const FVector V = FVector::CrossProduct(Up, U);
			const int32 N = Profile.Num();
			TArray<FVector2D> Normals2D;
			Normals2D.SetNum(N);
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D T = Profile[FMath::Min(i + 1, N - 1)] - Profile[FMath::Max(i - 1, 0)];
				Normals2D[i] = FVector2D(T.Y, -T.X).GetSafeNormal();
			}
			double Length = 0.0;
			TArray<double> Along;
			Along.Add(0.0);
			for (int32 i = 1; i < N; ++i)
			{
				Length += FVector2D::Distance(Profile[i], Profile[i - 1]);
				Along.Add(Length);
			}
			auto At = [&](int32 Ring, int32 Seg) -> FVert
			{
				const double A = 2.0 * PI * Seg / Segments;
				const FVector Radial = U * FMath::Cos(A) + V * FMath::Sin(A);
				const FVector P = Origin + Radial * Profile[Ring].X + Up * Profile[Ring].Y;
				const FVector Nrm = (Radial * Normals2D[Ring].X + Up * Normals2D[Ring].Y).GetSafeNormal();
				return { FVector3f(P), FVector3f(Nrm), FVector2f(static_cast<float>(Seg) / Segments, static_cast<float>(Along[Ring] / FMath::Max(Length, 1e-3))) };
			};
			for (int32 Ring = 0; Ring + 1 < N; ++Ring)
			{
				for (int32 Seg = 0; Seg < Segments; ++Seg)
				{
					const FVert A = At(Ring, Seg), B = At(Ring, Seg + 1), C = At(Ring + 1, Seg + 1), D = At(Ring + 1, Seg);
					if (Profile[Ring].X < 1e-3)
					{
						Tri(S, A, C, D);
					}
					else if (Profile[Ring + 1].X < 1e-3)
					{
						Tri(S, A, B, C);
					}
					else
					{
						Quad(S, A, B, C, D);
					}
				}
			}
		}

		/** A tube along Points with a radius per point; ends closed with a pole. */
		void Tube(FName Slot, const TArray<FVector>& Points, const TArray<double>& Radii, int32 Segments)
		{
			const int32 S = Section(Slot);
			const int32 N = Points.Num();
			TArray<FVector> Frames, Sides, Ups;
			for (int32 i = 0; i < N; ++i)
			{
				const FVector T = (Points[FMath::Min(i + 1, N - 1)] - Points[FMath::Max(i - 1, 0)]).GetSafeNormal();
				FVector Side = FVector::CrossProduct(T, FVector::UpVector);
				if (Side.SizeSquared() < 1e-4)
				{
					Side = FVector::CrossProduct(T, FVector::ForwardVector);
				}
				Side.Normalize();
				Frames.Add(T);
				Sides.Add(Side);
				Ups.Add(FVector::CrossProduct(Side, T));
			}
			auto At = [&](int32 i, int32 Seg) -> FVert
			{
				const double A = 2.0 * PI * Seg / Segments;
				const FVector Radial = Sides[i] * FMath::Cos(A) + Ups[i] * FMath::Sin(A);
				// Taper-aware normal: tilt by the radius slope along the tube.
				const double Slope = (Radii[FMath::Min(i + 1, N - 1)] - Radii[FMath::Max(i - 1, 0)]) /
					FMath::Max(1e-3, FVector::Distance(Points[FMath::Min(i + 1, N - 1)], Points[FMath::Max(i - 1, 0)]));
				const FVector Nrm = (Radial - Frames[i] * Slope).GetSafeNormal();
				return { FVector3f(Points[i] + Radial * Radii[i]), FVector3f(Nrm), FVector2f(static_cast<float>(Seg) / Segments, static_cast<float>(i) / (N - 1)) };
			};
			for (int32 i = 0; i + 1 < N; ++i)
			{
				for (int32 Seg = 0; Seg < Segments; ++Seg)
				{
					Quad(S, At(i, Seg), At(i, Seg + 1), At(i + 1, Seg + 1), At(i + 1, Seg));
				}
			}
			// Caps.
			for (int32 End = 0; End < 2; ++End)
			{
				const int32 i = End == 0 ? 0 : N - 1;
				if (Radii[i] < 0.05)
				{
					continue;
				}
				const FVector Dir = Frames[i] * (End == 0 ? -1.0 : 1.0);
				const FVert Pole = { FVector3f(Points[i]), FVector3f(Dir), FVector2f(0.5f, End) };
				for (int32 Seg = 0; Seg < Segments; ++Seg)
				{
					FVert A = At(i, Seg), B = At(i, Seg + 1);
					A.N = B.N = FVector3f(Dir);
					Tri(S, Pole, A, B);
				}
			}
		}

		/** A flat-shaded box centred on C with half extents H along the axes X, Y, Z (orthonormal). */
		void Box(FName Slot, const FVector& C, const FVector& H, const FVector& X = FVector::ForwardVector, const FVector& Y = FVector::RightVector, const FVector& Z = FVector::UpVector)
		{
			const int32 S = Section(Slot);
			const FVector Axes[3] = { X.GetSafeNormal(), Y.GetSafeNormal(), Z.GetSafeNormal() };
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				for (int32 Sign = -1; Sign <= 1; Sign += 2)
				{
					const int32 B = (Axis + 1) % 3, D = (Axis + 2) % 3;
					const FVector Nrm = Axes[Axis] * Sign;
					FVert V[4];
					const double Us[4] = { -1, 1, 1, -1 }, Vs[4] = { -1, -1, 1, 1 };
					for (int32 k = 0; k < 4; ++k)
					{
						const FVector P = C + Nrm * H[Axis] + Axes[B] * (Us[k] * H[B]) + Axes[D] * (Vs[k] * H[D]);
						V[k] = { FVector3f(P), FVector3f(Nrm), FVector2f(0.5f + 0.5f * Us[k], 0.5f + 0.5f * Vs[k]) };
					}
					Quad(S, V[0], V[1], V[2], V[3]);
				}
			}
		}

		/** A flat-shaded triangle facing away from Inside. */
		void Facet(FName Slot, const FVector& A, const FVector& B, const FVector& C, const FVector& Inside)
		{
			FVector N = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			if (FVector::DotProduct(N, (A + B + C) / 3.0 - Inside) < 0.0)
			{
				N = -N;
			}
			const int32 S = Section(Slot);
			const FVector3f N3(N);
			Tri(S, { FVector3f(A), N3, FVector2f(0.f, 0.f) }, { FVector3f(B), N3, FVector2f(1.f, 0.f) }, { FVector3f(C), N3, FVector2f(0.f, 1.f) });
		}

		/** A box from A to B (a beam), W wide across Side and T thick. */
		void Beam(FName Slot, const FVector& A, const FVector& B, double W, double T, const FVector& Side)
		{
			const FVector Along = (B - A).GetSafeNormal();
			FVector Across = Side - Along * FVector::DotProduct(Side, Along);
			if (Across.SizeSquared() < 1e-6)
			{
				Across = FVector::CrossProduct(Along, FVector::UpVector);
			}
			Across.Normalize();
			Box(Slot, (A + B) * 0.5, FVector((B - A).Size() * 0.5, W * 0.5, T * 0.5), Along, Across, FVector::CrossProduct(Along, Across));
		}

		UStaticMesh* Build(UObject* Outer, const TCHAR* Name)
		{
			FMeshDescription Mesh;
			FStaticMeshAttributes Attributes(Mesh);
			Attributes.Register();
			TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
			TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
			TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
			TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
			TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
			UVs.SetNumChannels(1);

			// A unique name: NewObject with a name already used on this outer would reconstruct that (live) mesh in place.
			UStaticMesh* Result = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), Name), RF_Transient);
			for (const FSection& Sec : Sections)
			{
				const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
				SlotNames[Group] = Sec.Slot;
				// The imported slot name only exists in editor builds.
				FStaticMaterial Material(nullptr, Sec.Slot);
				// Runtime-built meshes get no texture streaming data from a cook; a default density keeps the renderer happy.
				Material.UVChannelData = FMeshUVChannelInfo(1.f);
#if WITH_EDITORONLY_DATA
				Material.ImportedMaterialSlotName = Sec.Slot;
#endif
				Result->GetStaticMaterials().Add(Material);
				for (int32 t = 0; t + 2 < Sec.Verts.Num(); t += 3)
				{
					TArray<FVertexInstanceID, TInlineAllocator<3>> Corners;
					for (int32 k = 0; k < 3; ++k)
					{
						const FVert& V = Sec.Verts[t + k];
						const FVertexID Vertex = Mesh.CreateVertex();
						Positions[Vertex] = V.P;
						const FVertexInstanceID Instance = Mesh.CreateVertexInstance(Vertex);
						Normals[Instance] = V.N;
						FVector3f Tan = FVector3f::CrossProduct(FVector3f::UpVector, V.N);
						if (Tan.SizeSquared() < 1e-4f)
						{
							Tan = FVector3f::ForwardVector;
						}
						Tangents[Instance] = Tan.GetSafeNormal();
						Signs[Instance] = 1.f;
						UVs.Set(Instance, 0, V.UV);
						Colors[Instance] = FVector4f(1.f, 1.f, 1.f, 1.f);
						Corners.Add(Instance);
					}
					Mesh.CreateTriangle(Group, Corners);
				}
			}
			UStaticMesh::FBuildMeshDescriptionsParams Params;
			Params.bMarkPackageDirty = false;
			Params.bBuildSimpleCollision = false;
			Params.bCommitMeshDescription = false;
			Params.bFastBuild = true;
			Result->BuildFromMeshDescriptions({ &Mesh }, Params);
			return Result;
		}
	};
}
