// ADerbyBallpark's stands: bleachers in an arc beyond the wall (the batter's eye left clear), the grandstand round
// home, and stands down the lines, every row seated with instanced fans. Each crowd section hangs off its own pivot
// so the Tick can bounce it; a fan cheers (arms up) three times in ten. Metres.

#include "Games/Derby/DerbyBallpark.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Derby/DerbyBallparkParts.h"

using namespace MovecadeKart::Derby;
using namespace MovecadeKart::Derby::Ballpark;

/**
 * One arc of stepped stands round the plate, swept in steps of StepDeg: its rows (how high the first sits, how much each
 * rises), and the back wall behind the last row with a neon trim and, for a grandstand, a roof. Metres and degrees.
 */
struct FDerbyStandArc
{
	double FromDeg = 0.0;
	double ToDeg = 0.0;
	double StepDeg = 0.0;
	int32 Rows = 0;
	double FirstRowZ = 0.0;
	double RowRise = 0.0;
	/** Degrees of arc each step's riser covers (a hair over the step, so the steps meet) and its fans spread over. */
	double RiserArcDeg = 0.0;
	double FanArcDeg = 0.0;
	/** The back wall: this far behind the last row, this thick, rising this far above the top row. */
	double BackGapM = 0.0;
	double FacadeDepthM = 0.0;
	double FacadeAboveM = 0.0;
	/** The trim's strip: in front of the back wall by TrimInset, TrimAbove the top row. */
	double TrimInsetM = 0.0;
	double TrimAboveM = 0.0;
	double TrimDepthM = 0.0;
	double TrimHeightM = 0.0;
	/** A roof over the upper rows (none when RoofDepth is 0), its front edge RoofInset in from the back wall. */
	double RoofInsetM = 0.0;
	double RoofDepthM = 0.0;
	double RoofArcDeg = 0.0;
	double RoofThicknessM = 0.0;
};

namespace
{
	const TCHAR* const Shirts[] = { TEXT("#d8433b"), TEXT("#f2f0ea"), TEXT("#2f5fc4"), TEXT("#f2b23f"), TEXT("#1f2a44"), TEXT("#3fc9c2") };
	const TCHAR* const Skins[] = { TEXT("#e9b48f"), TEXT("#8a5a3c") };
	/** Each section bounces on its own beat (ADerbyBallpark::Tick); fans are spread over them at random. */
	constexpr int32 CrowdSections = 12;
	constexpr int32 CrowdSeed = 0xfa25;
	/** Some seats are empty; some fans cheer; each is a touch bigger or smaller and turned a little. */
	constexpr float EmptySeatChance = 0.18f;
	constexpr float CheerChance = 0.3f;
	constexpr float FanMinScale = 0.85f;
	constexpr float FanMaxScale = 1.05f;
	constexpr float FanTurnDeg = 25.f;

	/** Every row: this deep, its riser block this far in and its seat bench further, a fan per this much of bench. */
	constexpr double RowDepthM = 1.1;
	constexpr double RiserInsetM = 0.55;
	constexpr double SeatInsetM = 0.75;
	constexpr double FanInsetM = 0.45;
	constexpr double SeatRaiseM = 0.2;
	constexpr double SeatBenchShare = 0.96;
	constexpr double FanSpacingM = 0.75;

	/** Every seat bench: its depth and height. */
	constexpr double SeatBenchM = 0.4;

	/** The bleachers beyond the wall, from pole to pole and a little past, leaving the batter's eye clear. */
	constexpr FDerbyStandArc Bleachers{ .FromDeg = -50.0, .ToDeg = 50.0, .StepDeg = 2.5, .Rows = 16, .FirstRowZ = 1.6, .RowRise = 0.62,
		.RiserArcDeg = 2.56, .FanArcDeg = 2.5, .BackGapM = 0.6, .FacadeDepthM = 1.0, .FacadeAboveM = 3.0,
		.TrimInsetM = 0.55, .TrimAboveM = 2.4, .TrimDepthM = 0.1, .TrimHeightM = 0.25 };
	/** Their first row this far past the fence (the fence clamped to the poles), the batter's eye this wide either side. */
	constexpr double BleacherBehindFenceM = 6.0;
	constexpr double BattersEyeHalfDeg = 10.0;

	/** The grandstand round home, from first base's side behind the plate to third's, under a roof with a neon edge. */
	constexpr FDerbyStandArc Grandstand{ .FromDeg = 52.0, .ToDeg = 308.0, .StepDeg = 3.0, .Rows = 18, .FirstRowZ = 1.4, .RowRise = 0.66,
		.RiserArcDeg = 3.06, .FanArcDeg = 3.0, .BackGapM = 0.6, .FacadeDepthM = 1.0, .FacadeAboveM = 6.0,
		.TrimInsetM = 10.4, .TrimAboveM = 5.7, .TrimDepthM = 0.15, .TrimHeightM = 0.2,
		.RoofInsetM = 5.0, .RoofDepthM = 11.0, .RoofArcDeg = 3.1, .RoofThicknessM = 0.5 };
	/** It hugs the backstop within this many degrees of straight behind home, easing out to the far distance down the lines. */
	constexpr double GrandstandNearM = 19.5;
	constexpr double GrandstandFarM = 34.0;
	constexpr double GrandstandHugDeg = 60.0;
	constexpr double GrandstandEaseDeg = 70.0;

	/** The stands down the lines: blocks of rows parallel to each foul line, a back wall behind each block. */
	struct FLineStandSpec
	{
		double FromM;
		double ToM;
		double BlockM;
		int32 Rows;
		double NearM;
		double FirstRowZ;
		double RowRise;
		/** Fans along each row from the block's start, this far apart. */
		int32 FansPerRow;
		double FirstFanM;
		double FanSpacingM;
		double BackGapM;
		double FacadeDepthM;
		double FacadeHeightM;
	};
	constexpr FLineStandSpec LineStands{ .FromM = 40.0, .ToM = 100.0, .BlockM = 4.0, .Rows = 12, .NearM = 12.0, .FirstRowZ = 1.4, .RowRise = 0.62,
		.FansPerRow = 5, .FirstFanM = 0.4, .FanSpacingM = 0.8, .BackGapM = 0.6, .FacadeDepthM = 1.0, .FacadeHeightM = 12.0 };

	/** A fan: a rounded body and a head, slot 0 the shirt, slot 1 the skin (cm, standing on the origin); a cheering
	 *  one has both arms up in a V with fists. */
	UStaticMesh* BuildFan(UObject* Outer, bool bCheer)
	{
		MovecadeKart::FMeshBuilder B;
		B.Section(TEXT("Shirt"));
		B.Section(TEXT("Skin"));
		B.Ellipsoid(TEXT("Shirt"), FVector(0.0, 0.0, 62.0), FVector(18.0, 23.0, 36.0), 6, 8, FVector2D::ZeroVector);
		B.Ellipsoid(TEXT("Skin"), FVector(0.0, 0.0, 113.0), FVector(13.0, 13.0, 15.0), 5, 8, FVector2D::ZeroVector);
		for (const double Side : { -1.0, 1.0 })
		{
			const FVector Shoulder(0.0, Side * 17.0, 84.0);
			const FVector Hand = bCheer ? FVector(4.0, Side * 34.0, 132.0) : FVector(10.0, Side * 21.0, 46.0);
			B.Limb(TEXT("Shirt"), Shoulder, Hand, 6.5, 5.5, 6, FVector2D::ZeroVector);
			B.Ellipsoid(TEXT("Skin"), Hand + (Hand - Shoulder).GetSafeNormal() * 5.0, FVector(6.0), 3, 6, FVector2D::ZeroVector);
		}
		return B.Build(Outer, bCheer ? TEXT("DerbyFanCheer") : TEXT("DerbyFan"));
	}
}

void ADerbyBallpark::BuildStandArc(const FDerbyStandArc& Arc, double MidDeg, double BaseM, UMaterialInterface* Riser, UMaterialInterface* Seat,
	TFunctionRef<void(const FVector&, double)> AddFan)
{
	const double Rad = FMath::DegreesToRadians(MidDeg);
	const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.0);
	for (int32 Row = 0; Row < Arc.Rows; ++Row)
	{
		const double Rr = BaseM + Row * RowDepthM;
		const double Z = Arc.FirstRowZ + Row * Arc.RowRise;
		const double Len = Rr * FMath::DegreesToRadians(Arc.RiserArcDeg);
		Box(Riser, Dir * (Rr + RiserInsetM) + FVector(0.0, 0.0, Z / 2.0), FVector(RowDepthM, Len, Z), MidDeg);
		Box(Seat, Dir * (Rr + SeatInsetM) + FVector(0.0, 0.0, Z + SeatRaiseM), FVector(SeatBenchM, Len * SeatBenchShare, SeatBenchM), MidDeg);
		const int32 Count = FMath::FloorToInt32(Len / FanSpacingM);
		for (int32 k = 0; k < Count; ++k)
		{
			const double Lat = (k + 0.5) / Count - 0.5;
			const double Ar = Rad + Lat * FMath::DegreesToRadians(Arc.FanArcDeg);
			// Facing home.
			AddFan(FVector(FMath::Cos(Ar), FMath::Sin(Ar), 0.0) * (Rr + FanInsetM) + FVector(0.0, 0.0, Z), MidDeg + 180.0);
		}
	}
}

void ADerbyBallpark::BuildStandBack(const FDerbyStandArc& Arc, double MidDeg, double BaseM, UMaterialInterface* Facade, UMaterialInterface* Trim)
{
	const FVector Dir(FMath::Cos(FMath::DegreesToRadians(MidDeg)), FMath::Sin(FMath::DegreesToRadians(MidDeg)), 0.0);
	const double Back = BaseM + Arc.Rows * RowDepthM + Arc.BackGapM;
	const double Top = Arc.FirstRowZ + Arc.Rows * Arc.RowRise;
	const double Width = Back * FMath::DegreesToRadians(Arc.RiserArcDeg);
	Box(Facade, Dir * Back + FVector(0.0, 0.0, (Top + Arc.FacadeAboveM) / 2.0), FVector(Arc.FacadeDepthM, Width, Top + Arc.FacadeAboveM), MidDeg);
	if (Arc.RoofDepthM > 0.0)
	{
		Box(Facade, Dir * (Back - Arc.RoofInsetM) + FVector(0.0, 0.0, Top + Arc.FacadeAboveM),
			FVector(Arc.RoofDepthM, Back * FMath::DegreesToRadians(Arc.RoofArcDeg), Arc.RoofThicknessM), MidDeg);
	}
	Box(Trim, Dir * (Back - Arc.TrimInsetM) + FVector(0.0, 0.0, Top + Arc.TrimAboveM), FVector(Arc.TrimDepthM, Width, Arc.TrimHeightM), MidDeg);
}

void ADerbyBallpark::BuildStands()
{
	using namespace MovecadeKart::Art;
	FanMesh = BuildFan(this, false);
	CheerMesh = BuildFan(this, true);
	UMaterialInterface* Riser = Materials.Surface(this, { Hex(TEXT("#4b505c")), TEXT("painted_concrete"), 3.0, 0.2, 0.5, 0.4, 0.9 });
	UMaterialInterface* Seat = Materials.Plain(this, Hex(TEXT("#24408e")), 0.6);
	UMaterialInterface* Facade = Materials.Plain(this, Hex(TEXT("#22252e")), 0.8);
	UMaterialInterface* Trim = Materials.GlowOpaque(this, Hex(TEXT("#3fc9c2")) * 2.4f);
	// Each section's fans, seated and cheering, share its shirt and skin (so a section reads as a block of colour).
	TArray<UInstancedStaticMeshComponent*, TInlineAllocator<CrowdSections>> Sections, Cheering;
	for (int32 i = 0; i < CrowdSections; ++i)
	{
		USceneComponent* P = Pivot(TEXT("CrowdSection"));
		Crowd.Add(P);
		UMaterialInterface* Shirt = Materials.Plain(this, Hex(Shirts[i % UE_ARRAY_COUNT(Shirts)]), 0.7);
		UMaterialInterface* Skin = Materials.Plain(this, Hex(Skins[(i / UE_ARRAY_COUNT(Shirts)) % UE_ARRAY_COUNT(Skins)]), 0.6);
		Sections.Add(FindGroup(FanMesh, Shirt, false, P, Skin));
		Cheering.Add(FindGroup(CheerMesh, Shirt, false, P, Skin));
	}
	FRandomStream R(CrowdSeed);
	const auto AddFan = [&](const FVector& FeetM, double YawDeg)
	{
		if (R.FRand() < EmptySeatChance)
		{
			return;
		}
		const int32 S = R.RandRange(0, CrowdSections - 1);
		const double Scale = R.FRandRange(FanMinScale, FanMaxScale);
		const double Yaw = YawDeg + R.FRandRange(-FanTurnDeg, FanTurnDeg);
		(R.FRand() < CheerChance ? Cheering : Sections)[S]->AddInstance(FTransform(FRotator(0.0, Yaw, 0.0), M(FeetM), FVector(Scale)), false);
	};
	// Bleachers beyond the wall.
	for (double A = Bleachers.FromDeg; A < Bleachers.ToDeg; A += Bleachers.StepDeg)
	{
		const double Mid = A + Bleachers.StepDeg / 2.0;
		if (FMath::Abs(Mid) < BattersEyeHalfDeg)
		{
			continue;
		}
		const double Base = FenceAt(FMath::Clamp(Mid, -FoulLineDeg, FoulLineDeg)) + BleacherBehindFenceM;
		BuildStandArc(Bleachers, Mid, Base, Riser, Seat, AddFan);
		BuildStandBack(Bleachers, Mid, Base, Facade, Trim);
	}
	// The grandstand round home.
	for (double A = Grandstand.FromDeg; A < Grandstand.ToDeg; A += Grandstand.StepDeg)
	{
		const double Mid = A + Grandstand.StepDeg / 2.0;
		// Down the lines the stand sits farther out (foul ground), behind the plate it hugs the backstop.
		const double Behind = FMath::Abs(FMath::FindDeltaAngleDegrees(Mid, 180.0));
		const double Ease = FMath::Clamp((Behind - GrandstandHugDeg) / GrandstandEaseDeg, 0.0, 1.0);
		const double Base = Behind < GrandstandHugDeg ? GrandstandNearM : FMath::Lerp(GrandstandNearM, GrandstandFarM, Ease);
		BuildStandArc(Grandstand, Mid, Base, Riser, Seat, AddFan);
		BuildStandBack(Grandstand, Mid, Base, Facade, Trim);
	}
	BuildLineStands(Riser, Facade, AddFan);
}

void ADerbyBallpark::BuildLineStands(UMaterialInterface* Riser, UMaterialInterface* Facade, TFunctionRef<void(const FVector&, double)> AddFan)
{
	// Down the lines: stands parallel to the foul lines, between the grandstand and the bleachers, facing the field.
	const FLineStandSpec& L = LineStands;
	for (const double Side : { -1.0, 1.0 })
	{
		const double LineRad = FMath::DegreesToRadians(Side * FoulLineDeg);
		const FVector Along(FMath::Cos(LineRad), FMath::Sin(LineRad), 0.0);
		const FVector Out = FVector(-FMath::Sin(LineRad), FMath::Cos(LineRad), 0.0) * Side;
		const double Yaw = Side * FoulLineDeg;
		for (double S = L.FromM; S < L.ToM; S += L.BlockM)
		{
			for (int32 Row = 0; Row < L.Rows; ++Row)
			{
				const double D = L.NearM + Row * RowDepthM;
				const double Z = L.FirstRowZ + Row * L.RowRise;
				const FVector C = Along * (S + L.BlockM / 2.0) + Out * (D + RiserInsetM);
				Box(Riser, C + FVector(0.0, 0.0, Z / 2.0), FVector(L.BlockM, RowDepthM, Z), Yaw);
				for (int32 k = 0; k < L.FansPerRow; ++k)
				{
					AddFan(Along * (S + L.FirstFanM + k * L.FanSpacingM) + Out * (D + FanInsetM) + FVector(0.0, 0.0, Z), Yaw + (Side > 0 ? -90.0 : 90.0));
				}
			}
			const FVector Back = Along * (S + L.BlockM / 2.0) + Out * (L.NearM + L.Rows * RowDepthM + L.BackGapM);
			Box(Facade, Back + FVector(0.0, 0.0, L.FacadeHeightM / 2.0), FVector(L.BlockM, L.FacadeDepthM, L.FacadeHeightM), Yaw);
		}
	}
}
