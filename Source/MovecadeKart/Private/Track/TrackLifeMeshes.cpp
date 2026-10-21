#include "Track/TrackLifeMeshes.h"

#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
	/** Indexed triangles per material slot, two UV channels, built into a UStaticMesh from one FMeshDescription. */
	struct FLifeMeshBuilder
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

	const FName Slot0(TEXT("Life"));

	/** A tapered blade from Root along a curve: Out is the horizontal lean direction. Five rows, the last a point. */
	void Blade(FLifeMeshBuilder& B, const FVector& Root, const FVector& Out, double Height, double Width, double Lean, double Curl, double Rand,
		const FVector2D& Part = FVector2D(0.0, 0.0), double HeightFrom = 0.0, double HeightTo = 1.0)
	{
		const int32 S = B.Section(Slot0);
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Out).GetSafeNormal();
		constexpr int32 Rows = 4;
		auto Spine = [&](double T)
		{
			// Leans out with height and curls further toward the tip.
			const double Reach = Height * (FMath::Sin(Lean) * T + Curl * T * T);
			const double Rise = Height * (FMath::Cos(Lean) * T - Curl * 0.45 * T * T);
			return Root + Out * Reach + FVector::UpVector * Rise;
		};
		int32 Prev[2] = { INDEX_NONE, INDEX_NONE };
		for (int32 k = 0; k <= Rows; ++k)
		{
			const double T = static_cast<double>(k) / Rows;
			const FVector P = Spine(T);
			const FVector Tangent = (Spine(FMath::Min(T + 0.05, 1.0)) - Spine(FMath::Max(T - 0.05, 0.0))).GetSafeNormal();
			FVector Nrm = FVector::CrossProduct(Tangent, Side).GetSafeNormal();
			if (Nrm.Z < 0.0)
			{
				Nrm = -Nrm;
			}
			const double H = FMath::Lerp(HeightFrom, HeightTo, T);
			const FVector2D UV1(Part.X, Rand);
			if (k == Rows)
			{
				const int32 Tip = B.Vert(S, P, Nrm, FVector2D(0.5, H), UV1);
				B.Tri(S, Prev[0], Prev[1], Tip);
				break;
			}
			const double W = Width * 0.5 * FMath::Pow(1.0 - T, 0.85);
			const int32 L = B.Vert(S, P - Side * W, Nrm, FVector2D(0.0, H), UV1);
			const int32 R = B.Vert(S, P + Side * W, Nrm, FVector2D(1.0, H), UV1);
			if (Prev[0] != INDEX_NONE)
			{
				B.Tri(S, Prev[0], Prev[1], R);
				B.Tri(S, Prev[0], R, L);
			}
			Prev[0] = L;
			Prev[1] = R;
		}
	}

	/** three.js metres (x, y, z) -> the imported prefab's mesh axes (x, z, y) in cm. */
	FVector PrefabAxes(const FVector& Three)
	{
		return FVector(Three.X, Three.Z, Three.Y) * 100.0;
	}
}

namespace MovecadeKart::Life
{
	UStaticMesh* BuildGrassTuftMesh(UObject* Outer, int32 Seed, int32 Blades, double Radius, double Height)
	{
		FLifeMeshBuilder B;
		FRandomStream Rng(Seed);
		for (int32 i = 0; i < Blades; ++i)
		{
			// Blades fan out round the tuft (golden angle), the inner ones taller and more upright.
			const double Angle = i * 2.39996 + Rng.FRandRange(-0.4, 0.4);
			const FVector Out(FMath::Cos(Angle), FMath::Sin(Angle), 0.0);
			const double Inner = 1.0 - static_cast<double>(i) / FMath::Max(Blades - 1, 1);
			const FVector Root = Out * Radius * Rng.FRandRange(0.05, 0.55) + FVector(0.0, 0.0, -3.0);
			const double H = Height * Rng.FRandRange(0.55, 0.85) * (0.75 + 0.35 * Inner);
			Blade(B, Root, Out, H, Rng.FRandRange(3.4, 5.0), FMath::Lerp(0.62, 0.15, Inner) + Rng.FRandRange(-0.08, 0.08),
				Rng.FRandRange(0.05, 0.3), Rng.FRand());
		}
		return B.Build(Outer, TEXT("GrassTuft"));
	}

	UStaticMesh* BuildFlowerMesh(UObject* Outer, int32 Petals)
	{
		FLifeMeshBuilder B;
		const int32 S = B.Section(Slot0);
		constexpr double StemH = 20.0;
		// The stem and two leaves (blade part, so they colour like the lawn).
		Blade(B, FVector(0.0, 0.0, -2.0), FVector(1.0, 0.0, 0.0), StemH + 2.0, 1.4, 0.08, 0.02, 0.5);
		Blade(B, FVector(0.0, 0.0, -1.0), FVector(0.7, 0.7, 0.0).GetSafeNormal(), 9.0, 3.0, 0.7, 0.2, 0.2, FVector2D::ZeroVector, 0.0, 0.4);
		Blade(B, FVector(0.0, 0.0, -1.0), FVector(-0.6, -0.8, 0.0).GetSafeNormal(), 8.0, 3.0, 0.75, 0.2, 0.7, FVector2D::ZeroVector, 0.0, 0.35);
		const FVector Head(StemH * FMath::Sin(0.08), 0.0, StemH * FMath::Cos(0.08) + 0.5);
		// Petals: flat ovals round the heart, tipped a little up.
		for (int32 i = 0; i < Petals; ++i)
		{
			const double A = 2.0 * PI * i / Petals;
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.28);
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Out).GetSafeNormal();
			const FVector Nrm = FVector::CrossProduct(Out, Side).GetSafeNormal() * -1.0;
			const FVector Up = Nrm.Z < 0.0 ? -Nrm : Nrm;
			const FVector2D Part(1.0, 0.0);
			const int32 Base = B.Vert(S, Head + Out * 0.8, Up, FVector2D(0.5, 1.0), Part);
			const int32 L = B.Vert(S, Head + Out * 3.0 - Side * 1.6, Up, FVector2D(0.0, 1.0), Part);
			const int32 Tip = B.Vert(S, Head + Out * 5.6, Up, FVector2D(0.5, 1.0), Part);
			const int32 R = B.Vert(S, Head + Out * 3.0 + Side * 1.6, Up, FVector2D(1.0, 1.0), Part);
			B.Tri(S, Base, L, Tip);
			B.Tri(S, Base, Tip, R);
		}
		B.Ellipsoid(Slot0, Head + FVector(0.0, 0.0, 0.6), FVector(1.9, 1.9, 1.1), 4, 8, FVector2D(2.0, 0.0));
		// The ellipsoid's UV0.y runs pole to pole; the heart sits at the top of the flower.
		for (FLifeMeshBuilder::FVert& V : B.Sections[S].Verts)
		{
			if (V.UV1.X > 1.5)
			{
				V.UV0.Y = 1.f;
			}
		}
		return B.Build(Outer, TEXT("LawnFlower"));
	}

	UStaticMesh* BuildSpectatorMesh(UObject* Outer)
	{
		FLifeMeshBuilder B;
		const FVector2D Shirt(0.0, 0.0), Skin(1.0, 0.0), Trousers(2.0, 0.0), Hair(3.0, 0.0), Shoes(4.0, 0.0), Dark(5.0, 0.0);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const double Y = Side;
			B.Ellipsoid(Slot0, FVector(4.0, 9.0 * Y, 4.5), FVector(11.0, 5.5, 4.5), 4, 8, Shoes);
			B.Limb(Slot0, FVector(0.0, 9.0 * Y, 7.0), FVector(0.0, 8.5 * Y, 72.0), 7.0, 7.5, 8, Trousers);
			// Arms (UV1.y = side): short sleeve, forearm, hand.
			const FVector2D ArmShirt(0.0, Y), ArmSkin(1.0, Y);
			B.Limb(Slot0, FVector(0.0, 21.0 * Y, 124.0), FVector(0.0, 23.0 * Y, 108.0), 6.6, 6.0, 8, ArmShirt);
			B.Limb(Slot0, FVector(0.0, 23.0 * Y, 110.0), FVector(0.5, 24.5 * Y, 80.0), 4.8, 4.2, 8, ArmSkin);
			B.Ellipsoid(Slot0, FVector(1.0, 25.0 * Y, 74.0), FVector(4.6, 3.8, 6.0), 4, 8, ArmSkin);
			// Eyes.
			B.Ellipsoid(Slot0, FVector(14.8, 5.6 * Y, 154.0), FVector(2.0, 2.0, 2.6), 3, 6, Dark);
		}
		B.Ellipsoid(Slot0, FVector(0.0, 0.0, 76.0), FVector(12.5, 17.0, 10.0), 5, 10, Trousers);
		B.Ellipsoid(Slot0, FVector(0.0, 0.0, 101.0), FVector(13.0, 19.0, 29.0), 7, 12, Shirt);
		B.Limb(Slot0, FVector(0.0, 0.0, 124.0), FVector(0.0, 0.0, 138.0), 5.5, 5.5, 8, Skin);
		B.Ellipsoid(Slot0, FVector(1.0, 0.0, 152.0), FVector(16.0, 15.0, 17.0), 7, 12, Skin);
		B.Ellipsoid(Slot0, FVector(-1.5, 0.0, 157.0), FVector(16.4, 15.8, 14.6), 6, 12, Hair);
		return B.Build(Outer, TEXT("Spectator"));
	}

	UStaticMesh* BuildButterflyMesh(UObject* Outer)
	{
		FLifeMeshBuilder B;
		const int32 S = B.Section(Slot0);
		B.Ellipsoid(Slot0, FVector::ZeroVector, FVector(2.2, 0.45, 0.45), 3, 6, FVector2D::ZeroVector);
		// Each wing is a fan from its root on the body: fore wing to the front, hind wing behind.
		static const FVector2D Outline[] = { { 1.4, 0.4 }, { 2.9, 4.2 }, { 2.2, 6.6 }, { 0.2, 6.2 }, { -0.6, 3.6 }, { -2.4, 4.6 }, { -2.8, 2.4 }, { -1.6, 0.4 } };
		constexpr int32 Num = UE_ARRAY_COUNT(Outline);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const FVector2D UV1(0.0, Side);
			const int32 Root = B.Vert(S, FVector(0.0, 0.4 * Side, 0.0), FVector::UpVector, FVector2D(0.0, 0.5), UV1);
			int32 Prev = INDEX_NONE;
			for (int32 i = 0; i < Num; ++i)
			{
				const FVector2D O = Outline[i];
				const int32 V = B.Vert(S, FVector(O.X, O.Y * Side, 0.0), FVector::UpVector, FVector2D(O.Y / 6.6, 0.5 + O.X / 6.0), UV1);
				if (Prev != INDEX_NONE)
				{
					B.Tri(S, Root, Prev, V);
				}
				Prev = V;
			}
		}
		return B.Build(Outer, TEXT("Butterfly"));
	}

	UStaticMesh* BuildGrandstandMesh(UObject* Outer, const FGrandstandSpec& Spec)
	{
		FLifeMeshBuilder B;
		const double W = Spec.Width, D = Spec.Tiers * Spec.TierDepth;
		// Built in three.js metres like the web's box() helper, then mapped to the prefab's mesh axes.
		auto Box = [&](FName Slot, double SX, double SY, double SZ, double X, double Y, double Z, double RotX = 0.0)
		{
			const FQuat Rot(FVector::XAxisVector, RotX);
			B.Box(Slot, FVector(X, Y, Z), FVector(SX, SY, SZ) * 0.5, FVector2D::ZeroVector, Rot, FVector(X, Y, Z), [](const FVector& P) { return PrefabAxes(P); });
		};
		Box(TEXT("Base"), W + 1.0, 3.2, D + 1.0, 0.0, -1.4, -D / 2.0 + 0.5);
		for (int32 t = 0; t < Spec.Tiers; ++t)
		{
			const double Y = 0.2 + t * Spec.TierRise, Z = -t * Spec.TierDepth;
			Box(TEXT("Tiers"), W, Spec.TierRise, Spec.TierDepth, 0.0, Y + Spec.TierRise / 2.0 - 0.45, Z - Spec.TierDepth / 2.0);
			Box(t % 2 ? TEXT("SeatsA") : TEXT("SeatsB"), W - 0.4, 0.18, 0.5, 0.0, Y + 0.52, Z - 0.45);
		}
		const double Top = 0.2 + Spec.Tiers * Spec.TierRise;
		Box(TEXT("Back"), W + 1.0, Top + 1.8, 0.4, 0.0, (Top + 1.8) / 2.0 - 0.2, -D - 0.2);
		const double RoofY = Top + 3.6;
		Box(TEXT("Roof"), W + 2.0, 0.35, D + 2.4, 0.0, RoofY, -D / 2.0 + 0.7, -0.08);
		Box(TEXT("Trim"), W + 2.0, 0.5, 0.25, 0.0, RoofY - 0.3, 1.8);
		for (const double X : { -W / 2.0, 0.0, W / 2.0 })
		{
			Box(TEXT("Roof"), 0.35, RoofY + 0.2, 0.35, X, RoofY / 2.0 - 0.1, -D - 0.4);
		}
		// A rail along the front of the lowest tier.
		Box(TEXT("Trim"), W, 0.08, 0.08, 0.0, 0.65 + 1.0, -0.06);
		for (double X = -W / 2.0; X <= W / 2.0 + 1e-3; X += W / 10.0)
		{
			Box(TEXT("Trim"), 0.06, 1.0, 0.06, X, 0.65 + 0.5, -0.06);
		}
		return B.Build(Outer, TEXT("Grandstand"));
	}

	TArray<FSpectatorSpot> GrandstandSpectators(const FGrandstandSpec& Spec)
	{
		// The web's seeded generator (Park-Miller, seed 7): the same places stay empty.
		int64 Seed = 7;
		auto Rand = [&Seed]()
		{
			Seed = (Seed * 16807) % 2147483647;
			return static_cast<double>(Seed) / 2147483647.0;
		};
		TArray<FSpectatorSpot> Spots;
		for (int32 t = 0; t < Spec.Tiers; ++t)
		{
			const double Y = 0.2 + t * Spec.TierRise, Z = -t * Spec.TierDepth;
			for (double X = -Spec.Width / 2.0 + 0.7; X < Spec.Width / 2.0 - 0.5; X += 0.72)
			{
				if (Rand() < 0.22)
				{
					continue;
				}
				Rand(); // colour
				Rand(); // height
				const double Jitter = (Rand() - 0.5) * 0.15;
				// Standing on the tier behind its seat strip, facing the road (local +Z).
				FSpectatorSpot& Spot = Spots.AddDefaulted_GetRef();
				Spot.Position = PrefabAxes(FVector(X + Jitter, Y + 0.45, Z - 1.02));
				Spot.Yaw = 90.0;
				Spot.Tier = t;
			}
		}
		return Spots;
	}
}
