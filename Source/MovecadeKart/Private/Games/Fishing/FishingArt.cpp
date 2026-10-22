#include "Games/Fishing/FishingArt.h"

#include "Art/KartMaterialCache.h"
#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Games/Fishing/FishingRules.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace MovecadeKart::Fishing::Art
{
	namespace
	{
		using MovecadeKart::FMeshBuilder;

		enum class EPattern : uint8 { Plain, Bars, Stripe, Spots, Scales, GillSpot, Mottle, Scutes };

		struct FShape
		{
			double Height;
			double Width;
			double Peak;
			EPattern Pattern;
			double Tail;
			double Dorsal;
			bool bWhiskers;
			double Nose;
		};

		const FShape& ShapeOf(int32 Species)
		{
			static const FShape Shapes[] = {
				{ 0.17, 0.075, 0.55, EPattern::Bars, 0.18, 0.13, false, 0.8 },     // perch
				{ 0.25, 0.07, 0.5, EPattern::GillSpot, 0.16, 0.12, false, 0.7 },  // bluegill
				{ 0.13, 0.07, 0.55, EPattern::Stripe, 0.17, 0.07, false, 0.8 },   // trout
				{ 0.19, 0.1, 0.55, EPattern::Scales, 0.2, 0.09, false, 0.7 },     // carp
				{ 0.085, 0.065, 0.45, EPattern::Spots, 0.15, 0.07, false, 1.2 },  // pike
				{ 0.11, 0.115, 0.72, EPattern::Mottle, 0.14, 0.06, true, 0.5 },   // catfish
				{ 0.09, 0.08, 0.6, EPattern::Scutes, 0.2, 0.06, false, 1.6 },     // sturgeon
				{ 0.2, 0.1, 0.55, EPattern::Scales, 0.22, 0.1, false, 0.7 },      // goldie
			};
			return Shapes[FMath::Clamp(Species, 0, static_cast<int32>(UE_ARRAY_COUNT(Shapes)) - 1)];
		}

		/** The body's thickness along its length, t from the tail (0) to the nose (1). */
		double Profile(const FShape& S, double T)
		{
			if (T <= 0.0 || T >= 1.0)
			{
				return 0.0;
			}
			if (T < 0.03)
			{
				return 0.16 * T / 0.03;
			}
			if (T < S.Peak)
			{
				return 0.16 + 0.84 * FMath::Pow(FMath::Sin(HALF_PI * (T - 0.03) / (S.Peak - 0.03)), 1.3);
			}
			return FMath::Pow(FMath::Cos(HALF_PI * (T - S.Peak) / (1.0 - S.Peak)), FMath::Max(0.35, 0.75 - 0.25 * S.Nose));
		}

		uint32 Hash(int32 A, int32 B)
		{
			uint32 H = static_cast<uint32>(A) * 73856093u ^ static_cast<uint32>(B) * 19349663u;
			H ^= H >> 13;
			H *= 0x5bd1e995u;
			return H ^ (H >> 15);
		}

		/** A flat triangle visible from both sides. */
		void Flat(FMeshBuilder& B, int32 S, const FVector& A, const FVector& Bp, const FVector& C)
		{
			const FVector N = FVector::CrossProduct(Bp - A, C - A).GetSafeNormal();
			for (const double Side : { 1.0, -1.0 })
			{
				const int32 I0 = B.Vert(S, A, N * Side, FVector2D(0.0, 0.0), FVector2D::ZeroVector);
				const int32 I1 = B.Vert(S, Bp, N * Side, FVector2D(1.0, 0.0), FVector2D::ZeroVector);
				const int32 I2 = B.Vert(S, C, N * Side, FVector2D(0.0, 1.0), FVector2D::ZeroVector);
				B.Tri(S, I0, I1, I2);
			}
		}

		/** An elliptical disc facing Up (+Z or -Z). */
		void Disc(FMeshBuilder& B, FName Slot, const FVector& C, double Rx, double Ry, double Up, int32 Segments = 24)
		{
			const int32 S = B.Section(Slot);
			const FVector N(0.0, 0.0, Up);
			const int32 Mid = B.Vert(S, C, N, FVector2D(0.5, 0.5), FVector2D::ZeroVector);
			TArray<int32> Ring;
			for (int32 i = 0; i <= Segments; ++i)
			{
				const double A = 2.0 * PI * i / Segments;
				Ring.Add(B.Vert(S, C + FVector(Rx * FMath::Cos(A), Ry * FMath::Sin(A), 0.0), N, FVector2D(0.5 + 0.5 * FMath::Cos(A), 0.5 + 0.5 * FMath::Sin(A)), FVector2D::ZeroVector));
			}
			for (int32 i = 0; i < Segments; ++i)
			{
				B.Tri(S, Mid, Ring[i], Ring[i + 1]);
			}
		}

		void BuildFishBody(FMeshBuilder& B, const FShape& S)
		{
			// Slot order: Back, Flank, Belly, Pattern, Fin, Eye.
			const int32 Back = B.Section(TEXT("Back"));
			const int32 Flank = B.Section(TEXT("Flank"));
			const int32 Belly = B.Section(TEXT("Belly"));
			const int32 Pattern = B.Section(TEXT("Pattern"));
			const int32 Fin = B.Section(TEXT("Fin"));
			B.Section(TEXT("Eye"));
			const int32 Rings = 30, Around = 22;
			auto Point = [&S](double T, double A, FVector& N)
			{
				const double F = Profile(S, T);
				const double E = 0.002;
				const double Fp = (Profile(S, FMath::Min(1.0, T + E)) - Profile(S, FMath::Max(0.0, T - E))) / (2.0 * E);
				const double C = FMath::Cos(A), Sn = FMath::Sin(A);
				// A slightly deeper belly than back, like a real fish.
				const double H = S.Height * (Sn < 0.0 ? 1.08 : 1.0);
				N = FVector(-S.Width * H * Fp, H * C, S.Width * Sn).GetSafeNormal();
				if (F < 1e-4)
				{
					N = FVector(T > 0.5 ? 1.0 : -1.0, 0.0, 0.0);
				}
				return FVector((T - 0.5) * 100.0, S.Width * 100.0 * F * C, H * 100.0 * F * Sn);
			};
			for (int32 i = 0; i < Rings; ++i)
			{
				const double T0 = static_cast<double>(i) / Rings, T1 = static_cast<double>(i + 1) / Rings;
				for (int32 j = 0; j < Around; ++j)
				{
					const double A0 = 2.0 * PI * j / Around, A1 = 2.0 * PI * (j + 1) / Around;
					const double Tm = (T0 + T1) / 2.0, Sm = FMath::Sin((A0 + A1) / 2.0);
					int32 Sec = Sm > 0.55 ? Back : (Sm < -0.45 ? Belly : Flank);
					const bool bBody = Tm > 0.1 && Tm < 0.88;
					switch (S.Pattern)
					{
					case EPattern::Bars: if (bBody && Sm > -0.3 && FMath::Frac(Tm * 7.0) < 0.38) Sec = Pattern; break;
					case EPattern::Stripe: if (bBody && FMath::Abs(Sm) < 0.2) Sec = Pattern; break;
					case EPattern::Spots: if (bBody && Sm > -0.4 && Hash(i, j) % 6 == 0) Sec = Pattern; break;
					case EPattern::Scales: if (bBody && Sec == Flank && (i + j) % 2 == 0) Sec = Pattern; break;
					case EPattern::GillSpot: if (Tm > 0.7 && Tm < 0.77 && FMath::Abs(Sm) < 0.35) Sec = Pattern; break;
					case EPattern::Mottle: if (bBody && Sm > -0.2 && Hash(i, j) % 5 == 0) Sec = Pattern; break;
					case EPattern::Scutes: if (bBody && (Sm > 0.9 || FMath::Abs(Sm) < 0.1) && FMath::Frac(Tm * 14.0) < 0.45) Sec = Pattern; break;
					default: break;
					}
					FVector N00, N01, N11, N10;
					const FVector P00 = Point(T0, A0, N00), P01 = Point(T0, A1, N01), P11 = Point(T1, A1, N11), P10 = Point(T1, A0, N10);
					const int32 I0 = B.Vert(Sec, P00, N00, FVector2D(T0, A0 / (2.0 * PI)), FVector2D::ZeroVector);
					const int32 I1 = B.Vert(Sec, P01, N01, FVector2D(T0, A1 / (2.0 * PI)), FVector2D::ZeroVector);
					const int32 I2 = B.Vert(Sec, P11, N11, FVector2D(T1, A1 / (2.0 * PI)), FVector2D::ZeroVector);
					const int32 I3 = B.Vert(Sec, P10, N10, FVector2D(T1, A0 / (2.0 * PI)), FVector2D::ZeroVector);
					if (Profile(S, T0) > 1e-4)
					{
						B.Tri(Sec, I0, I1, I2);
					}
					if (Profile(S, T1) > 1e-4 || Profile(S, T0) > 1e-4)
					{
						B.Tri(Sec, I0, I2, I3);
					}
				}
			}
			// The tail: forked, flat in the XZ plane.
			const double T = S.Tail * 100.0;
			const double Root = S.Height * 100.0 * 0.16;
			const FVector RootTop(-49.0, 0.0, Root), RootBottom(-49.0, 0.0, -Root);
			const FVector TipTop(-50.0 - T, 0.0, T * 0.85), TipBottom(-50.0 - T, 0.0, -T * 0.85), Notch(-50.0 - T * 0.55, 0.0, 0.0);
			Flat(B, Fin, RootTop, TipTop, Notch);
			Flat(B, Fin, RootTop, Notch, RootBottom);
			Flat(B, Fin, Notch, TipBottom, RootBottom);
			// The dorsal fin along the back, and a small one underneath.
			const double D = S.Dorsal * 100.0;
			const double Ta = S.Pattern == EPattern::Spots ? 0.18 : 0.36, Tb = S.Pattern == EPattern::Spots ? 0.32 : 0.66;
			const FVector DA((Ta - 0.5) * 100.0, 0.0, S.Height * 100.0 * Profile(S, Ta) * 0.92);
			const FVector DB((Tb - 0.5) * 100.0, 0.0, S.Height * 100.0 * Profile(S, Tb) * 0.92);
			const FVector DTip(((Ta + Tb) / 2.0 - 0.5) * 100.0 - D * 0.3, 0.0, FMath::Max(DA.Z, DB.Z) + D);
			Flat(B, Fin, DA, DTip, DB);
			const FVector AA((0.22 - 0.5) * 100.0, 0.0, -S.Height * 108.0 * Profile(S, 0.22) * 0.9);
			const FVector AB((0.34 - 0.5) * 100.0, 0.0, -S.Height * 108.0 * Profile(S, 0.34) * 0.9);
			Flat(B, Fin, AA, AA + FVector(-D * 0.3, 0.0, -D * 0.55), AB);
			// Pectoral fins behind the gills, swept back and out.
			for (const double Side : { -1.0, 1.0 })
			{
				const double Tp = 0.72;
				const FVector Base((Tp - 0.5) * 100.0, Side * S.Width * 100.0 * Profile(S, Tp) * 0.9, -S.Height * 30.0);
				Flat(B, Fin, Base, Base + FVector(-D * 0.9, Side * D * 0.5, -D * 0.2), Base + FVector(-D * 0.2, Side * D * 0.25, -D * 0.6));
			}
			if (S.bWhiskers)
			{
				for (const double Side : { -1.0, 1.0 })
				{
					for (const double Up : { -0.4, 0.2 })
					{
						const FQuat Q = FRotator(-Up * 30.0, Side * 35.0, 0.0).Quaternion();
						B.Box(TEXT("Fin"), FVector(56.0, Side * 4.0, Up * 4.0), FVector(9.0, 0.6, 0.6), FVector2D::ZeroVector, Q, FVector(48.0, Side * 3.0, Up * 4.0));
					}
				}
			}
			// The eyes.
			const double Te = 0.86;
			const double Ey = S.Width * 100.0 * Profile(S, Te) * 0.8, Ez = S.Height * 100.0 * Profile(S, Te) * 0.25;
			const double Er = FMath::Clamp(S.Height * 14.0, 1.4, 3.0);
			for (const double Side : { -1.0, 1.0 })
			{
				B.Ellipsoid(TEXT("Eye"), FVector((Te - 0.5) * 100.0, Side * Ey, Ez), FVector(Er, Er * 0.7, Er), 6, 10, FVector2D::ZeroVector);
			}
		}

		void BuildBoot(FMeshBuilder& B)
		{
			B.Section(TEXT("Back"));
			B.Section(TEXT("Flank"));
			B.Section(TEXT("Belly"));
			B.Section(TEXT("Pattern"));
			B.Section(TEXT("Fin"));
			B.Section(TEXT("Eye"));
			B.Box(TEXT("Belly"), FVector(0.0, 0.0, -22.0), FVector(48.0, 15.0, 4.0), FVector2D::ZeroVector);
			B.Box(TEXT("Flank"), FVector(4.0, 0.0, -8.0), FVector(42.0, 14.0, 11.0), FVector2D::ZeroVector);
			B.Ellipsoid(TEXT("Flank"), FVector(38.0, 0.0, -10.0), FVector(14.0, 14.0, 12.0), 6, 12, FVector2D::ZeroVector);
			B.Box(TEXT("Back"), FVector(-28.0, 0.0, 28.0), FVector(17.0, 15.0, 30.0), FVector2D::ZeroVector, FRotator(-8.0, 0.0, 0.0).Quaternion(), FVector(-28.0, 0.0, 0.0));
			B.Box(TEXT("Fin"), FVector(-6.0, 0.0, 8.0), FVector(14.0, 10.0, 3.0), FVector2D::ZeroVector, FRotator(20.0, 0.0, 0.0).Quaternion(), FVector(-6.0, 0.0, 8.0));
			for (int32 i = 0; i < 4; ++i)
			{
				B.Box(TEXT("Pattern"), FVector(-14.0 + i * 7.0, 0.0, 4.0 + i * 1.5), FVector(1.2, 12.0, 1.0), FVector2D::ZeroVector);
			}
		}

		void BuildTyre(FMeshBuilder& B)
		{
			B.Section(TEXT("Back"));
			B.Section(TEXT("Flank"));
			B.Section(TEXT("Belly"));
			B.Section(TEXT("Pattern"));
			B.Section(TEXT("Fin"));
			B.Section(TEXT("Eye"));
			// A torus about Y (the tyre seen side-on), then the rim, the hub and spokes.
			TArray<FVector2D> Ring;
			for (int32 i = 0; i <= 14; ++i)
			{
				const double A = 2.0 * PI * i / 14.0;
				Ring.Add(FVector2D(40.0 + 9.0 * FMath::Cos(A), 9.0 * FMath::Sin(A)));
			}
			B.Lathe(TEXT("Back"), Ring, 28, FVector::ZeroVector, FVector::RightVector, FVector::OneVector, FVector2D::ZeroVector);
			TArray<FVector2D> Rim;
			for (int32 i = 0; i <= 10; ++i)
			{
				const double A = 2.0 * PI * i / 10.0;
				Rim.Add(FVector2D(31.0 + 2.0 * FMath::Cos(A), 2.0 * FMath::Sin(A)));
			}
			B.Lathe(TEXT("Flank"), Rim, 28, FVector::ZeroVector, FVector::RightVector, FVector::OneVector, FVector2D::ZeroVector);
			B.Cylinder(TEXT("Pattern"), FVector(0.0, -4.0, 0.0), FVector(0.0, 4.0, 0.0), 5.0, 12, FVector2D::ZeroVector);
			for (int32 k = 0; k < 12; ++k)
			{
				const double A = PI * k / 12.0;
				B.Box(TEXT("Fin"), FVector::ZeroVector, FVector(31.0, 0.3, 0.3), FVector2D::ZeroVector, FQuat(FVector::RightVector, A));
			}
		}

		void BuildDuck(FMeshBuilder& B)
		{
			B.Section(TEXT("Back"));
			B.Section(TEXT("Flank"));
			B.Section(TEXT("Belly"));
			B.Section(TEXT("Pattern"));
			B.Section(TEXT("Fin"));
			B.Section(TEXT("Eye"));
			B.Ellipsoid(TEXT("Flank"), FVector(-6.0, 0.0, -6.0), FVector(42.0, 32.0, 26.0), 10, 16, FVector2D::ZeroVector);
			B.Ellipsoid(TEXT("Flank"), FVector(24.0, 0.0, 28.0), FVector(19.0, 18.0, 19.0), 10, 16, FVector2D::ZeroVector);
			B.Ellipsoid(TEXT("Back"), FVector(-44.0, 0.0, 6.0), FVector(10.0, 8.0, 12.0), 6, 10, FVector2D::ZeroVector);
			for (const double Side : { -1.0, 1.0 })
			{
				B.Ellipsoid(TEXT("Back"), FVector(-8.0, Side * 26.0, 0.0), FVector(24.0, 8.0, 13.0), 6, 12, FVector2D::ZeroVector);
				B.Ellipsoid(TEXT("Eye"), FVector(36.0, Side * 11.0, 34.0), FVector(3.0, 2.4, 3.6), 6, 10, FVector2D::ZeroVector);
			}
			B.Ellipsoid(TEXT("Pattern"), FVector(44.0, 0.0, 24.0), FVector(11.0, 9.0, 4.0), 6, 12, FVector2D::ZeroVector);
		}
	}

	const FFishLook& LookOf(int32 Species)
	{
		static const FFishLook Looks[] = {
			{ TEXT("#3f5a2a"), TEXT("#c2b23a"), TEXT("#f1e6b0"), TEXT("#2d3a1a"), TEXT("#e0743a"), 0.0, 0.0125 },  // perch
			{ TEXT("#2e4a5e"), TEXT("#5f9a8f"), TEXT("#f0a64a"), TEXT("#14202b"), TEXT("#3d5c6b"), 0.0, 0.016 },   // bluegill
			{ TEXT("#4c5a3c"), TEXT("#bcc4b4"), TEXT("#f2eee2"), TEXT("#ea6f86"), TEXT("#8a8f78"), 0.0, 0.0105 },  // trout
			{ TEXT("#6b4a1e"), TEXT("#c08a3a"), TEXT("#efd59a"), TEXT("#a26a22"), TEXT("#8e5a2a"), 0.0, 0.0145 },  // carp
			{ TEXT("#2f4a2a"), TEXT("#5d7d3e"), TEXT("#e8e6c8"), TEXT("#e6dc8a"), TEXT("#8a6a3a"), 0.0, 0.0065 },  // pike
			{ TEXT("#33322e"), TEXT("#6b665a"), TEXT("#d8d2c0"), TEXT("#24231f"), TEXT("#4a463c"), 0.0, 0.0095 },  // catfish
			{ TEXT("#4a4f55"), TEXT("#8a9096"), TEXT("#e6e2d8"), TEXT("#e0dcd0"), TEXT("#5a5f66"), 0.0, 0.0055 },  // sturgeon
			{ TEXT("#d88a10"), TEXT("#ffc22e"), TEXT("#fff0b0"), TEXT("#ffa514"), TEXT("#ff7a1a"), 0.7, 0.0145 },  // goldie
			{ TEXT("#4a3220"), TEXT("#6b4a2e"), TEXT("#262220"), TEXT("#d8c8a0"), TEXT("#5a3e26"), 0.0, 1.0 },     // boot
			{ TEXT("#1c1c1e"), TEXT("#a0a6ac"), TEXT("#1c1c1e"), TEXT("#c0c4c8"), TEXT("#8a9096"), 0.6, 1.0 },     // tyre
			{ TEXT("#f2b81a"), TEXT("#ffd52e"), TEXT("#ffe680"), TEXT("#ff8a1a"), TEXT("#ffd52e"), 0.0, 1.0 },     // duck
		};
		return Looks[FMath::Clamp(Species, 0, static_cast<int32>(UE_ARRAY_COUNT(Looks)) - 1)];
	}

	double LengthCm(int32 Species, int32 WeightG)
	{
		const TArray<FSpecies>& All = Fishing::Species();
		if (All.IsValidIndex(Species) && All[Species].Kind == EKind::Junk)
		{
			const TCHAR* Id = All[Species].Id;
			return FCString::Strcmp(Id, TEXT("tyre")) == 0 ? 62.0 : (FCString::Strcmp(Id, TEXT("duck")) == 0 ? 16.0 : 30.0);
		}
		return FMath::Clamp(FMath::Pow(FMath::Max(50, WeightG) / LookOf(Species).Density, 1.0 / 3.0), 12.0, 170.0);
	}

	UStaticMesh* BuildFish(UObject* Outer, int32 Species)
	{
		FMeshBuilder B;
		const TArray<FSpecies>& All = Fishing::Species();
		const TCHAR* Id = All.IsValidIndex(Species) ? All[Species].Id : TEXT("perch");
		if (FCString::Strcmp(Id, TEXT("boot")) == 0)
		{
			BuildBoot(B);
		}
		else if (FCString::Strcmp(Id, TEXT("tyre")) == 0)
		{
			BuildTyre(B);
		}
		else if (FCString::Strcmp(Id, TEXT("duck")) == 0)
		{
			BuildDuck(B);
		}
		else
		{
			BuildFishBody(B, ShapeOf(Species));
		}
		return B.Build(Outer, *FString::Printf(TEXT("Fish_%s"), Id));
	}

	void DressFish(FKartMaterialCache& Materials, UObject* Outer, UStaticMeshComponent* Component, int32 Species)
	{
		using MovecadeKart::Art::Hex;
		const FFishLook& L = LookOf(Species);
		// Wet skin is glossy; the belly a little less, the fins less again, the eye glassy.
		const double Wet = 0.28;
		Component->SetMaterialByName(TEXT("Back"), Materials.Plain(Outer, Hex(L.Back), Wet, L.Metallic));
		Component->SetMaterialByName(TEXT("Flank"), Materials.Plain(Outer, Hex(L.Flank), Wet, L.Metallic));
		Component->SetMaterialByName(TEXT("Belly"), Materials.Plain(Outer, Hex(L.Belly), Wet + 0.1, L.Metallic * 0.5));
		Component->SetMaterialByName(TEXT("Pattern"), Materials.Plain(Outer, Hex(L.Pattern), Wet, L.Metallic));
		Component->SetMaterialByName(TEXT("Fin"), Materials.Plain(Outer, Hex(L.Fin), 0.45, L.Metallic * 0.5));
		Component->SetMaterialByName(TEXT("Eye"), Materials.Plain(Outer, Hex(TEXT("#0c0c10")), 0.08));
	}

	UStaticMesh* BuildRodHandle(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Cork"));
		B.Section(TEXT("Reel"));
		B.Section(TEXT("Metal"));
		B.Cylinder(TEXT("Cork"), FVector(0.0, 0.0, 0.0), FVector(26.0, 0.0, 0.0), 1.8, 12, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Cork"), FVector(0.0, 0.0, 0.0), FVector(1.2, 1.9, 1.9), 4, 10, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Metal"), FVector(26.0, 0.0, 0.0), FVector(40.0, 0.0, 0.0), 1.15, 10, FVector2D::ZeroVector);
		// The spinning reel under the seat: its foot, the spool and body, the crank arm and knob.
		B.Box(TEXT("Metal"), FVector(32.0, 0.0, -2.6), FVector(2.4, 0.6, 1.6), FVector2D::ZeroVector);
		B.Cylinder(TEXT("Reel"), FVector(27.0, 0.0, -6.5), FVector(36.0, 0.0, -6.5), 3.6, 14, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Metal"), FVector(36.0, 0.0, -6.5), FVector(39.0, 0.0, -6.5), 2.6, 12, FVector2D::ZeroVector);
		B.Box(TEXT("Metal"), FVector(31.0, -4.6, -6.5), FVector(0.6, 1.2, 0.6), FVector2D::ZeroVector);
		B.Box(TEXT("Metal"), FVector(31.0, -5.6, -4.0), FVector(0.5, 0.5, 2.8), FVector2D::ZeroVector);
		B.Cylinder(TEXT("Reel"), FVector(31.0, -5.6, -1.3), FVector(31.0, -8.0, -1.3), 0.9, 8, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("FishingRodHandle"));
	}

	UStaticMesh* BuildRodSegment(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Blank"));
		B.Section(TEXT("Guide"));
		B.Lathe(TEXT("Blank"), { FVector2D(1.0, 0.0), FVector2D(0.82, 100.0) }, 10, FVector::ZeroVector, FVector::ForwardVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Box(TEXT("Guide"), FVector(97.0, 0.0, -1.8), FVector(1.2, 0.3, 1.1), FVector2D::ZeroVector);
		B.Box(TEXT("Guide"), FVector(97.0, 0.0, -2.9), FVector(1.2, 1.2, 0.3), FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("FishingRodSegment"));
	}

	UStaticMesh* BuildRodLine(UObject* Outer)
	{
		FMeshBuilder B;
		B.Cylinder(TEXT("Line"), FVector::ZeroVector, FVector(100.0, 0.0, 0.0), 1.0, 6, FVector2D::ZeroVector, false);
		return B.Build(Outer, TEXT("FishingLine"));
	}

	UStaticMesh* BuildBobber(UObject* Outer)
	{
		FMeshBuilder B;
		TArray<FVector2D> Top, Bottom;
		const double R = 6.5;
		for (int32 i = 0; i <= 8; ++i)
		{
			const double A = HALF_PI * i / 8.0;
			Bottom.Add(FVector2D(R * FMath::Sin(A), 1.5 - R * FMath::Cos(A)));
			Top.Add(FVector2D(R * FMath::Cos(A), 1.5 + R * FMath::Sin(A)));
		}
		B.Lathe(TEXT("Top"), Top, 18, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Lathe(TEXT("Bottom"), Bottom, 18, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Cylinder(TEXT("Stick"), FVector(0.0, 0.0, 7.0), FVector(0.0, 0.0, 17.0), 0.8, 8, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Top"), FVector(0.0, 0.0, 17.5), FVector(1.4, 1.4, 1.4), 4, 8, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("FishingBobber"));
	}

	UStaticMesh* BuildPine(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Trunk"));
		B.Section(TEXT("Needles"));
		B.Cylinder(TEXT("Trunk"), FVector(0.0, 0.0, -40.0), FVector(0.0, 0.0, 260.0), 22.0, 8, FVector2D::ZeroVector);
		const double Bases[] = { 120.0, 330.0, 520.0, 690.0 };
		const double Radii[] = { 230.0, 185.0, 135.0, 85.0 };
		const double Heights[] = { 330.0, 290.0, 240.0, 210.0 };
		for (int32 i = 0; i < 4; ++i)
		{
			B.Lathe(TEXT("Needles"), { FVector2D(0.0, Bases[i] - 10.0), FVector2D(Radii[i] * 0.55, Bases[i] - 30.0), FVector2D(Radii[i], Bases[i] + 10.0), FVector2D(Radii[i] * 0.5, Bases[i] + Heights[i] * 0.5), FVector2D(0.0, Bases[i] + Heights[i]) },
				10, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("FishingPine"));
	}

	UStaticMesh* BuildReed(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Stalk"));
		B.Section(TEXT("Head"));
		B.Lathe(TEXT("Stalk"), { FVector2D(1.3, -60.0), FVector2D(1.0, 90.0), FVector2D(0.35, 170.0), FVector2D(0.0, 172.0) }, 6, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Head"), FVector(0.0, 0.0, 130.0), FVector(2.6, 2.6, 11.0), 6, 8, FVector2D::ZeroVector);
		B.Box(TEXT("Stalk"), FVector(4.0, 0.0, 70.0), FVector(0.4, 1.6, 70.0), FVector2D::ZeroVector, FRotator(-12.0, 0.0, 0.0).Quaternion(), FVector(0.0, 0.0, 0.0));
		B.Box(TEXT("Stalk"), FVector(-3.0, 2.0, 55.0), FVector(0.4, 1.4, 55.0), FVector2D::ZeroVector, FRotator(14.0, 30.0, 0.0).Quaternion(), FVector(0.0, 0.0, 0.0));
		return B.Build(Outer, TEXT("FishingReed"));
	}

	UStaticMesh* BuildLilyPad(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Pad"));
		B.Section(TEXT("Flower"));
		Disc(B, TEXT("Pad"), FVector(0.0, 0.0, 0.8), 30.0, 30.0, 1.0, 20);
		for (int32 k = 0; k < 7; ++k)
		{
			const double A = 2.0 * PI * k / 7.0;
			const FQuat Q = FRotator(-38.0, FMath::RadiansToDegrees(A), 0.0).Quaternion();
			const FVector At = FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * 5.0 + FVector(0.0, 0.0, 6.0);
			B.Box(TEXT("Flower"), At, FVector(6.0, 2.6, 0.6), FVector2D::ZeroVector, Q, At);
		}
		B.Ellipsoid(TEXT("Flower"), FVector(0.0, 0.0, 7.0), FVector(3.0, 3.0, 3.0), 4, 8, FVector2D::ZeroVector);
		return B.Build(Outer, TEXT("FishingLily"));
	}

	UStaticMesh* BuildBoat(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Hull"));
		B.Section(TEXT("Trim"));
		B.Section(TEXT("Seat"));
		TArray<FVector2D> Hull;
		for (int32 i = 0; i <= 8; ++i)
		{
			const double A = -HALF_PI + HALF_PI * i / 8.0;
			Hull.Add(FVector2D(FMath::Cos(A), FMath::Sin(A)));
		}
		B.Lathe(TEXT("Hull"), Hull, 24, FVector::ZeroVector, FVector::UpVector, FVector(190.0, 64.0, 42.0), FVector2D::ZeroVector);
		Disc(B, TEXT("Trim"), FVector(0.0, 0.0, -6.0), 184.0, 60.0, 1.0, 28);
		B.Box(TEXT("Seat"), FVector(20.0, 0.0, 4.0), FVector(14.0, 54.0, 2.5), FVector2D::ZeroVector);
		B.Box(TEXT("Seat"), FVector(-90.0, 0.0, 4.0), FVector(12.0, 40.0, 2.5), FVector2D::ZeroVector);
		for (const double Side : { -1.0, 1.0 })
		{
			B.Box(TEXT("Seat"), FVector(-10.0, Side * 30.0, 8.0), FVector(70.0, 1.2, 1.2), FVector2D::ZeroVector, FRotator(0.0, Side * 8.0, 4.0).Quaternion(), FVector(-10.0, Side * 30.0, 8.0));
		}
		return B.Build(Outer, TEXT("FishingBoat"));
	}

	UStaticMesh* BuildBucket(UObject* Outer)
	{
		FMeshBuilder B;
		B.Section(TEXT("Pail"));
		B.Section(TEXT("Band"));
		B.Section(TEXT("Water"));
		B.Lathe(TEXT("Pail"), { FVector2D(0.0, 0.0), FVector2D(13.0, 0.0), FVector2D(13.0, 0.5), FVector2D(17.0, 30.0) }, 18, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		TArray<FVector2D> Rim;
		for (int32 i = 0; i <= 8; ++i)
		{
			const double A = 2.0 * PI * i / 8.0;
			Rim.Add(FVector2D(17.0 + 0.9 * FMath::Cos(A), 30.0 + 0.9 * FMath::Sin(A)));
		}
		B.Lathe(TEXT("Band"), Rim, 18, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		Disc(B, TEXT("Water"), FVector(0.0, 0.0, 24.0), 15.8, 15.8, 1.0, 18);
		return B.Build(Outer, TEXT("FishingBucket"));
	}

	UStaticMesh* BuildTerrain(UObject* Outer, const FVector2D& Min, const FVector2D& Max, int32 CellsX, int32 CellsY, TFunctionRef<double(double, double)> HeightAt)
	{
		FMeshBuilder B;
		const int32 Bed = B.Section(TEXT("Bed"));
		const int32 Bank = B.Section(TEXT("Bank"));
		const FVector2D Cell((Max.X - Min.X) / CellsX, (Max.Y - Min.Y) / CellsY);
		auto At = [&](int32 i, int32 j, FVector& N)
		{
			const double X = Min.X + i * Cell.X, Y = Min.Y + j * Cell.Y;
			const double Ex = Cell.X * 0.5, Ey = Cell.Y * 0.5;
			N = FVector(-(HeightAt(X + Ex, Y) - HeightAt(X - Ex, Y)) / (2.0 * Ex), -(HeightAt(X, Y + Ey) - HeightAt(X, Y - Ey)) / (2.0 * Ey), 1.0).GetSafeNormal();
			return FVector(X, Y, HeightAt(X, Y));
		};
		for (int32 i = 0; i < CellsX; ++i)
		{
			for (int32 j = 0; j < CellsY; ++j)
			{
				FVector N[4];
				const FVector P[4] = { At(i, j, N[0]), At(i + 1, j, N[1]), At(i + 1, j + 1, N[2]), At(i, j + 1, N[3]) };
				const double Mid = (P[0].Z + P[1].Z + P[2].Z + P[3].Z) / 4.0;
				const int32 S = Mid > 25.0 ? Bank : Bed;
				int32 I[4];
				for (int32 k = 0; k < 4; ++k)
				{
					I[k] = B.Vert(S, P[k], N[k], FVector2D(P[k].X / 500.0, P[k].Y / 500.0), FVector2D::ZeroVector);
				}
				B.Tri(S, I[0], I[1], I[2]);
				B.Tri(S, I[0], I[2], I[3]);
			}
		}
		return B.Build(Outer, TEXT("FishingTerrain"));
	}
}
