#include "FX/KartItemMeshes.h"

#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
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
		void Lathe(FName Slot, const TArray<FVector2D>& Profile, int32 Segments, const FVector& Origin = FVector::ZeroVector, const FVector& Axis = FVector::UpVector)
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

			UStaticMesh* Result = NewObject<UStaticMesh>(Outer, Name, RF_Transient);
			for (const FSection& Sec : Sections)
			{
				const FPolygonGroupID Group = Mesh.CreatePolygonGroup();
				SlotNames[Group] = Sec.Slot;
				Result->GetStaticMaterials().Add(FStaticMaterial(nullptr, Sec.Slot, Sec.Slot));
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

	/** A sphere-ish profile from the bottom pole to the top pole. */
	TArray<FVector2D> Arc(double Radius, double Z0, double FromDeg, double ToDeg, int32 Steps, double SquashZ = 1.0)
	{
		TArray<FVector2D> Out;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double A = FMath::DegreesToRadians(FMath::Lerp(FromDeg, ToDeg, static_cast<double>(i) / Steps));
			Out.Add(FVector2D(FMath::Max(0.0, Radius * FMath::Cos(A)), Z0 + Radius * SquashZ * FMath::Sin(A)));
		}
		return Out;
	}

	/**
	 * The thrown projectile: a "buzz disc", a flat puck with a bumper rim and eight teeth, and a low lens on top with
	 * a hub. Deliberately not a domed shell (the web's look, which belongs to a famous franchise).
	 */
	void AddShell(FItemMeshBuilder& B)
	{
		// Belly: the flat underside.
		B.Lathe(TEXT("Belly"), { {0.0, -9.0}, {20.0, -9.0}, {26.0, -7.5}, {28.0, -5.0} }, 32);
		// Rim: a rounded bumper band, with eight teeth standing out of it.
		B.Lathe(TEXT("Rim"), { {28.0, -5.0}, {31.5, -3.5}, {32.5, 0.0}, {31.5, 3.5}, {28.0, 5.0} }, 32);
		for (int32 k = 0; k < 8; ++k)
		{
			const double A = 2.0 * PI * k / 8.0;
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Along(-Out.Y, Out.X, 0.0);
			// Swept teeth, like a saw: offset along the spin direction.
			B.Box(TEXT("Rim"), Out * 34.0 + Along * 2.5, FVector(5.0, 3.5, 3.2), Out, Along, FVector::UpVector);
		}
		// Dome: a low lens with a raised hub.
		B.Lathe(TEXT("Dome"), { {28.0, 5.0}, {24.0, 7.5}, {16.0, 9.0}, {9.0, 9.5}, {8.5, 12.5}, {6.0, 14.0}, {0.0, 14.5} }, 32);
	}
}

namespace MovecadeKart::Fx
{
	UStaticMesh* BuildShellMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		AddShell(B);
		return B.Build(Outer, TEXT("SM_Item_Shell"));
	}

	UStaticMesh* BuildWingedShellMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		AddShell(B);
		// A drone: four arms out to ducted rotor rings, so it reads as flying without wings.
		for (int32 k = 0; k < 4; ++k)
		{
			const double A = PI * 0.25 + PI * 0.5 * k;
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0);
			B.Box(TEXT("Wings"), Out * 38.0 + FVector(0.0, 0.0, 8.0), FVector(12.0, 2.2, 1.8), Out, FVector(-Out.Y, Out.X, 0.0), FVector::UpVector);
			// The duct: a short open ring around the rotor.
			B.Lathe(TEXT("Wings"), { {13.0, 5.0}, {15.0, 5.5}, {15.5, 8.0}, {15.0, 10.5}, {13.0, 11.0}, {12.5, 8.0}, {13.0, 5.0} }, 20, Out * 52.0);
		}
		return B.Build(Outer, TEXT("SM_Item_WingedShell"));
	}

	UStaticMesh* BuildBananaMesh(UObject* Outer)
	{
		// The dropped hazard is a striped traffic cone on a square foot: the web's banana peel is that franchise's
		// signature hazard. Peel is the cone (orange), Tips the foot and the reflective bands.
		FItemMeshBuilder B;
		B.Box(TEXT("Tips"), FVector(0.0, 0.0, 1.5), FVector(22.0, 22.0, 1.5));
		B.Lathe(TEXT("Peel"), { {17.0, 3.0}, {14.5, 14.0}, {12.0, 24.0} }, 24);
		B.Lathe(TEXT("Tips"), { {12.0, 24.0}, {10.4, 31.0} }, 24);
		B.Lathe(TEXT("Peel"), { {10.4, 31.0}, {8.0, 41.0} }, 24);
		B.Lathe(TEXT("Tips"), { {8.0, 41.0}, {6.6, 47.0} }, 24);
		B.Lathe(TEXT("Peel"), { {6.6, 47.0}, {4.0, 56.0}, {2.0, 57.5}, {0.0, 58.0} }, 24);
		return B.Build(Outer, TEXT("SM_Item_Cone"));
	}

	FVector BombFuseTip()
	{
		return FVector(7.0, 0.0, 60.0);
	}

	UStaticMesh* BuildBombMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		B.Lathe(TEXT("Body"), Arc(40.0, 0.0, -90.0, 90.0, 18), 32);
		// Cap: a short collar on top.
		B.Lathe(TEXT("Cap"), { {0.0, 37.0}, {13.0, 37.0}, {13.5, 40.0}, {13.0, 46.0}, {11.0, 47.0}, {0.0, 47.0} }, 20);
		// Fuse: a little curved cord.
		TArray<FVector> Fuse;
		TArray<double> R;
		for (int32 i = 0; i <= 6; ++i)
		{
			const double T = i / 6.0;
			Fuse.Add(FVector(7.0 * T * T, 0.0, 46.0 + 14.0 * T));
			R.Add(2.6);
		}
		B.Tube(TEXT("Fuse"), Fuse, R, 8);
		return B.Build(Outer, TEXT("SM_Item_Bomb"));
	}

	FVector RocketExhaust()
	{
		return FVector(-158.0, 0.0, 0.0);
	}

	UStaticMesh* BuildRocketMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		// Profile along +X (radius, x): exhaust cup at the back, body, ogive nose.
		B.Lathe(TEXT("Exhaust"), { {0.0, -150.0}, {30.0, -150.0}, {40.0, -154.0}, {48.0, -146.0} }, 28, FVector::ZeroVector, FVector::ForwardVector);
		B.Lathe(TEXT("Body"), { {48.0, -146.0}, {58.0, -132.0}, {62.0, -112.0} }, 28, FVector::ZeroVector, FVector::ForwardVector);
		B.Lathe(TEXT("Band"), { {62.5, -112.0}, {63.5, -104.0}, {63.5, -88.0}, {62.5, -80.0} }, 28, FVector::ZeroVector, FVector::ForwardVector);
		B.Lathe(TEXT("Body"), { {62.0, -80.0}, {64.0, -20.0}, {63.0, 30.0}, {58.0, 72.0}, {48.0, 104.0}, {33.0, 128.0}, {16.0, 145.0}, {0.0, 152.0} }, 28, FVector::ZeroVector, FVector::ForwardVector);
		// Four fins at the back.
		for (int32 k = 0; k < 4; ++k)
		{
			const double A = k * 90.0 + 45.0;
			const FVector Out(0.0, FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)));
			const FVector Thick = FVector::CrossProduct(FVector::ForwardVector, Out);
			B.Box(TEXT("Fins"), FVector(-118.0, 0.0, 0.0) + Out * 74.0, FVector(26.0, 2.5, 20.0), FVector::ForwardVector, Thick, Out);
		}
		return B.Build(Outer, TEXT("SM_Item_Rocket"));
	}

	UStaticMesh* BuildChamferCubeMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		const int32 S = B.Section(TEXT("Core"));
		const double H = 50.0, C = 8.0, In = H - C;
		using FVert = FItemMeshBuilder::FVert;
		auto V = [](const FVector& P, const FVector& N) { return FVert{ FVector3f(P), FVector3f(N.GetSafeNormal()), FVector2f(0.5f + 0.5f * static_cast<float>(P.Y / 50.0), 0.5f - 0.5f * static_cast<float>(P.Z / 50.0)) }; };
		// Faces.
		for (int32 A = 0; A < 3; ++A)
		{
			for (int32 Sa = -1; Sa <= 1; Sa += 2)
			{
				const int32 Bx = (A + 1) % 3, Cx = (A + 2) % 3;
				FVector N = FVector::ZeroVector;
				N[A] = Sa;
				FVector P[4];
				const double Us[4] = { -1, 1, 1, -1 }, Vs[4] = { -1, -1, 1, 1 };
				for (int32 k = 0; k < 4; ++k)
				{
					P[k][A] = Sa * H;
					P[k][Bx] = Us[k] * In;
					P[k][Cx] = Vs[k] * In;
				}
				B.Quad(S, V(P[0], N), V(P[1], N), V(P[2], N), V(P[3], N));
				// Edges toward the next axis (each edge once: A with A+1).
				for (int32 Sb = -1; Sb <= 1; Sb += 2)
				{
					FVector E[4];
					for (int32 k = 0; k < 2; ++k)
					{
						const double W = k == 0 ? -In : In;
						E[k] = FVector::ZeroVector;
						E[k][A] = Sa * H;
						E[k][Bx] = Sb * In;
						E[k][Cx] = W;
						E[3 - k] = FVector::ZeroVector;
						E[3 - k][A] = Sa * In;
						E[3 - k][Bx] = Sb * H;
						E[3 - k][Cx] = W;
					}
					FVector EN = FVector::ZeroVector;
					EN[A] = Sa;
					EN[Bx] = Sb;
					B.Quad(S, V(E[0], EN), V(E[1], EN), V(E[2], EN), V(E[3], EN));
				}
			}
		}
		// Corners.
		for (int32 Sx = -1; Sx <= 1; Sx += 2)
		{
			for (int32 Sy = -1; Sy <= 1; Sy += 2)
			{
				for (int32 Sz = -1; Sz <= 1; Sz += 2)
				{
					const FVector N(Sx, Sy, Sz);
					B.Tri(S, V(FVector(Sx * H, Sy * In, Sz * In), N), V(FVector(Sx * In, Sy * H, Sz * In), N), V(FVector(Sx * In, Sy * In, Sz * H), N));
				}
			}
		}
		return B.Build(Outer, TEXT("SM_Item_BoxCore"));
	}

	namespace
	{
		/** A canister along Axis from Base: rounded body, a white band, a metal neck, cap and side valve. */
		void AddCanister(FItemMeshBuilder& B, const FVector& Base, const FVector& Axis)
		{
			const FVector Up = Axis.GetSafeNormal();
			B.Lathe(TEXT("Body"), { {0.0, 0.0}, {9.0, 0.4}, {13.0, 2.5}, {14.0, 6.0}, {14.0, 30.0}, {13.0, 36.0}, {9.5, 40.5}, {6.5, 42.0} }, 24, Base, Up);
			B.Lathe(TEXT("Band"), { {14.2, 14.0}, {14.9, 15.0}, {14.9, 22.0}, {14.2, 23.0} }, 24, Base, Up);
			B.Lathe(TEXT("Metal"), { {6.5, 41.5}, {6.8, 47.0}, {9.0, 47.5}, {9.0, 52.0}, {7.0, 53.5}, {0.0, 54.0} }, 16, Base, Up);
			// A valve knob off the side of the neck.
			const FVector Side = FVector::CrossProduct(Up, FMath::Abs(Up.Z) > 0.9 ? FVector::RightVector : FVector::UpVector).GetSafeNormal();
			const FVector Third = FVector::CrossProduct(Up, Side);
			B.Beam(TEXT("Metal"), Base + Up * 49.0, Base + Up * 49.0 + Side * 14.0, 4.0, 4.0, Third);
			B.Box(TEXT("Metal"), Base + Up * 49.0 + Side * 15.0, FVector(3.0, 6.0, 6.0), Side, Third, Up);
		}

		/** Ear-clipping triangulation of a simple polygon given counter-clockwise. */
		TArray<int32> Triangulate(const TArray<FVector2D>& Poly)
		{
			TArray<int32> Idx, Out;
			for (int32 i = 0; i < Poly.Num(); ++i)
			{
				Idx.Add(i);
			}
			auto Cross = [](const FVector2D& A, const FVector2D& B, const FVector2D& C) { return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X); };
			for (int32 Guard = 0; Idx.Num() > 3 && Guard < 1000; ++Guard)
			{
				for (int32 i = 0; i < Idx.Num(); ++i)
				{
					const int32 A = Idx[(i + Idx.Num() - 1) % Idx.Num()], Bi = Idx[i], C = Idx[(i + 1) % Idx.Num()];
					if (Cross(Poly[A], Poly[Bi], Poly[C]) <= 0.0)
					{
						continue; // reflex corner
					}
					bool bEar = true;
					for (const int32 k : Idx)
					{
						if (k != A && k != Bi && k != C && Cross(Poly[A], Poly[Bi], Poly[k]) >= 0.0 && Cross(Poly[Bi], Poly[C], Poly[k]) >= 0.0 &&
							Cross(Poly[C], Poly[A], Poly[k]) >= 0.0)
						{
							bEar = false;
							break;
						}
					}
					if (bEar)
					{
						Out.Append({ A, Bi, C });
						Idx.RemoveAt(i);
						break;
					}
				}
			}
			if (Idx.Num() == 3)
			{
				Out.Append({ Idx[0], Idx[1], Idx[2] });
			}
			return Out;
		}
	}

	UStaticMesh* BuildBoxLabelsMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		const int32 S = B.Section(TEXT("Label"));
		const double Out = 50.6, Half = 39.0;
		const FVector Normals[6] = { FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector, FVector::UpVector, -FVector::UpVector };
		for (const FVector& N : Normals)
		{
			// Upright on the sides; on the top and bottom any fixed "up" will do.
			const FVector Up = FMath::Abs(N.Z) > 0.5 ? FVector::ForwardVector : FVector::UpVector;
			// A viewer looking at the face (along -N) has this on their right (UE is left-handed: Y = Z x X).
			const FVector Right = FVector::CrossProduct(Up, -N);
			auto V = [&](double U, double W)
			{
				const FVector P = N * Out + Right * (U * 2.0 - 1.0) * Half + Up * (1.0 - W * 2.0) * Half;
				return FItemMeshBuilder::FVert{ FVector3f(P), FVector3f(N), FVector2f(static_cast<float>(U), static_cast<float>(W)) };
			};
			B.Quad(S, V(0.0, 0.0), V(1.0, 0.0), V(1.0, 1.0), V(0.0, 1.0));
		}
		return B.Build(Outer, TEXT("SM_ItemBox_Labels"));
	}

	UStaticMesh* BuildNitroMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		AddCanister(B, FVector(0.0, 0.0, -27.0), FVector::UpVector);
		return B.Build(Outer, TEXT("SM_Item_Nitro"));
	}

	UStaticMesh* BuildTripleNitroMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		AddCanister(B, FVector(0.0, 0.0, -27.0), FVector::UpVector);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const double Tilt = FMath::DegreesToRadians(24.0);
			AddCanister(B, FVector(-6.0, Side * 17.0, -30.0), FVector(0.0, Side * FMath::Sin(Tilt), FMath::Cos(Tilt)));
		}
		return B.Build(Outer, TEXT("SM_Item_TripleNitro"));
	}

	UStaticMesh* BuildPrismMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		const int32 Sides = 8;
		const FVector Top(0.0, 0.0, 36.0), Bottom(0.0, 0.0, -30.0), Centre(0.0, 0.0, 4.0);
		TArray<FVector> Upper, Lower;
		for (int32 i = 0; i < Sides; ++i)
		{
			const double A = 2.0 * UE_DOUBLE_PI * i / Sides;
			Upper.Add(FVector(FMath::Cos(A) * 23.0, FMath::Sin(A) * 23.0, 8.0));
			Lower.Add(FVector(FMath::Cos(A + UE_DOUBLE_PI / Sides) * 23.0, FMath::Sin(A + UE_DOUBLE_PI / Sides) * 23.0, 0.0));
		}
		for (int32 i = 0; i < Sides; ++i)
		{
			const int32 j = (i + 1) % Sides;
			// Crown: each side split into kite facets for extra sparkle.
			const FVector Mid = (Upper[i] + Upper[j]) * 0.5 * 0.82 + FVector(0.0, 0.0, 17.0);
			B.Facet(TEXT("Crystal"), Top, Upper[i], Mid, Centre);
			B.Facet(TEXT("Crystal"), Top, Mid, Upper[j], Centre);
			B.Facet(TEXT("Crystal"), Upper[i], Upper[j], Mid, Centre);
			// Girdle: a zigzag band between the two rings.
			B.Facet(TEXT("Crystal"), Upper[i], Lower[i], Upper[j], Centre);
			B.Facet(TEXT("Crystal"), Lower[i], Lower[j], Upper[j], Centre);
			// Pavilion down to the point.
			B.Facet(TEXT("Crystal"), Lower[i], Bottom, Lower[j], Centre);
		}
		return B.Build(Outer, TEXT("SM_Item_Prism"));
	}

	UStaticMesh* BuildLightningMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		// The glyph in (x, z), counter-clockwise: a top bar slanting down-left, a step, a long point.
		const TArray<FVector2D> Poly = { {-2.0, 34.0}, {-14.0, 2.0}, {-1.0, 2.0}, {-10.0, -34.0}, {16.0, 8.0}, {3.0, 8.0}, {14.0, 34.0} };
		const double T = 6.0;
		const TArray<int32> Tris = Triangulate(Poly);
		for (int32 Face = -1; Face <= 1; Face += 2)
		{
			for (int32 t = 0; t + 2 < Tris.Num(); t += 3)
			{
				auto P = [&](int32 k) { return FVector(Poly[Tris[t + k]].X, Face * T, Poly[Tris[t + k]].Y); };
				B.Facet(TEXT("Bolt"), P(0), P(1), P(2), FVector(P(0).X, 0.0, P(0).Z));
			}
		}
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2D A = Poly[i], C = Poly[(i + 1) % Poly.Num()];
			const FVector A0(A.X, -T, A.Y), A1(A.X, T, A.Y), C0(C.X, -T, C.Y), C1(C.X, T, C.Y);
			// The outward side is to the right of a counter-clockwise edge.
			const FVector Out = FVector(C.Y - A.Y, 0.0, -(C.X - A.X)).GetSafeNormal();
			const FVector Inside = (A0 + C1) * 0.5 - Out * 10.0;
			B.Facet(TEXT("Edge"), A0, C0, C1, Inside);
			B.Facet(TEXT("Edge"), A0, C1, A1, Inside);
		}
		return B.Build(Outer, TEXT("SM_Item_Lightning"));
	}

	UStaticMesh* BuildInkMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		// A drop: round bottom, swelling, then a soft point.
		B.Lathe(TEXT("Ink"), { {0.0, -26.0}, {12.0, -24.0}, {19.5, -18.0}, {22.0, -9.0}, {20.5, 0.0}, {16.0, 9.0}, {10.0, 18.0}, {5.0, 26.0}, {1.5, 32.0}, {0.0, 34.0} }, 28);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const FVector Eye(16.5, Side * 8.0, -4.0);
			B.Lathe(TEXT("Eyes"), Arc(6.5, 0.0, -90.0, 90.0, 8, 0.55), 16, Eye, FVector(1.0, Side * 0.25, 0.1).GetSafeNormal());
			B.Lathe(TEXT("Pupils"), Arc(3.2, 0.0, -90.0, 90.0, 6, 0.6), 12, Eye + FVector(3.2, Side * 0.6, -0.8), FVector(1.0, Side * 0.2, 0.0).GetSafeNormal());
		}
		return B.Build(Outer, TEXT("SM_Item_Ink"));
	}

	UStaticMesh* BuildStrikeMesh(UObject* Outer, int32 Seed, double Height)
	{
		FItemMeshBuilder B;
		FRandomStream Rand(Seed);
		const int32 Steps = 12;
		TArray<FVector> Points;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double T = static_cast<double>(i) / Steps;
			// Wide zigzags up high, tightening to land on the target.
			const double Wander = (i == 0 || i == Steps) ? 0.0 : 70.0 * (1.0 - T * 0.7);
			Points.Add(FVector(Rand.FRandRange(-Wander, Wander), Rand.FRandRange(-Wander, Wander), Height * (1.0 - T)));
		}
		for (int32 i = 0; i < Steps; ++i)
		{
			const double W = FMath::Lerp(26.0, 16.0, static_cast<double>(i) / Steps);
			// Two crossed slabs per segment, so the bolt reads from any side.
			B.Beam(TEXT("Bolt"), Points[i], Points[i + 1], W, W * 0.6, FVector::RightVector);
			B.Beam(TEXT("Bolt"), Points[i], Points[i + 1], W, W * 0.6, FVector::ForwardVector);
		}
		for (int32 Fork = 0; Fork < 2; ++Fork)
		{
			FVector P = Points[2 + Fork * 4];
			const FVector Dir = FVector(Rand.FRandRange(-1.0, 1.0), Rand.FRandRange(-1.0, 1.0), 0.0).GetSafeNormal();
			for (int32 k = 0; k < 4; ++k)
			{
				const FVector Next = P + Dir * Rand.FRandRange(40.0, 80.0) + FVector(Rand.FRandRange(-25.0, 25.0), Rand.FRandRange(-25.0, 25.0), -Height / Steps * 0.8);
				B.Beam(TEXT("Bolt"), P, Next, 10.0 - k * 2.0, 6.0, FVector::RightVector);
				P = Next;
			}
		}
		return B.Build(Outer, *FString::Printf(TEXT("SM_FX_Strike%d"), Seed));
	}
}
