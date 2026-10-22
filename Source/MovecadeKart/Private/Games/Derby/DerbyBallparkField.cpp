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
	// Every part below is metres: X toward centre field, Y toward right field, Z up; sizes are full extents.

	/** A flat disc: centre (X, its base Z), radius and thickness. */
	struct FDiscSpec
	{
		double X;
		double Z;
		double RadiusM;
		double HeightM;
	};

	/** The ground: the concourse beyond everything, the outfield lawn and its mown stripes, the warning track. */
	struct FGroundSpec
	{
		FDiscSpec Concourse;
		FDiscSpec Lawn;
		/** Every other stripe this wide is lighter, out to the warning track (probed along each lateral). */
		double StripeWidthM;
		int32 StripesEachSide;
		double StripeStartM;
		double StripeLineGapM;
		double StripeMinLengthM;
		double StripeThicknessM;
		double StripeProbeEndM;
		double StripeProbeStepM;
		/** The track along the wall, laid in arc steps a hair wider than the step so they meet. */
		double TrackWidthM;
		double TrackFromDeg;
		double TrackToDeg;
		double TrackStepDeg;
		double TrackArcDeg;
		double TrackZ;
		double TrackThicknessM;
	};
	constexpr FGroundSpec OutfieldSpec{ .Concourse = { 0.0, -0.3, 420.0, 0.2 }, .Lawn = { 0.0, -0.1, 128.0, 0.1 },
		.StripeWidthM = 6.0, .StripesEachSide = 17, .StripeStartM = 4.0, .StripeLineGapM = 1.0, .StripeMinLengthM = 1.0, .StripeThicknessM = 0.01,
		.StripeProbeEndM = 130.0, .StripeProbeStepM = 0.5,
		.TrackWidthM = 4.6, .TrackFromDeg = -46.0, .TrackToDeg = 46.0, .TrackStepDeg = 1.5, .TrackArcDeg = 1.6, .TrackZ = 0.012, .TrackThicknessM = 0.02 };

	/** The diamond: the skin, its grass square, the circles, the mound, the bases and the chalk. */
	struct FDiamondSpec
	{
		FDiscSpec Skin;
		double InfieldGrassX;
		double InfieldGrassSize;
		double InfieldGrassThickness;
		FDiscSpec HomeCircle;
		/** The mound's two tiers (centred on ADerbyBallpark::MoundX) and the rubber on top. */
		double MoundRadius;
		double MoundHeight;
		double MoundTopRadius;
		double MoundTopHeight;
		double RubberZ;
		double RubberDepth;
		double RubberWidth;
		double RubberThickness;
		/** Base paths along the lines, the cut-outs round the bags, the bags (turned 45 degrees). */
		double PathWidth;
		double PathZ;
		double PathThickness;
		double CutoutRadius;
		double CutoutHeight;
		double BagZ;
		double BagSize;
		double BagThickness;
		/** Home plate. */
		double PlateX;
		double PlateZ;
		double PlateSize;
		double PlateThickness;
		/** The batter's boxes either side of the plate (4 ft by 6 ft), and the chalk lines out to the poles. */
		double BoxCentreX;
		double BoxOffsetY;
		double BoxHalfWidth;
		double BoxHalfLength;
		double BoxLength;
		double BoxWidth;
		double ChalkWidth;
		double ChalkZ;
		double ChalkThickness;
		double FoulLineWidth;
		double FoulLineZ;
		double FoulLineStart;
	};
	constexpr FDiamondSpec DiamondSpec{ .Skin = { 17.0, 0.0, 29.0, 0.03 }, .InfieldGrassX = 19.4, .InfieldGrassSize = 23.5, .InfieldGrassThickness = 0.07,
		.HomeCircle = { 0.0, 0.0, 4.2, 0.09 }, .MoundRadius = 2.75, .MoundHeight = 0.25, .MoundTopRadius = 1.8, .MoundTopHeight = 0.33,
		.RubberZ = 0.34, .RubberDepth = 0.15, .RubberWidth = 0.6, .RubberThickness = 0.04,
		.PathWidth = 1.9, .PathZ = 0.08, .PathThickness = 0.02, .CutoutRadius = 2.2, .CutoutHeight = 0.095, .BagZ = 0.12, .BagSize = 0.38, .BagThickness = 0.08,
		.PlateX = 0.22, .PlateZ = 0.1, .PlateSize = 0.43, .PlateThickness = 0.02,
		.BoxCentreX = 0.25, .BoxOffsetY = 0.95, .BoxHalfWidth = 0.61, .BoxHalfLength = 0.915, .BoxLength = 1.83, .BoxWidth = 1.22, .ChalkWidth = 0.06,
		.ChalkZ = 0.101, .ChalkThickness = 0.01, .FoulLineWidth = 0.12, .FoulLineZ = 0.1, .FoulLineStart = 1.0 };
	/** 90 ft between the bases, on the X axis: home, first (+Y), second, third. */
	const FVector Bases[] = { FVector(0.0, 0.0, 0.0), FVector(19.40, 19.40, 0.0), FVector(38.79, 0.0, 0.0), FVector(19.40, -19.40, 0.0) };

	/** The backstop's padded curve behind home, in arc steps. */
	struct FBackstopSpec
	{
		double RadiusM;
		double FromDeg;
		double ToDeg;
		double StepDeg;
		double ArcDeg;
		double Depth;
		double Height;
	};
	constexpr FBackstopSpec BackstopSpec{ .RadiusM = 18.0, .FromDeg = 120.0, .ToDeg = 240.0, .StepDeg = 4.0, .ArcDeg = 4.1, .Depth = 0.4, .Height = 2.0 };

	/** The outfield wall (its height is the rules' fence), the foul poles and their screens, the batter's eye, the lettering. */
	struct FWallSpec
	{
		double LineBelowTopCm;
		double ThicknessCm;
		int32 Steps;
		double PoleBehindFence;
		double PoleDiameter;
		double PoleHeight;
		/** Each pole's screen, on its fair side. */
		double ScreenBack;
		double ScreenOut;
		double ScreenZ;
		double ScreenDepth;
		double ScreenWidth;
		double ScreenHeight;
		/** The batter's eye: dark panels behind the fence in dead centre, in arc steps. */
		double EyeFromDeg;
		double EyeToDeg;
		double EyeStepDeg;
		double EyeArcDeg;
		double EyeBehindFence;
		double EyeDepth;
		double EyeHeight;
		/** Lettering just proud of the wall's face: markers (feet to the nearest five), ads. */
		double FaceInset;
		double MarkerZ;
		float MarkerSizeCm;
		double MarkerRoundFt;
		double LineMarkerFromDeg;
		double LineMarkerInset;
		double AdZ;
		float AdSizeCm;
	};
	constexpr FWallSpec WallSpec{ .LineBelowTopCm = 14.0, .ThicknessCm = 45.0, .Steps = 180, .PoleBehindFence = 0.3, .PoleDiameter = 0.5, .PoleHeight = 25.0,
		.ScreenBack = 0.3, .ScreenOut = 0.7, .ScreenZ = 14.0, .ScreenDepth = 0.05, .ScreenWidth = 1.2, .ScreenHeight = 18.0,
		.EyeFromDeg = -9.0, .EyeToDeg = 9.0, .EyeStepDeg = 3.0, .EyeArcDeg = 3.1, .EyeBehindFence = 4.0, .EyeDepth = 1.0, .EyeHeight = 10.0,
		.FaceInset = 0.12, .MarkerZ = 1.5, .MarkerSizeCm = 150.f, .MarkerRoundFt = 5.0, .LineMarkerFromDeg = 44.0, .LineMarkerInset = 2.0,
		.AdZ = 1.4, .AdSizeCm = 120.f };
	const double MarkerSprays[] = { -45.0, -22.5, 0.0, 22.5, 45.0 };
	struct FAd
	{
		const TCHAR* Text;
		const TCHAR* Color;
		double SprayDeg;
	};
	const FAd Ads[] = {
		{ TEXT("MOVECADE"), TEXT("#f2b23f"), -37.0 }, { TEXT("SWING!"), TEXT("#3fc9c2"), -30.0 }, { TEXT("MOVECADE"), TEXT("#ff6b5a"), -12.0 },
		{ TEXT("PARTY MODE"), TEXT("#a083ff"), 12.0 }, { TEXT("MOVECADE"), TEXT("#f2b23f"), 30.0 }, { TEXT("GO DEEP"), TEXT("#3fc9c2"), 37.0 },
	};

	/** A light tower: the mast, the head aimed at the middle of the field, its lamp bank and the catwalk rail under it. */
	struct FTowerSpec
	{
		double HeightM;
		double MastSize;
		double AimX;
		double HeadAboveMast;
		double HeadDepth;
		double HeadWidth;
		double HeadHeight;
		int32 LampRows;
		int32 LampColumns;
		double LampSpacing;
		double LampFront;
		double LampDepth;
		double LampSize;
		double RailFront;
		double RailBelowHead;
		double RailThickness;
		double RailLength;
	};
	constexpr FTowerSpec TowerSpec{ .HeightM = 46.0, .MastSize = 1.4, .AimX = 45.0, .HeadAboveMast = 3.5, .HeadDepth = 1.0, .HeadWidth = 13.0, .HeadHeight = 8.0,
		.LampRows = 3, .LampColumns = 5, .LampSpacing = 2.5, .LampFront = 0.55, .LampDepth = 0.2, .LampSize = 2.0,
		.RailFront = 0.6, .RailBelowHead = 4.4, .RailThickness = 0.15, .RailLength = 12.0 };
	/** Where the towers stand (spray degrees, metres out): two pairs behind the stands down the lines, one behind home. */
	const FVector2D TowerSpots[] = { FVector2D(28.0, 168.0), FVector2D(-28.0, 168.0), FVector2D(62.0, 142.0), FVector2D(-62.0, 142.0), FVector2D(128.0, 64.0), FVector2D(-128.0, 64.0) };

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
		const double Line = H - WallSpec.LineBelowTopCm;
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
		for (int32 i = 0; i <= WallSpec.Steps; ++i)
		{
			const double A = -FoulLineDeg + 2.0 * FoulLineDeg * i / WallSpec.Steps;
			const double Rad = FMath::DegreesToRadians(A);
			const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.0);
			const double R = FenceAt(A) * 100.0;
			Base.Add(Dir * R);
			Mid.Add(Dir * R + FVector(0.0, 0.0, Line));
			Top.Add(Dir * R + FVector(0.0, 0.0, H));
			BackTop.Add(Dir * (R + WallSpec.ThicknessCm) + FVector(0.0, 0.0, H));
			BackBase.Add(Dir * (R + WallSpec.ThicknessCm));
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
	const FGroundSpec& G = OutfieldSpec;
	UMaterialInterface* Grass = Materials.Surface(this, { Hex(TEXT("#2f6b2a")), TEXT("leafy_grass"), 4.0, 0.25, 0.5, 0.4, 0.9, 0.0, 0.0, false, 0.6 });
	UMaterialInterface* GrassLight = Materials.Surface(this, { Hex(TEXT("#3d8033")), TEXT("leafy_grass"), 4.0, 0.25, 0.5, 0.4, 0.9, 0.0, 0.0, false, 0.6 });
	UMaterialInterface* Dirt = Materials.Surface(this, { Hex(TEXT("#a8673b")), TEXT("gravelly_sand"), 2.0, 0.3, 0.5, 0.4, 0.95, 0.0, 0.0, false, 0.3 });
	UMaterialInterface* Track = Materials.Surface(this, { Hex(TEXT("#8a5332")), TEXT("gravelly_sand"), 2.0, 0.3, 0.5, 0.4, 0.95, 0.0, 0.0, false, 0.3 });
	UMaterialInterface* Concourse = Materials.Surface(this, { Hex(TEXT("#16181e")), TEXT("painted_concrete"), 4.0, 0.2, 0.5, 0.4, 0.9 });
	UMaterialInterface* Chalk = Materials.Plain(this, Hex(TEXT("#f4f4ee")), 0.7, 0.0, 0.15);
	UMaterialInterface* Rubber = Materials.Plain(this, Hex(TEXT("#f4f4ee")), 0.5);

	// The ground beyond, the outfield lawn (dark), its mown stripes, the warning track, then the diamond.
	Disc(Concourse, FVector(G.Concourse.X, 0.0, G.Concourse.Z), G.Concourse.RadiusM, G.Concourse.HeightM);
	Disc(Grass, FVector(G.Lawn.X, 0.0, G.Lawn.Z), G.Lawn.RadiusM, G.Lawn.HeightM);
	BuildOutfieldGrass(GrassLight);
	for (double A = G.TrackFromDeg; A < G.TrackToDeg; A += G.TrackStepDeg)
	{
		const double Mid = A + G.TrackStepDeg / 2.0;
		const double R = FenceAt(Mid) - G.TrackWidthM / 2.0;
		Box(Track, FVector(FMath::Cos(FMath::DegreesToRadians(Mid)) * R, FMath::Sin(FMath::DegreesToRadians(Mid)) * R, G.TrackZ),
			FVector(G.TrackWidthM, R * FMath::DegreesToRadians(G.TrackArcDeg), G.TrackThicknessM), Mid);
	}
	BuildDiamond(Dirt, Grass, Rubber, Chalk);
	// The backstop behind home.
	const FBackstopSpec& Back = BackstopSpec;
	UMaterialInterface* Pad = Materials.Plain(this, Hex(TEXT("#16331f")), 0.7);
	for (double A = Back.FromDeg; A <= Back.ToDeg; A += Back.StepDeg)
	{
		const double Mid = A + Back.StepDeg / 2.0;
		const double R = Back.RadiusM;
		Box(Pad, FVector(FMath::Cos(FMath::DegreesToRadians(Mid)) * R, FMath::Sin(FMath::DegreesToRadians(Mid)) * R, Back.Height / 2.0),
			FVector(Back.Depth, R * FMath::DegreesToRadians(Back.ArcDeg), Back.Height), Mid);
	}
}

void ADerbyBallpark::BuildOutfieldGrass(UMaterialInterface* Stripe)
{
	const FGroundSpec& G = OutfieldSpec;
	for (int32 k = -G.StripesEachSide; k <= G.StripesEachSide; ++k)
	{
		if (k % 2 == 0)
		{
			continue;
		}
		const double Y = k * G.StripeWidthM;
		// From the foul line (or the plate) out to the warning track: probe the fence along this lateral.
		const double X = FMath::Max(FMath::Abs(Y), 0.0);
		double End = X;
		for (double Probe = X; Probe < G.StripeProbeEndM; Probe += G.StripeProbeStepM)
		{
			const double Spray = FMath::RadiansToDegrees(FMath::Atan2(Y, Probe));
			if (FMath::Sqrt(Probe * Probe + Y * Y) >= FenceAt(Spray) - G.TrackWidthM)
			{
				break;
			}
			End = Probe;
		}
		const double Start = FMath::Max(X + G.StripeLineGapM, G.StripeStartM);
		if (End > Start + G.StripeMinLengthM)
		{
			Box(Stripe, FVector((Start + End) / 2.0, Y, 0.0), FVector(End - Start, G.StripeWidthM, G.StripeThicknessM));
		}
	}
}

void ADerbyBallpark::BuildDiamond(UMaterialInterface* Dirt, UMaterialInterface* Grass, UMaterialInterface* Rubber, UMaterialInterface* Chalk)
{
	const FDiamondSpec& D = DiamondSpec;
	// The infield skin and its grass, the plate's circle, the mound and the rubber.
	Disc(Dirt, FVector(D.Skin.X, 0.0, D.Skin.Z), D.Skin.RadiusM, D.Skin.HeightM);
	Box(Grass, FVector(D.InfieldGrassX, 0.0, 0.0), FVector(D.InfieldGrassSize, D.InfieldGrassSize, D.InfieldGrassThickness), 45.0);
	Disc(Dirt, FVector(D.HomeCircle.X, 0.0, D.HomeCircle.Z), D.HomeCircle.RadiusM, D.HomeCircle.HeightM);
	Disc(Dirt, FVector(MoundX, 0.0, 0.0), D.MoundRadius, D.MoundHeight);
	Disc(Dirt, FVector(MoundX, 0.0, 0.0), D.MoundTopRadius, D.MoundTopHeight);
	Box(Rubber, FVector(MoundX, 0.0, D.RubberZ), FVector(D.RubberDepth, D.RubberWidth, D.RubberThickness));
	// Base paths: dirt along the lines between the bags; the bags on their cut-outs.
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector A = Bases[i], B = Bases[(i + 1) % 4];
		const FVector Run = B - A;
		Box(Dirt, (A + B) / 2.0 + FVector(0.0, 0.0, D.PathZ), FVector(Run.Size(), D.PathWidth, D.PathThickness), FMath::RadiansToDegrees(FMath::Atan2(Run.Y, Run.X)));
	}
	for (int32 i = 1; i < 4; ++i)
	{
		Disc(Dirt, Bases[i], D.CutoutRadius, D.CutoutHeight);
		Box(Rubber, Bases[i] + FVector(0.0, 0.0, D.BagZ), FVector(D.BagSize, D.BagSize, D.BagThickness), 45.0, true);
	}
	// Home plate, the batter's boxes, the foul lines out to the poles.
	Box(Rubber, FVector(D.PlateX, 0.0, D.PlateZ), FVector(D.PlateSize, D.PlateSize, D.PlateThickness));
	for (const double Side : { -1.0, 1.0 })
	{
		const double Cy = Side * D.BoxOffsetY;
		Box(Chalk, FVector(D.BoxCentreX, Cy - D.BoxHalfWidth, D.ChalkZ), FVector(D.BoxLength, D.ChalkWidth, D.ChalkThickness));
		Box(Chalk, FVector(D.BoxCentreX, Cy + D.BoxHalfWidth, D.ChalkZ), FVector(D.BoxLength, D.ChalkWidth, D.ChalkThickness));
		Box(Chalk, FVector(D.BoxCentreX - D.BoxHalfLength, Cy, D.ChalkZ), FVector(D.ChalkWidth, D.BoxWidth, D.ChalkThickness));
		Box(Chalk, FVector(D.BoxCentreX + D.BoxHalfLength, Cy, D.ChalkZ), FVector(D.ChalkWidth, D.BoxWidth, D.ChalkThickness));
		const double L = FenceAt(FoulLineDeg);
		const double A = FMath::DegreesToRadians(Side * FoulLineDeg);
		const double Centre = L / 2.0 + D.FoulLineStart;
		Box(Chalk, FVector(FMath::Cos(A) * Centre, FMath::Sin(A) * Centre, D.FoulLineZ), FVector(L - 2.0 * D.FoulLineStart, D.FoulLineWidth, D.ChalkThickness),
			Side * FoulLineDeg);
	}
}

void ADerbyBallpark::BuildWall()
{
	using namespace MovecadeKart::Art;
	const FWallSpec& W = WallSpec;
	UMaterialInterface* Pad = Materials.Plain(this, Hex(TEXT("#1b4a2c")), 0.75);
	UMaterialInterface* Cap = Materials.Plain(this, Hex(TEXT("#f2c230")), 0.5, 0.0, 0.25);
	UMaterialInterface* Pole = Materials.Plain(this, Hex(TEXT("#f2c230")), 0.4, 0.2, 0.6);
	UMaterialInterface* Eye = Materials.Plain(this, Hex(TEXT("#0f2318")), 0.9);
	UStaticMeshComponent* WallPart = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("Wall")));
	WallPart->SetStaticMesh(BuildWallMesh(this));
	WallPart->SetMaterial(0, Pad);
	WallPart->SetMaterial(1, Cap);
	WallPart->SetupAttachment(RootComponent);
	WallPart->SetMobility(EComponentMobility::Movable);
	WallPart->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AddPart(WallPart);
	// The foul poles, with the screen on each pole's fair side.
	for (const double Side : { -1.0, 1.0 })
	{
		const FVector Base = Ground(Side * FoulLineDeg, FenceAt(FoulLineDeg) + W.PoleBehindFence) / 100.0;
		const FVector PoleSize(W.PoleDiameter, W.PoleDiameter, W.PoleHeight);
		FindGroup(Cylinder, Pole, true)->AddInstance(FTransform(FRotator::ZeroRotator, M(Base + FVector(0.0, 0.0, W.PoleHeight / 2.0)), PoleSize), false);
		Box(Pole, Base + FVector(-W.ScreenBack, -Side * W.ScreenOut, W.ScreenZ), FVector(W.ScreenDepth, W.ScreenWidth, W.ScreenHeight), Side * FoulLineDeg);
	}
	// The batter's eye in dead centre: a dark screen the pitch is seen against.
	for (double A = W.EyeFromDeg; A < W.EyeToDeg; A += W.EyeStepDeg)
	{
		const double Mid = A + W.EyeStepDeg / 2.0;
		const double R = FenceAt(Mid) + W.EyeBehindFence;
		const double Rad = FMath::DegreesToRadians(Mid);
		Box(Eye, FVector(FMath::Cos(Rad) * R, FMath::Sin(Rad) * R, W.EyeHeight / 2.0), FVector(W.EyeDepth, R * FMath::DegreesToRadians(W.EyeArcDeg), W.EyeHeight), Mid);
	}
	BuildWallMarkings();
}

void ADerbyBallpark::BuildWallMarkings()
{
	using namespace MovecadeKart::Art;
	const FWallSpec& W = WallSpec;
	// Distance markers (feet, to the nearest five) down the lines, in the gaps and at centre, facing home.
	const FLinearColor Marker = Hex(TEXT("#f4f4ee"));
	for (const double Mark : MarkerSprays)
	{
		const double Ft = FMath::RoundToDouble(FenceAt(Mark) / MetresPerFoot / W.MarkerRoundFt) * W.MarkerRoundFt;
		// The line markers sit a little inside the poles.
		const double At = FMath::Abs(Mark) > W.LineMarkerFromDeg ? Mark - FMath::Sign(Mark) * W.LineMarkerInset : Mark;
		FVector Face;
		double Yaw = 0.0;
		WallFace(At, W.FaceInset, Face, Yaw);
		Text(FString::Printf(TEXT("%d"), static_cast<int32>(Ft)), Face + FVector(0.0, 0.0, W.MarkerZ), Yaw, W.MarkerSizeCm, Marker);
	}
	// The sponsors' panels.
	for (const FAd& Ad : Ads)
	{
		FVector Face;
		double Yaw = 0.0;
		WallFace(Ad.SprayDeg, W.FaceInset, Face, Yaw);
		Text(Ad.Text, Face + FVector(0.0, 0.0, W.AdZ), Yaw, W.AdSizeCm, Hex(Ad.Color));
	}
}

void ADerbyBallpark::BuildTowers()
{
	using namespace MovecadeKart::Art;
	const FTowerSpec& T = TowerSpec;
	UMaterialInterface* Steel = Materials.Plain(this, Hex(TEXT("#2a2d35")), 0.5, 0.7);
	UMaterialInterface* Lamp = Materials.GlowOpaque(this, Hex(TEXT("#fff3d6")) * 18.f);
	// The catwalk's glowing rail under the lamps: a solid bar, so opaque like the lamps.
	UMaterialInterface* Rail = Materials.GlowOpaque(this, Hex(TEXT("#ffe2a8")) * 1.2f);
	for (const FVector2D& Spot : TowerSpots)
	{
		const FVector Base = Ground(Spot.X, Spot.Y) / 100.0;
		Box(Steel, Base + FVector(0.0, 0.0, T.HeightM / 2.0), FVector(T.MastSize, T.MastSize, T.HeightM), Spot.X, true);
		// The head faces the middle of the field.
		const FVector Aim = FVector(T.AimX, 0.0, 0.0) - Base;
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Aim.Y, Aim.X));
		const FVector Face(FMath::Cos(FMath::DegreesToRadians(Yaw)), FMath::Sin(FMath::DegreesToRadians(Yaw)), 0.0);
		const FVector Side(-Face.Y, Face.X, 0.0);
		const FVector Head = Base + FVector(0.0, 0.0, T.HeightM + T.HeadAboveMast);
		Box(Steel, Head, FVector(T.HeadDepth, T.HeadWidth, T.HeadHeight), Yaw, true);
		// The lamp bank, centred on the head.
		for (int32 Row = 0; Row < T.LampRows; ++Row)
		{
			for (int32 Column = 0; Column < T.LampColumns; ++Column)
			{
				const FVector P = Head + Face * T.LampFront + Side * ((Column - T.LampColumns / 2) * T.LampSpacing) + FVector(0.0, 0.0, (Row - T.LampRows / 2) * T.LampSpacing);
				Box(Lamp, P, FVector(T.LampDepth, T.LampSize, T.LampSize), Yaw);
			}
		}
		Box(Rail, Head + Face * T.RailFront + FVector(0.0, 0.0, -T.RailBelowHead), FVector(T.RailThickness, T.RailLength, T.RailThickness), Yaw);
	}
}
