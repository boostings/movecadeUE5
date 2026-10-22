#include "Games/Samurai/SamuraiArt.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"

namespace MovecadeKart::Samurai::Art
{
	namespace
	{
		using MovecadeKart::FMeshBuilder;

		/** The halves split on local Y = 0 (web x): Plus keeps y >= 0. Angles about Z from +X. */
		void Arc(EPart Part, double& A0, double& A1)
		{
			switch (Part)
			{
			case EPart::Plus: A0 = 0.0; A1 = UE_DOUBLE_PI; break;
			case EPart::Minus: A0 = UE_DOUBLE_PI; A1 = 2.0 * UE_DOUBLE_PI; break;
			default: A0 = 0.0; A1 = 2.0 * UE_DOUBLE_PI; break;
			}
		}

		/**
		 * A surface of revolution about Z over [A0, A1]: Profile is (radius, height) from the bottom pole to the top,
		 * in cm; Lobes ripples the radius (the apple's five shoulders); Offset moves it.
		 */
		void LatheArc(FMeshBuilder& B, FName Slot, const TArray<FVector2D>& Profile, int32 Segments, double A0, double A1,
			const FVector& Offset = FVector::ZeroVector, int32 Lobes = 0, double LobeDepth = 0.0, const FVector& Scale = FVector::OneVector)
		{
			const int32 S = B.Section(Slot);
			const int32 N = Profile.Num();
			const int32 Base = B.Sections[S].Verts.Num();
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D T = Profile[FMath::Min(i + 1, N - 1)] - Profile[FMath::Max(i - 1, 0)];
				const FVector2D N2 = FVector2D(T.Y, -T.X).GetSafeNormal();
				for (int32 k = 0; k <= Segments; ++k)
				{
					const double A = FMath::Lerp(A0, A1, static_cast<double>(k) / Segments);
					const double Lobe = 1.0 + LobeDepth * FMath::Cos(Lobes * A);
					const FVector Radial(FMath::Cos(A), FMath::Sin(A), 0.0);
					const FVector P = Offset + FVector(Radial.X * Profile[i].X * Lobe * Scale.X, Radial.Y * Profile[i].X * Lobe * Scale.Y, Profile[i].Y * Scale.Z);
					const FVector Nrm = FVector(Radial.X * N2.X / Scale.X, Radial.Y * N2.X / Scale.Y, N2.Y / Scale.Z).GetSafeNormal();
					B.Vert(S, P, Nrm, FVector2D(static_cast<double>(k) / Segments, static_cast<double>(i) / FMath::Max(N - 1, 1)), FVector2D::ZeroVector);
				}
			}
			const int32 Row = Segments + 1;
			for (int32 i = 0; i + 1 < N; ++i)
			{
				for (int32 k = 0; k < Segments; ++k)
				{
					const int32 a = Base + i * Row + k, b = a + 1, c = a + Row + 1, d = a + Row;
					if (Profile[i].X > 1e-3)
					{
						B.Tri(S, a, b, c);
					}
					if (Profile[i + 1].X > 1e-3)
					{
						B.Tri(S, a, c, d);
					}
				}
			}
		}

		/** The flat face a cut leaves on Y = 0: the profile mirrored either side of Z, facing into the cut. */
		void Cap(FMeshBuilder& B, FName Slot, const TArray<FVector2D>& Profile, EPart Part, double Inset = 1.0, const FVector& Offset = FVector::ZeroVector,
			const FVector& Scale = FVector::OneVector)
		{
			if (Part == EPart::Whole)
			{
				return;
			}
			const int32 S = B.Section(Slot);
			const FVector N(0.0, Part == EPart::Plus ? -1.0 : 1.0, 0.0);
			const double Lift = Part == EPart::Plus ? 0.05 : -0.05;
			for (int32 i = 0; i + 1 < Profile.Num(); ++i)
			{
				auto P = [&](double R, double Z) { return Offset + FVector(R * Inset * Scale.X, Lift, Z * Scale.Z); };
				const int32 a = B.Vert(S, P(-Profile[i].X, Profile[i].Y), N, FVector2D(0, i), FVector2D::ZeroVector);
				const int32 b = B.Vert(S, P(Profile[i].X, Profile[i].Y), N, FVector2D(1, i), FVector2D::ZeroVector);
				const int32 c = B.Vert(S, P(Profile[i + 1].X, Profile[i + 1].Y), N, FVector2D(1, i + 1), FVector2D::ZeroVector);
				const int32 d = B.Vert(S, P(-Profile[i + 1].X, Profile[i + 1].Y), N, FVector2D(0, i + 1), FVector2D::ZeroVector);
				B.Tri(S, a, b, c);
				B.Tri(S, a, c, d);
			}
		}

		TArray<FVector2D> Ellipse(double Rx, double Rz, int32 Steps, double Z0 = 0.0)
		{
			TArray<FVector2D> P;
			for (int32 i = 0; i <= Steps; ++i)
			{
				const double T = UE_DOUBLE_PI * i / Steps;
				P.Add(FVector2D(FMath::Max(0.0, FMath::Sin(T)) * Rx, Z0 - FMath::Cos(T) * Rz));
			}
			return P;
		}

		/** A box from Min to Max kept to the part's side of Y = 0 (whole when Whole). */
		void HalfBox(FMeshBuilder& B, FName Slot, FVector Min, FVector Max, EPart Part)
		{
			if (Part == EPart::Plus)
			{
				Min.Y = FMath::Max(Min.Y, 0.0);
			}
			else if (Part == EPart::Minus)
			{
				Max.Y = FMath::Min(Max.Y, 0.0);
			}
			if (Max.Y - Min.Y < 0.01)
			{
				return;
			}
			B.Box(Slot, (Min + Max) * 0.5, (Max - Min) * 0.5, FVector2D::ZeroVector);
		}

		/** A cylinder along Y (a roller), cut at Y = 0 for a half. */
		void Roller(FMeshBuilder& B, FName Slot, double Z, double X, double Radius, double HalfLength, EPart Part)
		{
			double From = -HalfLength, To = HalfLength;
			if (Part == EPart::Plus) From = 0.0;
			if (Part == EPart::Minus) To = 0.0;
			B.Cylinder(Slot, FVector(X, From, Z), FVector(X, To, Z), Radius, 14, FVector2D::ZeroVector);
		}

		void BuildApple(FMeshBuilder& B, EPart Part)
		{
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Skin = { { 0, -78 }, { 25, -86 }, { 55, -85 }, { 85, -60 }, { 98, -20 }, { 100, 15 }, { 92, 50 }, { 72, 78 }, { 40, 86 }, { 15, 74 }, { 0, 68 } };
			LatheArc(B, TEXT("Skin"), Skin, Part == EPart::Whole ? 40 : 20, A0, A1, FVector::ZeroVector, 5, 0.03);
			Cap(B, TEXT("Flesh"), Skin, Part, 0.97);
			// Seeds in the core of a cut half.
			if (Part != EPart::Whole)
			{
				for (int32 i = 0; i < 5; ++i)
				{
					const double A = i * 2.0 * UE_DOUBLE_PI / 5.0;
					B.Ellipsoid(TEXT("Seed"), FVector(FMath::Cos(A) * 14.0, Part == EPart::Plus ? 0.5 : -0.5, 4.0 + FMath::Sin(A) * 14.0), FVector(4.0, 1.5, 7.0), 4, 8, FVector2D::ZeroVector);
				}
			}
			if (Part != EPart::Minus)
			{
				B.Limb(TEXT("Stem"), FVector(0, 0, 66), FVector(6, 0, 105), 4.0, 3.0, 8, FVector2D::ZeroVector);
				B.Ellipsoid(TEXT("Leaf"), FVector(22, 0, 96), FVector(20, 9, 2.2), 4, 10, FVector2D::ZeroVector);
			}
		}

		void BuildMelon(FMeshBuilder& B, EPart Part)
		{
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Rind = Ellipse(82.0, 82.0 * 0.88, 16);
			// Stripes: alternate wedges of dark rind.
			const int32 Wedges = Part == EPart::Whole ? 16 : 8;
			for (int32 w = 0; w < Wedges; ++w)
			{
				const double W0 = FMath::Lerp(A0, A1, static_cast<double>(w) / Wedges);
				const double W1 = FMath::Lerp(A0, A1, static_cast<double>(w + 1) / Wedges);
				LatheArc(B, w % 2 ? FName(TEXT("Stripe")) : FName(TEXT("Rind")), Rind, 3, W0, W1);
			}
			Cap(B, TEXT("Pith"), Rind, Part, 0.995);
			if (Part != EPart::Whole)
			{
				// The red flesh inset from the pale rind, and black seeds.
				const double Lift = Part == EPart::Plus ? -0.12 : 0.12;
				Cap(B, TEXT("Flesh"), Ellipse(82.0 * 0.9, 82.0 * 0.88 * 0.9, 16), Part, 1.0, FVector(0, Lift, 0));
				for (int32 i = 0; i < 12; ++i)
				{
					const double A = i * 2.0 * UE_DOUBLE_PI / 12.0;
					const double R = 30.0 + (i % 3) * 9.0;
					B.Ellipsoid(TEXT("Seed"), FVector(FMath::Cos(A) * R, Part == EPart::Plus ? -0.4 : 0.4, FMath::Sin(A) * R * 0.85), FVector(3.0, 1.2, 5.0), 3, 8, FVector2D::ZeroVector);
				}
			}
		}

		void BuildLantern(FMeshBuilder& B, EPart Part)
		{
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Paper = Ellipse(66.0, 66.0 * 1.16, 18);
			LatheArc(B, TEXT("Paper"), Paper, Part == EPart::Whole ? 36 : 18, A0, A1);
			Cap(B, TEXT("Inside"), Paper, Part, 0.96);
			// Nine ribs and the top and bottom caps.
			for (int32 r = 1; r <= 9; ++r)
			{
				const double T = UE_DOUBLE_PI * r / 10.0;
				const double R = FMath::Sin(T) * 66.0 + 0.8, Z = -FMath::Cos(T) * 66.0 * 1.16;
				LatheArc(B, TEXT("Rib"), { { R, Z - 1.2 }, { R + 0.8, Z }, { R, Z + 1.2 } }, Part == EPart::Whole ? 36 : 18, A0, A1);
			}
			for (const double Z : { -76.0, 76.0 })
			{
				const double S = Z > 0 ? 1.0 : -1.0;
				LatheArc(B, TEXT("Cap"), { { 0, Z - 6 * S }, { 26, Z - 6 * S }, { 28, Z + 2 * S }, { 0, Z + 4 * S } }, 18, A0, A1);
			}
		}

		void BuildBamboo(FMeshBuilder& B, EPart Part)
		{
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Tube = { { 0, -95 }, { 34, -95 }, { 36, -60 }, { 34, -25 }, { 37, -20 }, { 34, -15 }, { 35, 30 }, { 34, 60 }, { 37, 65 }, { 34, 70 }, { 35, 95 }, { 0, 95 } };
			LatheArc(B, TEXT("Cane"), Tube, Part == EPart::Whole ? 24 : 12, A0, A1);
			Cap(B, TEXT("Pith"), Tube, Part, 0.9);
			if (Part != EPart::Minus)
			{
				B.Ellipsoid(TEXT("Leaf"), FVector(30, 12, 80), FVector(30, 6, 2.0), 3, 10, FVector2D::ZeroVector);
				B.Ellipsoid(TEXT("Leaf"), FVector(-24, 18, 55), FVector(26, 5, 2.0), 3, 10, FVector2D::ZeroVector);
			}
		}

		void BuildBomb(FMeshBuilder& B, EPart Part)
		{
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Shell = Ellipse(69.0, 69.0, 16);
			LatheArc(B, TEXT("Shell"), Shell, Part == EPart::Whole ? 32 : 16, A0, A1);
			Cap(B, TEXT("Inside"), Shell, Part, 0.92);
			LatheArc(B, TEXT("Band"), { { 69.5, -3 }, { 71, 0 }, { 69.5, 3 } }, Part == EPart::Whole ? 32 : 16, A0, A1);
			if (Part != EPart::Minus)
			{
				LatheArc(B, TEXT("Band"), { { 0, 62 }, { 18, 62 }, { 18, 78 }, { 0, 78 } }, 16, 0.0, 2.0 * UE_DOUBLE_PI);
				// The fuse: a short curl, its ember at the end.
				const FVector Fuse[] = { FVector(0, 0, 76), FVector(5, 0, 92), FVector(14, 0, 102), FVector(26, 0, 104) };
				for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(Fuse); ++i)
				{
					B.Limb(TEXT("Fuse"), Fuse[i], Fuse[i + 1], 3.0, 3.0, 8, FVector2D::ZeroVector);
				}
				B.Ellipsoid(TEXT("Ember"), Fuse[3], FVector(7.0), 6, 10, FVector2D::ZeroVector);
			}
		}

		void BuildScroll(FMeshBuilder& B, EPart Part)
		{
			// A sheet 1.13 x .86 bowed toward the camera, with rollers top and bottom.
			const double W = 113.0 * 0.5, H = 86.0 * 0.5;
			const int32 S = B.Section(TEXT("Paper"));
			const int32 Cols = 12;
			for (int32 c = 0; c < Cols; ++c)
			{
				const double Y0 = FMath::Lerp(-W, W, static_cast<double>(c) / Cols), Y1 = FMath::Lerp(-W, W, static_cast<double>(c + 1) / Cols);
				if ((Part == EPart::Plus && Y1 <= 0.0) || (Part == EPart::Minus && Y0 >= 0.0))
				{
					continue;
				}
				auto Bow = [W](double Y) { return -6.0 * (1.0 - (Y / W) * (Y / W)); };
				for (const double Face : { -1.0, 1.0 })
				{
					const FVector N(Face, 0, 0);
					const double X0 = Bow(Y0) + Face * 0.9, X1 = Bow(Y1) + Face * 0.9;
					const int32 a = B.Vert(S, FVector(X0, Y0, -H), N, FVector2D(c / double(Cols), 1), FVector2D::ZeroVector);
					const int32 b = B.Vert(S, FVector(X1, Y1, -H), N, FVector2D((c + 1) / double(Cols), 1), FVector2D::ZeroVector);
					const int32 cc = B.Vert(S, FVector(X1, Y1, H), N, FVector2D((c + 1) / double(Cols), 0), FVector2D::ZeroVector);
					const int32 d = B.Vert(S, FVector(X0, Y0, H), N, FVector2D(c / double(Cols), 0), FVector2D::ZeroVector);
					B.Tri(S, a, b, cc);
					B.Tri(S, a, cc, d);
				}
			}
			// A seal and a line of brushwork on the face.
			HalfBox(B, TEXT("Ink"), FVector(-8.0, -6.0, -24.0), FVector(-6.5, 6.0, 26.0), Part);
			HalfBox(B, TEXT("Seal"), FVector(-8.2, 14.0, -30.0), FVector(-6.6, 26.0, -18.0), Part);
			Roller(B, TEXT("Roller"), H + 4.0, 0.0, 6.0, W + 8.0, Part);
			Roller(B, TEXT("Roller"), -H - 4.0, 0.0, 6.0, W + 8.0, Part);
		}
	}

	UStaticMesh* TargetMesh(UObject* Outer, EVariant Variant, EPart Part)
	{
		FMeshBuilder B;
		// Every part of a target registers the same slots in the same order, so one material list fits them all.
		static const TArray<FName> Slots[] = {
			{ TEXT("Skin"), TEXT("Flesh"), TEXT("Seed"), TEXT("Stem"), TEXT("Leaf") },
			{ TEXT("Rind"), TEXT("Stripe"), TEXT("Pith"), TEXT("Flesh"), TEXT("Seed") },
			{ TEXT("Paper"), TEXT("Inside"), TEXT("Rib"), TEXT("Cap") },
			{ TEXT("Cane"), TEXT("Pith"), TEXT("Leaf") },
			{ TEXT("Shell"), TEXT("Inside"), TEXT("Band"), TEXT("Fuse"), TEXT("Ember") },
			{ TEXT("Paper"), TEXT("Ink"), TEXT("Seal"), TEXT("Roller") },
		};
		for (const FName& Slot : Slots[static_cast<int32>(Variant)])
		{
			B.Section(Slot);
		}
		switch (Variant)
		{
		case EVariant::Apple: BuildApple(B, Part); break;
		case EVariant::Melon: BuildMelon(B, Part); break;
		case EVariant::Lantern: BuildLantern(B, Part); break;
		case EVariant::Bamboo: BuildBamboo(B, Part); break;
		case EVariant::Bomb: BuildBomb(B, Part); break;
		case EVariant::Scroll: BuildScroll(B, Part); break;
		}
		return B.Build(Outer, *FString::Printf(TEXT("Samurai_%s_%d"), VariantName(Variant), static_cast<int32>(Part)));
	}

	TArray<UMaterialInterface*> TargetMaterials(UObject* Outer, EVariant Variant)
	{
		using namespace MovecadeKart::Art;
		// Slot order follows each builder's first use of a slot name, whole or half alike. Skins are glossy with a
		// touch of emissive in their own hue (light scattering through the skin at sunset); cut flesh is wet (low
		// roughness) and glows a little brighter, so a fresh cut reads juicy against the shade.
		switch (Variant)
		{
		case EVariant::Apple:
			return { Plain(Outer, Hex(TEXT("#e8263f")), 0.22, 0.0, 0.1), Plain(Outer, Hex(TEXT("#fbe7bd")), 0.18, 0.0, 0.32), Plain(Outer, Hex(TEXT("#3a2414")), 0.35),
				Plain(Outer, Hex(TEXT("#5b3a1e")), 0.7), Plain(Outer, Hex(TEXT("#3fa34d")), 0.45, 0.0, 0.08) };
		case EVariant::Melon:
			return { Plain(Outer, Hex(TEXT("#3fbf6a")), 0.3, 0.0, 0.06), Plain(Outer, Hex(TEXT("#17682f")), 0.34), Plain(Outer, Hex(TEXT("#eef6d6")), 0.3, 0.0, 0.2),
				Plain(Outer, Hex(TEXT("#ff3b47")), 0.16, 0.0, 0.42), Plain(Outer, Hex(TEXT("#141414")), 0.3) };
		case EVariant::Lantern:
			return { Plain(Outer, Hex(TEXT("#ffc12e")), 0.6, 0.0, 0.9), Plain(Outer, Hex(TEXT("#ffe39a")), 0.6, 0.0, 1.6), Plain(Outer, Hex(TEXT("#2a1a10")), 0.5),
				Plain(Outer, Hex(TEXT("#1a1a1a")), 0.3, 0.6) };
		case EVariant::Bamboo:
			return { Surface(Outer, { Hex(TEXT("#34d399")), TEXT("dark_wooden_planks"), 0.3, 0.2, 0.3, 0.2, 0.3, 0.0, 0.08, true, 1.0 }), Plain(Outer, Hex(TEXT("#f2f6cf")), 0.35, 0.0, 0.3),
				Plain(Outer, Hex(TEXT("#2f9e5c")), 0.5, 0.0, 0.06) };
		case EVariant::Bomb:
			return { Plain(Outer, Hex(TEXT("#232c3b")), 0.26, 0.6), Plain(Outer, Hex(TEXT("#0b0d10")), 0.8), Plain(Outer, Hex(TEXT("#b08a4a")), 0.3, 0.9),
				Plain(Outer, Hex(TEXT("#5a4630")), 0.8), Glow(Outer, Hex(TEXT("#ff8c42")) * 9.f) };
		case EVariant::Scroll:
			return { Plain(Outer, Hex(TEXT("#fde68a")), 0.7, 0.0, 0.3), Plain(Outer, Hex(TEXT("#1d1b1a")), 0.6), Plain(Outer, Hex(TEXT("#d03a2a")), 0.45, 0.0, 0.2),
				Plain(Outer, Hex(TEXT("#6b2f1a")), 0.35) };
		}
		return {};
	}

	UStaticMesh* KatanaMesh(UObject* Outer)
	{
		// katanaGeometry.ts at the rig's .64: the blade from .095 to 1.85 along +X, widths .14 to .09 then the kissaki,
		// curving toward the spine; the guard at the base, the wrapped grip behind it.
		constexpr double S = 0.64 * 100.0;
		FMeshBuilder B;
		const int32 Sections = 20;
		const int32 Blade = B.Section(TEXT("Blade"));
		const int32 Edge = B.Section(TEXT("Edge"));
		TArray<FVector> Spine, Cutting, Mid;
		for (int32 i = 0; i <= Sections; ++i)
		{
			const double T = static_cast<double>(i) / Sections;
			const double Y = FMath::Lerp(0.095, 1.85, T);
			double Width = FMath::Lerp(0.14, 0.09, T);
			if (i == Sections - 2) Width = 0.073;
			if (i == Sections - 1) Width = 0.048;
			if (i == Sections) Width = 0.008;
			const double Curve = 0.1 * FMath::Pow(T, 1.55);
			// Spine on -Z, edge on +Z; the curve sweeps the whole blade back toward the spine.
			Spine.Add(FVector(Y * S, 0.0, (-Curve - Width * 0.5) * S * 0.6));
			Cutting.Add(FVector(Y * S, 0.0, (-Curve + Width * 0.5) * S * 0.6));
			Mid.Add(FVector(Y * S, 0.0, -Curve * S * 0.6));
		}
		const double Thick = 0.035 * S * 0.5;
		for (int32 i = 0; i < Sections; ++i)
		{
			for (const double Side : { -1.0, 1.0 })
			{
				// Each face: spine to ridge (the flat), ridge to edge (the bevel, brighter).
				const FVector Off(0.0, Side * Thick, 0.0);
				const FVector N(0.0, Side, 0.0);
				auto Quad = [&](int32 Sec, const FVector& A, const FVector& Bb, const FVector& C, const FVector& D)
				{
					const int32 a = B.Vert(Sec, A, N, FVector2D(0, 0), FVector2D::ZeroVector);
					const int32 b = B.Vert(Sec, Bb, N, FVector2D(1, 0), FVector2D::ZeroVector);
					const int32 c = B.Vert(Sec, C, N, FVector2D(1, 1), FVector2D::ZeroVector);
					const int32 d = B.Vert(Sec, D, N, FVector2D(0, 1), FVector2D::ZeroVector);
					B.Tri(Sec, a, b, c);
					B.Tri(Sec, a, c, d);
				};
				Quad(Blade, Spine[i] + Off * 0.6, Spine[i + 1] + Off * 0.6, Mid[i + 1] + Off, Mid[i] + Off);
				Quad(Edge, Mid[i] + Off, Mid[i + 1] + Off, Cutting[i + 1], Cutting[i]);
			}
			// The spine's back.
			const int32 a = B.Vert(Blade, Spine[i] + FVector(0, -Thick * 0.6, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 b = B.Vert(Blade, Spine[i] + FVector(0, Thick * 0.6, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 c = B.Vert(Blade, Spine[i + 1] + FVector(0, Thick * 0.6, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 d = B.Vert(Blade, Spine[i + 1] + FVector(0, -Thick * 0.6, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			B.Tri(Blade, a, b, c);
			B.Tri(Blade, a, c, d);
		}
		// Habaki, tsuba, grip and kashira.
		B.Box(TEXT("Brass"), FVector(0.11 * S, 0.0, -0.005 * S), FVector(0.03 * S, Thick * 1.6, 0.05 * S), FVector2D::ZeroVector);
		B.Cylinder(TEXT("Guard"), FVector(0.07 * S, 0, 0), FVector(0.09 * S, 0, 0), 0.075 * S, 20, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Grip"), FVector(-0.34 * S, 0, 0), FVector(0.07 * S, 0, 0), 0.032 * S, 12, FVector2D::ZeroVector);
		for (int32 i = 0; i < 7; ++i)
		{
			const double X = (-0.3 + i * 0.05) * S;
			B.Ellipsoid(TEXT("Wrap"), FVector(X, 0, 0), FVector(0.012 * S, 0.034 * S, 0.034 * S), 3, 10, FVector2D::ZeroVector);
		}
		B.Cylinder(TEXT("Brass"), FVector(-0.37 * S, 0, 0), FVector(-0.34 * S, 0, 0), 0.036 * S, 12, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("Katana"));
	}

	TArray<UMaterialInterface*> KatanaMaterials(UObject* Outer)
	{
		using namespace MovecadeKart::Art;
		// Polished tamahagane: a mirror flat, the hamon's bright edge catching the sunset (a whisper of emissive so
		// the edge reads even in the fill's shade), gilt habaki, black lacquered tsuba, dark wrap, bone ito.
		return {
			Plain(Outer, Hex(TEXT("#c7d0d9")), 0.1, 1.0),
			Plain(Outer, Hex(TEXT("#f4f8fb")), 0.04, 1.0, 0.18),
			Plain(Outer, Hex(TEXT("#d9b066")), 0.22, 1.0),
			Plain(Outer, Hex(TEXT("#1b1b1f")), 0.32, 0.7),
			Plain(Outer, Hex(TEXT("#2a1610")), 0.85),
			Plain(Outer, Hex(TEXT("#e8e0cf")), 0.75),
		};
	}

	UStaticMesh* RingMesh(UObject* Outer, double Inner, double OuterR, int32 Segments, bool bBar)
	{
		FMeshBuilder B;
		const int32 S = B.Section(TEXT("Ring"));
		const FVector N(-1, 0, 0);
		for (int32 i = 0; i < Segments; ++i)
		{
			const double A0 = 2.0 * UE_DOUBLE_PI * i / Segments + (Segments == 8 ? UE_DOUBLE_PI / 8.0 : 0.0);
			const double A1 = 2.0 * UE_DOUBLE_PI * (i + 1) / Segments + (Segments == 8 ? UE_DOUBLE_PI / 8.0 : 0.0);
			auto P = [](double R, double A) { return FVector(0.0, FMath::Cos(A) * R, FMath::Sin(A) * R); };
			const int32 a = B.Vert(S, P(Inner, A0), N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 b = B.Vert(S, P(OuterR, A0), N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 c = B.Vert(S, P(OuterR, A1), N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 d = B.Vert(S, P(Inner, A1), N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			B.Tri(S, a, b, c);
			B.Tri(S, a, c, d);
		}
		if (bBar)
		{
			// The "no" bar across the octagon, at -45 degrees.
			const FVector Dir = FVector(0.0, FMath::Cos(-UE_DOUBLE_PI / 4.0), FMath::Sin(-UE_DOUBLE_PI / 4.0));
			const FVector Side = FVector(0.0, -Dir.Z, Dir.Y);
			const double Half = OuterR * 0.97, W = (OuterR - Inner) * 0.5;
			const int32 a = B.Vert(S, -Dir * Half - Side * W, N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 b = B.Vert(S, Dir * Half - Side * W, N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 c = B.Vert(S, Dir * Half + Side * W, N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 d = B.Vert(S, -Dir * Half + Side * W, N, FVector2D::ZeroVector, FVector2D::ZeroVector);
			B.Tri(S, a, b, c);
			B.Tri(S, a, c, d);
		}
		return B.Build(Outer, TEXT("SamuraiRing"));
	}
}
