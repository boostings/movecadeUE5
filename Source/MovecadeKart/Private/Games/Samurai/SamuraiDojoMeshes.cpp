#include "Games/Samurai/SamuraiDojoMeshes.h"

#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"

// The numbers below are the props' modelling (profiles and proportions in cm), not tuning.

namespace MovecadeKart::Samurai::DojoMeshes
{
	using MovecadeKart::FMeshBuilder;

	UStaticMesh* Lantern(UObject* Outer)
	{
		FMeshBuilder B;
		// The paper: a half-ellipse profile, every other rib standing proud.
		constexpr int32 Ribs = 14;
		TArray<FVector2D> Paper;
		for (int32 i = 0; i <= Ribs; ++i)
		{
			const double T = UE_DOUBLE_PI * i / Ribs;
			Paper.Add(FVector2D(FMath::Sin(T) * 34.0 + (i > 0 && i < Ribs && i % 2 == 0 ? 1.2 : 0.0), -FMath::Cos(T) * 46.0));
		}
		B.Lathe(TEXT("Paper"), Paper, 24, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Cap"), FVector(0, 0, 40), FVector(0, 0, 50), 20.0, 18, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Cap"), FVector(0, 0, -50), FVector(0, 0, -40), 20.0, 18, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DojoLantern"));
	}

	UStaticMesh* Ridge(UObject* Outer)
	{
		FMeshBuilder B;
		const int32 S = B.Section(TEXT("Ridge"));
		constexpr int32 Steps = 48;
		constexpr double HalfWidthCm = 6000.0;
		// Three octaves of sine: broad hills, shoulders and a ragged crest.
		auto Height = [](double Y) { return 600.0 + 520.0 * FMath::Sin(Y * 0.0011) + 260.0 * FMath::Sin(Y * 0.0031 + 1.3) + 120.0 * FMath::Sin(Y * 0.0083 + 0.4); };
		const FVector N(-1, 0, 0);
		for (int32 i = 0; i < Steps; ++i)
		{
			const double Y0 = FMath::Lerp(-HalfWidthCm, HalfWidthCm, static_cast<double>(i) / Steps);
			const double Y1 = FMath::Lerp(-HalfWidthCm, HalfWidthCm, static_cast<double>(i + 1) / Steps);
			const int32 a = B.Vert(S, FVector(0, Y0, -200), N, FVector2D(0, 1), FVector2D::ZeroVector);
			const int32 b = B.Vert(S, FVector(0, Y1, -200), N, FVector2D(1, 1), FVector2D::ZeroVector);
			const int32 c = B.Vert(S, FVector(0, Y1, Height(Y1)), N, FVector2D(1, 0), FVector2D::ZeroVector);
			const int32 d = B.Vert(S, FVector(0, Y0, Height(Y0)), N, FVector2D(0, 0), FVector2D::ZeroVector);
			B.Tri(S, a, b, c);
			B.Tri(S, a, c, d);
		}
		return B.Build(Outer, TEXT("DojoRidge"));
	}

	UStaticMesh* Pine(UObject* Outer)
	{
		FMeshBuilder B;
		B.Limb(TEXT("Trunk"), FVector(0, 0, 0), FVector(8, 0, 320), 16.0, 9.0, 10, FVector2D::ZeroVector);
		// Each tier: height, radius, thickness.
		const double Tiers[][3] = { { 150, 120, 26 }, { 210, 100, 24 }, { 265, 78, 22 }, { 312, 54, 22 } };
		for (const auto& T : Tiers)
		{
			B.Lathe(TEXT("Needles"), { { 0, T[0] - T[2] * 0.4 }, { T[1], T[0] - T[2] * 0.2 }, { T[1] * 0.8, T[0] + T[2] * 0.5 }, { 0, T[0] + T[2] } }, 14,
				FVector(T[1] * 0.12, 0, 0), FVector::UpVector, FVector(1.0, 0.8, 1.0), FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("DojoPine"));
	}

	UStaticMesh* Stone(UObject* Outer)
	{
		FMeshBuilder B;
		B.Lathe(TEXT("Stone"), { { 0, -6 }, { 34, -6 }, { 40, 0 }, { 36, 5 }, { 18, 8 }, { 0, 8.5 } }, 12, FVector::ZeroVector, FVector::UpVector, FVector(1.0, 0.78, 1.0),
			FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("DojoStone"));
	}

	UStaticMesh* Kasagi(UObject* Outer)
	{
		FMeshBuilder B;
		constexpr int32 Steps = 24;
		const int32 S = B.Section(TEXT("Beam"));
		// A 52 x 30 cm section along Y, half length 100, its ends rising 34 cm (a quartic sweep).
		auto Rise = [](double U) { return 34.0 * U * U * U * U; };
		for (int32 i = 0; i < Steps; ++i)
		{
			const double U0 = FMath::Lerp(-1.0, 1.0, static_cast<double>(i) / Steps), U1 = FMath::Lerp(-1.0, 1.0, static_cast<double>(i + 1) / Steps);
			const double H0 = Rise(U0), H1 = Rise(U1);
			for (int32 Face = 0; Face < 4; ++Face)
			{
				// Front, back, top, bottom.
				FVector P[4];
				FVector N;
				switch (Face)
				{
				case 0: N = FVector(-1, 0, 0); P[0] = FVector(-26, U0 * 100, H0 - 15); P[1] = FVector(-26, U1 * 100, H1 - 15); P[2] = FVector(-26, U1 * 100, H1 + 15); P[3] = FVector(-26, U0 * 100, H0 + 15); break;
				case 1: N = FVector(1, 0, 0); P[0] = FVector(26, U0 * 100, H0 - 15); P[1] = FVector(26, U1 * 100, H1 - 15); P[2] = FVector(26, U1 * 100, H1 + 15); P[3] = FVector(26, U0 * 100, H0 + 15); break;
				case 2: N = FVector(0, 0, 1); P[0] = FVector(-26, U0 * 100, H0 + 15); P[1] = FVector(-26, U1 * 100, H1 + 15); P[2] = FVector(26, U1 * 100, H1 + 15); P[3] = FVector(26, U0 * 100, H0 + 15); break;
				default: N = FVector(0, 0, -1); P[0] = FVector(-26, U0 * 100, H0 - 15); P[1] = FVector(-26, U1 * 100, H1 - 15); P[2] = FVector(26, U1 * 100, H1 - 15); P[3] = FVector(26, U0 * 100, H0 - 15); break;
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
