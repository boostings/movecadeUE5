#include "Games/Stacker/StackerRooftop.h"

#include "Art/KartMeshBuilder.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using MovecadeKart::Art::Hex;

DEFINE_LOG_CATEGORY_STATIC(LogStacker, Log, All);

namespace
{
	/** Every part the venue builds carries this tag, so a rebuild finds and removes them. */
	const FName RoofTag(TEXT("StackerRooftop"));
	const TCHAR* const LaneColors[] = { TEXT("#6d8bff"), TEXT("#3fc9c2"), TEXT("#f2b23f"), TEXT("#a083ff") };
	static_assert(UE_ARRAY_COUNT(LaneColors) == AStackerRooftop::MaxLanes, "a colour per lane");

	/** RooftopEnvironment.tsx makeCity: three bands of buildings, their z and their parallax against the scroll. */
	constexpr int32 BandCount = 3;
	const int32 BandBuildings[BandCount] = { 18, 23, 28 };
	const double BandZ[BandCount] = { -14.0, -29.0, -49.0 };
	const double BandParallax[BandCount] = { 0.18, 0.12, 0.07 };
	/** The city's own stream: the same skyline every time. */
	constexpr int32 CitySeed = 0x5c17;
	/** Share of windows lit: the building under the roof, and the bands. */
	constexpr double RoofWindowsLit = 0.3;
	constexpr double BandWindowsLit = 0.26;
	/** RooftopEnvironment.tsx: four clouds at these x, y 9 / 11 alternating, z -62, the group drifting sin(.025t)*2.2. */
	const double CloudX[] = { -36.0, -12.0, 18.0, 43.0 };
	constexpr double CloudDriftRate = 0.025;
	constexpr double CloudDrift = 2.2;
	/** The HVAC fan's spin (rad/s). */
	constexpr double FanSpin = 2.4;
	/** The game settles its scroll exactly; anything under a fiftieth of a row is not worth moving ~10 batches for. */
	constexpr double MinScrollMove = 0.002;

	const TCHAR* const SkyMaterialPath = TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky");
	const TCHAR* const CloudTexturePath = TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds");

	/** StackerCanvas.tsx's golden hour: light directions (web, toward the light), colours, haze and sky tint. */
	struct FGoldenHour
	{
		static constexpr double SunX = 11.0, SunY = 15.0, SunZ = 10.0;
		static constexpr double RimX = 10.0, RimY = 5.0, RimZ = -14.0;
		static constexpr const TCHAR* SunColor = TEXT("#ffd09a");
		static constexpr const TCHAR* RimColor = TEXT("#9fc4ff");
		static constexpr const TCHAR* GroundColor = TEXT("#7a6a5a");
		static constexpr float GroundBounce = 0.5f;
		static constexpr const TCHAR* HorizonColor = TEXT("#e3ae94");
		static constexpr const TCHAR* SkyTopColor = TEXT("#2a5580");
		static constexpr const TCHAR* SkyBelowColor = TEXT("#8c7a6a");
		static constexpr const TCHAR* SkySunColor = TEXT("#ffcf94");
		static constexpr const TCHAR* SunHazeColor = TEXT("#ffc27a");
		static constexpr const TCHAR* CloudLitColor = TEXT("#ffe2c4");
		static constexpr float FogCutoff = 900000.f;
		static constexpr float FogHorizonShare = 0.75f;
		static constexpr float SunHazeShare = 0.6f;
		static constexpr float SunHazeExponent = 6.f;
		static constexpr double SaturationR = 1.08, SaturationG = 1.06, SaturationB = 1.0;
		static constexpr double Contrast = 1.06;
		static constexpr float SkyMidMix = 0.4f;
		static constexpr double SkySunX = 0.36, SkySunY = 0.085, SkySunZ = -1.0;
		static constexpr float SunDiscRadians = 0.035f;
		static constexpr float SunGlow = 1.1f;
		static constexpr float GlowPower = 9.0f;
		static constexpr float HorizonGlow = 0.9f;
		static constexpr float SkyBrightness = 1.0f;
		static constexpr float SkyClouds = 0.4f;
		static constexpr float SkyCloudScale = 0.35f;
		static constexpr float CloudShadeMix = 0.45f;
		static constexpr float CloudShadeDarken = 0.8f;
	};

	// The venue's modelling data (RooftopEnvironment.tsx / RooftopProps.tsx), in web units (x across, y up, z toward
	// the camera; Box sizes are x, y, z extents), grouped by part. A part's numbers are only read by its Build* function.

	/** A box placed relative to a part's anchor: centre offset and size. */
	struct FBoxPart
	{
		double X, Y, Z;
		double SizeX, SizeY, SizeZ;
		bool bShadows;
	};

	FVector OffsetOf(const FBoxPart& Part)
	{
		return FVector(Part.X, Part.Y, Part.Z);
	}

	FVector SizeOf(const FBoxPart& Part)
	{
		return FVector(Part.SizeX, Part.SizeY, Part.SizeZ);
	}

	/** The roof deck: its slab, the parapet, the lip along the front and the hazard stripe on it. */
	struct FDeckShape
	{
		static constexpr double SlabDrop = 0.05;
		static constexpr double SlabThick = 0.1;
		static constexpr double CentreZ = 1.0;
		static constexpr double Depth = 14.0;
		static constexpr double ParapetY = -1.03;
		static constexpr double ParapetThick = 0.66;
		static constexpr double LipRise = 0.25;
		static constexpr double LipZ = 8.0;
		static constexpr double LipHeight = 0.5;
		static constexpr double LipDeep = 0.3;
		static constexpr double StripeBlock = 0.8;
		static constexpr double StripeLift = 0.003;
		static constexpr double StripeZ = 7.3;
		static constexpr double StripeThick = 0.01;
		static constexpr double StripeDeep = 0.3;
	};

	/** A lane's work zone: a dashed outline in the lane's colour, the plinth, its cap and the painted number. */
	struct FZoneShape
	{
		static constexpr int32 FrontDashes = 9;
		static constexpr int32 SideDashes = 5;
		static constexpr double HalfWidth = 3.65;
		static constexpr double DashStep = 0.91;
		static constexpr double DashLift = 0.004;
		static constexpr double CentreZ = 1.0;
		static constexpr double HalfDepth = 2.4;
		static constexpr double SideDashStart = 0.25;
		static constexpr double SideDashStep = 1.1;
		static constexpr double DashLong = 0.5;
		static constexpr double DashThin = 0.08;
		static constexpr double DashThick = 0.01;
		static constexpr float DashShade = 0.9f;
		/** The plinth rises from the deck to just under the foundation block. */
		static constexpr double PlinthTop = 0.03;
		static constexpr double PlinthWide = 6.78;
		static constexpr double PlinthDeep = 2.08;
		static constexpr float PlinthShade = 0.75f;
		static constexpr double CapY = 0.02;
		static constexpr double CapWide = 6.9;
		static constexpr double CapThick = 0.02;
		static constexpr double CapDeep = 2.2;
		static constexpr double NumberX = 2.8;
		static constexpr double NumberLift = 0.01;
		static constexpr double NumberZ = 3.0;
		static constexpr float NumberSize = 60.f;
	};

	/** The building under the roof (its body runs down out of frame) and its window grid toward the camera. */
	struct FBuildingShape
	{
		static constexpr double TopY = -1.36;
		static constexpr double Height = 25.0;
		static constexpr double Inset = 0.3;
		static constexpr double Deep = 13.6;
		static constexpr int32 WindowRows = 14;
		static constexpr double WindowPitchX = 1.6;
		static constexpr double WindowPitchY = 1.7;
		static constexpr double FirstWindowY = -2.6;
		static constexpr double WindowZ = 7.82;
		static constexpr double WindowHalfWide = 0.4;
		static constexpr double WindowHalfTall = 0.475;
	};

	/** RooftopEnvironment.tsx makeCity: a band's spread, each tower's random size, its crown and window grid. */
	struct FCityShape
	{
		static constexpr double SpanBase = 26.0;
		static constexpr double SpanPerBand = 22.0;
		static constexpr double Jitter = 0.6;
		static constexpr double MinWidthShare = 0.7;
		static constexpr double MaxWidthShare = 0.95;
		static constexpr double MinDepth = 4.0;
		static constexpr double MaxDepth = 8.0;
		static constexpr double HeightBase = 14.0;
		static constexpr double HeightPerBand = 4.0;
		static constexpr double HeightRandom = 6.0;
		static constexpr double HeightRandomPerBand = 5.0;
		static constexpr double Base = -26.0;
		static constexpr double SetbackMinHeight = 18.0;
		static constexpr double SetbackChance = 0.6;
		static constexpr double SetbackRise = 2.0;
		static constexpr double SetbackShare = 0.7;
		static constexpr double SetbackHeight = 4.0;
		static constexpr double LedgeRise = 0.08;
		static constexpr double LedgeShare = 1.02;
		static constexpr double LedgeThick = 0.16;
		static constexpr double AntennaChance = 0.35;
		static constexpr double AntennaOffset = 0.2;
		static constexpr double AntennaRise = 1.6;
		static constexpr double AntennaThick = 0.08;
		static constexpr double AntennaHeight = 3.2;
		static constexpr double WindowPitchX = 0.9;
		static constexpr double WindowPitchY = 1.2;
		static constexpr double WindowFirstY = 1.0;
		static constexpr double WindowTopMargin = 0.6;
		static constexpr double WindowProud = 0.02;
		static constexpr double WindowWideShare = 0.25;
		static constexpr double WindowHalfTall = 0.3;
	};

	/** RooftopEnvironment.tsx: each cloud's puffs (offset and radii, web units) around its centre. */
	struct FPuff
	{
		double X, Y, Z;
		double RadiusX, RadiusY, RadiusZ;
	};
	const FPuff CloudPuffs[] = {
		{ 0.0, 0.0, 0.0, 8.0, 1.15, 2.6 },
		{ -2.0, 0.5, 0.2, 4.2, 1.9, 2.5 },
		{ 5.0, -0.15, 0.0, 3.5, 1.1, 2.0 },
		{ 2.2, 0.75, 0.4, 2.6, 1.3, 1.8 },
	};
	struct FCloudShape
	{
		static constexpr double Y = 9.0;
		static constexpr double AlternateRise = 2.0;
		static constexpr double Z = -62.0;
		static constexpr int32 Rings = 10;
		static constexpr int32 Segments = 18;
	};

	/** The crane behind the deck, from its base: a lattice mast (legs and rungs), then its fixed parts. */
	struct FCraneShape
	{
		static constexpr double BaseOutside = 2.0;
		static constexpr double BaseZ = -13.0;
		static constexpr double LegOffset = 0.45;
		static constexpr double LegThick = 0.12;
		static constexpr double MastHeight = 12.0;
		static constexpr int32 Rungs = 12;
		static constexpr double FirstRung = 0.5;
		static constexpr double RungWide = 0.9;
		static constexpr double RungThick = 0.08;
	};
	/** The jib and counter-jib (crane paint), then the counterweight, the cab top, the cable and the hook (steel). */
	const FBoxPart CraneJibs[] = {
		{ 6.0, 11.4, 0.0, 12.0, 0.5, 0.6, true },
		{ -2.5, 11.4, 0.0, 5.0, 0.5, 0.6, true },
	};
	const FBoxPart CraneSteel[] = {
		{ -4.2, 10.9, 0.0, 1.4, 1.0, 1.0, true },
		{ 0.0, 12.3, 0.0, 0.2, 1.8, 0.2, true },
		{ 9.0, 8.4, 0.0, 0.03, 6.0, 0.03, false },
		{ 9.0, 5.3, 0.0, 0.4, 0.3, 0.3, true },
	};

	/** The railing along the back of the deck and down its sides: two rails and posts about two metres apart. */
	struct FRailShape
	{
		static constexpr double Inset = 0.2;
		static constexpr double BackZ = -3.5;
		static constexpr double SideZ = 2.25;
		static constexpr double SideLength = 11.5;
		static constexpr double Thick = 0.05;
		static constexpr double PostSpacing = 2.0;
		static constexpr double PostCentreY = 0.5;
		static constexpr double PostHeight = 1.0;
	};
	const double RailHeights[] = { 0.45, 0.95 };

	/** The props near the deck's sides (x from the side line, W / 2 - SideInset): cones, a pallet of boxes, a spool. */
	struct FPropShape
	{
		static constexpr double SideInset = 1.6;
		static constexpr double ConeRadius = 0.18;
		static constexpr double ConeHeight = 0.55;
		static constexpr double SpoolX = 0.3;
		static constexpr double SpoolY = 0.45;
		static constexpr double SpoolZ = 2.2;
		static constexpr double SpoolDrum = 0.9;
		static constexpr double SpoolWidth = 0.5;
		static constexpr double SpoolCore = 0.4;
		static constexpr double SpoolCoreWidth = 0.52;
	};
	/** The two cones (x from the left side line, z), the pallet and its three boxes (x from the right side line). */
	const double ConeSpots[][2] = { { 0.0, 4.6 }, { 0.7, 5.2 } };
	const FBoxPart Pallet = { 0.0, 0.07, 4.0, 1.2, 0.14, 1.0, true };
	const FBoxPart PalletBoxes[] = {
		{ -0.25, 0.42, 4.0, 0.55, 0.56, 0.6, true },
		{ 0.3, 0.36, 3.85, 0.45, 0.44, 0.5, true },
		{ 0.05, 0.86, 3.95, 0.42, 0.32, 0.44, true },
	};

	/** The HVAC unit by the right side (from its foot), its grille and its four-bladed fan (cm under the fan pivot). */
	struct FHvacShape
	{
		static constexpr double OffsetX = -0.6;
		static constexpr double Z = -2.0;
		static constexpr int32 Blades = 4;
		static constexpr double BladeReachCm = 20.0;
		static constexpr double BladeLong = 0.4;
		static constexpr double BladeWide = 0.12;
		static constexpr double BladeThin = 0.02;
		static constexpr double FanY = 1.16;
	};
	const FBoxPart HvacBody = { 0.0, 0.55, 0.0, 1.8, 1.1, 1.2, true };
	const FBoxPart HvacGrille = { 0.0, 1.12, 0.0, 1.0, 0.04, 1.0, false };

	/** The two flags at the back corners: pole, hinge, the cloth hanging off it (cm, scale), and their wave. */
	struct FFlagShape
	{
		static constexpr double CornerInset = 0.6;
		static constexpr double Z = -3.2;
		static constexpr double PoleRadius = 0.04;
		static constexpr double PoleHeight = 3.2;
		static constexpr double HingeY = 2.85;
		static constexpr double ClothAcrossCm = 50.0;
		static constexpr double ClothDropCm = -20.0;
		static constexpr double ClothThin = 0.02;
		static constexpr double ClothLong = 1.0;
		static constexpr double ClothTall = 0.6;
		static constexpr double YawBase = 20.0;
		static constexpr double YawSwing = 14.0;
		static constexpr double YawRate = 1.7;
		static constexpr double YawPhase = 1.3;
		static constexpr double RollSwing = 4.0;
		static constexpr double RollRate = 3.1;
	};

	/** A window: one quad on a facade, facing the deck (web +z), in UE cm. */
	void WindowQuad(MovecadeKart::FMeshBuilder& B, int32 Section, const FVector& WebCentre, double HalfW, double HalfH)
	{
		const FVector C = MovecadeKart::Coords::ToUE(WebCentre);
		const FVector N = MovecadeKart::Coords::DirToUE(FVector(0.0, 0.0, 1.0));
		const double Y = HalfW * 100.0, Z = HalfH * 100.0;
		const int32 A = B.Vert(Section, C + FVector(0.0, -Y, -Z), N, FVector2D(0.0, 1.0), FVector2D::ZeroVector);
		const int32 Bv = B.Vert(Section, C + FVector(0.0, Y, -Z), N, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		const int32 Cv = B.Vert(Section, C + FVector(0.0, Y, Z), N, FVector2D(1.0, 0.0), FVector2D::ZeroVector);
		const int32 D = B.Vert(Section, C + FVector(0.0, -Y, Z), N, FVector2D(0.0, 0.0), FVector2D::ZeroVector);
		B.Tri(Section, A, Bv, Cv);
		B.Tri(Section, A, Cv, D);
	}
}

AStackerRooftop::AStackerRooftop()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// The deck, the building, the crane and the props never move: Static, so their draws are cached.
	RootComponent->SetMobility(EComponentMobility::Static);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Rim = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Rim"));
	Rim->SetupAttachment(RootComponent);
	Rim->SetMobility(EComponentMobility::Movable);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	// The sky never changes during a round: one capture after the build, not one every frame.
	SkyLight->bRealTimeCapture = false;
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(RootComponent);
	SkyDome->SetStaticMesh(SphereMesh.Object);
	// Two kilometres across: everything, the far city band included, sits inside it.
	SkyDome->SetRelativeScale3D(FVector(20000.0));
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCastShadow(false);
	SkyDome->SetMobility(EComponentMobility::Static);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetMobility(EComponentMobility::Movable);
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
}

FVector AStackerRooftop::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

FLinearColor AStackerRooftop::LaneColor(int32 Lane)
{
	return Hex(LaneColors[((Lane % MaxLanes) + MaxLanes) % MaxLanes]);
}

void AStackerRooftop::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The venue is modelled around the origin (the game's web coordinates assume it).
	if (!GetActorTransform().Equals(FTransform::Identity))
	{
		SetActorTransform(FTransform::Identity);
	}
	// In the editor the venue previews itself. A game builds once, for the real lane count (SetLanes), instead of
	// building for one lane here and again for four a moment later.
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		Build();
	}
}

void AStackerRooftop::BeginPlay()
{
	Super::BeginPlay();
	AppliedScroll = -1.0;
}

void AStackerRooftop::SetLanes(int32 LaneCount)
{
	LaneCount = FMath::Clamp(LaneCount, 1, MaxLanes);
	if (LaneCount != Lanes || !bBuilt)
	{
		Lanes = LaneCount;
		Build();
	}
}

void AStackerRooftop::SetScroll(double MaxScroll)
{
	if (FMath::Abs(MaxScroll - AppliedScroll) < MinScrollMove)
	{
		return;
	}
	AppliedScroll = MaxScroll;
	for (int32 i = 0; i < Bands.Num(); ++i)
	{
		if (Bands[i])
		{
			Bands[i]->SetRelativeLocation(ToWorld(FVector(0.0, -MaxScroll * BandParallax[i], 0.0)));
		}
	}
}

void AStackerRooftop::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), RoofTag);
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	StaticGroups.Reset();
	MovingGroups.Reset();
	Materials.Reset();
	Bands.Reset();
	Flags.Reset();
	Fan = nullptr;
	Clouds = nullptr;
	AppliedScroll = -1.0;
}

EComponentMobility::Type AStackerRooftop::MobilityUnder(const USceneComponent* Parent) const
{
	return Parent && Parent->Mobility == EComponentMobility::Movable ? EComponentMobility::Movable : EComponentMobility::Static;
}

UInstancedStaticMeshComponent* AStackerRooftop::Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows, USceneComponent* Parent)
{
	FArcadeInstanceGroups& Groups = MobilityUnder(Parent) == EComponentMobility::Movable ? MovingGroups : StaticGroups;
	return Groups.Find(Mesh, Material, bShadows, Parent);
}

UStaticMeshComponent* AStackerRooftop::Part(UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Mats, bool bShadows, USceneComponent* Parent)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("RoofPart")));
	C->SetStaticMesh(Mesh);
	for (int32 i = 0; i < Mats.Num(); ++i)
	{
		C->SetMaterial(i, Mats[i]);
	}
	C->SetMobility(MobilityUnder(Parent));
	C->SetupAttachment(Parent ? Parent : RootComponent.Get());
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(RoofTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

USceneComponent* AStackerRooftop::Pivot(const TCHAR* Name, const FVector& WebAt)
{
	USceneComponent* P = NewObject<USceneComponent>(this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), Name));
	P->SetMobility(EComponentMobility::Movable);
	P->SetupAttachment(RootComponent);
	P->SetRelativeLocation(ToWorld(WebAt));
	P->ComponentTags.Add(RoofTag);
	AddInstanceComponent(P);
	P->RegisterComponent();
	return P;
}

void AStackerRooftop::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad, bool bShadows, USceneComponent* Parent)
{
	// Web size (x across, y up, z deep) to the engine cube's (X deep, Y across, Z up), 1 m each.
	Group(Cube, Material, bShadows, Parent)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), false);
}

void AStackerRooftop::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows)
{
	Group(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, 0.0)), FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

void AStackerRooftop::Build()
{
	Clear();
	StaticGroups.Init(this, TEXT("RoofGroup"), RoofTag, EComponentMobility::Static);
	MovingGroups.Init(this, TEXT("RoofGroup"), RoofTag, EComponentMobility::Movable);
	BuildLook();
	BuildDeck();
	BuildCity();
	BuildSky();
	BuildProps();
	bBuilt = true;
	// One capture of the finished scene (the sky light is not real-time).
	SkyLight->RecaptureSky();
}

void AStackerRooftop::BuildLook()
{
	using MovecadeKart::Coords::DirToUE;
	using G = FGoldenHour;
	// StackerCanvas.tsx: a golden-hour rooftop; the sun high on the right, a cool rim from behind.
	Sun->SetWorldRotation((-DirToUE(FVector(G::SunX, G::SunY, G::SunZ)).GetSafeNormal()).Rotation());
	Sun->SetLightColor(Hex(G::SunColor));
	Sun->SetIntensity(Look.SunIntensity);
	Sun->SetCastShadows(true);
	Sun->SetAtmosphereSunLight(false);
	// The sun wins forward shading's single directional slot over the rim.
	Sun->SetForwardShadingPriority(1);
	Rim->SetWorldRotation((-DirToUE(FVector(G::RimX, G::RimY, G::RimZ)).GetSafeNormal()).Rotation());
	Rim->SetLightColor(Hex(G::RimColor));
	Rim->SetIntensity(Look.RimIntensity);
	Rim->SetCastShadows(false);
	Rim->SetAtmosphereSunLight(false);
	Rim->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(Look.SkyLightIntensity);
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(G::GroundColor) * G::GroundBounce);
	BuildSkyMaterial();
	Fog->SetStartDistance(Look.FogStartDistance);
	Fog->SetFogDensity(Look.FogDensity);
	Fog->SetFogHeightFalloff(Look.FogHeightFalloff);
	Fog->SetFogMaxOpacity(Look.FogMaxOpacity);
	Fog->SetFogCutoffDistance(G::FogCutoff);
	Fog->SetFogInscatteringColor(Hex(G::HorizonColor) * G::FogHorizonShare);
	Fog->SetDirectionalInscatteringColor(Hex(G::SunHazeColor) * G::SunHazeShare);
	Fog->SetDirectionalInscatteringExponent(G::SunHazeExponent);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = Look.Vignette;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Look.Bloom;
	// A touch of warmth and punch.
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(G::SaturationR, G::SaturationG, G::SaturationB, 1.0);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(G::Contrast, G::Contrast, G::Contrast, 1.0);
}

void AStackerRooftop::BuildSkyMaterial()
{
	using G = FGoldenHour;
	// The tracks' sky dome material, retinted for the golden hour; without it the dome stays plain (reported once).
	if (!SkyMaterial)
	{
		UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, SkyMaterialPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UE_CLOG(!SkyMaster, LogStacker, Warning, TEXT("Stacker rooftop: sky material %s is missing; the sky dome stays plain"), SkyMaterialPath);
		SkyMaterial = SkyMaster ? UMaterialInstanceDynamic::Create(SkyMaster, this) : nullptr;
	}
	UMaterialInstanceDynamic* Sky = SkyMaterial;
	if (!Sky)
	{
		return;
	}
	const FLinearColor Top = Hex(G::SkyTopColor), Horizon = Hex(G::HorizonColor);
	Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
	Sky->SetVectorParameterValue(TEXT("SkyMid"), FMath::Lerp(Horizon, Top, G::SkyMidMix));
	Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
	Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(G::SkyBelowColor));
	const FVector SunDir = MovecadeKart::Coords::DirToUE(FVector(G::SkySunX, G::SkySunY, G::SkySunZ)).GetSafeNormal();
	Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0));
	Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(G::SkySunColor));
	Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(G::SunDiscRadians));
	Sky->SetScalarParameterValue(TEXT("SunGlow"), G::SunGlow);
	Sky->SetScalarParameterValue(TEXT("GlowPower"), G::GlowPower);
	Sky->SetScalarParameterValue(TEXT("HorizonGlow"), G::HorizonGlow);
	Sky->SetScalarParameterValue(TEXT("Brightness"), G::SkyBrightness);
	UTexture* CloudMap = LoadObject<UTexture>(nullptr, CloudTexturePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UE_CLOG(!CloudMap, LogStacker, Warning, TEXT("Stacker rooftop: cloud texture %s is missing; the sky has no clouds"), CloudTexturePath);
	if (CloudMap)
	{
		Sky->SetTextureParameterValue(TEXT("CloudMap"), CloudMap);
	}
	Sky->SetScalarParameterValue(TEXT("Clouds"), G::SkyClouds);
	Sky->SetScalarParameterValue(TEXT("CloudScale"), G::SkyCloudScale);
	Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(G::CloudLitColor));
	Sky->SetVectorParameterValue(TEXT("CloudShade"), FMath::Lerp(Horizon, Top, G::CloudShadeMix) * G::CloudShadeDarken);
	SkyDome->SetMaterial(0, Sky);
}

void AStackerRooftop::BuildDeck()
{
	using D = FDeckShape;
	const double W = DeckWidth();
	UMaterialInterface* Deck = Materials.Surface(this, { Hex(TEXT("#8d8a84")), TEXT("painted_concrete"), 3.0, 0.2, 0.5, 0.4, 0.85, 0.0, 0.0, false, 1.0 });
	UMaterialInterface* Edge = Materials.Surface(this, { Hex(TEXT("#6e6a64")), TEXT("painted_concrete"), 2.0, 0.2, 0.5, 0.4, 0.9, 0.0, 0.0, true, 1.0 });
	UMaterialInterface* Hazard = Materials.Plain(this, Hex(TEXT("#e8b931")), 0.6);
	UMaterialInterface* HazardDark = Materials.Plain(this, Hex(TEXT("#1e1e22")), 0.6);
	// The deck, its parapet edge, the lip along the front with the yellow hazard stripe on it.
	Box(Deck, FVector(0.0, DeckY - D::SlabDrop, D::CentreZ), FVector(W, D::SlabThick, D::Depth), 0.0, false);
	Box(Edge, FVector(0.0, D::ParapetY, D::CentreZ), FVector(W, D::ParapetThick, D::Depth), 0.0, false);
	Box(Edge, FVector(0.0, DeckY + D::LipRise, D::LipZ), FVector(W, D::LipHeight, D::LipDeep));
	for (int32 i = 0; i < FMath::CeilToInt32(W / D::StripeBlock); ++i)
	{
		Box(i % 2 ? HazardDark : Hazard, FVector(-W / 2.0 + D::StripeBlock / 2.0 + i * D::StripeBlock, DeckY + D::StripeLift, D::StripeZ),
			FVector(D::StripeBlock, D::StripeThick, D::StripeDeep), 0.0, false);
	}
	for (int32 Lane = 0; Lane < Lanes; ++Lane)
	{
		BuildLaneZone(Lane);
	}
}

void AStackerRooftop::BuildLaneZone(int32 Lane)
{
	// A work zone per lane: dashed in the lane's colour, numbered, with the lane's plinth.
	using Zn = FZoneShape;
	const double X = LaneX(Lane, Lanes);
	const FLinearColor C = LaneColor(Lane);
	UMaterialInterface* Dash = Materials.Plain(this, C * Zn::DashShade, 0.55);
	for (int32 d = 0; d < Zn::FrontDashes; ++d)
	{
		Box(Dash, FVector(X - Zn::HalfWidth + d * Zn::DashStep, DeckY + Zn::DashLift, Zn::CentreZ - Zn::HalfDepth), FVector(Zn::DashLong, Zn::DashThick, Zn::DashThin), 0.0, false);
		Box(Dash, FVector(X - Zn::HalfWidth + d * Zn::DashStep, DeckY + Zn::DashLift, Zn::CentreZ + Zn::HalfDepth), FVector(Zn::DashLong, Zn::DashThick, Zn::DashThin), 0.0, false);
	}
	for (int32 d = 0; d < Zn::SideDashes; ++d)
	{
		const double Z = Zn::CentreZ - Zn::HalfDepth + Zn::SideDashStart + d * Zn::SideDashStep;
		Box(Dash, FVector(X - Zn::HalfWidth, DeckY + Zn::DashLift, Z), FVector(Zn::DashThin, Zn::DashThick, Zn::DashLong), 0.0, false);
		Box(Dash, FVector(X + Zn::HalfWidth, DeckY + Zn::DashLift, Z), FVector(Zn::DashThin, Zn::DashThick, Zn::DashLong), 0.0, false);
	}
	// The plinth rises from the deck to the foundation's underside, so the first block sits on it.
	Box(Materials.Plain(this, C * Zn::PlinthShade, 0.45, 0.1), FVector(X, (DeckY + Zn::PlinthTop) / 2.0, 0.0), FVector(Zn::PlinthWide, Zn::PlinthTop - DeckY, Zn::PlinthDeep));
	Box(Materials.Plain(this, Hex(TEXT("#2a2c33")), 0.6), FVector(X, Zn::CapY, 0.0), FVector(Zn::CapWide, Zn::CapThick, Zn::CapDeep), 0.0, false);
	UTextRenderComponent* Number = NewObject<UTextRenderComponent>(this, MakeUniqueObjectName(this, UTextRenderComponent::StaticClass(), TEXT("ZoneNumber")));
	Number->SetMobility(EComponentMobility::Static);
	Number->SetupAttachment(RootComponent);
	Number->SetRelativeLocation(ToWorld(FVector(X + Zn::NumberX, DeckY + Zn::NumberLift, Zn::NumberZ)));
	// Lying flat on the deck, reading toward the camera.
	Number->SetRelativeRotation(FRotator(90.0, 180.0, 0.0));
	Number->SetHorizontalAlignment(EHTA_Center);
	Number->SetVerticalAlignment(EVRTA_TextCenter);
	Number->SetWorldSize(Zn::NumberSize);
	Number->SetTextRenderColor((C * Zn::DashShade).ToFColor(true));
	Number->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Lane + 1)));
	Number->SetCastShadow(false);
	Number->ComponentTags.Add(RoofTag);
	AddInstanceComponent(Number);
	Number->RegisterComponent();
}

void AStackerRooftop::BuildCity()
{
	using MovecadeKart::FMeshBuilder;
	using Bd = FBuildingShape;
	using Cy = FCityShape;
	// Three bands of towers, deterministic: heights, setbacks, ledges, antennas and lit window grids.
	FRandomStream R(CitySeed);
	const TCHAR* const Facades[] = { TEXT("#8c95a6"), TEXT("#a39a8f"), TEXT("#7d889b"), TEXT("#b0a294"), TEXT("#6f7c90") };
	// The windows are opaque and emissive (not translucent), so they sit in the fog with the facades.
	UMaterialInterface* Window = Materials.Plain(this, Hex(TEXT("#edd3a4")), 0.4, 0.0, 1.9);
	UMaterialInterface* Dark = Materials.Plain(this, Hex(TEXT("#3a4458")), 0.15, 0.4);
	UMaterialInterface* Antenna = Materials.Plain(this, Hex(TEXT("#3a3d44")), 0.5, 0.6);
	TArray<UMaterialInterface*> FacadeMats;
	for (const TCHAR* F : Facades)
	{
		FacadeMats.Add(Materials.Plain(this, Hex(F), 0.85));
	}
	const TArray<UMaterialInterface*> WindowMats = { Window, Dark };
	// The building under the roof: its body runs down out of frame, windows on the face toward the camera.
	{
		const double W = DeckWidth();
		Box(FacadeMats[1], FVector(0.0, Bd::TopY - Bd::Height / 2.0, FDeckShape::CentreZ), FVector(W - Bd::Inset, Bd::Height, Bd::Deep), 0.0, false);
		FMeshBuilder B;
		const int32 Lit = B.Section(TEXT("Lit"));
		const int32 Unlit = B.Section(TEXT("Dark"));
		for (int32 r = 0; r < Bd::WindowRows; ++r)
		{
			for (int32 c = 0; c < FMath::FloorToInt32(W / Bd::WindowPitchX); ++c)
			{
				const bool bLit = R.FRand() < RoofWindowsLit;
				WindowQuad(B, bLit ? Lit : Unlit, FVector(-W / 2.0 + Bd::WindowPitchX / 2.0 + c * Bd::WindowPitchX, Bd::FirstWindowY - r * Bd::WindowPitchY, Bd::WindowZ),
					Bd::WindowHalfWide, Bd::WindowHalfTall);
			}
		}
		Part(B.Build(this, TEXT("RoofWindows")), WindowMats, false);
	}
	for (int32 Band = 0; Band < BandCount; ++Band)
	{
		USceneComponent* Parent = Pivot(TEXT("CityBand"), FVector::ZeroVector);
		Bands.Add(Parent);
		FMeshBuilder B;
		const int32 Lit = B.Section(TEXT("Lit"));
		const int32 Unlit = B.Section(TEXT("Dark"));
		const int32 N = BandBuildings[Band];
		const double Span = Cy::SpanBase + Band * Cy::SpanPerBand + DeckWidth();
		for (int32 i = 0; i < N; ++i)
		{
			const double X = -Span / 2.0 + (i + 0.5) * Span / N + R.FRandRange(-Cy::Jitter, Cy::Jitter);
			const double Wd = Span / N * R.FRandRange(Cy::MinWidthShare, Cy::MaxWidthShare);
			const double Dp = R.FRandRange(Cy::MinDepth, Cy::MaxDepth);
			// The near band stays under the parapet, the middle rises past it, the far one climbs into the haze, all
			// low enough to leave the golden sky and its clouds above them (more of it as the stacks climb).
			const double H = Cy::HeightBase + Band * Cy::HeightPerBand + R.FRandRange(0.0, Cy::HeightRandom + Band * Cy::HeightRandomPerBand);
			const double Base = Cy::Base;
			const double Z = BandZ[Band] - Dp / 2.0;
			// No shadows from the city: it is beyond the sun's useful shadow range and every move would invalidate it.
			Box(FacadeMats[(i + Band) % FacadeMats.Num()], FVector(X, Base + H / 2.0, Z), FVector(Wd, H, Dp), 0.0, false, Parent);
			// A setback on the taller ones, a ledge, a rooftop antenna.
			if (H > Cy::SetbackMinHeight && R.FRand() < Cy::SetbackChance)
			{
				Box(FacadeMats[(i + 2) % FacadeMats.Num()], FVector(X, Base + H + Cy::SetbackRise, Z), FVector(Wd * Cy::SetbackShare, Cy::SetbackHeight, Dp * Cy::SetbackShare),
					0.0, false, Parent);
			}
			Box(Dark, FVector(X, Base + H + Cy::LedgeRise, Z), FVector(Wd * Cy::LedgeShare, Cy::LedgeThick, Dp * Cy::LedgeShare), 0.0, false, Parent);
			if (R.FRand() < Cy::AntennaChance)
			{
				Box(Antenna, FVector(X + Wd * Cy::AntennaOffset, Base + H + Cy::AntennaRise, Z), FVector(Cy::AntennaThick, Cy::AntennaHeight, Cy::AntennaThick), 0.0, false, Parent);
			}
			// Windows: a grid on the face toward the deck, some lit, baked into the band's one window mesh.
			const int32 Cols = FMath::Max(2, FMath::FloorToInt32(Wd / Cy::WindowPitchX));
			const int32 Rows = FMath::Max(2, FMath::FloorToInt32(H / Cy::WindowPitchY));
			for (int32 r = 0; r < Rows; ++r)
			{
				for (int32 c = 0; c < Cols; ++c)
				{
					const bool bLit = R.FRand() < BandWindowsLit;
					const double WX = X - Wd / 2.0 + (c + 0.5) * Wd / Cols;
					const double WY = Base + Cy::WindowFirstY + r * Cy::WindowPitchY;
					if (WY > Base + H - Cy::WindowTopMargin)
					{
						continue;
					}
					WindowQuad(B, bLit ? Lit : Unlit, FVector(WX, WY, BandZ[Band] + Cy::WindowProud), Wd / Cols * Cy::WindowWideShare, Cy::WindowHalfTall);
				}
			}
		}
		Part(B.Build(this, *FString::Printf(TEXT("CityWindows%d"), Band)), WindowMats, false, Parent);
	}
}

void AStackerRooftop::BuildSky()
{
	using MovecadeKart::FMeshBuilder;
	using Cl = FCloudShape;
	// RooftopEnvironment.tsx: four soft clouds in the haze beyond the far band, a few puffs each, drifting together.
	Clouds = Pivot(TEXT("Clouds"), FVector::ZeroVector);
	FMeshBuilder B;
	for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(CloudX)); ++i)
	{
		const FVector At(CloudX[i], Cl::Y + (i % 2) * Cl::AlternateRise, Cl::Z);
		for (const FPuff& Puff : CloudPuffs)
		{
			// Web radii (x across, y up, z deep) to UE radii (X deep, Y across, Z up), cm.
			B.Ellipsoid(TEXT("Cloud"), MovecadeKart::Coords::ToUE(At + FVector(Puff.X, Puff.Y, Puff.Z)), FVector(Puff.RadiusZ, Puff.RadiusX, Puff.RadiusY) * 100.0,
				Cl::Rings, Cl::Segments, FVector2D::ZeroVector);
		}
	}
	// Lit by the low sun with a little of their own warmth, so the undersides stay peach rather than grey.
	Part(B.Build(this, TEXT("StackerClouds")), { Materials.Plain(this, Hex(TEXT("#f1d4bf")), 1.0, 0.0, 0.35) }, false, Clouds);
}

void AStackerRooftop::BuildProps()
{
	const double W = DeckWidth();
	BuildCrane(W);
	BuildRailing(W);
	BuildDeckProps(W);
}

void AStackerRooftop::BuildCrane(double W)
{
	// The crane behind the deck: a lattice mast, the jib, the counter-jib and a hook on its cable.
	using Cr = FCraneShape;
	UMaterialInterface* Crane = Materials.Plain(this, Hex(TEXT("#d39a32")), 0.5, 0.2);
	UMaterialInterface* Steel = Materials.Plain(this, Hex(TEXT("#4a4d55")), 0.4, 0.8);
	const FVector CraneBase(-W / 2.0 - Cr::BaseOutside, DeckY, Cr::BaseZ);
	for (const double Dx : { -Cr::LegOffset, Cr::LegOffset })
	{
		for (const double Dz : { -Cr::LegOffset, Cr::LegOffset })
		{
			Box(Crane, CraneBase + FVector(Dx, Cr::MastHeight / 2.0, Dz), FVector(Cr::LegThick, Cr::MastHeight, Cr::LegThick));
		}
	}
	for (int32 i = 0; i < Cr::Rungs; ++i)
	{
		Box(Crane, CraneBase + FVector(0.0, Cr::FirstRung + i, Cr::LegOffset), FVector(Cr::RungWide, Cr::RungThick, Cr::RungThick), 0.0, false);
		Box(Crane, CraneBase + FVector(0.0, Cr::FirstRung + i, -Cr::LegOffset), FVector(Cr::RungWide, Cr::RungThick, Cr::RungThick), 0.0, false);
	}
	for (const FBoxPart& Jib : CraneJibs)
	{
		Box(Crane, CraneBase + OffsetOf(Jib), SizeOf(Jib), 0.0, Jib.bShadows);
	}
	for (const FBoxPart& Part : CraneSteel)
	{
		Box(Steel, CraneBase + OffsetOf(Part), SizeOf(Part), 0.0, Part.bShadows);
	}
}

void AStackerRooftop::BuildRailing(double W)
{
	using Rl = FRailShape;
	UMaterialInterface* Rail = Materials.Plain(this, Hex(TEXT("#c9ccd2")), 0.35, 0.9);
	for (const double Y : RailHeights)
	{
		Box(Rail, FVector(0.0, DeckY + Y, Rl::BackZ), FVector(W - 2.0 * Rl::Inset, Rl::Thick, Rl::Thick), 0.0, false);
		for (const double Side : { -1.0, 1.0 })
		{
			Box(Rail, FVector(Side * (W / 2.0 - Rl::Inset), DeckY + Y, Rl::SideZ), FVector(Rl::Thick, Rl::Thick, Rl::SideLength), 0.0, false);
		}
	}
	const int32 Posts = FMath::CeilToInt32(W / Rl::PostSpacing);
	for (int32 i = 0; i <= Posts; ++i)
	{
		Box(Rail, FVector(-W / 2.0 + Rl::Inset + i * (W - 2.0 * Rl::Inset) / Posts, DeckY + Rl::PostCentreY, Rl::BackZ), FVector(Rl::Thick, Rl::PostHeight, Rl::Thick), 0.0, false);
	}
}

void AStackerRooftop::BuildDeckProps(double W)
{
	// Cones, a cable spool, a pallet with boxes, the HVAC unit with its fan, two flags.
	using Pr = FPropShape;
	UMaterialInterface* Steel = Materials.Plain(this, Hex(TEXT("#4a4d55")), 0.4, 0.8);
	UMaterialInterface* Rail = Materials.Plain(this, Hex(TEXT("#c9ccd2")), 0.35, 0.9);
	UMaterialInterface* Cone = Materials.Plain(this, Hex(TEXT("#f26b2c")), 0.5);
	UMaterialInterface* Wood = Materials.Surface(this, { Hex(TEXT("#a07a52")), TEXT("dark_wooden_planks"), 0.5, 0.4, 0.6, 0.4, 0.8 });
	UMaterialInterface* Cardboard = Materials.Plain(this, Hex(TEXT("#b58b5a")), 0.8);
	UMaterialInterface* Hvac = Materials.Plain(this, Hex(TEXT("#b9bcc2")), 0.45, 0.5);
	const double Side = W / 2.0 - Pr::SideInset;
	for (const auto& Spot : ConeSpots)
	{
		Post(Cone, FVector(-Side + Spot[0], DeckY, Spot[1]), Pr::ConeRadius, Pr::ConeHeight);
	}
	const FVector PalletAnchor(Side, DeckY, 0.0);
	Box(Wood, PalletAnchor + OffsetOf(Pallet), SizeOf(Pallet), 0.0, Pallet.bShadows);
	for (const FBoxPart& Carton : PalletBoxes)
	{
		Box(Cardboard, PalletAnchor + OffsetOf(Carton), SizeOf(Carton), 0.0, Carton.bShadows);
	}
	// The spool lies on its side: a wooden drum with a steel core.
	const FVector SpoolAt = ToWorld(FVector(-Side + Pr::SpoolX, DeckY + Pr::SpoolY, Pr::SpoolZ));
	Group(Cylinder, Wood, true)->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), SpoolAt, FVector(Pr::SpoolDrum, Pr::SpoolDrum, Pr::SpoolWidth)), false);
	Group(Cylinder, Steel, true)->AddInstance(FTransform(FRotator(0.0, 0.0, 90.0), SpoolAt, FVector(Pr::SpoolCore, Pr::SpoolCore, Pr::SpoolCoreWidth)), false);
	BuildHvac(Side, Hvac, Steel);
	BuildFlags(W, Rail);
}

void AStackerRooftop::BuildHvac(double Side, UMaterialInterface* Body, UMaterialInterface* Steel)
{
	using Hv = FHvacShape;
	const FVector HvacAt(Side + Hv::OffsetX, DeckY, Hv::Z);
	Box(Body, HvacAt + OffsetOf(HvacBody), SizeOf(HvacBody), 0.0, HvacBody.bShadows);
	Box(Steel, HvacAt + OffsetOf(HvacGrille), SizeOf(HvacGrille), 0.0, HvacGrille.bShadows);
	// The moving parts cast no shadows: a spinning or waving caster would invalidate the cached shadow every frame.
	Fan = Pivot(TEXT("HvacFan"), HvacAt + FVector(0.0, Hv::FanY, 0.0));
	for (int32 b = 0; b < Hv::Blades; ++b)
	{
		const FRotator Turn(0.0, b * (360.0 / Hv::Blades), 0.0);
		UStaticMeshComponent* Blade = Part(Cube, { Steel }, false, Fan);
		Blade->SetRelativeRotation(Turn);
		Blade->SetRelativeLocation(Turn.RotateVector(FVector(Hv::BladeReachCm, 0.0, 0.0)));
		Blade->SetRelativeScale3D(FVector(Hv::BladeLong, Hv::BladeWide, Hv::BladeThin));
	}
}

void AStackerRooftop::BuildFlags(double W, UMaterialInterface* PoleMaterial)
{
	using Fl = FFlagShape;
	const TCHAR* const FlagColors[] = { TEXT("#d86654"), TEXT("#4c8990") };
	for (int32 f = 0; f < static_cast<int32>(UE_ARRAY_COUNT(FlagColors)); ++f)
	{
		const FVector Pole(f == 0 ? -W / 2.0 + Fl::CornerInset : W / 2.0 - Fl::CornerInset, DeckY, Fl::Z);
		Post(PoleMaterial, Pole, Fl::PoleRadius, Fl::PoleHeight);
		USceneComponent* Hinge = Pivot(TEXT("Flag"), Pole + FVector(0.0, Fl::HingeY, 0.0));
		UStaticMeshComponent* Cloth = Part(Cube, { Materials.Plain(this, Hex(FlagColors[f]), 0.8) }, false, Hinge);
		Cloth->SetRelativeLocation(FVector(0.0, Fl::ClothAcrossCm, Fl::ClothDropCm));
		Cloth->SetRelativeScale3D(FVector(Fl::ClothThin, Fl::ClothLong, Fl::ClothTall));
		Flags.Add(Hinge);
	}
}

void AStackerRooftop::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		Build();
	}
	Clock += DeltaSeconds;
	if (Fan)
	{
		Fan->SetRelativeRotation(FRotator(0.0, FMath::RadiansToDegrees(Clock * FanSpin), 0.0));
	}
	// The flags swing on their poles, each out of step with the other.
	for (int32 f = 0; f < Flags.Num(); ++f)
	{
		if (Flags[f])
		{
			using Fl = FFlagShape;
			Flags[f]->SetRelativeRotation(FRotator(0.0, Fl::YawBase + Fl::YawSwing * FMath::Sin(Clock * Fl::YawRate + f * Fl::YawPhase), Fl::RollSwing * FMath::Sin(Clock * Fl::RollRate + f)));
		}
	}
	if (Clouds)
	{
		Clouds->SetRelativeLocation(ToWorld(FVector(FMath::Sin(Clock * CloudDriftRate) * CloudDrift, 0.0, 0.0)));
	}
}
