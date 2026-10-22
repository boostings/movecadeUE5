// ABowlingAlley: builds the house from BowlingAlleyLayout.h (one table per part) and runs its light show.
#include "Games/Bowling/BowlingAlley.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "BowlingAlleyLayout.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Bowling/BowlingRules.h"
#include "Games/Bowling/BowlingSim.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBowlingAlley, Log, All);

using MovecadeKart::Art::Hex;
using namespace MovecadeKart::Bowling::AlleyLayout;

namespace
{
	const FName AlleyTag(TEXT("BowlingAlley"));
	const FName ColorParam(TEXT("Color"));

	double PinRadiusAt(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		for (int32 i = 1; i < UE_ARRAY_COUNT(PinProfile); ++i)
		{
			if (T <= PinProfile[i][0])
			{
				const double Span = PinProfile[i][0] - PinProfile[i - 1][0];
				const double Mix = Span <= 0.0 ? 0.0 : (T - PinProfile[i - 1][0]) / Span;
				return FMath::Lerp(PinProfile[i - 1][1], PinProfile[i][1], Mix);
			}
		}
		return 0.0;
	}

	/** The pin spot (web world units) for pin 1..10 on the lane at LaneX. */
	FVector PinSpot(int32 Id, double LaneX)
	{
		const FVector2D P = MovecadeKart::Bowling::PinPosition(Id);
		return FVector(LaneX + P.X * ABowlingAlley::VisualPinScale, DeckY, DeckZ - P.Y * ABowlingAlley::VisualPinScale);
	}
}

ABowlingAlley::ABowlingAlley()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// The house never moves: static geometry is cached by the renderer (lights stay movable children).
	RootComponent->SetMobility(EComponentMobility::Static);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	// Every box and post in the house is one of these: without them the alley builds empty.
	UE_CLOG(!Cube || !Cylinder, LogBowlingAlley, Warning, TEXT("No engine basic shapes: the bowling alley builds without its boxes and posts."));
}

FVector ABowlingAlley::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

void ABowlingAlley::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorTransform(FTransform::Identity);
	Build();
}

void ABowlingAlley::Build()
{
	Clear();
	Groups.Init(this, TEXT("AlleyGroup"), AlleyTag, EComponentMobility::Static);
	if (!PinMesh)
	{
		BuildMeshes();
	}
	BuildShell();
	BuildLanes();
	BuildDeck();
	BuildCeiling();
	BuildNeon();
	BuildLights();
	BuildGrade();
	BuildHouseRacks();
}

void ABowlingAlley::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), AlleyTag);
	for (UActorComponent* Component : Old)
	{
		RemoveInstanceComponent(Component);
		Component->DestroyComponent();
	}
	Groups.Reset();
	Materials.Reset();
	NeonMids.Reset();
	NeonColors.Reset();
	ShowSpots.Reset();
	DeckKey = nullptr;
	ShownEnergy = -1.0;
}

// ---- Building blocks --------------------------------------------------------------------------------------------

UMaterialInterface* ABowlingAlley::Neon(const TCHAR* Key, const FLinearColor& Color)
{
	return Materials.FindOrMake(Key, [this, &Color]() -> UMaterialInterface*
	{
		UMaterialInstanceDynamic* Mid = MovecadeKart::Art::GlowOpaque(this, Color);
		NeonMids.Add(Mid);
		NeonColors.Add(Color);
		return Mid;
	});
}

UMaterialInterface* ABowlingAlley::DarkMaterial()
{
	return Materials.Plain(this, Hex(Looks::DarkColor), Looks::DarkRoughness);
}

UMaterialInterface* ABowlingAlley::MachineMaterial()
{
	return Materials.Plain(this, Hex(Looks::MachineColor), Looks::MachineRoughness, Looks::MachineMetallic);
}

UMaterialInterface* ABowlingAlley::PitMaterial()
{
	return Materials.Plain(this, Hex(Looks::PitColor), Looks::PitRoughness);
}

void ABowlingAlley::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad, bool bShadows)
{
	// The engine cube is 1 m: web (x, y, z) sizes become UE (z, x, y) scales.
	Groups.Find(Cube, Material, bShadows)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), true);
}

void ABowlingAlley::Boxes(UMaterialInterface* Material, TConstArrayView<FAlleyBox> Parts, double OffsetX, double OffsetZ)
{
	for (const FAlleyBox& Part : Parts)
	{
		Box(Material, FVector(Part.X + OffsetX, Part.Y, Part.Z + OffsetZ), FVector(Part.W, Part.H, Part.D), Part.Yaw, Part.bShadows);
	}
}

void ABowlingAlley::WallBox(UMaterialInterface* Material, const FVector& C, const FVector& S, double RollRad)
{
	Groups.Find(Cube, Material, false)->AddInstance(FTransform(FRotator(0.0, 0.0, FMath::RadiansToDegrees(RollRad)), ToWorld(C), FVector(S.Z, S.X, S.Y)), true);
}

void ABowlingAlley::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height)
{
	// The engine cylinder is 1 m tall and 1 m across, centred.
	Groups.Find(Cylinder, Material)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, 0.0)), FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

template <class TComponent>
TComponent* ABowlingAlley::NewPart(const TCHAR* Prefix)
{
	TComponent* Part = NewObject<TComponent>(this, MakeUniqueObjectName(this, TComponent::StaticClass(), Prefix));
	Part->SetupAttachment(RootComponent);
	Part->SetMobility(EComponentMobility::Movable);
	return Part;
}

void ABowlingAlley::RegisterPart(USceneComponent* Part)
{
	Part->ComponentTags.Add(AlleyTag);
	AddInstanceComponent(Part);
	Part->RegisterComponent();
}

// ---- Meshes -----------------------------------------------------------------------------------------------------

void ABowlingAlley::BuildMeshes()
{
	BuildPinMesh();
	BuildBallMesh();
	BuildGutterMesh();
	PinMaterials = {
		Materials.Surface(this, Looks::PinIvory()),
		Materials.Plain(this, Hex(Looks::PinNeckColor), Looks::PinNeckRoughness),
	};
}

void ABowlingAlley::BuildPinMesh()
{
	// The USBC silhouette lathed in three ivory runs and two red neck bands, centred on the pin's middle (the solver
	// poses pin centres).
	MovecadeKart::FMeshBuilder B;
	const double H = PinHeight * 100.0, R = PinBelly * 100.0;
	for (const auto& Band : PinBands)
	{
		const bool bBand = Band[2] > 0.5;
		TArray<FVector2D> Profile;
		const int32 Steps = FMath::Max(2, FMath::CeilToInt32((Band[1] - Band[0]) * PinStepsPerHeight));
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double T = FMath::Lerp(Band[0], Band[1], static_cast<double>(i) / Steps);
			Profile.Add(FVector2D(PinRadiusAt(T) * R * (bBand ? BandProud : 1.0), (T - 0.5) * H));
		}
		B.Lathe(bBand ? FName(TEXT("Band")) : FName(TEXT("Ivory")), Profile, PinSegments, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector,
			Band[0], Band[1]);
	}
	PinMesh = B.Build(this, TEXT("Pin"));
}

void ABowlingAlley::BuildBallMesh()
{
	// A sphere with a three-hole grip, the holes as shallow dark lenses on the shell.
	MovecadeKart::FMeshBuilder B;
	const double R = BallRadius * 100.0;
	B.Ellipsoid(TEXT("Shell"), FVector::ZeroVector, FVector(R), BallRings, BallSegments, FVector2D::ZeroVector);
	for (const auto& Hole : BallHoles)
	{
		const FVector N = FVector(Hole[0], Hole[1], Hole[2]).GetSafeNormal();
		B.Ellipsoid(TEXT("Holes"), N * R * HoleDepth, FVector(R * HoleRadius, R * HoleRadius, R * HoleThickness), 4, 12, FVector2D::ZeroVector);
	}
	BallMesh = B.Build(this, TEXT("Ball"));
}

void ABowlingAlley::BuildGutterMesh()
{
	// A half pipe the length of the lane, open to the sky.
	MovecadeKart::FMeshBuilder B;
	const int32 S = B.Section(TEXT("Gutter"));
	const double R = GutterRadiusCm, L = LaneSurfaceLength * 100.0;
	auto P = [&](double A, double X) { return FVector(X, -FMath::Cos(A) * R, -FMath::Sin(A) * R); };
	auto N = [](double A) { return FVector(0.0, FMath::Cos(A), FMath::Sin(A)); };
	for (int32 i = 0; i < GutterSegments; ++i)
	{
		const double A0 = UE_PI * i / GutterSegments, A1 = UE_PI * (i + 1) / GutterSegments;
		const int32 a = B.Vert(S, P(A0, -L / 2), N(A0), FVector2D(0, 0), FVector2D::ZeroVector);
		const int32 b = B.Vert(S, P(A1, -L / 2), N(A1), FVector2D(1, 0), FVector2D::ZeroVector);
		const int32 c = B.Vert(S, P(A1, L / 2), N(A1), FVector2D(1, 1), FVector2D::ZeroVector);
		const int32 d = B.Vert(S, P(A0, L / 2), N(A0), FVector2D(0, 1), FVector2D::ZeroVector);
		B.Tri(S, a, b, c);
		B.Tri(S, a, c, d);
	}
	GutterMesh = B.Build(this, TEXT("Gutter"));
}

// ---- The room ---------------------------------------------------------------------------------------------------

void ABowlingAlley::BuildShell()
{
	UMaterialInterface* Carpet = Materials.Surface(this, Looks::Carpet());
	UMaterialInterface* Wall = Materials.Surface(this, Looks::Wall());
	UMaterialInterface* Wallpaper = Materials.Surface(this, Looks::Wallpaper());
	UMaterialInterface* Wainscot = Materials.Surface(this, Looks::Wainscot());
	UMaterialInterface* Ceiling = Materials.Plain(this, Hex(Looks::CeilingColor), Looks::CeilingRoughness);
	UMaterialInterface* Accent = Neon(TEXT("NeonViolet"), Hex(NeonViolet) * VioletGlow);
	Boxes(Carpet, Shell::Carpet);
	Boxes(Wall, Shell::Walls);
	Boxes(Wallpaper, Shell::Wallpaper);
	Boxes(Wainscot, Shell::Wainscot);
	Boxes(Accent, Shell::Accent);
	Boxes(Ceiling, Shell::Ceiling);
}

void ABowlingAlley::BuildLanes()
{
	UMaterialInterface* Maple = Materials.Surface(this, Looks::Maple());
	UMaterialInterface* Slab = Materials.Plain(this, Hex(Looks::SlabColor), Looks::SlabRoughness);
	UMaterialInterface* Approach = Materials.Surface(this, Looks::Approach());
	UMaterialInterface* Dark = DarkMaterial();
	UMaterialInterface* Machine = MachineMaterial();
	UMaterialInterface* Foul = Neon(TEXT("Foul"), Hex(FoulLineColor) * FoulGlow);
	UMaterialInterface* Return = Materials.Plain(this, Hex(Looks::ReturnColor), Looks::ReturnRoughness);
	// One continuous approach behind all five lanes, and one foul line across it.
	Boxes(Approach, Lanes::Approach);
	Boxes(Foul, Lanes::FoulLine);
	for (const double X : LaneCenters)
	{
		Boxes(Slab, Lanes::Slab, X);
		Boxes(Maple, Lanes::Maple, X);
		BuildLaneMarkings(X);
	}
	BuildGutters();
	// Ball returns, one per lane pair, on the inner dividers.
	for (int32 i = 1; i + 1 < UE_ARRAY_COUNT(DividerCenters); ++i)
	{
		Boxes(Return, Lanes::ReturnHood, DividerCenters[i]);
		Boxes(Machine, Lanes::ReturnLid, DividerCenters[i]);
		Boxes(Dark, Lanes::ReturnMouth, DividerCenters[i]);
	}
}

void ABowlingAlley::BuildLaneMarkings(double LaneX)
{
	// The played lane's arrows glow, so a new bowler can see what to aim over.
	UMaterialInterface* Dark = DarkMaterial();
	UMaterialInterface* Arrow = LaneX == 0.0 ? Neon(TEXT("Marker"), Hex(NeonCyan) * MarkerGlow) : Dark;
	for (const double Ax : Lanes::ArrowOffsets)
	{
		Boxes(Arrow, Lanes::Arrow, LaneX + Ax);
	}
	for (const double Dx : Lanes::DotOffsets)
	{
		Boxes(Dark, Lanes::Dot, LaneX + Dx);
	}
}

void ABowlingAlley::BuildGutters()
{
	// One gutter channel in every gap and along both outer edges, capping walls on the outside only.
	UMaterialInterface* Gutter = Materials.Plain(this, Hex(Looks::GutterColor), Looks::GutterRoughness, Looks::GutterMetallic);
	UMaterialInterface* Machine = MachineMaterial();
	UInstancedStaticMeshComponent* Gutters = Groups.Find(GutterMesh, Gutter, false);
	for (int32 i = 0; i < UE_ARRAY_COUNT(DividerCenters); ++i)
	{
		const double X = DividerCenters[i];
		Gutters->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(FVector(X, Lanes::GutterY, LaneSurfaceCenterZ)), FVector::OneVector), true);
		if (i == 0 || i == UE_ARRAY_COUNT(DividerCenters) - 1)
		{
			Boxes(Machine, Lanes::Capping, X + (X < 0.0 ? -Lanes::CappingOut : Lanes::CappingOut));
		}
	}
}

void ABowlingAlley::BuildDeck()
{
	UMaterialInterface* Machine = MachineMaterial();
	UMaterialInterface* Dark = DarkMaterial();
	UMaterialInterface* Kick = Materials.Plain(this, Hex(Looks::KickColor), Looks::KickRoughness, Looks::KickMetallic);
	UMaterialInterface* KickNeon = Neon(TEXT("KickNeon"), Hex(NeonCyan) * KickGlow);
	UMaterialInterface* Pit = PitMaterial();
	Boxes(Machine, Deck::MachineRun);
	Boxes(Dark, Deck::MachineHood);
	for (const double X : DividerCenters)
	{
		Boxes(Machine, Deck::MachineRib, X);
		// The rack sits in a frame of light (and the lane mirrors it).
		Boxes(Kick, Deck::Kickback, X);
		Boxes(KickNeon, Deck::KickbackNeon, X);
	}
	// Each lane's masking unit shows its own panel of house art.
	UMaterialInterface* Art[UE_ARRAY_COUNT(Deck::BillboardPaths)];
	for (int32 i = 0; i < UE_ARRAY_COUNT(Art); ++i)
	{
		Art[i] = Materials.Unlit(this, Deck::BillboardPaths[i], FLinearColor(BillboardGlow, BillboardGlow, BillboardGlow));
	}
	for (int32 Lane = 0; Lane < UE_ARRAY_COUNT(LaneCenters); ++Lane)
	{
		const double X = LaneCenters[Lane];
		Boxes(Dark, Deck::Housing, X);
		// The pins stand against darkness.
		Boxes(Pit, Deck::Pit, X);
		BuildMasking(X, Art[(Lane + 2) % UE_ARRAY_COUNT(Art)]);
	}
}

void ABowlingAlley::BuildMasking(double X, UMaterialInterface* PanelArt)
{
	Boxes(DarkMaterial(), Masking::Frame, X);
	Boxes(Materials.GlowOpaque(this, Hex(Looks::PanelColor) * PanelGlow), Masking::Panel, X);
	Boxes(PanelArt, Masking::Art, X);
	Boxes(MachineMaterial(), Masking::Rails, X);
	Boxes(PitMaterial(), Masking::Shadow, X);
	Boxes(Materials.GlowOpaque(this, Hex(Looks::CoveColor) * CoveGlow), Masking::Cove, X);
	Boxes(Neon(TEXT("NeonViolet"), Hex(NeonViolet) * VioletGlow), Masking::Accent, X);
}

void ABowlingAlley::BuildCeiling()
{
	UMaterialInterface* Iron = Materials.Plain(this, Hex(Looks::IronColor), Looks::IronRoughness, Looks::IronMetallic);
	UMaterialInterface* Lens = Materials.GlowOpaque(this, Hex(TroughColor) * LensGlow);
	for (const double Z : Ceiling::ChordZ)
	{
		Boxes(Iron, Ceiling::Chords, 0.0, Z);
		for (const double X : Ceiling::HangerX)
		{
			Boxes(Iron, Ceiling::Hanger, X, Z);
		}
	}
	for (const double Z : Ceiling::BeamZ)
	{
		Boxes(Iron, Ceiling::Beam, 0.0, Z);
	}
	for (const double X : Ceiling::GirderX)
	{
		Boxes(Iron, Ceiling::Girder, X);
	}
	for (const double X : TroughCenters)
	{
		Boxes(Iron, Ceiling::Trough, X);
		Boxes(Lens, Ceiling::TroughLens, X);
	}
	UMaterialInterface* Tungsten = Materials.GlowOpaque(this, Hex(Looks::TungstenColor) * TungstenGlow);
	for (const double X : Ceiling::PendantX)
	{
		Boxes(Iron, Ceiling::PendantRod, X);
		Post(Iron, FVector(X, Ceiling::ShadeY, Ceiling::PendantZ), Ceiling::ShadeRadius, Ceiling::ShadeHeight);
		Post(Tungsten, FVector(X, Ceiling::BulbY, Ceiling::PendantZ), Ceiling::BulbRadius, Ceiling::BulbHeight);
	}
}

void ABowlingAlley::BuildNeon()
{
	UMaterialInterface* Pink = Neon(TEXT("NeonPink"), Hex(NeonPink) * TubeGlow);
	UMaterialInterface* Cyan = Neon(TEXT("NeonCyan"), Hex(NeonCyan) * TubeGlow);
	UMaterialInterface* Tube = Materials.Plain(this, Hex(Looks::StandoffColor), Looks::StandoffRoughness, Looks::StandoffMetallic);
	// The classic cosmic-bowling run at head height.
	Boxes(Pink, NeonRuns::SidePink);
	Boxes(Cyan, NeonRuns::SideCyan);
	for (const double Side : { -1.0, 1.0 })
	{
		for (double Z = NeonRuns::StandoffFirstZ; Z <= NeonRuns::StandoffLastZ; Z += NeonRuns::StandoffStep)
		{
			Boxes(Tube, NeonRuns::Standoff, Side * NeonRuns::StandoffX, Z);
		}
	}
	// Framing the lightboxes above the masking units.
	Boxes(Cyan, NeonRuns::BackCyan);
	Boxes(Pink, NeonRuns::BackPink);
	BuildHouseSign();
}

void ABowlingAlley::BuildHouseSign()
{
	using namespace Sign;
	UMaterialInterface* Pink = Neon(TEXT("NeonPink"), Hex(NeonPink) * TubeGlow);
	UMaterialInterface* Cyan = Neon(TEXT("NeonCyan"), Hex(NeonCyan) * TubeGlow);
	UMaterialInterface* Violet = Neon(TEXT("NeonViolet"), Hex(NeonViolet) * VioletGlow);
	const FVector Logo(0.0, LogoY, NeonRuns::BackZ + LogoAheadOfWall);
	// The ball: a ring of short tubes, then its finger holes.
	for (int32 i = 0; i < RingSegments; ++i)
	{
		const double A = 2.0 * UE_DOUBLE_PI * (i + 0.5) / RingSegments;
		const double Len = 2.0 * UE_DOUBLE_PI * RingRadius / RingSegments * RingOverlap;
		WallBox(Cyan, Logo + FVector(FMath::Cos(A) * RingRadius, FMath::Sin(A) * RingRadius, 0.0), FVector(Len, RingTube, TubeDepth), A + UE_DOUBLE_PI / 2.0);
	}
	for (const auto& Hole : FingerHoles)
	{
		WallBox(Pink, Logo + FVector(Hole[0], Hole[1], 0.0), FVector(HoleSize, HoleSize, TubeDepth), UE_DOUBLE_PI / 4.0);
	}
	// The pins: an outline from the pin's own profile, segments up each flank, a base and the neck band.
	for (const double Side : { -1.0, 1.0 })
	{
		const FVector Base = Logo + FVector(Side * PinX, PinBaseY, 0.0);
		for (const double Flank : { -1.0, 1.0 })
		{
			for (int32 i = 0; i < PinOutlineSteps; ++i)
			{
				const double T0 = static_cast<double>(i) / PinOutlineSteps, T1 = static_cast<double>(i + 1) / PinOutlineSteps;
				const FVector2D P0(Flank * PinRadiusAt(T0) * PinOutlineBelly, T0 * PinOutlineHeight);
				const FVector2D P1(Flank * PinRadiusAt(T1) * PinOutlineBelly, T1 * PinOutlineHeight);
				const FVector2D D = P1 - P0;
				WallBox(Pink, Base + FVector((P0.X + P1.X) / 2.0, (P0.Y + P1.Y) / 2.0, 0.0), FVector(D.Size() * OutlineOverlap, OutlineTube, TubeDepth), FMath::Atan2(D.Y, D.X));
			}
		}
		WallBox(Pink, Base, FVector(PinOutlineBelly * BaseWidth, OutlineTube, TubeDepth), 0.0);
		WallBox(Violet, Base + FVector(0.0, PinOutlineHeight * PinNeck, 0.0), FVector(PinOutlineBelly * NeckWidth, NeckTube, TubeDepth), 0.0);
	}
}

// ---- Light ------------------------------------------------------------------------------------------------------

void ABowlingAlley::BuildLights()
{
	using namespace DeckKeyLight;
	USpotLightComponent* Key = NewPart<USpotLightComponent>(TEXT("DeckKey"));
	const FVector From = ToWorld(FVector(0.0, FromY, FromZ));
	const FVector To = ToWorld(FVector(0.0, ToY, ToZ));
	Key->SetRelativeLocation(From);
	Key->SetRelativeRotation((To - From).Rotation());
	Key->SetIntensityUnits(ELightUnits::Lumens);
	Key->SetIntensity(Lumens);
	Key->SetAttenuationRadius(RadiusCm);
	Key->SetOuterConeAngle(FMath::RadiansToDegrees(ConeRadians));
	Key->SetInnerConeAngle(FMath::RadiansToDegrees(ConeRadians * InnerCone));
	Key->SetLightColor(Hex(Color));
	Key->SetSourceRadius(SourceRadiusCm);
	Key->SetCastShadows(true);
	RegisterPart(Key);
	DeckKey = Key;
	BuildFillLights();
	BuildTroughLights();
	BuildShowSpots();
}

void ABowlingAlley::BuildFillLights()
{
	// Fill lights never shadow: the deck key and the bowler's own contact shadow carry the shape.
	for (const FFillLight& Fill : FillLights)
	{
		UPointLightComponent* Light = NewPart<UPointLightComponent>(TEXT("AlleyPoint"));
		Light->SetRelativeLocation(ToWorld(FVector(Fill.X, Fill.Y, Fill.Z)));
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Fill.Lumens);
		Light->SetAttenuationRadius(Fill.RadiusM * 100.f);
		Light->SetLightColor(Hex(Fill.Color));
		Light->SetSourceRadius(FillSourceRadiusCm);
		Light->SetCastShadows(false);
		RegisterPart(Light);
	}
}

void ABowlingAlley::BuildTroughLights()
{
	using namespace TroughLight;
	for (const double X : TroughCenters)
	{
		URectLightComponent* Rect = NewPart<URectLightComponent>(TEXT("Trough"));
		Rect->SetRelativeLocation(ToWorld(FVector(X, Y, Z)));
		Rect->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
		Rect->SetSourceWidth(WidthCm);
		Rect->SetSourceHeight(LengthCm);
		Rect->SetIntensityUnits(ELightUnits::Lumens);
		Rect->SetIntensity(X == 0.0 ? PlayedLumens : HouseLumens);
		Rect->SetAttenuationRadius(RadiusCm);
		Rect->SetBarnDoorAngle(BarnDoorDegrees);
		Rect->SetLightColor(Hex(TroughColor));
		// A 15.6 m shadowed rect is the most expensive light in the room for a soft shadow the key already gives.
		Rect->SetCastShadows(false);
		RegisterPart(Rect);
	}
}

void ABowlingAlley::BuildShowSpots()
{
	for (const FShowSpot& Show : MovecadeKart::Bowling::AlleyLayout::ShowSpots)
	{
		const FVector At(Show.X, Show.Y, Show.Z);
		USpotLightComponent* Spot = NewPart<USpotLightComponent>(TEXT("ShowSpot"));
		Spot->SetRelativeLocation(ToWorld(At));
		Spot->SetRelativeRotation((ToWorld(FVector(-At.X * ShowSpot::AimPull, ShowSpot::AimY, ShowSpot::AimZ)) - ToWorld(At)).Rotation());
		Spot->SetIntensityUnits(ELightUnits::Lumens);
		Spot->SetIntensity(0.f);
		Spot->SetAttenuationRadius(ShowSpot::RadiusCm);
		Spot->SetOuterConeAngle(ShowSpot::OuterDegrees);
		Spot->SetInnerConeAngle(ShowSpot::InnerDegrees);
		Spot->SetLightColor(Hex(Show.Color));
		Spot->SetCastShadows(false);
		Spot->SetVisibility(false);
		RegisterPart(Spot);
		ShowSpots.Add(Spot);
	}
}

void ABowlingAlley::BuildGrade()
{
	// No sun indoors: auto exposure would lift the dark room and burn out the deck, so it is held to a narrow band
	// set by the lane's light; bloom for the neon, a vignette to pull the eye down the lane, a touch of punch.
	UPostProcessComponent* PostProcess = NewObject<UPostProcessComponent>(this, MakeUniqueObjectName(this, UPostProcessComponent::StaticClass(), TEXT("AlleyGrade")));
	PostProcess->SetupAttachment(RootComponent);
	PostProcess->bUnbound = true;
	PostProcess->Priority = 1.f;
	FPostProcessSettings& S = PostProcess->Settings;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = Grade::ExposureMin;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = Grade::ExposureMax;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Grade::Bloom;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = Grade::BloomThreshold;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = Grade::Vignette;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(Grade::Saturation, Grade::Saturation, Grade::SaturationBlue, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(Grade::Contrast, Grade::Contrast, Grade::Contrast, 1.f);
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = Grade::Fringe;
	RegisterPart(PostProcess);
}

void ABowlingAlley::BuildHouseRacks()
{
	// The four house lanes' racks, all in one instanced group; the played lane's pins are the game's (they move).
	UInstancedStaticMeshComponent* Racks = Groups.Find(PinMesh, GetPinMaterial(0), true, nullptr, GetPinMaterial(1));
	for (const double LaneX : LaneCenters)
	{
		if (LaneX == 0.0)
		{
			continue;
		}
		for (int32 Id = 1; Id <= MovecadeKart::Bowling::Pins; ++Id)
		{
			Racks->AddInstance(FTransform(ToWorld(PinSpot(Id, LaneX) + FVector(0.0, PinHeight / 2.0, 0.0))), true);
		}
	}
}

// ---- The light show ---------------------------------------------------------------------------------------------

void ABowlingAlley::SetShow(double Energy, const FLinearColor& Tint, double Time)
{
	Energy = FMath::Max(0.0, Energy);
	if (Energy <= ShowOnEnergy && ShownEnergy == 0.0)
	{
		return;
	}
	const bool bOn = Energy > ShowOnEnergy;
	ShownEnergy = bOn ? Energy : 0.0;
	const double Beat = Energy > 1.0 ? TurkeyBeat : ShowBeat;
	PulseNeon(Energy, Time, Beat, bOn);
	SweepShowSpots(Energy, Tint, Time, Beat, bOn);
	if (DeckKey)
	{
		DeckKey->SetIntensity(static_cast<float>(DeckKeyLight::Lumens * (1.0 + DeckKeyFlash * FMath::Min(Energy, 1.0) * FMath::Max(0.0, FMath::Sin(Time * Beat * 2.0)))));
	}
}

void ABowlingAlley::PulseNeon(double Energy, double Time, double Beat, bool bOn)
{
	// The neon breathes with the show, chasing at the beat (each tube a step behind).
	for (int32 i = 0; i < NeonMids.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mid = NeonMids[i])
		{
			const double Pulse = bOn ? 0.5 + 0.5 * FMath::Sin(Time * Beat * UE_DOUBLE_PI + i * NeonPhaseStep) : 0.0;
			const float Gain = static_cast<float>(1.0 + FMath::Min(Energy, MaxShowEnergy) * (NeonRest + NeonPulse * Pulse));
			Mid->SetVectorParameterValue(ColorParam, NeonColors[i] * Gain);
		}
	}
}

void ABowlingAlley::SweepShowSpots(double Energy, const FLinearColor& Tint, double Time, double Beat, bool bOn)
{
	using namespace ShowSpot;
	for (int32 i = 0; i < ShowSpots.Num(); ++i)
	{
		USpotLightComponent* Spot = ShowSpots[i];
		if (!Spot)
		{
			continue;
		}
		Spot->SetVisibility(bOn);
		if (!bOn)
		{
			continue;
		}
		// Pairs alternate; each sweeps its own figure over the deck; a turkey tints them toward the beat's colour.
		const double Phase = Time * Beat + (i % 2) * UE_DOUBLE_PI;
		const double Flash = FMath::Max(0.0, FMath::Sin(Phase));
		Spot->SetIntensity(static_cast<float>(Lumens * FMath::Min(Energy, MaxShowEnergy) * (FlashFloor + FlashBeat * Flash)));
		const FVector Base = ToWorld(FVector(BaseLeftX + BaseStepX * (i % 2), AimY, AimZ));
		const FVector Sweep = ToWorld(FVector(FMath::Sin(Time * SweepRateX + i) * SweepX, 0.0, FMath::Cos(Time * SweepRateZ + i) * SweepZ)) - ToWorld(FVector::ZeroVector);
		Spot->SetWorldRotation((Base + Sweep - Spot->GetComponentLocation()).Rotation());
		if (Energy > 1.0)
		{
			Spot->SetLightColor(FLinearColor::LerpUsingHSV(Spot->GetLightColor(), Tint, TurkeyTint));
		}
	}
}
