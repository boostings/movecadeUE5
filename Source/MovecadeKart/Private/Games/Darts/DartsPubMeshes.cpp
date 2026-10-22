// ADartsPub: the meshes modelled in code, built once per pub (they survive OnConstruction rebuilds): the board's
// face, its wire spider and hoop, the bounty wedge, the reticle ring, the dart, a bottle, a bar stool and a bulb.

#include "Games/Darts/DartsPub.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Engine/StaticMesh.h"
#include "Games/Darts/DartsPubGeometry.h"
#include "Games/Darts/DartsRules.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::FMeshBuilder;
using MovecadeKart::Darts::PubGeometry::BoardLocal;

namespace
{
	/** The board's outer edge (the surround that carries the number ring), board radii. */
	constexpr double SurroundRadius = 1.38;
	/** The board's depth behind the face, metres. */
	constexpr double BoardDepth = 0.045;
	/** The spider's wire (metres): its radius, and how far it stands proud of the sisal. */
	constexpr double WireTube = 0.0032;
	/** The bounty wedge's outline width (board radii) and how far the wedge and the outline sit off the face (m). */
	constexpr double WedgeEdge = 0.026;
	constexpr double WedgeLift = 0.008;
	constexpr double WedgeEdgeLift = 0.011;
	/** The reticle ring's radii, metres. */
	constexpr double ReticleInnerM = 0.1;
	constexpr double ReticleOuterM = 0.15;
	/** dartGeometry.ts: the tip is at web y 0.62 and the dart renders at 0.48 scale; the mesh puts the tip at the origin. */
	constexpr double DartTipY = 0.62;
	constexpr double DartScaleCm = 0.48 * 100.0;

	/** One annular sector of the board face (board radii, radians), facing the camera. */
	void Annulus(FMeshBuilder& B, FName Slot, double R0, double R1, double A0, double A1, double Lift, int32 Segments)
	{
		const int32 S = B.Section(Slot);
		const FVector N(-1.0, 0.0, 0.0);
		auto P = [Lift](double R, double T) { return BoardLocal(R * FMath::Cos(T), R * FMath::Sin(T), Lift); };
		auto UV = [](double R, double T) { return FVector2D(0.5 + 0.5 * R * FMath::Cos(T), 0.5 - 0.5 * R * FMath::Sin(T)); };
		for (int32 i = 0; i < Segments; ++i)
		{
			const double T0 = FMath::Lerp(A0, A1, static_cast<double>(i) / Segments);
			const double T1 = FMath::Lerp(A0, A1, static_cast<double>(i + 1) / Segments);
			const int32 a = B.Vert(S, P(R0, T0), N, UV(R0, T0), FVector2D::ZeroVector);
			const int32 b = B.Vert(S, P(R1, T0), N, UV(R1, T0), FVector2D::ZeroVector);
			const int32 c = B.Vert(S, P(R1, T1), N, UV(R1, T1), FVector2D::ZeroVector);
			const int32 d = B.Vert(S, P(R0, T1), N, UV(R0, T1), FVector2D::ZeroVector);
			B.Tri(S, a, b, c);
			// A sector from the centre is a fan: its inner edge is a point.
			if (R0 > 1e-6)
			{
				B.Tri(S, a, c, d);
			}
		}
	}

	/** A flat strip from From to To on the face, Width board radii wide. */
	void Strip(FMeshBuilder& B, FName Slot, const FVector2D& From, const FVector2D& To, double Width, double Lift)
	{
		const int32 S = B.Section(Slot);
		const FVector N(-1.0, 0.0, 0.0);
		const FVector2D Side = FVector2D(-(To - From).Y, (To - From).X).GetSafeNormal() * Width * 0.5;
		const int32 a = B.Vert(S, BoardLocal(From.X - Side.X, From.Y - Side.Y, Lift), N, FVector2D(0, 0), FVector2D::ZeroVector);
		const int32 b = B.Vert(S, BoardLocal(From.X + Side.X, From.Y + Side.Y, Lift), N, FVector2D(1, 0), FVector2D::ZeroVector);
		const int32 c = B.Vert(S, BoardLocal(To.X + Side.X, To.Y + Side.Y, Lift), N, FVector2D(1, 1), FVector2D::ZeroVector);
		const int32 d = B.Vert(S, BoardLocal(To.X - Side.X, To.Y - Side.Y, Lift), N, FVector2D(0, 1), FVector2D::ZeroVector);
		B.Tri(S, a, b, c);
		B.Tri(S, a, c, d);
	}

	/** A torus about the board normal: Radius board radii, Tube metres, Lift metres out. */
	void Hoop(FMeshBuilder& B, FName Slot, double Radius, double Tube, double Lift, int32 Segments = 96, int32 Sides = 12)
	{
		TArray<FVector2D> Profile;
		for (int32 i = 0; i <= Sides; ++i)
		{
			const double T = 2.0 * UE_DOUBLE_PI * i / Sides;
			Profile.Add(FVector2D(Radius * ADartsPub::BoardRadius * 100.0 + Tube * 100.0 * FMath::Cos(T), Tube * 100.0 * FMath::Sin(T)));
		}
		B.Lathe(Slot, Profile, Segments, FVector(-Lift * 100.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), FVector::OneVector, FVector2D::ZeroVector);
	}

	/** A part of the dart lathed about +X: Profile is (radius, web height along the dart) in web metres. */
	void DartPart(FMeshBuilder& B, FName Slot, std::initializer_list<FVector2D> WebProfile, int32 Segments = 16)
	{
		TArray<FVector2D> Profile;
		for (const FVector2D& P : WebProfile)
		{
			Profile.Add(FVector2D(P.X * DartScaleCm, (P.Y - DartTipY) * DartScaleCm));
		}
		B.Lathe(Slot, Profile, Segments, FVector::ZeroVector, FVector::ForwardVector, FVector::OneVector, FVector2D::ZeroVector);
	}
}

void ADartsPub::BuildMeshes()
{
	BuildBoardMeshes();
	BuildDartMesh();
	BuildPropMeshes();
}

void ADartsPub::BuildBoardMeshes()
{
	// The face: every sector's single beds and its treble and double, the bulls, the surround and the board's edge.
	// Slot order is first use: cream, red, night, teal, surround (BuildBoardFace dresses them in that order).
	{
		FMeshBuilder B;
		for (int32 i = 0; i < SectorCount; ++i)
		{
			const double C = SectorCenterAngle(i);
			const double A0 = C - SectorArc / 2.0, A1 = C + SectorArc / 2.0;
			const bool bEven = i % 2 == 0;
			const FName Single = bEven ? TEXT("Cream") : TEXT("Night");
			const FName Ring = bEven ? TEXT("Red") : TEXT("Teal");
			Annulus(B, Single, OuterBullRadius, TripleInnerRadius, A0, A1, 0.0, 4);
			Annulus(B, Ring, TripleInnerRadius, TripleOuterRadius, A0, A1, 0.0, 4);
			Annulus(B, Single, TripleOuterRadius, DoubleInnerRadius, A0, A1, 0.0, 4);
			Annulus(B, Ring, DoubleInnerRadius, DoubleOuterRadius, A0, A1, 0.0, 4);
		}
		Annulus(B, TEXT("Teal"), BullRadius, OuterBullRadius, 0.0, 2.0 * UE_DOUBLE_PI, 0.0, 32);
		Annulus(B, TEXT("Red"), 0.0, BullRadius, 0.0, 2.0 * UE_DOUBLE_PI, 0.0, 24);
		// The surround carries the number ring.
		Annulus(B, TEXT("Surround"), DoubleOuterRadius, SurroundRadius, 0.0, 2.0 * UE_DOUBLE_PI, -0.001, 96);
		B.Cylinder(TEXT("Surround"), BoardLocal(0, 0, -BoardDepth), BoardLocal(0, 0, -0.001), SurroundRadius * BoardRadius * 100.0, 96, FVector2D::ZeroVector, false);
		BoardMesh = B.Build(this, TEXT("DartBoard"));
	}
	// The spider: round wire rings at the six radii and twenty round spokes along the sector edges (boardWires.ts),
	// standing proud of the sisal so they catch the board light; the brass hoop round the number ring.
	{
		FMeshBuilder B;
		for (const double R : { BullRadius, OuterBullRadius, TripleInnerRadius, TripleOuterRadius, DoubleInnerRadius, DoubleOuterRadius })
		{
			Hoop(B, TEXT("Wire"), R, WireTube, WireTube, R < 0.2 ? 32 : 120, 6);
		}
		for (int32 i = 0; i < SectorCount; ++i)
		{
			const double A = SectorCenterAngle(i) - SectorArc / 2.0;
			const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
			B.Cylinder(TEXT("Wire"), BoardLocal(Dir.X * OuterBullRadius, Dir.Y * OuterBullRadius, WireTube),
				BoardLocal(Dir.X * DoubleOuterRadius, Dir.Y * DoubleOuterRadius, WireTube), WireTube * 100.0, 6, FVector2D::ZeroVector, false);
		}
		Hoop(B, TEXT("Brass"), 1.12, 0.012, 0.018);
		WireMesh = B.Build(this, TEXT("DartWires"));
	}
	// The called shot: one sector from the outer bull to the double, centred on +Y (the game rolls it into place),
	// with a bright outline so the wedge reads at TV distance.
	{
		FMeshBuilder B;
		const double A0 = UE_DOUBLE_PI / 2.0 - SectorArc / 2.0, A1 = UE_DOUBLE_PI / 2.0 + SectorArc / 2.0;
		Annulus(B, TEXT("Wedge"), OuterBullRadius, DoubleOuterRadius, A0, A1, WedgeLift, 6);
		Annulus(B, TEXT("Edge"), DoubleOuterRadius - WedgeEdge, DoubleOuterRadius + WedgeEdge * 0.5, A0, A1, WedgeEdgeLift, 8);
		Annulus(B, TEXT("Edge"), OuterBullRadius, OuterBullRadius + WedgeEdge, A0, A1, WedgeEdgeLift, 4);
		for (const double A : { A0, A1 })
		{
			const FVector2D Dir(FMath::Cos(A), FMath::Sin(A));
			Strip(B, TEXT("Edge"), Dir * OuterBullRadius, Dir * (DoubleOuterRadius + WedgeEdge * 0.5), WedgeEdge, WedgeEdgeLift);
		}
		WedgeMesh = B.Build(this, TEXT("DartWedge"));
	}
	// The reticle ring, facing the camera.
	{
		FMeshBuilder B;
		Annulus(B, TEXT("Ring"), ReticleInnerM / BoardRadius, ReticleOuterM / BoardRadius, 0.0, 2.0 * UE_DOUBLE_PI, 0.0, 40);
		RingMesh = B.Build(this, TEXT("DartReticle"));
	}
}

void ADartsPub::BuildDartMesh()
{
	using namespace MovecadeKart::Art;
	// The dart (dartGeometry.ts): steel point, nose and knurl rings, a tungsten barrel, the shaft, four flights.
	// Four sections (the metals share one): fewer draws per dart.
	FMeshBuilder B;
	DartPart(B, TEXT("Steel"), { { 0.0, 0.30 }, { 0.012, 0.30 }, { 0.0, 0.62 } }, 12);
	DartPart(B, TEXT("Steel"), { { 0.0, 0.30 }, { 0.037, 0.30 }, { 0.014, 0.345 }, { 0.0, 0.345 } });
	DartPart(B, TEXT("Barrel"), { { 0.0, -0.1 }, { 0.043, -0.1 }, { 0.037, 0.3 }, { 0.0, 0.3 } });
	DartPart(B, TEXT("Shaft"), { { 0.0, -0.48 }, { 0.019, -0.48 }, { 0.016, -0.1 }, { 0.0, -0.1 } }, 12);
	// The barrel's knurl: nine small rings up its length.
	for (int32 i = 0; i < 9; ++i)
	{
		const double Y = -0.065 + i * 0.037;
		TArray<FVector2D> Ring;
		for (int32 k = 0; k <= 6; ++k)
		{
			const double T = 2.0 * UE_DOUBLE_PI * k / 6.0;
			Ring.Add(FVector2D((0.042 + 0.0035 * FMath::Cos(T)) * 48.0, (Y + 0.0035 * FMath::Sin(T) - DartTipY) * 48.0));
		}
		B.Lathe(TEXT("Steel"), Ring, 16, FVector::ZeroVector, FVector::ForwardVector, FVector::OneVector, FVector2D::ZeroVector);
	}
	// Flights: a fin outline in (out, along) web metres, both faces, every 90 degrees about the shaft.
	const FVector2D Fin[] = { { 0.012, -0.28 }, { 0.132, -0.39 }, { 0.152, -0.43 }, { 0.134, -0.49 }, { 0.072, -0.565 }, { 0.012, -0.535 } };
	const int32 S = B.Section(TEXT("Flight"));
	for (int32 k = 0; k < 4; ++k)
	{
		const double Spin = k * UE_DOUBLE_PI / 2.0;
		const FVector Out(0.0, FMath::Cos(Spin), FMath::Sin(Spin));
		const FVector Normal(0.0, -FMath::Sin(Spin), FMath::Cos(Spin));
		for (const double Face : { 1.0, -1.0 })
		{
			TArray<int32, TInlineAllocator<6>> Ids;
			for (const FVector2D& P : Fin)
			{
				const FVector Pos = FVector((P.Y - DartTipY) * 48.0, 0.0, 0.0) + Out * P.X * 48.0 + Normal * Face * 0.14;
				Ids.Add(B.Vert(S, Pos, Normal * Face, FVector2D(P.X / 0.16, -P.Y), FVector2D::ZeroVector));
			}
			for (int32 t = 1; t + 1 < Ids.Num(); ++t)
			{
				B.Tri(S, Ids[0], Ids[t], Ids[t + 1]);
			}
		}
	}
	DartMesh = B.Build(this, TEXT("Dart"));
	// Slots as sectioned: steel, barrel, shaft, flight (ADartsGame recolours the flight per thrower).
	DartMaterials = {
		Plain(this, Hex(TEXT("#eef3f6")), 0.16, 1.0),
		Plain(this, Hex(TEXT("#8f9aa3")), 0.34, 1.0),
		Plain(this, Hex(TEXT("#1c2227")), 0.35, 0.3),
		Plain(this, Hex(TEXT("#f7f1e5")), 0.7),
	};
}

void ADartsPub::BuildPropMeshes()
{
	// A bottle, lathed (cm): body, shoulder, neck, cap.
	{
		FMeshBuilder B;
		const TArray<FVector2D> Body = { { 0, 0 }, { 3.6, 0 }, { 3.8, 1 }, { 3.8, 17 }, { 3.2, 21 }, { 1.4, 24 }, { 1.2, 30 }, { 0, 30 } };
		B.Lathe(TEXT("Glass"), Body, 20, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		const TArray<FVector2D> Cap = { { 0, 29.5 }, { 1.45, 29.5 }, { 1.45, 32 }, { 0, 32 } };
		B.Lathe(TEXT("Cap"), Cap, 12, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		BottleMesh = B.Build(this, TEXT("Bottle"));
	}
	// A bar stool (cm, origin on the floor): a padded seat on a brass frame with a foot ring.
	{
		FMeshBuilder B;
		B.Cylinder(TEXT("Seat"), FVector(0, 0, 70), FVector(0, 0, 78), 19.0, 24, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Seat"), FVector(0, 0, 78), FVector(19.0, 19.0, 3.5), 4, 24, FVector2D::ZeroVector);
		for (int32 k = 0; k < 4; ++k)
		{
			const double A = UE_DOUBLE_PI / 4.0 + k * UE_DOUBLE_PI / 2.0;
			const FVector Foot(FMath::Cos(A) * 21.0, FMath::Sin(A) * 21.0, 0.0);
			const FVector Top(FMath::Cos(A) * 12.0, FMath::Sin(A) * 12.0, 70.0);
			B.Cylinder(TEXT("Frame"), Foot, Top, 1.3, 8, FVector2D::ZeroVector);
		}
		TArray<FVector2D> Ring;
		for (int32 i = 0; i <= 8; ++i)
		{
			const double T = 2.0 * UE_DOUBLE_PI * i / 8.0;
			Ring.Add(FVector2D(18.4 + 1.1 * FMath::Cos(T), 26.0 + 1.1 * FMath::Sin(T)));
		}
		B.Lathe(TEXT("Frame"), Ring, 24, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector);
		StoolMesh = B.Build(this, TEXT("Stool"));
	}
	// A festoon bulb (cm): a small glowing pear.
	{
		FMeshBuilder B;
		B.Ellipsoid(TEXT("Bulb"), FVector(0, 0, -3), FVector(2.6, 2.6, 3.4), 6, 10, FVector2D::ZeroVector);
		BulbMesh = B.Build(this, TEXT("Bulb"));
	}
}
