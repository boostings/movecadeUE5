#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace MovecadeKart
{
	/**
	 * Meshes modelled in code: indexed triangles per material slot with two UV channels, built into a transient
	 * UStaticMesh from one FMeshDescription (UE cm). Lathes, ellipsoids, limbs, cylinders and boxes cover the props of
	 * every game; winding follows the vertex normals, so shapes can be described in any handedness.
	 */
	struct FMeshBuilder
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

		int32 Section(FName Slot)
		{
			for (int32 i = 0; i < Sections.Num(); ++i)
			{
				if (Sections[i].Slot == Slot)
				{
					return i;
				}
			}
			Sections.Add({ Slot, {}, {} });
			return Sections.Num() - 1;
		}

		int32 Vert(int32 S, const FVector& P, const FVector& N, const FVector2D& UV0, const FVector2D& UV1)
		{
			return Sections[S].Verts.Add({ FVector3f(P), FVector3f(N.GetSafeNormal()), FVector2f(UV0), FVector2f(UV1) });
		}

		/** A triangle wound to face along its vertices' normals (UE's front faces wind against the right-hand rule). */
		void Tri(int32 S, int32 A, int32 B, int32 C)
		{
			const TArray<FVert>& V = Sections[S].Verts;
			const FVector3f Face = FVector3f::CrossProduct(V[B].P - V[A].P, V[C].P - V[A].P);
			const FVector3f Want = V[A].N + V[B].N + V[C].N;
			if (FVector3f::DotProduct(Face, Want) > 0.f)
			{
				Sections[S].Indices.Append({ A, C, B });
			}
			else
			{
				Sections[S].Indices.Append({ A, B, C });
			}
		}

		/**
		 * A smooth surface of revolution about Axis through Origin. Profile is (radius, height) from pole to pole;
		 * Scale stretches the result per local axis (U, V, Axis) for ellipsoids.
		 */
		void Lathe(FName Slot, const TArray<FVector2D>& Profile, int32 Segments, const FVector& Origin, const FVector& Axis,
			const FVector& Scale, const FVector2D& UV1, double HeightFrom = 0.0, double HeightTo = 1.0)
		{
			const int32 S = Section(Slot);
			const FVector Up = Axis.GetSafeNormal();
			const FVector Ref = FMath::Abs(Up.Z) > 0.9 ? FVector::ForwardVector : FVector::UpVector;
			const FVector U = FVector::CrossProduct(Up, Ref).GetSafeNormal();
			const FVector V = FVector::CrossProduct(Up, U);
			const int32 N = Profile.Num();
			const int32 Base = Sections[S].Verts.Num();
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D T = Profile[FMath::Min(i + 1, N - 1)] - Profile[FMath::Max(i - 1, 0)];
				const FVector2D N2 = FVector2D(T.Y, -T.X).GetSafeNormal();
				for (int32 Seg = 0; Seg <= Segments; ++Seg)
				{
					const double A = 2.0 * PI * Seg / Segments;
					const FVector Radial = U * FMath::Cos(A) + V * FMath::Sin(A);
					const FVector Local = Radial * Profile[i].X + Up * Profile[i].Y;
					const FVector P = Origin + FVector(Local.X * Scale.X, Local.Y * Scale.Y, Local.Z * Scale.Z);
					const FVector LocalN = Radial * N2.X + Up * N2.Y;
					// Normals transform by the inverse scale.
					const FVector Nrm = FVector(LocalN.X / Scale.X, LocalN.Y / Scale.Y, LocalN.Z / Scale.Z).GetSafeNormal();
					const double H = FMath::Lerp(HeightFrom, HeightTo, static_cast<double>(i) / FMath::Max(N - 1, 1));
					Vert(S, P, Nrm, FVector2D(static_cast<double>(Seg) / Segments, H), UV1);
				}
			}
			const int32 Row = Segments + 1;
			for (int32 i = 0; i + 1 < N; ++i)
			{
				for (int32 Seg = 0; Seg < Segments; ++Seg)
				{
					const int32 A = Base + i * Row + Seg, B = A + 1, C = A + Row + 1, D = A + Row;
					if (Profile[i].X > 1e-3)
					{
						Tri(S, A, B, C);
					}
					if (Profile[i + 1].X > 1e-3)
					{
						Tri(S, A, C, D);
					}
				}
			}
		}

		/** An ellipsoid with radii R about Centre (axis Z). */
		void Ellipsoid(FName Slot, const FVector& Centre, const FVector& R, int32 Rings, int32 Segments, const FVector2D& UV1)
		{
			TArray<FVector2D> Profile;
			for (int32 i = 0; i <= Rings; ++i)
			{
				const double A = -HALF_PI + PI * i / Rings;
				Profile.Add(FVector2D(FMath::Cos(A), FMath::Sin(A)));
			}
			Lathe(Slot, Profile, Segments, Centre, FVector::UpVector, R, UV1);
		}

		/** A rounded limb from A to B (radii RA, RB, hemispherical ends). */
		void Limb(FName Slot, const FVector& A, const FVector& B, double RA, double RB, int32 Segments, const FVector2D& UV1)
		{
			const FVector Axis = B - A;
			const double L = Axis.Size();
			TArray<FVector2D> Profile;
			for (int32 i = 0; i <= 3; ++i)
			{
				const double T = -HALF_PI + HALF_PI * i / 3.0;
				Profile.Add(FVector2D(FMath::Cos(T) * RA, FMath::Sin(T) * RA));
			}
			for (int32 i = 1; i <= 3; ++i)
			{
				const double T = HALF_PI * i / 3.0;
				Profile.Add(FVector2D(FMath::Cos(T) * RB, L + FMath::Sin(T) * RB));
			}
			Lathe(Slot, Profile, Segments, A, Axis, FVector::OneVector, UV1);
		}

		/** An open or capped cylinder from A to B (smooth sides, flat caps). */
		void Cylinder(FName Slot, const FVector& A, const FVector& B, double Radius, int32 Segments, const FVector2D& UV1, bool bCaps = true)
		{
			TArray<FVector2D> Profile;
			const double L = (B - A).Size();
			if (bCaps)
			{
				Profile.Add(FVector2D(0.0, 0.0));
				Profile.Add(FVector2D(Radius, 0.0));
			}
			Profile.Add(FVector2D(Radius, 0.0));
			Profile.Add(FVector2D(Radius, L));
			if (bCaps)
			{
				Profile.Add(FVector2D(Radius, L));
				Profile.Add(FVector2D(0.0, L));
			}
			Lathe(Slot, Profile, Segments, A, B - A, FVector::OneVector, UV1);
		}

		/** An axis-aligned box (half extents H about C), flat shaded, UVs per face. */
		void Box(FName Slot, const FVector& C, const FVector& H, const FVector2D& UV1, const FQuat& Rotation = FQuat::Identity, const FVector& Pivot = FVector::ZeroVector,
			TFunctionRef<FVector(const FVector&)> Map = [](const FVector& P) { return P; })
		{
			const int32 S = Section(Slot);
			static const FVector Normals[6] = { FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector, FVector::UpVector, -FVector::UpVector };
			for (const FVector& Nrm : Normals)
			{
				// Two axes spanning the face.
				const FVector T1 = FMath::Abs(Nrm.Z) > 0.5 ? FVector::ForwardVector : FVector::UpVector;
				const FVector T2 = FVector::CrossProduct(Nrm, T1);
				FVector Corners[4];
				const double Signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
				int32 Idx[4];
				for (int32 k = 0; k < 4; ++k)
				{
					const FVector Local = Nrm * H + T1 * H * Signs[k][0] + T2 * H * Signs[k][1];
					Corners[k] = Map(Pivot + Rotation.RotateVector(C + Local - Pivot));
					const FVector N = Map(Rotation.RotateVector(Nrm)) - Map(FVector::ZeroVector);
					Idx[k] = Vert(S, Corners[k], N, FVector2D(Signs[k][0] * 0.5 + 0.5, Signs[k][1] * 0.5 + 0.5), UV1);
				}
				Tri(S, Idx[0], Idx[1], Idx[2]);
				Tri(S, Idx[0], Idx[2], Idx[3]);
			}
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
			UVs.SetNumChannels(2);

			UStaticMesh* Result = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), Name), RF_Transient);
			for (const FSection& Sec : Sections)
			{
				const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
				SlotNames[Group] = Sec.Slot;
				FStaticMaterial Material(nullptr, Sec.Slot);
				Material.UVChannelData = FMeshUVChannelInfo(1.f);
	#if WITH_EDITORONLY_DATA
				Material.ImportedMaterialSlotName = Sec.Slot;
	#endif
				Result->GetStaticMaterials().Add(Material);
				TArray<FVertexInstanceID> Instances;
				Instances.Reserve(Sec.Verts.Num());
				for (const FVert& V : Sec.Verts)
				{
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
					UVs.Set(Instance, 0, V.UV0);
					UVs.Set(Instance, 1, V.UV1);
					Colors[Instance] = FVector4f(1.f, 1.f, 1.f, 1.f);
					Instances.Add(Instance);
				}
				for (int32 t = 0; t + 2 < Sec.Indices.Num(); t += 3)
				{
					Mesh.CreateTriangle(Group, { Instances[Sec.Indices[t]], Instances[Sec.Indices[t + 1]], Instances[Sec.Indices[t + 2]] });
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
