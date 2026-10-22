#include "Track/TrackLifeMeshes.h"

#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "Math/RandomStream.h"

namespace
{
	using FLifeMeshBuilder = MovecadeKart::FMeshBuilder;

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

	/** A lawn flower: a 20 cm stem with two leaves, petals round a heart (cm). */
	namespace Flower
	{
		constexpr double StemHeight = 20.0;
		constexpr double StemLean = 0.08;
		/** Petals: from the heart out to the tip, widest at the middle, tipped up. */
		constexpr double PetalTilt = 0.28;
		constexpr double PetalBase = 0.8;
		constexpr double PetalMiddle = 3.0;
		constexpr double PetalTip = 5.6;
		constexpr double PetalHalfWidth = 1.6;
		const FVector HeartOffset(0.0, 0.0, 0.6);
		const FVector HeartRadii(1.9, 1.9, 1.1);
	}

	/**
	 * The spectator, built from ellipsoids and limbs (cm, facing +X, feet at the origin). Parts with bMirrored are
	 * built once per side with their Y flipped; bArm parts carry the side in UV1.y so the crowd material swings them.
	 * The order is the build order (and so the vertex order).
	 */
	namespace Spectator
	{
		enum class EShape : uint8 { Ellipsoid, Limb };
		enum class EPart : uint8 { Shirt, Skin, Trousers, Hair, Shoes, Dark };

		struct FPart
		{
			EShape Shape;
			EPart Part;
			bool bArm;
			/** Ellipsoid centre, or limb start. */
			FVector A;
			/** Ellipsoid radii, or limb end. */
			FVector B;
			/** Limb radii at the start and end. */
			double R0;
			double R1;
			/** Ellipsoid rings (unused for limbs) and segments around. */
			int32 Rings;
			int32 Segments;
		};

		const FPart Mirrored[] = {
			{ EShape::Ellipsoid, EPart::Shoes, false, FVector(4.0, 9.0, 4.5), FVector(11.0, 5.5, 4.5), 0.0, 0.0, 4, 8 },      // shoe
			{ EShape::Limb, EPart::Trousers, false, FVector(0.0, 9.0, 7.0), FVector(0.0, 8.5, 72.0), 7.0, 7.5, 0, 8 },       // leg
			{ EShape::Limb, EPart::Shirt, true, FVector(0.0, 21.0, 124.0), FVector(0.0, 23.0, 108.0), 6.6, 6.0, 0, 8 },     // sleeve
			{ EShape::Limb, EPart::Skin, true, FVector(0.0, 23.0, 110.0), FVector(0.5, 24.5, 80.0), 4.8, 4.2, 0, 8 },       // forearm
			{ EShape::Ellipsoid, EPart::Skin, true, FVector(1.0, 25.0, 74.0), FVector(4.6, 3.8, 6.0), 0.0, 0.0, 4, 8 },      // hand
			{ EShape::Ellipsoid, EPart::Dark, false, FVector(14.8, 5.6, 154.0), FVector(2.0, 2.0, 2.6), 0.0, 0.0, 3, 6 },    // eye
		};

		const FPart Centre[] = {
			{ EShape::Ellipsoid, EPart::Trousers, false, FVector(0.0, 0.0, 76.0), FVector(12.5, 17.0, 10.0), 0.0, 0.0, 5, 10 }, // hips
			{ EShape::Ellipsoid, EPart::Shirt, false, FVector(0.0, 0.0, 101.0), FVector(13.0, 19.0, 29.0), 0.0, 0.0, 7, 12 },   // torso
			{ EShape::Limb, EPart::Skin, false, FVector(0.0, 0.0, 124.0), FVector(0.0, 0.0, 138.0), 5.5, 5.5, 0, 8 },          // neck
			{ EShape::Ellipsoid, EPart::Skin, false, FVector(1.0, 0.0, 152.0), FVector(16.0, 15.0, 17.0), 0.0, 0.0, 7, 12 },    // head
			{ EShape::Ellipsoid, EPart::Hair, false, FVector(-1.5, 0.0, 157.0), FVector(16.4, 15.8, 14.6), 0.0, 0.0, 6, 12 },   // hair
		};
	}

	/** The butterfly's wing outline (cm), a fan from its root on the body: fore wing to the front, hind wing behind. */
	namespace Butterfly
	{
		const FVector2D Wing[] = { { 1.4, 0.4 }, { 2.9, 4.2 }, { 2.2, 6.6 }, { 0.2, 6.2 }, { -0.6, 3.6 }, { -2.4, 4.6 }, { -2.8, 2.4 }, { -1.6, 0.4 } };
		const FVector BodyRadii(2.2, 0.45, 0.45);
		constexpr double WingRoot = 0.4;
	}

	/** The grandstand's parts beyond its spec (three.js metres, like the web's box() helper). */
	namespace Stand
	{
		constexpr double BaseHeight = 3.2;
		constexpr double BaseDrop = -1.4;
		constexpr double Margin = 1.0;
		constexpr double FirstTierY = 0.2;
		constexpr double TierSink = 0.45;
		constexpr double SeatInset = 0.4;
		constexpr double SeatHeight = 0.18;
		constexpr double SeatDepth = 0.5;
		constexpr double SeatRise = 0.52;
		constexpr double SeatBack = 0.45;
		constexpr double BackExtra = 1.8;
		constexpr double BackThickness = 0.4;
		constexpr double RoofClearance = 3.6;
		constexpr double RoofThickness = 0.35;
		constexpr double RoofOverhang = 2.0;
		constexpr double RoofDepthExtra = 2.4;
		constexpr double RoofPitch = -0.08;
		constexpr double PostSize = 0.35;
		constexpr double RailHeight = 0.65;
		constexpr double RailBar = 0.08;
		constexpr double RailPost = 0.06;
		constexpr int32 RailSpans = 10;
		/** Spectators: one per 0.72 m along each tier, standing behind the seat strip (Park-Miller seed 7, about a fifth
		 *  of the places left empty, as the web filled them). */
		constexpr double SpotStep = 0.72;
		constexpr double SpotStartInset = 0.7;
		constexpr double SpotEndInset = 0.5;
		constexpr double EmptyChance = 0.22;
		constexpr double SpotJitter = 0.15;
		constexpr double SpotRise = 0.45;
		constexpr double SpotBack = 1.02;
		constexpr double SpotYaw = 90.0;
		constexpr int64 SpotSeed = 7;
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
		using namespace Flower;
		FLifeMeshBuilder B;
		const int32 S = B.Section(Slot0);
		// The stem and two leaves (blade part, so they colour like the lawn).
		Blade(B, FVector(0.0, 0.0, -2.0), FVector(1.0, 0.0, 0.0), StemHeight + 2.0, 1.4, StemLean, 0.02, 0.5);
		Blade(B, FVector(0.0, 0.0, -1.0), FVector(0.7, 0.7, 0.0).GetSafeNormal(), 9.0, 3.0, 0.7, 0.2, 0.2, FVector2D::ZeroVector, 0.0, 0.4);
		Blade(B, FVector(0.0, 0.0, -1.0), FVector(-0.6, -0.8, 0.0).GetSafeNormal(), 8.0, 3.0, 0.75, 0.2, 0.7, FVector2D::ZeroVector, 0.0, 0.35);
		const FVector Head(StemHeight * FMath::Sin(StemLean), 0.0, StemHeight * FMath::Cos(StemLean) + 0.5);
		// Petals: flat ovals round the heart, tipped a little up.
		for (int32 i = 0; i < Petals; ++i)
		{
			const double A = 2.0 * PI * i / Petals;
			const FVector Out(FMath::Cos(A), FMath::Sin(A), PetalTilt);
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Out).GetSafeNormal();
			const FVector Nrm = FVector::CrossProduct(Out, Side).GetSafeNormal() * -1.0;
			const FVector Up = Nrm.Z < 0.0 ? -Nrm : Nrm;
			const FVector2D Part(1.0, 0.0);
			const int32 Base = B.Vert(S, Head + Out * PetalBase, Up, FVector2D(0.5, 1.0), Part);
			const int32 L = B.Vert(S, Head + Out * PetalMiddle - Side * PetalHalfWidth, Up, FVector2D(0.0, 1.0), Part);
			const int32 Tip = B.Vert(S, Head + Out * PetalTip, Up, FVector2D(0.5, 1.0), Part);
			const int32 R = B.Vert(S, Head + Out * PetalMiddle + Side * PetalHalfWidth, Up, FVector2D(1.0, 1.0), Part);
			B.Tri(S, Base, L, Tip);
			B.Tri(S, Base, Tip, R);
		}
		B.Ellipsoid(Slot0, Head + HeartOffset, HeartRadii, 4, 8, FVector2D(2.0, 0.0));
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
		using namespace Spectator;
		FLifeMeshBuilder B;
		// UV1.x picks the part's colour in M_TrackCrowd; UV1.y the arm side.
		auto AddPart = [&B](const FPart& P, double Side)
		{
			const FVector A(P.A.X, P.A.Y * Side, P.A.Z);
			const FVector2D Uv1(static_cast<double>(P.Part), P.bArm ? Side : 0.0);
			if (P.Shape == EShape::Ellipsoid)
			{
				B.Ellipsoid(Slot0, A, P.B, P.Rings, P.Segments, Uv1);
			}
			else
			{
				B.Limb(Slot0, A, FVector(P.B.X, P.B.Y * Side, P.B.Z), P.R0, P.R1, P.Segments, Uv1);
			}
		};
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			for (const FPart& P : Mirrored)
			{
				AddPart(P, Side);
			}
		}
		for (const FPart& P : Centre)
		{
			AddPart(P, 1.0);
		}
		return B.Build(Outer, TEXT("Spectator"));
	}

	UStaticMesh* BuildButterflyMesh(UObject* Outer)
	{
		FLifeMeshBuilder B;
		const int32 S = B.Section(Slot0);
		B.Ellipsoid(Slot0, FVector::ZeroVector, Butterfly::BodyRadii, 3, 6, FVector2D::ZeroVector);
		constexpr int32 Num = UE_ARRAY_COUNT(Butterfly::Wing);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const FVector2D UV1(0.0, Side);
			const int32 Root = B.Vert(S, FVector(0.0, Butterfly::WingRoot * Side, 0.0), FVector::UpVector, FVector2D(0.0, 0.5), UV1);
			int32 Prev = INDEX_NONE;
			for (int32 i = 0; i < Num; ++i)
			{
				const FVector2D O = Butterfly::Wing[i];
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
		using namespace Stand;
		FLifeMeshBuilder B;
		const double W = Spec.Width, D = Spec.Tiers * Spec.TierDepth;
		// Built in three.js metres like the web's box() helper, then mapped to the prefab's mesh axes.
		auto Box = [&](FName Slot, double SX, double SY, double SZ, double X, double Y, double Z, double RotX = 0.0)
		{
			const FQuat Rot(FVector::XAxisVector, RotX);
			B.Box(Slot, FVector(X, Y, Z), FVector(SX, SY, SZ) * 0.5, FVector2D::ZeroVector, Rot, FVector(X, Y, Z), [](const FVector& P) { return PrefabAxes(P); });
		};
		Box(TEXT("Base"), W + Margin, BaseHeight, D + Margin, 0.0, BaseDrop, -D / 2.0 + 0.5);
		for (int32 t = 0; t < Spec.Tiers; ++t)
		{
			const double Y = FirstTierY + t * Spec.TierRise, Z = -t * Spec.TierDepth;
			Box(TEXT("Tiers"), W, Spec.TierRise, Spec.TierDepth, 0.0, Y + Spec.TierRise / 2.0 - TierSink, Z - Spec.TierDepth / 2.0);
			Box(t % 2 ? TEXT("SeatsA") : TEXT("SeatsB"), W - SeatInset, SeatHeight, SeatDepth, 0.0, Y + SeatRise, Z - SeatBack);
		}
		const double Top = FirstTierY + Spec.Tiers * Spec.TierRise;
		Box(TEXT("Back"), W + Margin, Top + BackExtra, BackThickness, 0.0, (Top + BackExtra) / 2.0 - 0.2, -D - 0.2);
		const double RoofY = Top + RoofClearance;
		Box(TEXT("Roof"), W + RoofOverhang, RoofThickness, D + RoofDepthExtra, 0.0, RoofY, -D / 2.0 + 0.7, RoofPitch);
		Box(TEXT("Trim"), W + RoofOverhang, 0.5, 0.25, 0.0, RoofY - 0.3, 1.8);
		for (const double X : { -W / 2.0, 0.0, W / 2.0 })
		{
			Box(TEXT("Roof"), PostSize, RoofY + 0.2, PostSize, X, RoofY / 2.0 - 0.1, -D - 0.4);
		}
		// A rail along the front of the lowest tier.
		Box(TEXT("Trim"), W, RailBar, RailBar, 0.0, RailHeight + 1.0, -RailPost);
		for (double X = -W / 2.0; X <= W / 2.0 + 1e-3; X += W / RailSpans)
		{
			Box(TEXT("Trim"), RailPost, 1.0, RailPost, X, RailHeight + 0.5, -RailPost);
		}
		return B.Build(Outer, TEXT("Grandstand"));
	}

	TArray<FSpectatorSpot> GrandstandSpectators(const FGrandstandSpec& Spec)
	{
		using namespace Stand;
		// The web's seeded generator (Park-Miller): the same places stay empty.
		int64 Seed = SpotSeed;
		auto Rand = [&Seed]()
		{
			Seed = (Seed * 16807) % 2147483647;
			return static_cast<double>(Seed) / 2147483647.0;
		};
		TArray<FSpectatorSpot> Spots;
		for (int32 t = 0; t < Spec.Tiers; ++t)
		{
			const double Y = FirstTierY + t * Spec.TierRise, Z = -t * Spec.TierDepth;
			for (double X = -Spec.Width / 2.0 + SpotStartInset; X < Spec.Width / 2.0 - SpotEndInset; X += SpotStep)
			{
				if (Rand() < EmptyChance)
				{
					continue;
				}
				Rand(); // colour
				Rand(); // height
				const double Jitter = (Rand() - 0.5) * SpotJitter;
				// Standing on the tier behind its seat strip, facing the road (local +Z).
				FSpectatorSpot& Spot = Spots.AddDefaulted_GetRef();
				Spot.Position = PrefabAxes(FVector(X + Jitter, Y + SpotRise, Z - SpotBack));
				Spot.Yaw = SpotYaw;
				Spot.Tier = t;
			}
		}
		return Spots;
	}
}
