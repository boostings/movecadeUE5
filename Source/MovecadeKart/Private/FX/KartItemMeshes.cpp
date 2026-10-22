#include "FX/KartItemMeshes.h"

#include "Engine/StaticMesh.h"
#include "FX/KartItemMeshBuilder.h"
#include "Math/RandomStream.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	using MovecadeKart::Fx::Detail::FItemMeshBuilder;

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

	// The parts' shapes. Lathe profiles are (radius, height) in cm from one pole to the other; boxes are a centre and
	// half extents in cm.

	/** The thrown projectile: a "buzz disc", a flat puck with a bumper rim and eight swept teeth, and a low lens on top
	 *  with a hub. Deliberately not a domed shell (the web's look, which belongs to a famous franchise). */
	namespace Shell
	{
		const FVector2D Belly[] = { {0.0, -9.0}, {20.0, -9.0}, {26.0, -7.5}, {28.0, -5.0} };
		const FVector2D Rim[] = { {28.0, -5.0}, {31.5, -3.5}, {32.5, 0.0}, {31.5, 3.5}, {28.0, 5.0} };
		const FVector2D Dome[] = { {28.0, 5.0}, {24.0, 7.5}, {16.0, 9.0}, {9.0, 9.5}, {8.5, 12.5}, {6.0, 14.0}, {0.0, 14.5} };
		constexpr int32 Segments = 32;
		constexpr int32 Teeth = 8;
		constexpr double ToothRadius = 34.0;
		/** Teeth are offset along the spin direction, like a saw. */
		constexpr double ToothSweep = 2.5;
		const FVector ToothHalfSize(5.0, 3.5, 3.2);
	}

	/** The sky seeker's drone: four arms out to ducted rotor rings, so it reads as flying without wings. */
	namespace Drone
	{
		constexpr int32 Arms = 4;
		constexpr double ArmRadius = 38.0;
		constexpr double ArmHeight = 8.0;
		const FVector ArmHalfSize(12.0, 2.2, 1.8);
		constexpr double DuctRadius = 52.0;
		const FVector2D Duct[] = { {13.0, 5.0}, {15.0, 5.5}, {15.5, 8.0}, {15.0, 10.5}, {13.0, 11.0}, {12.5, 8.0}, {13.0, 5.0} };
		constexpr int32 DuctSegments = 20;
	}

	/** The dropped hazard: a striped traffic cone on a square foot (the web's banana peel is that franchise's signature
	 *  hazard). Peel is the orange cone, Tips the foot and the reflective bands. */
	namespace Cone
	{
		const FVector FootCentre(0.0, 0.0, 1.5);
		const FVector FootHalfSize(22.0, 22.0, 1.5);
		const FVector2D Base[] = { {17.0, 3.0}, {14.5, 14.0}, {12.0, 24.0} };
		const FVector2D LowerBand[] = { {12.0, 24.0}, {10.4, 31.0} };
		const FVector2D Middle[] = { {10.4, 31.0}, {8.0, 41.0} };
		const FVector2D UpperBand[] = { {8.0, 41.0}, {6.6, 47.0} };
		const FVector2D Tip[] = { {6.6, 47.0}, {4.0, 56.0}, {2.0, 57.5}, {0.0, 58.0} };
		constexpr int32 Segments = 24;
	}

	/** A round bomb with a collar cap and a little curved fuse whose tip is at FuseTip. */
	namespace Bomb
	{
		constexpr double Radius = 40.0;
		constexpr int32 ArcSteps = 18;
		constexpr int32 Segments = 32;
		const FVector2D Cap[] = { {0.0, 37.0}, {13.0, 37.0}, {13.5, 40.0}, {13.0, 46.0}, {11.0, 47.0}, {0.0, 47.0} };
		constexpr int32 CapSegments = 20;
		/** The fuse curls 7 cm out over 14 cm up from the cap, 2.6 cm thick. */
		constexpr int32 FusePoints = 6;
		constexpr double FuseReach = 7.0;
		constexpr double FuseBase = 46.0;
		constexpr double FuseRise = 14.0;
		constexpr double FuseRadius = 2.6;
		constexpr int32 FuseSegments = 8;
		const FVector FuseTip(7.0, 0.0, 60.0);
	}

	/** The toy rocket along +X: exhaust cup at the back, body, a band, an ogive nose, four fins. */
	namespace Rocket
	{
		const FVector2D ExhaustCup[] = { {0.0, -150.0}, {30.0, -150.0}, {40.0, -154.0}, {48.0, -146.0} };
		const FVector2D Tail[] = { {48.0, -146.0}, {58.0, -132.0}, {62.0, -112.0} };
		const FVector2D Band[] = { {62.5, -112.0}, {63.5, -104.0}, {63.5, -88.0}, {62.5, -80.0} };
		const FVector2D Body[] = { {62.0, -80.0}, {64.0, -20.0}, {63.0, 30.0}, {58.0, 72.0}, {48.0, 104.0}, {33.0, 128.0}, {16.0, 145.0}, {0.0, 152.0} };
		constexpr int32 Segments = 28;
		constexpr int32 Fins = 4;
		const FVector FinRoot(-118.0, 0.0, 0.0);
		constexpr double FinReach = 74.0;
		const FVector FinHalfSize(26.0, 2.5, 20.0);
		const FVector Exhaust(-158.0, 0.0, 0.0);
	}

	/** The item box's gem: a 1 m cube (half size 50 cm) with 8 cm chamfers. */
	namespace BoxCore
	{
		constexpr double Half = 50.0;
		constexpr double Chamfer = 8.0;
	}

	/** The "?" labels: 78 cm quads 0.6 cm outside each face of the 1 m cube. */
	namespace BoxLabels
	{
		constexpr double FaceOffset = 50.6;
		constexpr double Half = 39.0;
	}

	/** A nitro canister: rounded body, a white band, a metal neck and cap, and a valve knob off the neck. */
	namespace Canister
	{
		const FVector2D Body[] = { {0.0, 0.0}, {9.0, 0.4}, {13.0, 2.5}, {14.0, 6.0}, {14.0, 30.0}, {13.0, 36.0}, {9.5, 40.5}, {6.5, 42.0} };
		const FVector2D Band[] = { {14.2, 14.0}, {14.9, 15.0}, {14.9, 22.0}, {14.2, 23.0} };
		const FVector2D Neck[] = { {6.5, 41.5}, {6.8, 47.0}, {9.0, 47.5}, {9.0, 52.0}, {7.0, 53.5}, {0.0, 54.0} };
		constexpr int32 Segments = 24;
		constexpr int32 NeckSegments = 16;
		constexpr double ValveHeight = 49.0;
		constexpr double ValveStem = 14.0;
		constexpr double ValveStemThickness = 4.0;
		constexpr double ValveKnobOffset = 15.0;
		const FVector ValveKnobHalfSize(3.0, 6.0, 6.0);
		/** The single canister stands on this base; the triple fans two more out at TripleTilt degrees. */
		const FVector SingleBase(0.0, 0.0, -27.0);
		constexpr double TripleTilt = 24.0;
		constexpr double TripleSide = 17.0;
		const FVector TripleSideBase(-6.0, 0.0, -30.0);
	}

	/** A faceted crystal: an eight-sided bipyramid with a girdle of kite facets. */
	namespace Prism
	{
		constexpr int32 Sides = 8;
		const FVector Top(0.0, 0.0, 36.0);
		const FVector Bottom(0.0, 0.0, -30.0);
		const FVector Centre(0.0, 0.0, 4.0);
		constexpr double GirdleRadius = 23.0;
		constexpr double UpperRingHeight = 8.0;
		/** The crown's kite points sit pulled in toward the axis and raised. */
		constexpr double KiteInset = 0.82;
		constexpr double KiteRise = 17.0;
	}

	/** The lightning glyph in (x, z), counter-clockwise: a top bar slanting down-left, a step, a long point. */
	namespace Bolt
	{
		const FVector2D Outline[] = { {-2.0, 34.0}, {-14.0, 2.0}, {-1.0, 2.0}, {-10.0, -34.0}, {16.0, 8.0}, {3.0, 8.0}, {14.0, 34.0} };
		constexpr double HalfThickness = 6.0;
	}

	/** An ink drop (round bottom, swelling, a soft point) with two big eyes. */
	namespace Ink
	{
		const FVector2D Drop[] = { {0.0, -26.0}, {12.0, -24.0}, {19.5, -18.0}, {22.0, -9.0}, {20.5, 0.0}, {16.0, 9.0}, {10.0, 18.0}, {5.0, 26.0}, {1.5, 32.0}, {0.0, 34.0} };
		constexpr int32 Segments = 28;
	}

	/** A lightning strike: 12 jagged steps tightening onto the target, two forks of four segments. */
	namespace Strike
	{
		constexpr int32 Steps = 12;
		constexpr double Wander = 70.0;
		constexpr double WanderTighten = 0.7;
		constexpr double TopWidth = 26.0;
		constexpr double BottomWidth = 16.0;
		constexpr double Depth = 0.6;
		constexpr int32 Forks = 2;
		constexpr int32 ForkSegments = 4;
	}

	void AddShell(FItemMeshBuilder& B)
	{
		B.Lathe(TEXT("Belly"), Shell::Belly, Shell::Segments);
		B.Lathe(TEXT("Rim"), Shell::Rim, Shell::Segments);
		for (int32 k = 0; k < Shell::Teeth; ++k)
		{
			const double A = 2.0 * PI * k / 8.0;
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0);
			const FVector Along(-Out.Y, Out.X, 0.0);
			B.Box(TEXT("Rim"), Out * Shell::ToothRadius + Along * Shell::ToothSweep, Shell::ToothHalfSize, Out, Along, FVector::UpVector);
		}
		B.Lathe(TEXT("Dome"), Shell::Dome, Shell::Segments);
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
		for (int32 k = 0; k < Drone::Arms; ++k)
		{
			const double A = PI * 0.25 + PI * 0.5 * k;
			const FVector Out(FMath::Cos(A), FMath::Sin(A), 0.0);
			B.Box(TEXT("Wings"), Out * Drone::ArmRadius + FVector(0.0, 0.0, Drone::ArmHeight), Drone::ArmHalfSize, Out, FVector(-Out.Y, Out.X, 0.0), FVector::UpVector);
			// The duct: a short open ring around the rotor.
			B.Lathe(TEXT("Wings"), Drone::Duct, Drone::DuctSegments, Out * Drone::DuctRadius);
		}
		return B.Build(Outer, TEXT("SM_Item_WingedShell"));
	}

	UStaticMesh* BuildBananaMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		B.Box(TEXT("Tips"), Cone::FootCentre, Cone::FootHalfSize);
		B.Lathe(TEXT("Peel"), Cone::Base, Cone::Segments);
		B.Lathe(TEXT("Tips"), Cone::LowerBand, Cone::Segments);
		B.Lathe(TEXT("Peel"), Cone::Middle, Cone::Segments);
		B.Lathe(TEXT("Tips"), Cone::UpperBand, Cone::Segments);
		B.Lathe(TEXT("Peel"), Cone::Tip, Cone::Segments);
		return B.Build(Outer, TEXT("SM_Item_Cone"));
	}

	FVector BombFuseTip()
	{
		return Bomb::FuseTip;
	}

	UStaticMesh* BuildBombMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		B.Lathe(TEXT("Body"), Arc(Bomb::Radius, 0.0, -90.0, 90.0, Bomb::ArcSteps), Bomb::Segments);
		B.Lathe(TEXT("Cap"), Bomb::Cap, Bomb::CapSegments);
		TArray<FVector> Fuse;
		TArray<double> R;
		for (int32 i = 0; i <= Bomb::FusePoints; ++i)
		{
			const double T = i / 6.0;
			Fuse.Add(FVector(Bomb::FuseReach * T * T, 0.0, Bomb::FuseBase + Bomb::FuseRise * T));
			R.Add(Bomb::FuseRadius);
		}
		B.Tube(TEXT("Fuse"), Fuse, R, Bomb::FuseSegments);
		return B.Build(Outer, TEXT("SM_Item_Bomb"));
	}

	FVector RocketExhaust()
	{
		return Rocket::Exhaust;
	}

	UStaticMesh* BuildRocketMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		B.Lathe(TEXT("Exhaust"), Rocket::ExhaustCup, Rocket::Segments, FVector::ZeroVector, FVector::ForwardVector);
		B.Lathe(TEXT("Body"), Rocket::Tail, Rocket::Segments, FVector::ZeroVector, FVector::ForwardVector);
		B.Lathe(TEXT("Band"), Rocket::Band, Rocket::Segments, FVector::ZeroVector, FVector::ForwardVector);
		B.Lathe(TEXT("Body"), Rocket::Body, Rocket::Segments, FVector::ZeroVector, FVector::ForwardVector);
		for (int32 k = 0; k < Rocket::Fins; ++k)
		{
			const double A = k * 90.0 + 45.0;
			const FVector Out(0.0, FMath::Cos(FMath::DegreesToRadians(A)), FMath::Sin(FMath::DegreesToRadians(A)));
			const FVector Thick = FVector::CrossProduct(FVector::ForwardVector, Out);
			B.Box(TEXT("Fins"), Rocket::FinRoot + Out * Rocket::FinReach, Rocket::FinHalfSize, FVector::ForwardVector, Thick, Out);
		}
		return B.Build(Outer, TEXT("SM_Item_Rocket"));
	}

	UStaticMesh* SharedRocketMesh()
	{
		// Every kart wears the same rocket; the components using it keep it alive, and it is rebuilt after a GC
		// that found no user (a level change).
		static TWeakObjectPtr<UStaticMesh> Shared;
		if (!Shared.IsValid())
		{
			Shared = BuildRocketMesh(GetTransientPackage());
		}
		return Shared.Get();
	}

	UStaticMesh* BuildChamferCubeMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		const int32 S = B.Section(TEXT("Core"));
		const double H = BoxCore::Half, In = H - BoxCore::Chamfer;
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
		void AddCanister(FItemMeshBuilder& B, const FVector& Base, const FVector& Axis)
		{
			const FVector Up = Axis.GetSafeNormal();
			B.Lathe(TEXT("Body"), Canister::Body, Canister::Segments, Base, Up);
			B.Lathe(TEXT("Band"), Canister::Band, Canister::Segments, Base, Up);
			B.Lathe(TEXT("Metal"), Canister::Neck, Canister::NeckSegments, Base, Up);
			const FVector Side = FVector::CrossProduct(Up, FMath::Abs(Up.Z) > 0.9 ? FVector::RightVector : FVector::UpVector).GetSafeNormal();
			const FVector Third = FVector::CrossProduct(Up, Side);
			B.Beam(TEXT("Metal"), Base + Up * Canister::ValveHeight, Base + Up * Canister::ValveHeight + Side * Canister::ValveStem,
				Canister::ValveStemThickness, Canister::ValveStemThickness, Third);
			B.Box(TEXT("Metal"), Base + Up * Canister::ValveHeight + Side * Canister::ValveKnobOffset, Canister::ValveKnobHalfSize, Side, Third, Up);
		}

		/** Ear-clipping triangulation of a simple polygon given counter-clockwise. */
		TArray<int32> Triangulate(TConstArrayView<FVector2D> Poly)
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
		const double Out = BoxLabels::FaceOffset, Half = BoxLabels::Half;
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
		AddCanister(B, Canister::SingleBase, FVector::UpVector);
		return B.Build(Outer, TEXT("SM_Item_Nitro"));
	}

	UStaticMesh* BuildTripleNitroMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		AddCanister(B, Canister::SingleBase, FVector::UpVector);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const double Tilt = FMath::DegreesToRadians(Canister::TripleTilt);
			AddCanister(B, FVector(Canister::TripleSideBase.X, Side * Canister::TripleSide, Canister::TripleSideBase.Z), FVector(0.0, Side * FMath::Sin(Tilt), FMath::Cos(Tilt)));
		}
		return B.Build(Outer, TEXT("SM_Item_TripleNitro"));
	}

	UStaticMesh* BuildPrismMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		const int32 Sides = Prism::Sides;
		TArray<FVector> Upper, Lower;
		for (int32 i = 0; i < Sides; ++i)
		{
			const double A = 2.0 * UE_DOUBLE_PI * i / Sides;
			Upper.Add(FVector(FMath::Cos(A) * Prism::GirdleRadius, FMath::Sin(A) * Prism::GirdleRadius, Prism::UpperRingHeight));
			Lower.Add(FVector(FMath::Cos(A + UE_DOUBLE_PI / Sides) * Prism::GirdleRadius, FMath::Sin(A + UE_DOUBLE_PI / Sides) * Prism::GirdleRadius, 0.0));
		}
		for (int32 i = 0; i < Sides; ++i)
		{
			const int32 j = (i + 1) % Sides;
			// Crown: each side split into kite facets for extra sparkle.
			const FVector Mid = (Upper[i] + Upper[j]) * 0.5 * Prism::KiteInset + FVector(0.0, 0.0, Prism::KiteRise);
			B.Facet(TEXT("Crystal"), Prism::Top, Upper[i], Mid, Prism::Centre);
			B.Facet(TEXT("Crystal"), Prism::Top, Mid, Upper[j], Prism::Centre);
			B.Facet(TEXT("Crystal"), Upper[i], Upper[j], Mid, Prism::Centre);
			// Girdle: a zigzag band between the two rings.
			B.Facet(TEXT("Crystal"), Upper[i], Lower[i], Upper[j], Prism::Centre);
			B.Facet(TEXT("Crystal"), Lower[i], Lower[j], Upper[j], Prism::Centre);
			// Pavilion down to the point.
			B.Facet(TEXT("Crystal"), Lower[i], Prism::Bottom, Lower[j], Prism::Centre);
		}
		return B.Build(Outer, TEXT("SM_Item_Prism"));
	}

	UStaticMesh* BuildLightningMesh(UObject* Outer)
	{
		FItemMeshBuilder B;
		const TConstArrayView<FVector2D> Poly = Bolt::Outline;
		const double T = Bolt::HalfThickness;
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
		B.Lathe(TEXT("Ink"), Ink::Drop, Ink::Segments);
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
		const int32 Steps = Strike::Steps;
		TArray<FVector> Points;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double T = static_cast<double>(i) / Steps;
			// Wide zigzags up high, tightening to land on the target.
			const double Wander = (i == 0 || i == Steps) ? 0.0 : Strike::Wander * (1.0 - T * Strike::WanderTighten);
			Points.Add(FVector(Rand.FRandRange(-Wander, Wander), Rand.FRandRange(-Wander, Wander), Height * (1.0 - T)));
		}
		for (int32 i = 0; i < Steps; ++i)
		{
			const double W = FMath::Lerp(Strike::TopWidth, Strike::BottomWidth, static_cast<double>(i) / Steps);
			// Two crossed slabs per segment, so the bolt reads from any side.
			B.Beam(TEXT("Bolt"), Points[i], Points[i + 1], W, W * Strike::Depth, FVector::RightVector);
			B.Beam(TEXT("Bolt"), Points[i], Points[i + 1], W, W * Strike::Depth, FVector::ForwardVector);
		}
		for (int32 Fork = 0; Fork < Strike::Forks; ++Fork)
		{
			FVector P = Points[2 + Fork * 4];
			const FVector Dir = FVector(Rand.FRandRange(-1.0, 1.0), Rand.FRandRange(-1.0, 1.0), 0.0).GetSafeNormal();
			for (int32 k = 0; k < Strike::ForkSegments; ++k)
			{
				const FVector Next = P + Dir * Rand.FRandRange(40.0, 80.0) + FVector(Rand.FRandRange(-25.0, 25.0), Rand.FRandRange(-25.0, 25.0), -Height / Steps * 0.8);
				B.Beam(TEXT("Bolt"), P, Next, 10.0 - k * 2.0, 6.0, FVector::RightVector);
				P = Next;
			}
		}
		return B.Build(Outer, *FString::Printf(TEXT("SM_FX_Strike%d"), Seed));
	}
}
