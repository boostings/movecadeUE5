#include "Games/Samurai/SamuraiDojoMeshes.h"

#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"

// Each prop's shape is a small table of its proportions (UE cm) followed by the code that builds it.

namespace MovecadeKart::Samurai::DojoMeshes
{
	using MovecadeKart::FMeshBuilder;

	namespace
	{
		namespace LanternShape
		{
			/** A half-ellipse of paper, every other rib standing proud. */
			constexpr int32 Ribs = 14;
			constexpr double Radius = 34.0;
			constexpr double HalfHeight = 46.0;
			constexpr double RibBulge = 1.2;
			constexpr int32 Segments = 24;
			/** Black caps top and bottom. */
			constexpr double CapInner = 40.0;
			constexpr double CapOuter = 50.0;
			constexpr double CapRadius = 20.0;
			constexpr int32 CapSides = 18;
		}

		namespace RidgeShape
		{
			constexpr int32 Steps = 48;
			constexpr double HalfWidth = 6000.0;
			constexpr double Base = -200.0;
			/** The crest: a mean height plus three octaves of sine (amplitude, frequency per cm, phase). */
			constexpr double MeanHeight = 600.0;
			constexpr double Octaves[3][3] = { { 520.0, 0.0011, 0.0 }, { 260.0, 0.0031, 1.3 }, { 120.0, 0.0083, 0.4 } };
		}

		namespace PineShape
		{
			const FVector TrunkBase(0, 0, 0);
			const FVector TrunkTop(8, 0, 320);
			constexpr double TrunkRadius = 16.0;
			constexpr double TrunkTopRadius = 9.0;
			constexpr int32 TrunkSides = 10;
			/** Each tier: height, radius, thickness; a lathe of 14 sides, leaning out and squashed in depth. */
			constexpr double Tiers[][3] = { { 150, 120, 26 }, { 210, 100, 24 }, { 265, 78, 22 }, { 312, 54, 22 } };
			constexpr int32 TierSides = 14;
			constexpr double TierLean = 0.12;
			const FVector TierScale(1.0, 0.8, 1.0);
		}

		namespace StoneShape
		{
			/** A lumpy disc's profile (radius, height) and its squash in depth. */
			const TArray<FVector2D> Profile = { { 0, -6 }, { 34, -6 }, { 40, 0 }, { 36, 5 }, { 18, 8 }, { 0, 8.5 } };
			constexpr int32 Sides = 12;
			const FVector Scale(1.0, 0.78, 1.0);
		}

		namespace KasagiShape
		{
			constexpr int32 Steps = 24;
			/** A 52 x 30 cm beam along Y, half length 100 cm, its ends rising 34 cm (a quartic sweep). */
			constexpr double HalfDepth = 26.0;
			constexpr double HalfHeight = 15.0;
			constexpr double HalfLength = 100.0;
			constexpr double EndRise = 34.0;
		}
	}

	UStaticMesh* Lantern(UObject* Outer)
	{
		using namespace LanternShape;
		FMeshBuilder B;
		TArray<FVector2D> Paper;
		for (int32 i = 0; i <= Ribs; ++i)
		{
			const double T = UE_DOUBLE_PI * i / Ribs;
			Paper.Add(FVector2D(FMath::Sin(T) * Radius + (i > 0 && i < Ribs && i % 2 == 0 ? RibBulge : 0.0), -FMath::Cos(T) * HalfHeight));
		}
		B.Lathe(TEXT("Paper"), Paper, Segments, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Cap"), FVector(0, 0, CapInner), FVector(0, 0, CapOuter), CapRadius, CapSides, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Cap"), FVector(0, 0, -CapOuter), FVector(0, 0, -CapInner), CapRadius, CapSides, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DojoLantern"));
	}

	UStaticMesh* Ridge(UObject* Outer)
	{
		using namespace RidgeShape;
		FMeshBuilder B;
		const int32 S = B.Section(TEXT("Ridge"));
		auto Height = [](double Y)
		{
			return MeanHeight + Octaves[0][0] * FMath::Sin(Y * Octaves[0][1]) + Octaves[1][0] * FMath::Sin(Y * Octaves[1][1] + Octaves[1][2])
				+ Octaves[2][0] * FMath::Sin(Y * Octaves[2][1] + Octaves[2][2]);
		};
		const FVector N(-1, 0, 0);
		for (int32 i = 0; i < Steps; ++i)
		{
			const double Y0 = FMath::Lerp(-HalfWidth, HalfWidth, static_cast<double>(i) / Steps);
			const double Y1 = FMath::Lerp(-HalfWidth, HalfWidth, static_cast<double>(i + 1) / Steps);
			const int32 a = B.Vert(S, FVector(0, Y0, Base), N, FVector2D(0, 1), FVector2D::ZeroVector);
			const int32 b = B.Vert(S, FVector(0, Y1, Base), N, FVector2D(1, 1), FVector2D::ZeroVector);
			const int32 c = B.Vert(S, FVector(0, Y1, Height(Y1)), N, FVector2D(1, 0), FVector2D::ZeroVector);
			const int32 d = B.Vert(S, FVector(0, Y0, Height(Y0)), N, FVector2D(0, 0), FVector2D::ZeroVector);
			B.Tri(S, a, b, c);
			B.Tri(S, a, c, d);
		}
		return B.Build(Outer, TEXT("DojoRidge"));
	}

	UStaticMesh* Pine(UObject* Outer)
	{
		using namespace PineShape;
		FMeshBuilder B;
		B.Limb(TEXT("Trunk"), TrunkBase, TrunkTop, TrunkRadius, TrunkTopRadius, TrunkSides, FVector2D::ZeroVector);
		for (const auto& T : Tiers)
		{
			// The tier's underside, its rim, its shoulder and its crown.
			B.Lathe(TEXT("Needles"), { { 0, T[0] - T[2] * 0.4 }, { T[1], T[0] - T[2] * 0.2 }, { T[1] * 0.8, T[0] + T[2] * 0.5 }, { 0, T[0] + T[2] } }, TierSides,
				FVector(T[1] * TierLean, 0, 0), FVector::UpVector, TierScale, FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("DojoPine"));
	}

	UStaticMesh* Stone(UObject* Outer)
	{
		using namespace StoneShape;
		FMeshBuilder B;
		B.Lathe(TEXT("Stone"), Profile, Sides, FVector::ZeroVector, FVector::UpVector, Scale, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DojoStone"));
	}

	UStaticMesh* Kasagi(UObject* Outer)
	{
		using namespace KasagiShape;
		FMeshBuilder B;
		const int32 S = B.Section(TEXT("Beam"));
		auto Rise = [](double U) { return EndRise * U * U * U * U; };
		for (int32 i = 0; i < Steps; ++i)
		{
			const double U0 = FMath::Lerp(-1.0, 1.0, static_cast<double>(i) / Steps), U1 = FMath::Lerp(-1.0, 1.0, static_cast<double>(i + 1) / Steps);
			const double H0 = Rise(U0), H1 = Rise(U1);
			const double Y0 = U0 * HalfLength, Y1 = U1 * HalfLength;
			for (int32 Face = 0; Face < 4; ++Face)
			{
				// Front, back, top, bottom: each face's normal and its four corners along the segment.
				FVector P[4];
				FVector N;
				switch (Face)
				{
				case 0: N = FVector(-1, 0, 0); P[0] = FVector(-HalfDepth, Y0, H0 - HalfHeight); P[1] = FVector(-HalfDepth, Y1, H1 - HalfHeight); P[2] = FVector(-HalfDepth, Y1, H1 + HalfHeight); P[3] = FVector(-HalfDepth, Y0, H0 + HalfHeight); break;
				case 1: N = FVector(1, 0, 0); P[0] = FVector(HalfDepth, Y0, H0 - HalfHeight); P[1] = FVector(HalfDepth, Y1, H1 - HalfHeight); P[2] = FVector(HalfDepth, Y1, H1 + HalfHeight); P[3] = FVector(HalfDepth, Y0, H0 + HalfHeight); break;
				case 2: N = FVector(0, 0, 1); P[0] = FVector(-HalfDepth, Y0, H0 + HalfHeight); P[1] = FVector(-HalfDepth, Y1, H1 + HalfHeight); P[2] = FVector(HalfDepth, Y1, H1 + HalfHeight); P[3] = FVector(HalfDepth, Y0, H0 + HalfHeight); break;
				default: N = FVector(0, 0, -1); P[0] = FVector(-HalfDepth, Y0, H0 - HalfHeight); P[1] = FVector(-HalfDepth, Y1, H1 - HalfHeight); P[2] = FVector(HalfDepth, Y1, H1 - HalfHeight); P[3] = FVector(HalfDepth, Y0, H0 - HalfHeight); break;
				}
				const int32 a = B.Vert(S, P[0], N, FVector2D(0, 0), FVector2D::ZeroVector);
				const int32 b = B.Vert(S, P[1], N, FVector2D(1, 0), FVector2D::ZeroVector);
				const int32 c = B.Vert(S, P[2], N, FVector2D(1, 1), FVector2D::ZeroVector);
				const int32 d = B.Vert(S, P[3], N, FVector2D(0, 1), FVector2D::ZeroVector);
				B.Tri(S, a, b, c);
				B.Tri(S, a, c, d);
			}
		}
		return B.Build(Outer, TEXT("DojoKasagi"));
	}
}
