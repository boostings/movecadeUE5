#include "Games/Samurai/SamuraiArt.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"

// Each prop is a small table of its proportions (UE cm, targets at unit radius = 100 cm) and its looks, then the
// builder that applies it. Whole and halved builds share the tables, so a half is exactly a whole cut in two.

namespace MovecadeKart::Samurai::Art
{
	namespace
	{
		using MovecadeKart::FMeshBuilder;

		/** A plain lit look (Art::Plain): sRGB hex colour, roughness, metallic, emissive. */
		struct FPlainLook
		{
			const TCHAR* Color;
			double Roughness;
			double Metallic;
			double Emissive;
		};

		UMaterialInterface* Make(UObject* Outer, const FPlainLook& Look)
		{
			return MovecadeKart::Art::Plain(Outer, MovecadeKart::Art::Hex(Look.Color), Look.Roughness, Look.Metallic, Look.Emissive);
		}

		/** Round surfaces use this many segments whole, half as many per half. */
		int32 SegmentsFor(EPart Part, int32 Whole)
		{
			return Part == EPart::Whole ? Whole : Whole / 2;
		}

		namespace AppleShape
		{
			/** The skin's profile, bottom pole to top (radius, height). */
			const TArray<FVector2D> Skin = { { 0, -78 }, { 25, -86 }, { 55, -85 }, { 85, -60 }, { 98, -20 }, { 100, 15 }, { 92, 50 }, { 72, 78 }, { 40, 86 }, { 15, 74 }, { 0, 68 } };
			constexpr int32 Segments = 40;
			/** Five soft shoulders. */
			constexpr int32 Lobes = 5;
			constexpr double LobeDepth = 0.03;
			constexpr double FleshInset = 0.97;
			/** Five seeds in a ring in the core of a cut half. */
			constexpr int32 Seeds = 5;
			constexpr double SeedRing = 14.0;
			constexpr double SeedZ = 4.0;
			constexpr double SeedLift = 0.5;
			const FVector SeedSize(4.0, 1.5, 7.0);
			const FVector StemFrom(0, 0, 66), StemTo(6, 0, 105);
			constexpr double StemRadius = 4.0, StemTipRadius = 3.0;
			const FVector LeafAt(22, 0, 96), LeafSize(20, 9, 2.2);
			// Skin, Flesh, Seed, Stem, Leaf: glossy skin with a touch of its own hue, wet and brighter flesh.
			constexpr FPlainLook Looks[] = { { TEXT("#e8263f"), 0.22, 0.0, 0.1 }, { TEXT("#fbe7bd"), 0.18, 0.0, 0.32 }, { TEXT("#3a2414"), 0.35, 0.0, 0.0 },
				{ TEXT("#5b3a1e"), 0.7, 0.0, 0.0 }, { TEXT("#3fa34d"), 0.45, 0.0, 0.08 } };
		}

		namespace MelonShape
		{
			constexpr double Radius = 82.0;
			constexpr double Squash = 0.88;
			constexpr int32 ProfileSteps = 16;
			/** Alternate wedges of dark rind make the stripes. */
			constexpr int32 Wedges = 16;
			constexpr int32 WedgeSegments = 3;
			constexpr double PithInset = 0.995;
			/** The red flesh inset from the pale rind, standing just off the cut. */
			constexpr double FleshShare = 0.9;
			constexpr double FleshLift = 0.12;
			/** Black seeds in a ring of three radii. */
			constexpr int32 Seeds = 12;
			constexpr double SeedRing = 30.0;
			constexpr double SeedRingStep = 9.0;
			constexpr double SeedSquash = 0.85;
			constexpr double SeedLift = 0.4;
			const FVector SeedSize(3.0, 1.2, 5.0);
			// Rind, Stripe, Pith, Flesh, Seed.
			constexpr FPlainLook Looks[] = { { TEXT("#3fbf6a"), 0.3, 0.0, 0.06 }, { TEXT("#17682f"), 0.34, 0.0, 0.0 }, { TEXT("#eef6d6"), 0.3, 0.0, 0.2 },
				{ TEXT("#ff3b47"), 0.16, 0.0, 0.42 }, { TEXT("#141414"), 0.3, 0.0, 0.0 } };
		}

		namespace LanternShape
		{
			constexpr double Radius = 66.0;
			constexpr double Stretch = 1.16;
			constexpr int32 ProfileSteps = 18;
			constexpr int32 Segments = 36;
			constexpr double InsideInset = 0.96;
			/** Nine ribs standing proud of the paper. */
			constexpr int32 RibDivisions = 10;
			constexpr double RibProud = 0.8;
			constexpr double RibHalfWidth = 1.2;
			/** The caps at either pole. */
			constexpr double CapZ = 76.0;
			constexpr double CapRadius = 26.0, CapRim = 28.0;
			constexpr double CapInner = 6.0, CapRimZ = 2.0, CapTopZ = 4.0;
			constexpr int32 CapSegments = 18;
			// Paper, Inside, Rib, Cap: the paper glows from within.
			constexpr FPlainLook Looks[] = { { TEXT("#ffc12e"), 0.6, 0.0, 0.9 }, { TEXT("#ffe39a"), 0.6, 0.0, 1.6 }, { TEXT("#2a1a10"), 0.5, 0.0, 0.0 },
				{ TEXT("#1a1a1a"), 0.3, 0.6, 0.0 } };
		}

		namespace BambooShape
		{
			/** A section of cane with two nodes. */
			const TArray<FVector2D> Tube = { { 0, -95 }, { 34, -95 }, { 36, -60 }, { 34, -25 }, { 37, -20 }, { 34, -15 }, { 35, 30 }, { 34, 60 }, { 37, 65 }, { 34, 70 }, { 35, 95 }, { 0, 95 } };
			constexpr int32 Segments = 24;
			constexpr double PithInset = 0.9;
			/** Two leaves on the plus side (centre, radii). */
			const FVector LeafAt[] = { FVector(30, 12, 80), FVector(-24, 18, 55) };
			const FVector LeafSize[] = { FVector(30, 6, 2.0), FVector(26, 5, 2.0) };
			// Cane (a surface over the wood detail), Pith, Leaf.
			const MovecadeKart::Art::FSurfaceLook Cane{ MovecadeKart::Art::Hex(TEXT("#34d399")), TEXT("dark_wooden_planks"), 0.3, 0.2, 0.3, 0.2, 0.3, 0.0, 0.08, true, 1.0 };
			constexpr FPlainLook Pith{ TEXT("#f2f6cf"), 0.35, 0.0, 0.3 };
			constexpr FPlainLook Leaf{ TEXT("#2f9e5c"), 0.5, 0.0, 0.06 };
		}

		namespace BombShape
		{
			constexpr double Radius = 69.0;
			constexpr int32 ProfileSteps = 16;
			constexpr int32 Segments = 32;
			constexpr double InsideInset = 0.92;
			/** The brass band round the middle and the collar on top. */
			const TArray<FVector2D> Band = { { 69.5, -3 }, { 71, 0 }, { 69.5, 3 } };
			const TArray<FVector2D> Collar = { { 0, 62 }, { 18, 62 }, { 18, 78 }, { 0, 78 } };
			constexpr int32 CollarSegments = 16;
			/** The fuse: a short curl, its ember at the end. */
			const FVector Fuse[] = { FVector(0, 0, 76), FVector(5, 0, 92), FVector(14, 0, 102), FVector(26, 0, 104) };
			constexpr double FuseRadius = 3.0;
			constexpr double EmberRadius = 7.0;
			// Shell, Inside, Band, Fuse, then the ember (an opaque glow).
			constexpr FPlainLook Looks[] = { { TEXT("#232c3b"), 0.26, 0.6, 0.0 }, { TEXT("#0b0d10"), 0.8, 0.0, 0.0 }, { TEXT("#b08a4a"), 0.3, 0.9, 0.0 },
				{ TEXT("#5a4630"), 0.8, 0.0, 0.0 } };
			const TCHAR* const Ember = TEXT("#ff8c42");
			constexpr float EmberGlow = 9.f;
		}

		namespace ScrollShape
		{
			/** A 1.13 x .86 sheet bowed toward the camera, paper this thick each side of its middle. */
			constexpr double HalfWidth = 113.0 * 0.5;
			constexpr double HalfHeight = 86.0 * 0.5;
			constexpr int32 Columns = 12;
			constexpr double Bow = 6.0;
			constexpr double HalfThickness = 0.9;
			/** A line of brushwork and a seal on the face, and the rollers. */
			const FVector InkMin(-8.0, -6.0, -24.0), InkMax(-6.5, 6.0, 26.0);
			const FVector SealMin(-8.2, 14.0, -30.0), SealMax(-6.6, 26.0, -18.0);
			constexpr double RollerGap = 4.0;
			constexpr double RollerRadius = 6.0;
			constexpr double RollerOverhang = 8.0;
			constexpr int32 RollerSides = 14;
			// Paper, Ink, Seal, Roller.
			constexpr FPlainLook Looks[] = { { TEXT("#fde68a"), 0.7, 0.0, 0.3 }, { TEXT("#1d1b1a"), 0.6, 0.0, 0.0 }, { TEXT("#d03a2a"), 0.45, 0.0, 0.2 },
				{ TEXT("#6b2f1a"), 0.35, 0.0, 0.0 } };
		}

		namespace KatanaShape
		{
			/** katanaGeometry.ts at the rig's .64 (m to cm). */
			constexpr double Scale = 0.64 * 100.0;
			constexpr int32 Sections = 20;
			/** The blade runs from .095 to 1.85 along +X, .14 wide tapering to .09, then the kissaki's last three widths. */
			constexpr double BladeFrom = 0.095, BladeTo = 1.85;
			constexpr double WidthAtHilt = 0.14, WidthAtTip = 0.09;
			constexpr double KissakiWidths[] = { 0.073, 0.048, 0.008 };
			/** The curve sweeps back toward the spine (sori), flattened in depth. */
			constexpr double Sori = 0.1;
			constexpr double SoriPower = 1.55;
			constexpr double Flatten = 0.6;
			/** The blade's thickness, and the spine's share of it. */
			constexpr double Thickness = 0.035;
			constexpr double SpineShare = 0.6;
			/** Habaki, tsuba, grip, wrap and kashira (m before Scale). */
			const FVector HabakiAt(0.11, 0.0, -0.005);
			constexpr double HabakiHalfLength = 0.03, HabakiHalfHeight = 0.05, HabakiThickness = 1.6;
			constexpr double GuardFrom = 0.07, GuardTo = 0.09, GuardRadius = 0.075;
			constexpr int32 GuardSides = 20;
			constexpr double GripFrom = -0.34, GripRadius = 0.032;
			constexpr int32 GripSides = 12;
			constexpr int32 Wraps = 7;
			constexpr double WrapFrom = -0.3, WrapPitch = 0.05;
			const FVector WrapSize(0.012, 0.034, 0.034);
			constexpr double PommelFrom = -0.37, PommelRadius = 0.036;
			// Blade, Edge, Brass, Guard, Grip, Wrap: a mirror flat, the hamon's bright edge (a whisper of emissive so it
			// reads in the fill's shade), gilt habaki, black lacquered tsuba, dark wrap, bone ito.
			constexpr FPlainLook Looks[] = { { TEXT("#c7d0d9"), 0.1, 1.0, 0.0 }, { TEXT("#f4f8fb"), 0.04, 1.0, 0.18 }, { TEXT("#d9b066"), 0.22, 1.0, 0.0 },
				{ TEXT("#1b1b1f"), 0.32, 0.7, 0.0 }, { TEXT("#2a1610"), 0.85, 0.0, 0.0 }, { TEXT("#e8e0cf"), 0.75, 0.0, 0.0 } };
		}

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
			// Rings at the poles (radius ~0) would make slivers: skip their triangles.
			constexpr double PoleRadius = 1e-3;
			const int32 Row = Segments + 1;
			for (int32 i = 0; i + 1 < N; ++i)
			{
				for (int32 k = 0; k < Segments; ++k)
				{
					const int32 a = Base + i * Row + k, b = a + 1, c = a + Row + 1, d = a + Row;
					if (Profile[i].X > PoleRadius)
					{
						B.Tri(S, a, b, c);
					}
					if (Profile[i + 1].X > PoleRadius)
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
			// Each face stands a hair off the cut so the two halves' faces never z-fight.
			constexpr double FaceLift = 0.05;
			const int32 S = B.Section(Slot);
			const FVector N(0.0, Part == EPart::Plus ? -1.0 : 1.0, 0.0);
			const double Lift = Part == EPart::Plus ? FaceLift : -FaceLift;
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

		/** A half-ellipse profile (radius Rx, half height Rz) from the bottom pole up, Steps segments. */
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
			B.Cylinder(Slot, FVector(X, From, Z), FVector(X, To, Z), Radius, ScrollShape::RollerSides, FVector2D::ZeroVector);
		}

		void BuildApple(FMeshBuilder& B, EPart Part)
		{
			using namespace AppleShape;
			double A0, A1;
			Arc(Part, A0, A1);
			LatheArc(B, TEXT("Skin"), Skin, SegmentsFor(Part, Segments), A0, A1, FVector::ZeroVector, Lobes, LobeDepth);
			Cap(B, TEXT("Flesh"), Skin, Part, FleshInset);
			// Seeds in the core of a cut half.
			if (Part != EPart::Whole)
			{
				for (int32 i = 0; i < Seeds; ++i)
				{
					const double A = i * 2.0 * UE_DOUBLE_PI / Seeds;
					B.Ellipsoid(TEXT("Seed"), FVector(FMath::Cos(A) * SeedRing, Part == EPart::Plus ? SeedLift : -SeedLift, SeedZ + FMath::Sin(A) * SeedRing), SeedSize, 4, 8,
						FVector2D::ZeroVector);
				}
			}
			if (Part != EPart::Minus)
			{
				B.Limb(TEXT("Stem"), StemFrom, StemTo, StemRadius, StemTipRadius, 8, FVector2D::ZeroVector);
				B.Ellipsoid(TEXT("Leaf"), LeafAt, LeafSize, 4, 10, FVector2D::ZeroVector);
			}
		}

		void BuildMelon(FMeshBuilder& B, EPart Part)
		{
			using namespace MelonShape;
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Rind = Ellipse(Radius, Radius * Squash, ProfileSteps);
			const int32 WedgeCount = SegmentsFor(Part, Wedges);
			for (int32 w = 0; w < WedgeCount; ++w)
			{
				const double W0 = FMath::Lerp(A0, A1, static_cast<double>(w) / WedgeCount);
				const double W1 = FMath::Lerp(A0, A1, static_cast<double>(w + 1) / WedgeCount);
				LatheArc(B, w % 2 ? FName(TEXT("Stripe")) : FName(TEXT("Rind")), Rind, WedgeSegments, W0, W1);
			}
			Cap(B, TEXT("Pith"), Rind, Part, PithInset);
			if (Part != EPart::Whole)
			{
				const double Lift = Part == EPart::Plus ? -FleshLift : FleshLift;
				Cap(B, TEXT("Flesh"), Ellipse(Radius * FleshShare, Radius * Squash * FleshShare, ProfileSteps), Part, 1.0, FVector(0, Lift, 0));
				for (int32 i = 0; i < Seeds; ++i)
				{
					const double A = i * 2.0 * UE_DOUBLE_PI / Seeds;
					const double R = SeedRing + (i % 3) * SeedRingStep;
					B.Ellipsoid(TEXT("Seed"), FVector(FMath::Cos(A) * R, Part == EPart::Plus ? -SeedLift : SeedLift, FMath::Sin(A) * R * SeedSquash), SeedSize, 3, 8,
						FVector2D::ZeroVector);
				}
			}
		}

		void BuildLantern(FMeshBuilder& B, EPart Part)
		{
			using namespace LanternShape;
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Paper = Ellipse(Radius, Radius * Stretch, ProfileSteps);
			const int32 RoundSegments = SegmentsFor(Part, Segments);
			LatheArc(B, TEXT("Paper"), Paper, RoundSegments, A0, A1);
			Cap(B, TEXT("Inside"), Paper, Part, InsideInset);
			for (int32 r = 1; r < RibDivisions; ++r)
			{
				const double T = UE_DOUBLE_PI * r / RibDivisions;
				const double R = FMath::Sin(T) * Radius + RibProud, Z = -FMath::Cos(T) * Radius * Stretch;
				LatheArc(B, TEXT("Rib"), { { R, Z - RibHalfWidth }, { R + RibProud, Z }, { R, Z + RibHalfWidth } }, RoundSegments, A0, A1);
			}
			for (const double Z : { -CapZ, CapZ })
			{
				const double S = Z > 0 ? 1.0 : -1.0;
				LatheArc(B, TEXT("Cap"), { { 0, Z - CapInner * S }, { CapRadius, Z - CapInner * S }, { CapRim, Z + CapRimZ * S }, { 0, Z + CapTopZ * S } }, CapSegments, A0, A1);
			}
		}

		void BuildBamboo(FMeshBuilder& B, EPart Part)
		{
			using namespace BambooShape;
			double A0, A1;
			Arc(Part, A0, A1);
			LatheArc(B, TEXT("Cane"), Tube, SegmentsFor(Part, Segments), A0, A1);
			Cap(B, TEXT("Pith"), Tube, Part, PithInset);
			if (Part != EPart::Minus)
			{
				for (int32 i = 0; i < UE_ARRAY_COUNT(LeafAt); ++i)
				{
					B.Ellipsoid(TEXT("Leaf"), LeafAt[i], LeafSize[i], 3, 10, FVector2D::ZeroVector);
				}
			}
		}

		void BuildBomb(FMeshBuilder& B, EPart Part)
		{
			using namespace BombShape;
			double A0, A1;
			Arc(Part, A0, A1);
			const TArray<FVector2D> Shell = Ellipse(Radius, Radius, ProfileSteps);
			const int32 RoundSegments = SegmentsFor(Part, Segments);
			LatheArc(B, TEXT("Shell"), Shell, RoundSegments, A0, A1);
			Cap(B, TEXT("Inside"), Shell, Part, InsideInset);
			LatheArc(B, TEXT("Band"), Band, RoundSegments, A0, A1);
			if (Part != EPart::Minus)
			{
				LatheArc(B, TEXT("Band"), Collar, CollarSegments, 0.0, 2.0 * UE_DOUBLE_PI);
				for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(Fuse); ++i)
				{
					B.Limb(TEXT("Fuse"), Fuse[i], Fuse[i + 1], FuseRadius, FuseRadius, 8, FVector2D::ZeroVector);
				}
				B.Ellipsoid(TEXT("Ember"), Fuse[UE_ARRAY_COUNT(Fuse) - 1], FVector(EmberRadius), 6, 10, FVector2D::ZeroVector);
			}
		}

		void BuildScroll(FMeshBuilder& B, EPart Part)
		{
			using namespace ScrollShape;
			const double W = HalfWidth, H = HalfHeight;
			const int32 S = B.Section(TEXT("Paper"));
			for (int32 c = 0; c < Columns; ++c)
			{
				const double Y0 = FMath::Lerp(-W, W, static_cast<double>(c) / Columns), Y1 = FMath::Lerp(-W, W, static_cast<double>(c + 1) / Columns);
				if ((Part == EPart::Plus && Y1 <= 0.0) || (Part == EPart::Minus && Y0 >= 0.0))
				{
					continue;
				}
				auto BowAt = [W](double Y) { return -Bow * (1.0 - (Y / W) * (Y / W)); };
				for (const double Face : { -1.0, 1.0 })
				{
					const FVector N(Face, 0, 0);
					const double X0 = BowAt(Y0) + Face * HalfThickness, X1 = BowAt(Y1) + Face * HalfThickness;
					const int32 a = B.Vert(S, FVector(X0, Y0, -H), N, FVector2D(c / double(Columns), 1), FVector2D::ZeroVector);
					const int32 b = B.Vert(S, FVector(X1, Y1, -H), N, FVector2D((c + 1) / double(Columns), 1), FVector2D::ZeroVector);
					const int32 cc = B.Vert(S, FVector(X1, Y1, H), N, FVector2D((c + 1) / double(Columns), 0), FVector2D::ZeroVector);
					const int32 d = B.Vert(S, FVector(X0, Y0, H), N, FVector2D(c / double(Columns), 0), FVector2D::ZeroVector);
					B.Tri(S, a, b, cc);
					B.Tri(S, a, cc, d);
				}
			}
			HalfBox(B, TEXT("Ink"), InkMin, InkMax, Part);
			HalfBox(B, TEXT("Seal"), SealMin, SealMax, Part);
			Roller(B, TEXT("Roller"), H + RollerGap, 0.0, RollerRadius, W + RollerOverhang, Part);
			Roller(B, TEXT("Roller"), -H - RollerGap, 0.0, RollerRadius, W + RollerOverhang, Part);
		}

		template <int32 N>
		TArray<UMaterialInterface*> MakeAll(UObject* Outer, const FPlainLook (&Looks)[N])
		{
			TArray<UMaterialInterface*> Out;
			for (const FPlainLook& Look : Looks)
			{
				Out.Add(Make(Outer, Look));
			}
			return Out;
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
		// One material per slot, in the order TargetMesh registers them (whole and halves alike).
		switch (Variant)
		{
		case EVariant::Apple:
			return MakeAll(Outer, AppleShape::Looks);
		case EVariant::Melon:
			return MakeAll(Outer, MelonShape::Looks);
		case EVariant::Lantern:
			return MakeAll(Outer, LanternShape::Looks);
		case EVariant::Bamboo:
			return { MovecadeKart::Art::Surface(Outer, BambooShape::Cane), Make(Outer, BambooShape::Pith), Make(Outer, BambooShape::Leaf) };
		case EVariant::Bomb:
		{
			// The fuse's ember is solid and never fades (a cut bomb blows, it doesn't split): opaque unlit, so it
			// skips the translucency pass. The telegraph ring in front of the bomb stays translucent over it.
			TArray<UMaterialInterface*> Out = MakeAll(Outer, BombShape::Looks);
			Out.Add(MovecadeKart::Art::GlowOpaque(Outer, MovecadeKart::Art::Hex(BombShape::Ember) * BombShape::EmberGlow));
			return Out;
		}
		case EVariant::Scroll:
			return MakeAll(Outer, ScrollShape::Looks);
		}
		return {};
	}

	UStaticMesh* KatanaMesh(UObject* Outer)
	{
		using namespace KatanaShape;
		constexpr double S = Scale;
		FMeshBuilder B;
		const int32 Blade = B.Section(TEXT("Blade"));
		const int32 Edge = B.Section(TEXT("Edge"));
		TArray<FVector> Spine, Cutting, Mid;
		for (int32 i = 0; i <= Sections; ++i)
		{
			const double T = static_cast<double>(i) / Sections;
			const double Y = FMath::Lerp(BladeFrom, BladeTo, T);
			double Width = FMath::Lerp(WidthAtHilt, WidthAtTip, T);
			// The kissaki: the last three sections narrow to the point.
			const int32 FromTip = Sections - i;
			if (FromTip < UE_ARRAY_COUNT(KissakiWidths))
			{
				Width = KissakiWidths[UE_ARRAY_COUNT(KissakiWidths) - 1 - FromTip];
			}
			const double Curve = Sori * FMath::Pow(T, SoriPower);
			// Spine on -Z, edge on +Z; the curve sweeps the whole blade back toward the spine.
			Spine.Add(FVector(Y * S, 0.0, (-Curve - Width * 0.5) * S * Flatten));
			Cutting.Add(FVector(Y * S, 0.0, (-Curve + Width * 0.5) * S * Flatten));
			Mid.Add(FVector(Y * S, 0.0, -Curve * S * Flatten));
		}
		const double Thick = Thickness * S * 0.5;
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
				Quad(Blade, Spine[i] + Off * SpineShare, Spine[i + 1] + Off * SpineShare, Mid[i + 1] + Off, Mid[i] + Off);
				Quad(Edge, Mid[i] + Off, Mid[i + 1] + Off, Cutting[i + 1], Cutting[i]);
			}
			// The spine's back.
			const int32 a = B.Vert(Blade, Spine[i] + FVector(0, -Thick * SpineShare, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 b = B.Vert(Blade, Spine[i] + FVector(0, Thick * SpineShare, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 c = B.Vert(Blade, Spine[i + 1] + FVector(0, Thick * SpineShare, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			const int32 d = B.Vert(Blade, Spine[i + 1] + FVector(0, -Thick * SpineShare, 0), FVector(0, 0, -1), FVector2D::ZeroVector, FVector2D::ZeroVector);
			B.Tri(Blade, a, b, c);
			B.Tri(Blade, a, c, d);
		}
		// Habaki, tsuba, grip and its wrap, kashira.
		B.Box(TEXT("Brass"), HabakiAt * S, FVector(HabakiHalfLength * S, Thick * HabakiThickness, HabakiHalfHeight * S), FVector2D::ZeroVector);
		B.Cylinder(TEXT("Guard"), FVector(GuardFrom * S, 0, 0), FVector(GuardTo * S, 0, 0), GuardRadius * S, GuardSides, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Grip"), FVector(GripFrom * S, 0, 0), FVector(GuardFrom * S, 0, 0), GripRadius * S, GripSides, FVector2D::ZeroVector);
		for (int32 i = 0; i < Wraps; ++i)
		{
			const double X = (WrapFrom + i * WrapPitch) * S;
			B.Ellipsoid(TEXT("Wrap"), FVector(X, 0, 0), WrapSize * S, 3, 10, FVector2D::ZeroVector);
		}
		B.Cylinder(TEXT("Brass"), FVector(PommelFrom * S, 0, 0), FVector(GripFrom * S, 0, 0), PommelRadius * S, GripSides, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("Katana"));
	}

	TArray<UMaterialInterface*> KatanaMaterials(UObject* Outer)
	{
		return MakeAll(Outer, KatanaShape::Looks);
	}

	UStaticMesh* RingMesh(UObject* Outer, double Inner, double OuterR, int32 Segments, bool bBar)
	{
		FMeshBuilder B;
		const int32 S = B.Section(TEXT("Ring"));
		const FVector N(-1, 0, 0);
		// An octagon turns by an eighth of a half-turn so it stands on a flat side, like a stop sign.
		const double Turn = Segments == 8 ? UE_DOUBLE_PI / 8.0 : 0.0;
		for (int32 i = 0; i < Segments; ++i)
		{
			const double A0 = 2.0 * UE_DOUBLE_PI * i / Segments + Turn;
			const double A1 = 2.0 * UE_DOUBLE_PI * (i + 1) / Segments + Turn;
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
			// The "no" bar across the octagon, at -45 degrees, as wide as the ring and just inside its rim.
			constexpr double BarReach = 0.97;
			const FVector Dir = FVector(0.0, FMath::Cos(-UE_DOUBLE_PI / 4.0), FMath::Sin(-UE_DOUBLE_PI / 4.0));
			const FVector Side = FVector(0.0, -Dir.Z, Dir.Y);
			const double Half = OuterR * BarReach, W = (OuterR - Inner) * 0.5;
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
