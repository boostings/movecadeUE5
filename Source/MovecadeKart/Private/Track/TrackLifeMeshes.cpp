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
