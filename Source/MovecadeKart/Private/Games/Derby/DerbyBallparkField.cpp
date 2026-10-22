// ADerbyBallpark's playing field: the lawn and its mown stripes, the warning track, the diamond (skin, mound, bases,
// chalk), the backstop, the outfield wall with its poles, markers and ads, and the six light towers. Metres.

#include "Games/Derby/DerbyBallpark.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Derby/DerbyBallparkParts.h"

using namespace MovecadeKart::Derby;
using namespace MovecadeKart::Derby::Ballpark;

namespace
{
	/** The lawn: mown stripes this wide, every other one lighter, stopping short of the warning track. */
	constexpr double StripeWidthM = 6.0;
	constexpr int32 StripesEachSide = 17;
	constexpr double StripeProbeEndM = 130.0;
	constexpr double StripeProbeStepM = 0.5;
	/** The warning track along the wall: its width and the arc steps it is laid in (degrees). */
	constexpr double TrackWidthM = 4.6;
	constexpr double TrackStepDeg = 1.5;
	/** The diamond: 90 ft between the bases (on the X axis: home, first at +Y, second, third). */
	const FVector Bases[] = { FVector(0.0, 0.0, 0.0), FVector(19.40, 19.40, 0.0), FVector(38.79, 0.0, 0.0), FVector(19.40, -19.40, 0.0) };
	/** The batter's boxes: either side of the plate, 4 ft by 6 ft, chalked. */
	constexpr double BoxOffsetM = 0.95;
	/** The backstop's curve behind home. */
	constexpr double BackstopRadiusM = 18.0;
	/** The wall: its padded face, the yellow line below the cap, its thickness and the curve's steps. */
	constexpr double WallLineBelowTopCm = 14.0;
	constexpr double WallThicknessCm = 45.0;
	constexpr int32 WallSteps = 180;
	/** Markers and ads sit just proud of the wall's face. */
	constexpr double WallFaceInsetM = 0.12;
	/** The light towers (spray degrees, metres out), each head aimed at the middle of the field. */
	const FVector2D TowerSpots[] = { FVector2D(28.0, 168.0), FVector2D(-28.0, 168.0), FVector2D(62.0, 142.0), FVector2D(-62.0, 142.0), FVector2D(128.0, 64.0), FVector2D(-128.0, 64.0) };
	constexpr double TowerHeightM = 46.0;
	const FVector TowerAimM(45.0, 0.0, 0.0);
	/** Each head's lamp bank: rows by columns, this far apart. */
	constexpr int32 LampRows = 3;
	constexpr int32 LampColumns = 5;
	constexpr double LampSpacingM = 2.5;

	/** The wall's face at a spray angle: the point Inset metres in front of it and the yaw that faces home square to it. */
	void WallFace(double SprayDeg, double Inset, FVector& OutM, double& OutYaw)
	{
		const double A = FMath::DegreesToRadians(SprayDeg);
		const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0);
		const double R = FenceAt(SprayDeg);
		// d(Fence)/d(angle), from FenceDistanceM's cosine of twice the spray.
		const double DR = -FenceCentreExtraM * 2.0 * FMath::Sin(2.0 * A);
		const FVector Tangent = (Dir * DR + FVector(-Dir.Y, Dir.X, 0.0) * R).GetSafeNormal();
		FVector In(Tangent.Y, -Tangent.X, 0.0);
		if (FVector::DotProduct(In, Dir) > 0.0)
		{
			In = -In;
		}
		OutM = Dir * R + In * Inset;
		OutYaw = FMath::RadiansToDegrees(FMath::Atan2(In.Y, In.X));
	}

	/**
	 * The outfield wall as one smooth curved piece from pole to pole (cm): the padded face toward the field (slot 0),
	 * its yellow top line and cap (slot 1), and the back.
	 */
	UStaticMesh* BuildWallMesh(UObject* Outer)
	{
		MovecadeKart::FMeshBuilder B;
		const int32 Pad = B.Section(TEXT("Pad"));
		const int32 Cap = B.Section(TEXT("Cap"));
		const double H = FenceHeightM * 100.0;
		const double Line = H - WallLineBelowTopCm;
		auto Strip = [&B](int32 S, const TArray<FVector>& A, const TArray<FVector>& Bv, const TArray<FVector>& N)
		{
			for (int32 i = 0; i + 1 < A.Num(); ++i)
			{
				const int32 a = B.Vert(S, A[i], N[i], FVector2D(i * 0.1, 0.0), FVector2D::ZeroVector);
				const int32 b = B.Vert(S, A[i + 1], N[i + 1], FVector2D((i + 1) * 0.1, 0.0), FVector2D::ZeroVector);
				const int32 c = B.Vert(S, Bv[i + 1], N[i + 1], FVector2D((i + 1) * 0.1, 1.0), FVector2D::ZeroVector);
				const int32 d = B.Vert(S, Bv[i], N[i], FVector2D(i * 0.1, 1.0), FVector2D::ZeroVector);
				B.Tri(S, a, b, c);
				B.Tri(S, a, c, d);
			}
		};
		TArray<FVector> Base, Mid, Top, BackTop, BackBase, In, Out, Up;
		for (int32 i = 0; i <= WallSteps; ++i)
		{
			const double A = -FoulLineDeg + 2.0 * FoulLineDeg * i / WallSteps;
			const double Rad = FMath::DegreesToRadians(A);
			const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.0);
			const double R = FenceAt(A) * 100.0;
			Base.Add(Dir * R);
			Mid.Add(Dir * R + FVector(0.0, 0.0, Line));
			Top.Add(Dir * R + FVector(0.0, 0.0, H));
			BackTop.Add(Dir * (R + WallThicknessCm) + FVector(0.0, 0.0, H));
			BackBase.Add(Dir * (R + WallThicknessCm));
			In.Add(-Dir);
			Out.Add(Dir);
			Up.Add(FVector::UpVector);
		}
		Strip(Pad, Base, Mid, In);
		Strip(Cap, Mid, Top, In);
		Strip(Cap, Top, BackTop, Up);
		Strip(Pad, BackTop, BackBase, Out);
		return B.Build(Outer, TEXT("DerbyWall"));
	}
}

void ADerbyBallpark::BuildField()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Grass = Materials.Surface(this, { Hex(TEXT("#2f6b2a")), TEXT("leafy_grass"), 4.0, 0.25, 0.5, 0.4, 0.9, 0.0, 0.0, false, 0.6 });
	UMaterialInterface* GrassLight = Materials.Surface(this, { Hex(TEXT("#3d8033")), TEXT("leafy_grass"), 4.0, 0.25, 0.5, 0.4, 0.9, 0.0, 0.0, false, 0.6 });
	UMaterialInterface* Dirt = Materials.Surface(this, { Hex(TEXT("#a8673b")), TEXT("gravelly_sand"), 2.0, 0.3, 0.5, 0.4, 0.95, 0.0, 0.0, false, 0.3 });
	UMaterialInterface* Track = Materials.Surface(this, { Hex(TEXT("#8a5332")), TEXT("gravelly_sand"), 2.0, 0.3, 0.5, 0.4, 0.95, 0.0, 0.0, false, 0.3 });
	UMaterialInterface* Concourse = Materials.Surface(this, { Hex(TEXT("#16181e")), TEXT("painted_concrete"), 4.0, 0.2, 0.5, 0.4, 0.9 });
	UMaterialInterface* Chalk = Materials.Plain(this, Hex(TEXT("#f4f4ee")), 0.7, 0.0, 0.15);
	UMaterialInterface* Rubber = Materials.Plain(this, Hex(TEXT("#f4f4ee")), 0.5);

	// The ground beyond, the outfield lawn (dark), its mown stripes, the warning track, then the diamond.
	Disc(Concourse, FVector(0.0, 0.0, -0.3), 420.0, 0.2);
	Disc(Grass, FVector(0.0, 0.0, -0.1), 128.0, 0.1);
	BuildOutfieldGrass(GrassLight);
	for (double A = -46.0; A < 46.0; A += TrackStepDeg)
	{
		const double Mid = A + TrackStepDeg / 2.0;
		const double R = FenceAt(Mid) - TrackWidthM / 2.0;
		Box(Track, FVector(FMath::Cos(FMath::DegreesToRadians(Mid)) * R, FMath::Sin(FMath::DegreesToRadians(Mid)) * R, 0.012),
			FVector(TrackWidthM, R * FMath::DegreesToRadians(1.6), 0.02), Mid);
	}
	BuildDiamond(Dirt, Grass, Rubber, Chalk);
	// The backstop behind home and the low wall along the foul lines.
	UMaterialInterface* Pad = Materials.Plain(this, Hex(TEXT("#16331f")), 0.7);
	for (double A = 120.0; A <= 240.0; A += 4.0)
	{
		const double R = BackstopRadiusM;
		Box(Pad, FVector(FMath::Cos(FMath::DegreesToRadians(A + 2.0)) * R, FMath::Sin(FMath::DegreesToRadians(A + 2.0)) * R, 1.0), FVector(0.4, R * FMath::DegreesToRadians(4.1), 2.0), A + 2.0);
	}
}

void ADerbyBallpark::BuildOutfieldGrass(UMaterialInterface* Stripe)
{
	for (int32 k = -StripesEachSide; k <= StripesEachSide; ++k)
	{
		if (k % 2 == 0)
		{
			continue;
		}
		const double Y = k * StripeWidthM;
		// From the foul line (or the plate) out to the warning track: probe the fence along this lateral.
		const double X = FMath::Max(FMath::Abs(Y), 0.0);
		double End = X;
		for (double Probe = X; Probe < StripeProbeEndM; Probe += StripeProbeStepM)
		{
			const double Spray = FMath::RadiansToDegrees(FMath::Atan2(Y, Probe));
			if (FMath::Sqrt(Probe * Probe + Y * Y) >= FenceAt(Spray) - TrackWidthM)
			{
				break;
			}
			End = Probe;
		}
		const double Start = FMath::Max(X + 1.0, 4.0);
		if (End > Start + 1.0)
		{
			Box(Stripe, FVector((Start + End) / 2.0, Y, 0.0), FVector(End - Start, StripeWidthM, 0.01));
		}
	}
}

void ADerbyBallpark::BuildDiamond(UMaterialInterface* Dirt, UMaterialInterface* Grass, UMaterialInterface* Rubber, UMaterialInterface* Chalk)
{
	// The infield skin and its grass, the plate's circle, the mound and the rubber.
	Disc(Dirt, FVector(17.0, 0.0, 0.0), 29.0, 0.03);
	Box(Grass, FVector(19.4, 0.0, 0.0), FVector(23.5, 23.5, 0.07), 45.0);
	Disc(Dirt, FVector(0.0, 0.0, 0.0), 4.2, 0.09);
	Disc(Dirt, FVector(MoundX, 0.0, 0.0), 2.75, 0.25);
	Disc(Dirt, FVector(MoundX, 0.0, 0.0), 1.8, 0.33);
	Box(Rubber, FVector(MoundX, 0.0, 0.34), FVector(0.15, 0.6, 0.04));
	// Base paths: dirt along the lines between the bags; the bags on their cut-outs.
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector A = Bases[i], B = Bases[(i + 1) % 4];
		const FVector D = B - A;
		Box(Dirt, (A + B) / 2.0 + FVector(0.0, 0.0, 0.08), FVector(D.Size(), 1.9, 0.02), FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)));
	}
	for (int32 i = 1; i < 4; ++i)
	{
		Disc(Dirt, Bases[i], 2.2, 0.095);
		Box(Rubber, Bases[i] + FVector(0.0, 0.0, 0.12), FVector(0.38, 0.38, 0.08), 45.0, true);
	}
	// Home plate, the batter's boxes, the foul lines out to the poles.
	Box(Rubber, FVector(0.22, 0.0, 0.1), FVector(0.43, 0.43, 0.02));
	for (const double Side : { -1.0, 1.0 })
	{
		const double Cy = Side * BoxOffsetM;
		Box(Chalk, FVector(0.25, Cy - 0.61, 0.101), FVector(1.83, 0.06, 0.01));
		Box(Chalk, FVector(0.25, Cy + 0.61, 0.101), FVector(1.83, 0.06, 0.01));
		Box(Chalk, FVector(0.25 - 0.915, Cy, 0.101), FVector(0.06, 1.22, 0.01));
		Box(Chalk, FVector(0.25 + 0.915, Cy, 0.101), FVector(0.06, 1.22, 0.01));
		const double L = FenceAt(FoulLineDeg);
		const double A = FMath::DegreesToRadians(Side * FoulLineDeg);
		Box(Chalk, FVector(FMath::Cos(A) * (L / 2.0 + 1.0), FMath::Sin(A) * (L / 2.0 + 1.0), 0.1), FVector(L - 2.0, 0.12, 0.01), Side * FoulLineDeg);
	}
}

void ADerbyBallpark::BuildWall()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Pad = Materials.Plain(this, Hex(TEXT("#1b4a2c")), 0.75);
	UMaterialInterface* Cap = Materials.Plain(this, Hex(TEXT("#f2c230")), 0.5, 0.0, 0.25);
	UMaterialInterface* Pole = Materials.Plain(this, Hex(TEXT("#f2c230")), 0.4, 0.2, 0.6);
	UMaterialInterface* Eye = Materials.Plain(this, Hex(TEXT("#0f2318")), 0.9);
	UStaticMeshComponent* Wall = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("Wall")));
	Wall->SetStaticMesh(BuildWallMesh(this));
	Wall->SetMaterial(0, Pad);
	Wall->SetMaterial(1, Cap);
	Wall->SetupAttachment(RootComponent);
	Wall->SetMobility(EComponentMobility::Movable);
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AddPart(Wall);
	// The foul poles, with the screen on each pole's fair side.
	for (const double Side : { -1.0, 1.0 })
	{
		const FVector Base = Ground(Side * FoulLineDeg, FenceAt(FoulLineDeg) + 0.3) / 100.0;
		Groups.Find(Cylinder, Pole, true)->AddInstance(FTransform(FRotator::ZeroRotator, M(Base + FVector(0.0, 0.0, 12.5)), FVector(0.5, 0.5, 25.0)), false);
		Box(Pole, Base + FVector(-0.3, -Side * 0.7, 14.0), FVector(0.05, 1.2, 18.0), Side * FoulLineDeg);
	}
	// The batter's eye in dead centre: a dark screen the pitch is seen against.
	for (double A = -9.0; A < 9.0; A += 3.0)
	{
		const double Mid = A + 1.5;
		const double R = FenceAt(Mid) + 4.0;
		const double Rad = FMath::DegreesToRadians(Mid);
		Box(Eye, FVector(FMath::Cos(Rad) * R, FMath::Sin(Rad) * R, 5.0), FVector(1.0, R * FMath::DegreesToRadians(3.1), 10.0), Mid);
	}
	BuildWallMarkings();
}

void ADerbyBallpark::BuildWallMarkings()
{
	using namespace MovecadeKart::Art;
	// Distance markers (feet, to the nearest five) down the lines, in the gaps and at centre, facing home.
	const FLinearColor Marker = Hex(TEXT("#f4f4ee"));
	for (const double Mark : { -45.0, -22.5, 0.0, 22.5, 45.0 })
	{
		const double Ft = FMath::RoundToDouble(FenceAt(Mark) / MetresPerFoot / 5.0) * 5.0;
		// The line markers sit a little inside the poles.
		const double At = FMath::Abs(Mark) > 44.0 ? Mark - FMath::Sign(Mark) * 2.0 : Mark;
		FVector Face;
		double Yaw = 0.0;
		WallFace(At, WallFaceInsetM, Face, Yaw);
		Text(FString::Printf(TEXT("%d"), static_cast<int32>(Ft)), Face + FVector(0.0, 0.0, 1.5), Yaw, 150.f, Marker);
	}
	// The sponsors' panels.
	struct FAd { const TCHAR* Text; const TCHAR* Color; double SprayDeg; };
	const FAd Ads[] = {
		{ TEXT("MOVECADE"), TEXT("#f2b23f"), -37.0 }, { TEXT("SWING!"), TEXT("#3fc9c2"), -30.0 }, { TEXT("MOVECADE"), TEXT("#ff6b5a"), -12.0 },
		{ TEXT("PARTY MODE"), TEXT("#a083ff"), 12.0 }, { TEXT("MOVECADE"), TEXT("#f2b23f"), 30.0 }, { TEXT("GO DEEP"), TEXT("#3fc9c2"), 37.0 },
	};
	for (const FAd& Ad : Ads)
	{
		FVector Face;
		double Yaw = 0.0;
		WallFace(Ad.SprayDeg, WallFaceInsetM, Face, Yaw);
		Text(Ad.Text, Face + FVector(0.0, 0.0, 1.4), Yaw, 120.f, Hex(Ad.Color));
	}
}

void ADerbyBallpark::BuildTowers()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Steel = Materials.Plain(this, Hex(TEXT("#2a2d35")), 0.5, 0.7);
	UMaterialInterface* Lamp = Materials.GlowOpaque(this, Hex(TEXT("#fff3d6")) * 18.f);
	// The catwalk's glowing rail under the lamps: a solid bar, so opaque like the lamps.
	UMaterialInterface* Halo = Materials.GlowOpaque(this, Hex(TEXT("#ffe2a8")) * 1.2f);
	for (const FVector2D& Spot : TowerSpots)
	{
		const FVector Base = Ground(Spot.X, Spot.Y) / 100.0;
		Box(Steel, Base + FVector(0.0, 0.0, TowerHeightM / 2.0), FVector(1.4, 1.4, TowerHeightM), Spot.X, true);
		// The head faces the middle of the field.
		const FVector Aim = TowerAimM - Base;
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Aim.Y, Aim.X));
		const FVector Face(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0);
		const FVector Side(-Face.Y, Face.X, 0.0);
		const FVector Head = Base + FVector(0.0, 0.0, TowerHeightM + 3.5);
		Box(Steel, Head, FVector(1.0, 13.0, 8.0), Yaw, true);
		for (int32 Row = 0; Row < LampRows; ++Row)
		{
			for (int32 Column = 0; Column < LampColumns; ++Column)
			{
				const FVector P = Head + Face * 0.55 + Side * ((Column - 2) * LampSpacingM) + FVector(0.0, 0.0, (Row - 1) * LampSpacingM);
				Box(Lamp, P, FVector(0.2, 2.0, 2.0), Yaw);
			}
		}
		Box(Halo, Head + Face * 0.6 + FVector(0.0, 0.0, -4.4), FVector(0.15, 12.0, 0.15), Yaw);
	}
}
