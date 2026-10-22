#include "Games/Bowling/BowlingAlley.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
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

using MovecadeKart::Art::Hex;

namespace
{
	const FName AlleyTag(TEXT("BowlingAlley"));
	const FName ColorParam(TEXT("Color"));

	// venueLayout.ts / laneLayout.ts (web units). Every Box() below is placed in these units too.
	/** Lane centres house-left to house-right; the player bowls the middle one. */
	constexpr double LaneCenters[] = { -7.1, -3.55, 0.0, 3.55, 7.1 };
	constexpr double DividerCenters[] = { -8.875, -5.325, -1.775, 1.775, 5.325, 8.875 };
	constexpr double DeckZ = ABowlingAlley::DeckZ;
	constexpr double LaneFrontZ = 1.35;
	constexpr double PitDepth = 2.25;
	constexpr double MachineZ = -16.82;
	constexpr double MaskingZ = -15.02;
	constexpr double FoulZ = 0.55;
	constexpr double LaneBackZ = DeckZ - PitDepth;
	constexpr double LaneSurfaceLength = LaneFrontZ - LaneBackZ;
	constexpr double LaneSurfaceCenterZ = (LaneFrontZ + LaneBackZ) / 2.0;
	/** The trough lights hang over each lane pair and the played lane. */
	constexpr double TroughCenters[] = { -5.325, 0.0, 5.325 };

	// The rack and the ball, at the web's visual scales (BowlingPins.h).
	constexpr double PinHeight = MovecadeKart::Bowling::VisualPinHeight;
	constexpr double PinBelly = MovecadeKart::Bowling::VisualPinRadius;
	constexpr double BallRadius = MovecadeKart::Bowling::VisualBallRadius;
	constexpr double DeckY = MovecadeKart::Bowling::VisualDeckY;

	/** The house neon: hot magenta, ice cyan and the house violet. */
	const TCHAR* const NeonPink = TEXT("#ff3fb4");
	const TCHAR* const NeonCyan = TEXT("#38e1ff");
	const TCHAR* const NeonViolet = TEXT("#8a6cff");

	// The lights (lumens), brightest on the deck so the pins are the brightest thing in frame.
	constexpr float DeckKeyLumens = 5600.f;
	constexpr float PlayedTroughLumens = 3200.f;
	constexpr float HouseTroughLumens = 2000.f;
	/** The show spots at full energy; dark until a strike. */
	constexpr double ShowSpotLumens = 12000.0;

	// The light show (SetShow): the beat in radians per second (a turkey's strobe is faster), and how hard it pulses.
	constexpr double ShowBeat = 5.0;
	constexpr double TurkeyBeat = 9.0;
	/** Energy above this is "on": below it the room rests. */
	constexpr double ShowOnEnergy = 0.001;
	/** Energy past 1 only strobes faster; the brightness caps at this. */
	constexpr double MaxShowEnergy = 1.5;
	/** The deck key flashes up to this much brighter on the beat. */
	constexpr double DeckKeyFlash = 0.6;

	// The grade: no sun indoors, so auto exposure is held to a narrow band set by the lane's light.
	constexpr float ExposureMin = 6.3f;
	constexpr float ExposureMax = 6.9f;
	constexpr float Bloom = 0.85f;
	constexpr float Vignette = 0.45f;

	/** pinGeometry.ts PROFILE: (height 0..1, radius relative to the belly). */
	constexpr double PinProfile[][2] = {
		{ 0, 0 }, { 0.001, 0.426 }, { 0.012, 0.438 }, { 0.035, 0.52 }, { 0.07, 0.66 }, { 0.11, 0.8 }, { 0.16, 0.91 },
		{ 0.22, 0.978 }, { 0.3, 1 }, { 0.37, 0.975 }, { 0.43, 0.918 }, { 0.49, 0.835 }, { 0.545, 0.72 }, { 0.595, 0.57 },
		{ 0.64, 0.43 }, { 0.672, 0.348 }, { 0.7, 0.318 }, { 0.735, 0.362 }, { 0.775, 0.46 }, { 0.82, 0.53 }, { 0.86, 0.548 },
		{ 0.9, 0.51 }, { 0.935, 0.41 }, { 0.965, 0.25 }, { 0.985, 0.11 }, { 1, 0 },
	};

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

	/** The played lane's pin spot (web world units) for pin 1..10 on the lane at LaneX. */
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

void ABowlingAlley::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad, bool bShadows)
{
	// The engine cube is 1 m: web (x, y, z) sizes become UE (z, x, y) scales.
	Groups.Find(Cube, Material, bShadows)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), true);
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
	using namespace MovecadeKart::Art;
	// Lacquered ivory and a glossy red neck: the pins catch the deck key like the real thing.
	PinMaterials = {
		Materials.Surface(this, { Hex(TEXT("#fbf6ec")), TEXT("plastered_wall_02"), 0.4, 0.06, 0.15, 0.12, 0.14 }),
		Materials.Plain(this, Hex(TEXT("#d0102c")), 0.16),
	};
}

void ABowlingAlley::BuildPinMesh()
{
	// The USBC silhouette lathed in three ivory runs and two red neck bands: (from, to, is a band).
	MovecadeKart::FMeshBuilder B;
	const double H = PinHeight * 100.0, R = PinBelly * 100.0;
	const double Bands[][3] = { { 0.0, 0.658, 0 }, { 0.658, 0.686, 1 }, { 0.686, 0.704, 0 }, { 0.704, 0.732, 1 }, { 0.732, 1.0, 0 } };
	for (const auto& Band : Bands)
	{
		const bool bBand = Band[2] > 0.5;
		TArray<FVector2D> Profile;
		const int32 Steps = FMath::Max(2, FMath::CeilToInt32((Band[1] - Band[0]) * 60.0));
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double T = FMath::Lerp(Band[0], Band[1], static_cast<double>(i) / Steps);
			// Bands stand a hair proud of the body so they never z-fight. Centred on the pin's middle: the solver
			// poses pin centres.
			Profile.Add(FVector2D(PinRadiusAt(T) * R * (bBand ? 1.006 : 1.0), (T - 0.5) * H));
		}
		B.Lathe(bBand ? FName(TEXT("Band")) : FName(TEXT("Ivory")), Profile, 40, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector,
			Band[0], Band[1]);
	}
	PinMesh = B.Build(this, TEXT("Pin"));
}

void ABowlingAlley::BuildBallMesh()
{
	// A sphere with a three-hole grip, the holes as shallow dark lenses on the shell.
	MovecadeKart::FMeshBuilder B;
	const double R = BallRadius * 100.0;
	B.Ellipsoid(TEXT("Shell"), FVector::ZeroVector, FVector(R), 24, 36, FVector2D::ZeroVector);
	const FVector Holes[] = { FVector(0.3, -0.18, 0.93), FVector(0.3, 0.18, 0.93), FVector(-0.32, 0.0, 0.94) };
	for (const FVector& Hole : Holes)
	{
		const FVector N = Hole.GetSafeNormal();
		B.Ellipsoid(TEXT("Holes"), N * R * 0.975, FVector(R * 0.13, R * 0.13, R * 0.04), 4, 12, FVector2D::ZeroVector);
	}
	BallMesh = B.Build(this, TEXT("Ball"));
}

void ABowlingAlley::BuildGutterMesh()
{
	// A half pipe the length of the lane, open to the sky.
	MovecadeKart::FMeshBuilder B;
	const int32 S = B.Section(TEXT("Gutter"));
	const double R = 20.0, L = LaneSurfaceLength * 100.0;
	constexpr int32 Segs = 16;
	auto P = [&](double A, double X) { return FVector(X, -FMath::Cos(A) * R, -FMath::Sin(A) * R); };
	auto N = [](double A) { return FVector(0.0, FMath::Cos(A), FMath::Sin(A)); };
	for (int32 i = 0; i < Segs; ++i)
	{
		const double A0 = UE_PI * i / Segs, A1 = UE_PI * (i + 1) / Segs;
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
	UMaterialInterface* Carpet = Materials.Surface(this, { Hex(TEXT("#2b2140")), TEXT("leafy_grass"), 0.6, 0.35, 0.5, 0.4, 0.95, 0.0, 0.0, false, 1.0 });
	UMaterialInterface* Wall = Materials.Surface(this, { Hex(TEXT("#221d30")), TEXT("plastered_wall_02"), 2.4, 0.35, 0.5, 0.4, 0.9 });
	UMaterialInterface* Wallpaper = Materials.Surface(this, { Hex(TEXT("#33284d")), TEXT("plastered_wall_02"), 1.6, 0.45, 0.6, 0.4, 0.88 });
	UMaterialInterface* Wainscot = Materials.Surface(this, { Hex(TEXT("#5a3a26")), TEXT("dark_wooden_planks"), 1.2, 0.5, 0.7, 0.4, 0.45 });
	UMaterialInterface* Ceiling = Materials.Plain(this, Hex(TEXT("#16141c")), 0.9);
	UMaterialInterface* Accent = Neon(TEXT("NeonViolet"), Hex(NeonViolet) * 2.2f);

	Box(Carpet, FVector(0.0, -0.3, -4.0), FVector(22.0, 0.14, 24.0));
	// Back wall (behind the machines), its wallpaper and wainscot.
	Box(Wall, FVector(0.0, 4.2, -16.55 - 0.3), FVector(24.0, 9.2, 0.55));
	Box(Wallpaper, FVector(0.0, 4.55, -16.24 - 0.3), FVector(22.4, 7.4, 0.02));
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Wall, FVector(Side * 11.4, 4.2, -4.0), FVector(0.5, 9.2, 24.0));
		Box(Wallpaper, FVector(Side * 11.13, 4.55, -4.0), FVector(0.02, 7.4, 22.8));
		Box(Wainscot, FVector(Side * 11.1, 0.72, -4.0), FVector(0.04, 1.55, 22.8));
		// A violet accent line where the wainscot meets the wallpaper (the house colour, as a fixture).
		Box(Accent, FVector(Side * 11.08, 1.52, -4.0), FVector(0.03, 0.035, 22.6), 0.0, false);
	}
	// The near wall behind the concourse closes the room for the reverse shots.
	Box(Wall, FVector(0.0, 4.2, 8.4), FVector(24.0, 9.2, 0.55));
	Box(Ceiling, FVector(0.0, 5.85 + 0.16, -4.0), FVector(24.0, 0.32, 24.0));
}

void ABowlingAlley::BuildLanes()
{
	// Lacquered maple: honey boards over a dark slab; a mirror-glossy finish so the neon and the troughs streak down
	// the lane in its reflections.
	UMaterialInterface* Maple = Materials.Surface(this, { Hex(TEXT("#9a7450")), TEXT("dark_wooden_planks"), 0.9, 0.55, 0.3, 0.18, 0.09, 0.0, 0.0, false, 0.35 });
	UMaterialInterface* Slab = Materials.Plain(this, Hex(TEXT("#6e4526")), 0.38);
	UMaterialInterface* Approach = Materials.Surface(this, { Hex(TEXT("#b9895a")), TEXT("dark_wooden_planks"), 1.4, 0.45, 0.35, 0.25, 0.2, 0.0, 0.0, false });
	UMaterialInterface* Dark = Materials.Plain(this, Hex(TEXT("#0f0f16")), 0.76);
	UMaterialInterface* Machine = Materials.Plain(this, Hex(TEXT("#22242a")), 0.45, 0.2);
	UMaterialInterface* Foul = Neon(TEXT("Foul"), Hex(TEXT("#ff2a3d")) * 1.6f);
	UMaterialInterface* Return = Materials.Plain(this, Hex(TEXT("#f1e6d2")), 0.36);

	// One continuous approach behind all five lanes, and one foul line across it.
	Box(Approach, FVector(0.0, -0.13, 2.55), FVector(18.3, 0.22, 4.2));
	Box(Foul, FVector(0.0, 0.015, FoulZ), FVector(18.3, 0.025, 0.09), 0.0, false);
	for (const double X : LaneCenters)
	{
		Box(Slab, FVector(X, -0.07, LaneSurfaceCenterZ), FVector(LaneVisualWidth, 0.14, LaneSurfaceLength));
		Box(Maple, FVector(X, 0.008, LaneSurfaceCenterZ), FVector(LaneVisualWidth - 0.06, 0.02, LaneSurfaceLength - 0.05));
		BuildLaneMarkings(X);
	}
	BuildGutters();
	// Ball returns, one per lane pair, on the inner dividers at the approach.
	for (int32 i = 1; i + 1 < UE_ARRAY_COUNT(DividerCenters); ++i)
	{
		const double X = DividerCenters[i];
		Box(Return, FVector(X, 0.22, 1.75), FVector(0.62, 0.44, 1.5));
		Box(Machine, FVector(X, 0.45, 1.77), FVector(0.66, 0.04, 1.54));
		Box(Dark, FVector(X, 0.2, 2.47), FVector(0.38, 0.2, 0.08));
	}
}

void ABowlingAlley::BuildLaneMarkings(double LaneX)
{
	// Targeting arrows at 4.6 m, guide dots at 2 m past the foul line: too small to shadow anything. The played
	// lane's glow faintly, so a new bowler can see what to aim over.
	UMaterialInterface* Dark = Materials.Plain(this, Hex(TEXT("#0f0f16")), 0.76);
	UMaterialInterface* Arrow = LaneX == 0.0 ? Neon(TEXT("Marker"), Hex(NeonCyan) * 0.9f) : Dark;
	for (const double Ax : { -0.46, -0.23, 0.0, 0.23, 0.46 })
	{
		Box(Arrow, FVector(LaneX + Ax, 0.02, -1.62), FVector(0.07, 0.006, 0.18), UE_PI / 4.0, false);
	}
	for (const double Dx : { -0.38, -0.19, 0.0, 0.19, 0.38 })
	{
		Box(Dark, FVector(LaneX + Dx, 0.02, -4.7), FVector(0.05, 0.006, 0.05), 0.0, false);
	}
}

void ABowlingAlley::BuildGutters()
{
	// One gutter channel in every gap and along both outer edges, capping walls on the outside only.
	UMaterialInterface* Gutter = Materials.Plain(this, Hex(TEXT("#9ea3ad")), 0.22, 0.75);
	UMaterialInterface* Machine = Materials.Plain(this, Hex(TEXT("#22242a")), 0.45, 0.2);
	UInstancedStaticMeshComponent* Gutters = Groups.Find(GutterMesh, Gutter, false);
	for (int32 i = 0; i < UE_ARRAY_COUNT(DividerCenters); ++i)
	{
		const double X = DividerCenters[i];
		Gutters->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(FVector(X, -0.05, LaneSurfaceCenterZ)), FVector::OneVector), true);
		if (i == 0 || i == UE_ARRAY_COUNT(DividerCenters) - 1)
		{
			Box(Machine, FVector(X + (X < 0.0 ? -0.29 : 0.29), 0.02, LaneSurfaceCenterZ), FVector(0.18, 0.24, LaneSurfaceLength - 0.2));
		}
	}
}

void ABowlingAlley::BuildDeck()
{
	UMaterialInterface* Machine = Materials.Plain(this, Hex(TEXT("#22242a")), 0.45, 0.2);
	UMaterialInterface* Dark = Materials.Plain(this, Hex(TEXT("#0f0f16")), 0.76);
	UMaterialInterface* Kick = Materials.Plain(this, Hex(TEXT("#15131d")), 0.35, 0.2);
	UMaterialInterface* KickNeon = Neon(TEXT("KickNeon"), Hex(NeonCyan) * 2.4f);
	UMaterialInterface* Pit = Materials.Plain(this, Hex(TEXT("#050507")), 0.95);

	// The pinsetter run: one machine body behind all five decks.
	Box(Machine, FVector(0.0, 1.85, MachineZ), FVector(19.6, 3.8, 1.6));
	Box(Dark, FVector(0.0, 1.85 + 1.55, MachineZ + 0.55), FVector(19.0, 0.62, 1.9));
	for (const double X : DividerCenters)
	{
		Box(Machine, FVector(X, 1.7, MachineZ + 0.55), FVector(0.48, 3.5, 0.55));
		// The kickbacks: the side walls of each pin deck, trimmed in neon along the top so the rack sits in a frame
		// of light (and the lane mirrors it).
		const double KickFrom = DeckZ + 1.1, KickTo = MaskingZ + 0.2;
		Box(Kick, FVector(X, 0.55, (KickFrom + KickTo) / 2.0), FVector(0.14, 1.1, KickFrom - KickTo));
		Box(KickNeon, FVector(X, 1.115, (KickFrom + KickTo) / 2.0), FVector(0.05, 0.03, KickFrom - KickTo), 0.0, false);
		Box(KickNeon, FVector(X, 0.55, KickFrom + 0.005), FVector(0.05, 1.1, 0.03), 0.0, false);
	}
	// The masking units are lightboxes: each lane its own panel of house art (the Movecade billboards).
	UMaterialInterface* Art[4];
	for (int32 i = 0; i < UE_ARRAY_COUNT(Art); ++i)
	{
		Art[i] = Materials.Unlit(this, *FString::Printf(TEXT("/Game/Tracks/Shared/Textures/mc_billboard_%d.mc_billboard_%d"), i, i), FLinearColor(1.15f, 1.15f, 1.15f));
	}
	for (int32 Lane = 0; Lane < UE_ARRAY_COUNT(LaneCenters); ++Lane)
	{
		const double X = LaneCenters[Lane];
		// The lane's pinsetter housing, then the pit: a black well behind the deck, so the pins stand against darkness.
		Box(Dark, FVector(X, 1.4, MachineZ + 0.86), FVector(3.7, 2.35, 0.3));
		Box(Dark, FVector(X, 2.4, MachineZ + 0.92), FVector(3.5, 1.15, 0.12));
		Box(Pit, FVector(X, -0.35, DeckZ - 1.6), FVector(LaneVisualWidth + 0.2, 0.6, 2.2));
		BuildMasking(X, Art[(Lane + 2) % UE_ARRAY_COUNT(Art)]);
	}
}

void ABowlingAlley::BuildMasking(double X, UMaterialInterface* PanelArt)
{
	// A backlit panel framed in rails, the cove under it lighting the rack from the front.
	UMaterialInterface* Machine = Materials.Plain(this, Hex(TEXT("#22242a")), 0.45, 0.2);
	UMaterialInterface* Dark = Materials.Plain(this, Hex(TEXT("#0f0f16")), 0.76);
	UMaterialInterface* Pit = Materials.Plain(this, Hex(TEXT("#050507")), 0.95);
	UMaterialInterface* Cove = Materials.GlowOpaque(this, Hex(TEXT("#fff0d4")) * 2.6f);
	UMaterialInterface* Panel = Materials.GlowOpaque(this, Hex(TEXT("#1a1630")) * 1.4f);
	UMaterialInterface* Accent = Neon(TEXT("NeonViolet"), Hex(NeonViolet) * 2.2f);
	constexpr double PanelY = 2.05;
	Box(Dark, FVector(X, PanelY, MaskingZ - 0.12), FVector(3.62, 1.98, 0.18));
	Box(Panel, FVector(X, PanelY, MaskingZ), FVector(3.34, 1.72, 0.02), 0.0, false);
	Box(PanelArt, FVector(X, PanelY, MaskingZ + 0.012), FVector(3.2, 1.6, 0.01), 0.0, false);
	Box(Machine, FVector(X, PanelY + 0.93, MaskingZ + 0.06), FVector(3.46, 0.14, 0.16));
	Box(Machine, FVector(X, PanelY - 0.93, MaskingZ + 0.06), FVector(3.46, 0.14, 0.16));
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Dark, FVector(X + Side * 1.73, PanelY, MaskingZ + 0.06), FVector(0.1, 1.98, 0.12));
	}
	Box(Pit, FVector(X, PanelY - 1.62, MaskingZ - 0.18), FVector(3.3, 0.9, 0.02));
	Box(Cove, FVector(X, PanelY - 1.03, MaskingZ + 0.11), FVector(3.3, 0.06, 0.06), 0.0, false);
	Box(Accent, FVector(X, PanelY + 0.99, MaskingZ + 0.11), FVector(3.3, 0.022, 0.05), 0.0, false);
}

void ABowlingAlley::BuildCeiling()
{
	UMaterialInterface* Iron = Materials.Plain(this, Hex(TEXT("#1b1a22")), 0.55, 0.6);
	UMaterialInterface* Lens = Materials.GlowOpaque(this, Hex(TEXT("#f3ecdc")) * 2.4f);
	// The truss: paired chords every 3 m with hangers, heavier beams every 6 m and down the lane dividers.
	for (const double Z : { -13.2, -10.2, -7.2, -4.2, -1.2, 1.8, 4.8 })
	{
		Box(Iron, FVector(0.0, 5.5, Z), FVector(23.0, 0.16, 0.16));
		Box(Iron, FVector(0.0, 5.08, Z), FVector(23.0, 0.16, 0.16));
		for (const double X : { -9.0, -6.0, -3.0, 0.0, 3.0, 6.0, 9.0 })
		{
			Box(Iron, FVector(X, 5.29, Z), FVector(0.06, 0.5, 0.06), 0.0, false);
		}
	}
	for (const double Z : { -13.2, -7.2, -1.2, 4.8 })
	{
		Box(Iron, FVector(0.0, 5.18, Z), FVector(23.0, 0.24, 0.26));
	}
	for (const double X : { -8.875, -1.775, 1.775, 8.875 })
	{
		Box(Iron, FVector(X, 5.18, -4.0), FVector(0.26, 0.24, 23.4));
	}
	// Linear light troughs over each lane pair and the played lane.
	for (const double X : TroughCenters)
	{
		Box(Iron, FVector(X, 5.24, -5.6), FVector(0.62, 0.2, 16.2));
		Box(Lens, FVector(X, 5.13, -5.6), FVector(0.4, 0.04, 15.6), 0.0, false);
	}
	// Two warm pendants over the approach settee.
	UMaterialInterface* Tungsten = Materials.GlowOpaque(this, Hex(TEXT("#ffd9a8")) * 2.2f);
	for (const double X : { -3.8, 3.8 })
	{
		Box(Iron, FVector(X, 5.24, 2.6), FVector(0.02, 0.62, 0.02), 0.0, false);
		Post(Iron, FVector(X, 4.5, 2.6), 0.32, 0.22);
		Post(Tungsten, FVector(X, 4.44, 2.6), 0.2, 0.06);
	}
}

void ABowlingAlley::BuildNeon()
{
	UMaterialInterface* Pink = Neon(TEXT("NeonPink"), Hex(NeonPink) * 3.0f);
	UMaterialInterface* Cyan = Neon(TEXT("NeonCyan"), Hex(NeonCyan) * 3.0f);
	UMaterialInterface* Tube = Materials.Plain(this, Hex(TEXT("#0c0b12")), 0.4, 0.5);
	// Two tubes down each side wall at head height, the classic cosmic-bowling run, on dark standoffs.
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Pink, FVector(Side * 11.07, 3.05, -4.0), FVector(0.05, 0.05, 22.4), 0.0, false);
		Box(Cyan, FVector(Side * 11.07, 3.3, -4.0), FVector(0.05, 0.05, 22.4), 0.0, false);
		for (double Z = -14.5; Z <= 6.6; Z += 3.0)
		{
			Box(Tube, FVector(Side * 11.09, 3.18, Z), FVector(0.04, 0.4, 0.04), 0.0, false);
		}
	}
	// Above the masking units, across the whole back wall: a cyan run and a pink run framing the lightboxes.
	const double BackZ = -16.22;
	Box(Cyan, FVector(0.0, 3.32, BackZ), FVector(19.4, 0.05, 0.05), 0.0, false);
	Box(Pink, FVector(0.0, 4.9, BackZ), FVector(19.4, 0.05, 0.05), 0.0, false);
	BuildHouseSign(BackZ);
}

void ABowlingAlley::BuildHouseSign(double BackZ)
{
	// The house sign over the played lane: a neon ball (a ring with its finger holes) between two neon pins.
	UMaterialInterface* Pink = Neon(TEXT("NeonPink"), Hex(NeonPink) * 3.0f);
	UMaterialInterface* Cyan = Neon(TEXT("NeonCyan"), Hex(NeonCyan) * 3.0f);
	UMaterialInterface* Violet = Neon(TEXT("NeonViolet"), Hex(NeonViolet) * 2.2f);
	const FVector Logo(0.0, 4.12, BackZ + 0.02);
	constexpr int32 RingSegs = 28;
	const double RingR = 0.46;
	for (int32 i = 0; i < RingSegs; ++i)
	{
		const double A = 2.0 * UE_DOUBLE_PI * (i + 0.5) / RingSegs;
		// Each segment a little longer than its arc, so the ring reads as one tube.
		const double Len = 2.0 * UE_DOUBLE_PI * RingR / RingSegs * 1.08;
		WallBox(Cyan, Logo + FVector(FMath::Cos(A) * RingR, FMath::Sin(A) * RingR, 0.0), FVector(Len, 0.05, 0.04), A + UE_DOUBLE_PI / 2.0);
	}
	for (const FVector2D Hole : { FVector2D(-0.12, 0.14), FVector2D(0.12, 0.14), FVector2D(0.0, -0.08) })
	{
		WallBox(Pink, Logo + FVector(Hole.X, Hole.Y, 0.0), FVector(0.09, 0.09, 0.04), UE_DOUBLE_PI / 4.0);
	}
	for (const double Side : { -1.0, 1.0 })
	{
		// A pin's outline from its profile: segments up each flank, a base and the neck band.
		const FVector Base = Logo + FVector(Side * 1.25, -0.62, 0.0);
		const double H = 1.24, Belly = 0.2;
		constexpr int32 Steps = 14;
		for (const double Flank : { -1.0, 1.0 })
		{
			for (int32 i = 0; i < Steps; ++i)
			{
				const double T0 = static_cast<double>(i) / Steps, T1 = static_cast<double>(i + 1) / Steps;
				const FVector2D P0(Flank * PinRadiusAt(T0) * Belly, T0 * H), P1(Flank * PinRadiusAt(T1) * Belly, T1 * H);
				const FVector2D D = P1 - P0;
				WallBox(Pink, Base + FVector((P0.X + P1.X) / 2.0, (P0.Y + P1.Y) / 2.0, 0.0), FVector(D.Size() * 1.12, 0.045, 0.04), FMath::Atan2(D.Y, D.X));
			}
		}
		WallBox(Pink, Base, FVector(Belly * 0.9, 0.045, 0.04), 0.0);
		WallBox(Violet, Base + FVector(0.0, H * 0.69, 0.0), FVector(Belly * 0.75, 0.05, 0.04), 0.0);
	}
}

// ---- Light ------------------------------------------------------------------------------------------------------

void ABowlingAlley::BuildLights()
{
	// The deck key: cool, tight and by far the brightest light, so the pins are the brightest thing in frame.
	USpotLightComponent* Key = NewPart<USpotLightComponent>(TEXT("DeckKey"));
	const FVector From = ToWorld(FVector(0.0, 3.35, DeckZ + 0.75));
	const FVector To = ToWorld(FVector(0.0, 0.0, DeckZ - 0.85));
	Key->SetRelativeLocation(From);
	Key->SetRelativeRotation((To - From).Rotation());
	Key->SetIntensityUnits(ELightUnits::Lumens);
	Key->SetIntensity(DeckKeyLumens);
	Key->SetAttenuationRadius(700.f);
	Key->SetOuterConeAngle(FMath::RadiansToDegrees(0.78f));
	Key->SetInnerConeAngle(FMath::RadiansToDegrees(0.78f * 0.4f));
	Key->SetLightColor(Hex(TEXT("#dceaff")));
	Key->SetSourceRadius(30.f);
	Key->SetCastShadows(true);
	RegisterPart(Key);
	DeckKey = Key;
	BuildFillLights();
	BuildTroughLights();
	BuildShowSpots();
}

void ABowlingAlley::BuildFillLights()
{
	auto Point = [this](const FVector& Web, float Lumens, float RadiusM, const FLinearColor& Color)
	{
		UPointLightComponent* Light = NewPart<UPointLightComponent>(TEXT("AlleyPoint"));
		Light->SetRelativeLocation(ToWorld(Web));
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Lumens);
		Light->SetAttenuationRadius(RadiusM * 100.f);
		Light->SetLightColor(Color);
		Light->SetSourceRadius(15.f);
		// Fill lights never shadow: the deck key and the bowler's own contact shadow carry the shape.
		Light->SetCastShadows(false);
		RegisterPart(Light);
	};
	// The deck's front fill and its glow from above.
	Point(FVector(0.0, 1.55, DeckZ - 1.15), 1400.f, 5.f, Hex(TEXT("#fff0d4")));
	Point(FVector(0.0, 4.6, DeckZ - 0.25), 1700.f, 12.f, Hex(TEXT("#dcd2c0")));
	// The approach: warm, so the bowler reads against the cool lane.
	Point(FVector(0.0, 4.3, 1.4), 2400.f, 11.f, Hex(TEXT("#ffc78d")));
	Point(FVector(0.0, 4.3, -3.4), 1300.f, 10.f, Hex(TEXT("#ffbf82")));
	Point(FVector(8.4, 2.3, 7.4), 1400.f, 9.f, Hex(TEXT("#e0954a")));
	// The neon's spill on the side walls and the outer lanes.
	Point(FVector(-10.4, 3.2, -9.0), 900.f, 7.f, Hex(NeonPink));
	Point(FVector(-10.4, 3.2, -1.0), 700.f, 7.f, Hex(NeonCyan));
	Point(FVector(10.4, 3.2, -9.0), 900.f, 7.f, Hex(NeonCyan));
	Point(FVector(10.4, 3.2, -1.0), 700.f, 7.f, Hex(NeonPink));
	Point(FVector(0.0, 4.2, -15.6), 700.f, 5.f, Hex(NeonPink));
}

void ABowlingAlley::BuildTroughLights()
{
	// One soft rect light per trough: their long highlight runs down the lacquer.
	for (const double X : TroughCenters)
	{
		URectLightComponent* Rect = NewPart<URectLightComponent>(TEXT("Trough"));
		Rect->SetRelativeLocation(ToWorld(FVector(X, 5.08, -5.6)));
		Rect->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
		Rect->SetSourceWidth(40.f);
		Rect->SetSourceHeight(1560.f);
		Rect->SetIntensityUnits(ELightUnits::Lumens);
		Rect->SetIntensity(X == 0.0 ? PlayedTroughLumens : HouseTroughLumens);
		Rect->SetAttenuationRadius(1000.f);
		Rect->SetBarnDoorAngle(60.f);
		Rect->SetLightColor(Hex(TEXT("#f3ecdc")));
		// A 15.6 m shadowed rect is the most expensive light in the room for a soft shadow the key already gives.
		Rect->SetCastShadows(false);
		RegisterPart(Rect);
	}
}

void ABowlingAlley::BuildShowSpots()
{
	// Four coloured spots in the truss, aimed across the deck, dark until a strike.
	const struct { FVector At; const TCHAR* Color; } Shows[] = {
		{ FVector(-2.8, 5.0, -8.6), NeonPink }, { FVector(2.8, 5.0, -8.6), NeonCyan },
		{ FVector(-2.2, 5.0, -11.4), NeonCyan }, { FVector(2.2, 5.0, -11.4), NeonPink },
	};
	for (const auto& Show : Shows)
	{
		USpotLightComponent* Spot = NewPart<USpotLightComponent>(TEXT("ShowSpot"));
		Spot->SetRelativeLocation(ToWorld(Show.At));
		Spot->SetRelativeRotation((ToWorld(FVector(-Show.At.X * 0.4, 0.2, DeckZ - 0.6)) - ToWorld(Show.At)).Rotation());
		Spot->SetIntensityUnits(ELightUnits::Lumens);
		Spot->SetIntensity(0.f);
		Spot->SetAttenuationRadius(1200.f);
		Spot->SetOuterConeAngle(16.f);
		Spot->SetInnerConeAngle(6.f);
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
	UPostProcessComponent* Grade = NewObject<UPostProcessComponent>(this, MakeUniqueObjectName(this, UPostProcessComponent::StaticClass(), TEXT("AlleyGrade")));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
	Grade->Priority = 1.f;
	FPostProcessSettings& S = Grade->Settings;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = ExposureMin;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = ExposureMax;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Bloom;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = 1.0f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = Vignette;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.08f, 1.08f, 1.1f, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.07f, 1.07f, 1.07f, 1.f);
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.25f;
	RegisterPart(Grade);
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
		DeckKey->SetIntensity(static_cast<float>(DeckKeyLumens * (1.0 + DeckKeyFlash * FMath::Min(Energy, 1.0) * FMath::Max(0.0, FMath::Sin(Time * Beat * 2.0)))));
	}
}

void ABowlingAlley::PulseNeon(double Energy, double Time, double Beat, bool bOn)
{
	// The neon breathes with the show: up to double brightness, chasing at the beat (each tube a step behind).
	for (int32 i = 0; i < NeonMids.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mid = NeonMids[i])
		{
			const double Pulse = bOn ? 0.5 + 0.5 * FMath::Sin(Time * Beat * UE_DOUBLE_PI + i * 1.3) : 0.0;
			const float Gain = static_cast<float>(1.0 + FMath::Min(Energy, MaxShowEnergy) * (0.4 + 0.8 * Pulse));
			Mid->SetVectorParameterValue(ColorParam, NeonColors[i] * Gain);
		}
	}
}

void ABowlingAlley::SweepShowSpots(double Energy, const FLinearColor& Tint, double Time, double Beat, bool bOn)
{
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
		Spot->SetIntensity(static_cast<float>(ShowSpotLumens * FMath::Min(Energy, MaxShowEnergy) * (0.35 + 0.65 * Flash)));
		const FVector Base = ToWorld(FVector(-1.4 + 2.8 * (i % 2), 0.2, DeckZ - 0.6));
		const FVector Sweep = ToWorld(FVector(FMath::Sin(Time * 2.2 + i) * 1.2, 0.0, FMath::Cos(Time * 1.7 + i) * 0.8)) - ToWorld(FVector::ZeroVector);
		Spot->SetWorldRotation((Base + Sweep - Spot->GetComponentLocation()).Rotation());
		if (Energy > 1.0)
		{
			Spot->SetLightColor(FLinearColor::LerpUsingHSV(Spot->GetLightColor(), Tint, 0.1f));
		}
	}
}
