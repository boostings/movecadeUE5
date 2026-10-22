// ADerbyBallpark's stands: bleachers in an arc beyond the wall (the batter's eye left clear), the grandstand round
// home, and stands down the lines, every row seated with instanced fans. Each crowd section hangs off its own pivot
// so the Tick can bounce it; a fan cheers (arms up) three times in ten. Metres.

#include "Games/Derby/DerbyBallpark.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Derby/DerbyBallparkParts.h"

using namespace MovecadeKart::Derby::Ballpark;

/** One arc of stepped rows round the plate: how high the first row sits, how much each rises and how wide a step is. */
struct FDerbyStandArc
{
	int32 Rows = 0;
	double FirstRowZ = 0.0;
	double RowRise = 0.0;
	/** Degrees of arc each step's riser covers (a hair over the step, so the steps meet) and its fans spread over. */
	double RiserArcDeg = 0.0;
	double FanArcDeg = 0.0;
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

	/** The bleachers beyond the wall: from this far past the fence, in 2.5-degree steps, leaving the batter's eye clear. */
	constexpr FDerbyStandArc Bleachers{ 16, 1.6, 0.62, 2.56, 2.5 };
	constexpr double BleacherStepDeg = 2.5;
	constexpr double BleacherBehindFenceM = 6.0;
	constexpr double BattersEyeHalfDeg = 10.0;
	/** The grandstand round home: hugging the backstop behind the plate, farther out down the lines (foul ground). */
	constexpr FDerbyStandArc Grandstand{ 18, 1.4, 0.66, 3.06, 3.0 };
	constexpr double GrandstandStepDeg = 3.0;
	constexpr double GrandstandNearM = 19.5;
	constexpr double GrandstandFarM = 34.0;
	/** The stands down the lines: blocks along each foul line, twelve rows back from it. */
	constexpr int32 LineRows = 12;
	constexpr double LineNearM = 12.0;
	constexpr double LineRowRise = 0.62;
	constexpr double LineFirstRowZ = 1.4;
	constexpr double LineBlockM = 4.0;
	constexpr int32 LineFansPerRow = 5;

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
		Box(Seat, Dir * (Rr + SeatInsetM) + FVector(0.0, 0.0, Z + SeatRaiseM), FVector(0.4, Len * SeatBenchShare, 0.4), MidDeg);
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
		Sections.Add(Groups.Find(FanMesh, Shirt, false, P, Skin));
		Cheering.Add(Groups.Find(CheerMesh, Shirt, false, P, Skin));
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
	// Bleachers beyond the wall, the back wall and its neon trim on top.
	for (double A = -50.0; A < 50.0; A += BleacherStepDeg)
	{
		const double Mid = A + BleacherStepDeg / 2.0;
		if (FMath::Abs(Mid) < BattersEyeHalfDeg)
		{
			continue;
		}
		const double Base = FenceAt(FMath::Clamp(Mid, -45.0, 45.0)) + BleacherBehindFenceM;
		BuildStandArc(Bleachers, Mid, Base, Riser, Seat, AddFan);
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Mid)), FMath::Sin(FMath::DegreesToRadians(Mid)), 0.0);
		const double Back = Base + Bleachers.Rows * RowDepthM + 0.6;
		const double Top = Bleachers.FirstRowZ + Bleachers.Rows * Bleachers.RowRise;
		Box(Facade, Dir * Back + FVector(0.0, 0.0, (Top + 3.0) / 2.0), FVector(1.0, Back * FMath::DegreesToRadians(Bleachers.RiserArcDeg), Top + 3.0), Mid);
		Box(Trim, Dir * (Back - 0.55) + FVector(0.0, 0.0, Top + 2.4), FVector(0.1, Back * FMath::DegreesToRadians(Bleachers.RiserArcDeg), 0.25), Mid);
	}
	// The grandstand round home, from first base's side behind the plate to third's, under a roof with a neon edge.
	for (double A = 52.0; A < 308.0; A += GrandstandStepDeg)
	{
		const double Mid = A + GrandstandStepDeg / 2.0;
		// Down the lines the stand sits farther out (foul ground), behind the plate it hugs the backstop.
		const double Behind = FMath::Abs(FMath::FindDeltaAngleDegrees(Mid, 180.0));
		const double Base = Behind < 60.0 ? GrandstandNearM : FMath::Lerp(GrandstandNearM, GrandstandFarM, FMath::Clamp((Behind - 60.0) / 70.0, 0.0, 1.0));
		BuildStandArc(Grandstand, Mid, Base, Riser, Seat, AddFan);
		const FVector Dir(FMath::Cos(FMath::DegreesToRadians(Mid)), FMath::Sin(FMath::DegreesToRadians(Mid)), 0.0);
		const double Back = Base + Grandstand.Rows * RowDepthM + 0.6;
		const double Top = Grandstand.FirstRowZ + Grandstand.Rows * Grandstand.RowRise;
		Box(Facade, Dir * Back + FVector(0.0, 0.0, (Top + 6.0) / 2.0), FVector(1.0, Back * FMath::DegreesToRadians(Grandstand.RiserArcDeg), Top + 6.0), Mid);
		Box(Facade, Dir * (Back - 5.0) + FVector(0.0, 0.0, Top + 6.0), FVector(11.0, Back * FMath::DegreesToRadians(3.1), 0.5), Mid);
		Box(Trim, Dir * (Back - 10.4) + FVector(0.0, 0.0, Top + 5.7), FVector(0.15, Back * FMath::DegreesToRadians(Grandstand.RiserArcDeg), 0.2), Mid);
	}
	// Down the lines: stands parallel to the foul lines, between the grandstand and the bleachers.
	for (const double Side : { -1.0, 1.0 })
	{
		const double LineRad = FMath::DegreesToRadians(Side * 45.0);
		const FVector Along(FMath::Cos(LineRad), FMath::Sin(LineRad), 0.0);
		const FVector Out = FVector(-FMath::Sin(LineRad), FMath::Cos(LineRad), 0.0) * Side;
		const double Yaw = Side * 45.0;
		for (double S = 40.0; S < 100.0; S += LineBlockM)
		{
			for (int32 Row = 0; Row < LineRows; ++Row)
			{
				const double D = LineNearM + Row * RowDepthM;
				const double Z = LineFirstRowZ + Row * LineRowRise;
				const FVector C = Along * (S + 2.0) + Out * (D + RiserInsetM);
				Box(Riser, C + FVector(0.0, 0.0, Z / 2.0), FVector(LineBlockM, RowDepthM, Z), Yaw);
				for (int32 k = 0; k < LineFansPerRow; ++k)
				{
					// Facing the field across the line.
					AddFan(Along * (S + 0.4 + k * 0.8) + Out * (D + FanInsetM) + FVector(0.0, 0.0, Z), Yaw + (Side > 0 ? -90.0 : 90.0));
				}
			}
			const FVector Back = Along * (S + 2.0) + Out * (LineNearM + LineRows * RowDepthM + 0.6);
			Box(Facade, Back + FVector(0.0, 0.0, 6.0), FVector(LineBlockM, 1.0, 12.0), Yaw);
		}
	}
}
