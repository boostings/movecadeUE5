#include "Games/Bowling/BowlingAlley.h"

#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Games/Bowling/BowlingSim.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* const AlleyTag = TEXT("BowlingAlley");

	/** venueLayout.ts: lane centres house-left to house-right; the player bowls the middle one. */
	constexpr double LaneCenters[] = { -7.1, -3.55, 0.0, 3.55, 7.1 };
	constexpr double DividerCenters[] = { -8.875, -5.325, -1.775, 1.775, 5.325, 8.875 };
	constexpr double LaneBackZ = ABowlingAlley::DeckZ - ABowlingAlley::PitDepth;
	constexpr double LaneSurfaceLength = ABowlingAlley::LaneFrontZ - LaneBackZ;
	constexpr double LaneSurfaceCenterZ = (ABowlingAlley::LaneFrontZ + LaneBackZ) / 2.0;

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
}

ABowlingAlley::ABowlingAlley()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
}

FVector ABowlingAlley::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

FVector ABowlingAlley::PinSpot(int32 Id, double LaneX)
{
	const FVector2D P = MovecadeKart::Bowling::PinPosition(Id);
	return FVector(LaneX + P.X * VisualPinScale, DeckY, DeckZ - P.Y * VisualPinScale);
}

void ABowlingAlley::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorTransform(FTransform::Identity);
	Build();
}

void ABowlingAlley::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), AlleyTag);
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	HouseRacks.Reset();
}

UInstancedStaticMeshComponent* ABowlingAlley::Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d"), *GetNameSafe(Mesh), *GetNameSafe(Material), bShadows ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Groups.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("AlleyGroup")));
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, Material);
	C->SetupAttachment(RootComponent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(AlleyTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

void ABowlingAlley::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad)
{
	// The engine cube is 1 m: web (x, y, z) sizes become UE (z, x, y) scales.
	Group(Cube, Material)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), true);
}

void ABowlingAlley::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height)
{
	// The engine cylinder is 1 m tall and 1 m across, centred.
	Group(Cylinder, Material)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, 0.0)), FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

void ABowlingAlley::BuildMeshes()
{
	using MovecadeKart::FMeshBuilder;
	// The pin: the USBC silhouette lathed in three ivory runs and two red neck bands.
	{
		FMeshBuilder B;
		const double H = PinHeight * 100.0, R = PinBelly * 100.0;
		const double Bands[][3] = { { 0.0, 0.658, 0 }, { 0.658, 0.686, 1 }, { 0.686, 0.704, 0 }, { 0.704, 0.732, 1 }, { 0.732, 1.0, 0 } };
		for (const auto& Band : Bands)
		{
			TArray<FVector2D> Profile;
			const int32 Steps = FMath::Max(2, FMath::CeilToInt32((Band[1] - Band[0]) * 60.0));
			for (int32 i = 0; i <= Steps; ++i)
			{
				const double T = FMath::Lerp(Band[0], Band[1], static_cast<double>(i) / Steps);
				// Bands stand a hair proud of the body so they never z-fight.
				// Centred on the pin's middle: the solver poses pin centres.
				Profile.Add(FVector2D(PinRadiusAt(T) * R * (Band[2] > 0.5 ? 1.006 : 1.0), (T - 0.5) * H));
			}
			B.Lathe(Band[2] > 0.5 ? FName(TEXT("Band")) : FName(TEXT("Ivory")), Profile, 40, FVector::ZeroVector, FVector::UpVector, FVector::OneVector, FVector2D::ZeroVector,
				Band[0], Band[1]);
		}
		PinMesh = B.Build(this, TEXT("Pin"));
	}
	// The ball: a sphere with a three-hole grip.
	{
		FMeshBuilder B;
		const double R = BallRadius * 100.0;
		B.Ellipsoid(TEXT("Shell"), FVector::ZeroVector, FVector(R), 18, 28, FVector2D::ZeroVector);
		const FVector Holes[] = { FVector(0.3, -0.18, 0.93), FVector(0.3, 0.18, 0.93), FVector(-0.32, 0.0, 0.94) };
		for (const FVector& H : Holes)
		{
			const FVector N = H.GetSafeNormal();
			B.Ellipsoid(TEXT("Holes"), N * R * 0.975, FVector(R * 0.13, R * 0.13, R * 0.04), 4, 12, FVector2D::ZeroVector);
		}
		BallMesh = B.Build(this, TEXT("Ball"));
	}
	// A gutter: a half pipe, open to the sky.
	{
		FMeshBuilder B;
		const int32 S = B.Section(TEXT("Gutter"));
		const double R = 20.0, L = LaneSurfaceLength * 100.0;
		constexpr int32 Segs = 16;
		for (int32 i = 0; i < Segs; ++i)
		{
			const double A0 = UE_PI * i / Segs, A1 = UE_PI * (i + 1) / Segs;
			auto P = [&](double A, double X) { return FVector(X, -FMath::Cos(A) * R, -FMath::Sin(A) * R); };
			auto N = [&](double A) { return FVector(0.0, FMath::Cos(A), FMath::Sin(A)); };
			const int32 a = B.Vert(S, P(A0, -L / 2), N(A0), FVector2D(0, 0), FVector2D::ZeroVector);
			const int32 b = B.Vert(S, P(A1, -L / 2), N(A1), FVector2D(1, 0), FVector2D::ZeroVector);
			const int32 c = B.Vert(S, P(A1, L / 2), N(A1), FVector2D(1, 1), FVector2D::ZeroVector);
			const int32 d = B.Vert(S, P(A0, L / 2), N(A0), FVector2D(0, 1), FVector2D::ZeroVector);
			B.Tri(S, a, b, c);
			B.Tri(S, a, c, d);
		}
		GutterMesh = B.Build(this, TEXT("Gutter"));
	}
	using namespace MovecadeKart::Art;
	PinMaterials = {
		Surface(this, { Hex(TEXT("#f4efe6")), TEXT("plastered_wall_02"), 0.4, 0.08, 0.2, 0.2, 0.18 }),
		Plain(this, Hex(TEXT("#c41632")), 0.22),
	};
}

void ABowlingAlley::Build()
{
	Clear();
	if (!PinMesh)
	{
		BuildMeshes();
	}
	BuildShell();
	BuildLanes();
	BuildDeck();
	BuildCeiling();
	BuildLights();
	BuildHouseRacks();
}

void ABowlingAlley::BuildShell()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Carpet = Surface(this, { Hex(TEXT("#2b2140")), TEXT("leafy_grass"), 0.6, 0.35, 0.5, 0.4, 0.95, 0.0, 0.0, false, 1.0 });
	UMaterialInterface* Wall = Surface(this, { Hex(TEXT("#2a2436")), TEXT("plastered_wall_02"), 2.4, 0.35, 0.5, 0.4, 0.9 });
	UMaterialInterface* Wallpaper = Surface(this, { Hex(TEXT("#3b2f55")), TEXT("plastered_wall_02"), 1.6, 0.45, 0.6, 0.4, 0.88 });
	UMaterialInterface* Wainscot = Surface(this, { Hex(TEXT("#5a3a26")), TEXT("dark_wooden_planks"), 1.2, 0.5, 0.7, 0.4, 0.6 });
	UMaterialInterface* Ceiling = Plain(this, Hex(TEXT("#16141c")), 0.9);
	UMaterialInterface* Accent = Glow(this, Hex(TEXT("#6f68c4")) * 2.2f);

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
		Box(Accent, FVector(Side * 11.08, 1.52, -4.0), FVector(0.03, 0.035, 22.6));
	}
	// The near wall behind the concourse closes the room for the reverse shots.
	Box(Wall, FVector(0.0, 4.2, 8.4), FVector(24.0, 9.2, 0.55));
	Box(Ceiling, FVector(0.0, 5.85 + 0.16, -4.0), FVector(24.0, 0.32, 24.0));
}

void ABowlingAlley::BuildLanes()
{
	using namespace MovecadeKart::Art;
	// Lacquered maple: honey boards over a dark slab; low roughness so the lights streak down the lane.
	UMaterialInterface* Maple = Surface(this, { Hex(TEXT("#8f6b4a")), TEXT("dark_wooden_planks"), 0.9, 0.55, 0.3, 0.35, 0.2, 0.0, 0.0, false, 0.35 });
	UMaterialInterface* Slab = Plain(this, Hex(TEXT("#6e4526")), 0.38);
	UMaterialInterface* Approach = Surface(this, { Hex(TEXT("#b9895a")), TEXT("dark_wooden_planks"), 1.4, 0.45, 0.35, 0.3, 0.3, 0.0, 0.0, false });
	UMaterialInterface* Dark = Plain(this, Hex(TEXT("#0f0f16")), 0.76);
	UMaterialInterface* Machine = Plain(this, Hex(TEXT("#22242a")), 0.45, 0.2);
	UMaterialInterface* Gutter = Plain(this, Hex(TEXT("#9ea3ad")), 0.32, 0.6);
	UMaterialInterface* Foul = Plain(this, Hex(TEXT("#d23b3b")), 0.4);
	UMaterialInterface* Return = Plain(this, Hex(TEXT("#f1e6d2")), 0.36);

	// One continuous approach behind all five lanes, and one foul line across it.
	Box(Approach, FVector(0.0, -0.13, 2.55), FVector(18.3, 0.22, 4.2));
	Box(Foul, FVector(0.0, 0.015, FoulZ), FVector(18.3, 0.025, 0.09));
	for (const double X : LaneCenters)
	{
		Box(Slab, FVector(X, -0.07, LaneSurfaceCenterZ), FVector(LaneVisualWidth, 0.14, LaneSurfaceLength));
		Box(Maple, FVector(X, 0.008, LaneSurfaceCenterZ), FVector(LaneVisualWidth - 0.06, 0.02, LaneSurfaceLength - 0.05));
		// Targeting arrows at 4.6 m, guide dots at 2 m past the foul line.
		for (const double Ax : { -0.46, -0.23, 0.0, 0.23, 0.46 })
		{
			Box(Dark, FVector(X + Ax, 0.02, -1.62), FVector(0.07, 0.006, 0.18), UE_PI / 4.0);
		}
		for (const double Dx : { -0.38, -0.19, 0.0, 0.19, 0.38 })
		{
			Box(Dark, FVector(X + Dx, 0.02, -4.7), FVector(0.05, 0.006, 0.05));
		}
	}
	// One gutter channel in every gap and along both outer edges, capping walls on the outside only.
	UInstancedStaticMeshComponent* Gutters = Group(GutterMesh, Gutter, false);
	for (int32 i = 0; i < UE_ARRAY_COUNT(DividerCenters); ++i)
	{
		const double X = DividerCenters[i];
		Gutters->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(FVector(X, -0.05, LaneSurfaceCenterZ)), FVector::OneVector), true);
		if (i == 0 || i == UE_ARRAY_COUNT(DividerCenters) - 1)
		{
			Box(Machine, FVector(X + (X < 0.0 ? -0.29 : 0.29), 0.02, LaneSurfaceCenterZ), FVector(0.18, 0.24, LaneSurfaceLength - 0.2));
		}
	}
	// Ball returns, one per lane pair, on the inner dividers at the approach.
	for (int32 i = 1; i + 1 < UE_ARRAY_COUNT(DividerCenters); ++i)
	{
		const double X = DividerCenters[i];
		Box(Return, FVector(X, 0.22, 1.75), FVector(0.62, 0.44, 1.5));
		Box(Machine, FVector(X, 0.45, 1.77), FVector(0.66, 0.04, 1.54));
		Box(Dark, FVector(X, 0.2, 2.47), FVector(0.38, 0.2, 0.08));
	}
}

void ABowlingAlley::BuildDeck()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Machine = Plain(this, Hex(TEXT("#22242a")), 0.45, 0.2);
	UMaterialInterface* Dark = Plain(this, Hex(TEXT("#0f0f16")), 0.76);
	UMaterialInterface* Pit = Plain(this, Hex(TEXT("#050507")), 0.95);
	UMaterialInterface* Cove = Glow(this, Hex(TEXT("#fff0d4")) * 2.6f);
	UMaterialInterface* Accent = Glow(this, Hex(TEXT("#7a6cff")) * 1.6f);
	UMaterialInterface* Panel = Glow(this, Hex(TEXT("#1a1630")) * 1.4f);
	UMaterialInterface* PanelArt = Glow(this, Hex(TEXT("#3b2a78")) * 1.2f);

	// The pinsetter run: one machine body behind all five decks.
	Box(Machine, FVector(0.0, 1.85, MachineZ), FVector(19.6, 3.8, 1.6));
	Box(Dark, FVector(0.0, 1.85 + 1.55, MachineZ + 0.55), FVector(19.0, 0.62, 1.9));
	for (const double X : DividerCenters)
	{
		Box(Machine, FVector(X, 1.7, MachineZ + 0.55), FVector(0.48, 3.5, 0.55));
	}
	// The masking units are lightboxes: each lane its own panel of house art (the Movecade billboards).
	TArray<UMaterialInterface*> Art;
	for (int32 i = 0; i < 4; ++i)
	{
		Art.Add(Unlit(this, *FString::Printf(TEXT("/Game/Tracks/Shared/Textures/mc_billboard_%d.mc_billboard_%d"), i, i), FLinearColor(1.15f, 1.15f, 1.15f)));
	}
	int32 Lane = 0;
	for (const double X : LaneCenters)
	{
		PanelArt = Art[(Lane++ + 2) % Art.Num()];
		Box(Dark, FVector(X, 1.4, MachineZ + 0.86), FVector(3.7, 2.35, 0.3));
		Box(Dark, FVector(X, 2.4, MachineZ + 0.92), FVector(3.5, 1.15, 0.12));
		// The pit: a black well behind the deck, so the pins stand against darkness.
		Box(Pit, FVector(X, -0.35, DeckZ - 1.6), FVector(LaneVisualWidth + 0.2, 0.6, 2.2));
		// Masking unit: a backlit panel framed in rails, the cove under it lighting the rack from the front.
		Box(Dark, FVector(X, 2.05, MaskingZ - 0.12), FVector(3.62, 1.98, 0.18));
		Box(Panel, FVector(X, 2.05, MaskingZ), FVector(3.34, 1.72, 0.02));
		Box(PanelArt, FVector(X, 2.05, MaskingZ + 0.012), FVector(3.2, 1.6, 0.01));
		Box(Machine, FVector(X, 2.05 + 0.93, MaskingZ + 0.06), FVector(3.46, 0.14, 0.16));
		Box(Machine, FVector(X, 2.05 - 0.93, MaskingZ + 0.06), FVector(3.46, 0.14, 0.16));
		for (const double Side : { -1.0, 1.0 })
		{
			Box(Dark, FVector(X + Side * 1.73, 2.05, MaskingZ + 0.06), FVector(0.1, 1.98, 0.12));
		}
		Box(Pit, FVector(X, 2.05 - 1.62, MaskingZ - 0.18), FVector(3.3, 0.9, 0.02));
		Box(Cove, FVector(X, 2.05 - 1.03, MaskingZ + 0.11), FVector(3.3, 0.06, 0.06));
		Box(Accent, FVector(X, 2.05 + 0.99, MaskingZ + 0.11), FVector(3.3, 0.022, 0.05));
	}
}

void ABowlingAlley::BuildCeiling()
{
	using namespace MovecadeKart::Art;
	UMaterialInterface* Iron = Plain(this, Hex(TEXT("#1b1a22")), 0.55, 0.6);
	UMaterialInterface* Lens = Glow(this, Hex(TEXT("#f3ecdc")) * 2.4f);
	for (const double Z : { -13.2, -10.2, -7.2, -4.2, -1.2, 1.8, 4.8 })
	{
		Box(Iron, FVector(0.0, 5.5, Z), FVector(23.0, 0.16, 0.16));
		Box(Iron, FVector(0.0, 5.08, Z), FVector(23.0, 0.16, 0.16));
		for (const double X : { -9.0, -6.0, -3.0, 0.0, 3.0, 6.0, 9.0 })
		{
			Box(Iron, FVector(X, 5.29, Z), FVector(0.06, 0.5, 0.06), 0.0);
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
	for (const double X : { -5.325, 0.0, 5.325 })
	{
		Box(Iron, FVector(X, 5.24, -5.6), FVector(0.62, 0.2, 16.2));
		Box(Lens, FVector(X, 5.13, -5.6), FVector(0.4, 0.04, 15.6));
	}
	// Two warm pendants over the approach settee.
	UMaterialInterface* Tungsten = Glow(this, Hex(TEXT("#ffd9a8")) * 2.2f);
	for (const double X : { -3.8, 3.8 })
	{
		Box(Iron, FVector(X, 5.24, 2.6), FVector(0.02, 0.62, 0.02));
		Post(Iron, FVector(X, 4.5, 2.6), 0.32, 0.22);
		Post(Tungsten, FVector(X, 4.44, 2.6), 0.2, 0.06);
	}
}

void ABowlingAlley::BuildLights()
{
	auto Point = [this](const FVector& Web, float Lumens, float RadiusM, const TCHAR* Color, bool bShadows = false)
	{
		UPointLightComponent* L = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("AlleyPoint")));
		L->SetupAttachment(RootComponent);
		L->SetMobility(EComponentMobility::Movable);
		L->SetRelativeLocation(ToWorld(Web));
		L->SetIntensityUnits(ELightUnits::Lumens);
		L->SetIntensity(Lumens);
		L->SetAttenuationRadius(RadiusM * 100.f);
		L->SetLightColor(MovecadeKart::Art::Hex(Color));
		L->SetSourceRadius(15.f);
		L->SetCastShadows(bShadows);
		L->ComponentTags.Add(AlleyTag);
		AddInstanceComponent(L);
		L->RegisterComponent();
		return L;
	};
	// The deck key: cool, tight and by far the brightest light, so the pins are the brightest thing in frame.
	USpotLightComponent* Key = NewObject<USpotLightComponent>(this, TEXT("DeckKey"));
	Key->SetupAttachment(RootComponent);
	Key->SetMobility(EComponentMobility::Movable);
	const FVector From = ToWorld(FVector(0.0, 3.35, DeckZ + 0.75));
	const FVector To = ToWorld(FVector(0.0, 0.0, DeckZ - 0.85));
	Key->SetRelativeLocation(From);
	Key->SetRelativeRotation((To - From).Rotation());
	Key->SetIntensityUnits(ELightUnits::Lumens);
	Key->SetIntensity(5200.f);
	Key->SetAttenuationRadius(1100.f);
	Key->SetOuterConeAngle(FMath::RadiansToDegrees(0.78f));
	Key->SetInnerConeAngle(FMath::RadiansToDegrees(0.78f * 0.4f));
	Key->SetLightColor(MovecadeKart::Art::Hex(TEXT("#dceaff")));
	Key->SetSourceRadius(30.f);
	Key->SetCastShadows(true);
	Key->ComponentTags.Add(AlleyTag);
	AddInstanceComponent(Key);
	Key->RegisterComponent();

	Point(FVector(0.0, 1.55, DeckZ - 1.15), 1400.f, 7.f, TEXT("#fff0d4"));
	Point(FVector(0.0, 4.6, DeckZ - 0.25), 2000.f, 26.f, TEXT("#dcd2c0"));
	Point(FVector(0.0, 4.3, 1.4), 2400.f, 17.f, TEXT("#ffc78d"), true);
	Point(FVector(0.0, 4.3, -3.4), 1600.f, 15.f, TEXT("#ffbf82"));
	Point(FVector(8.4, 2.3, 7.4), 1600.f, 13.f, TEXT("#e0954a"));
	// The troughs spill down the lanes: one soft rect light per trough.
	for (const double X : { -5.325, 0.0, 5.325 })
	{
		URectLightComponent* Rect = NewObject<URectLightComponent>(this, MakeUniqueObjectName(this, URectLightComponent::StaticClass(), TEXT("Trough")));
		Rect->SetupAttachment(RootComponent);
		Rect->SetMobility(EComponentMobility::Movable);
		Rect->SetRelativeLocation(ToWorld(FVector(X, 5.08, -5.6)));
		Rect->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
		Rect->SetSourceWidth(40.f);
		Rect->SetSourceHeight(1560.f);
		Rect->SetIntensityUnits(ELightUnits::Lumens);
		Rect->SetIntensity(X == 0.0 ? 1900.f : 1400.f);
		Rect->SetAttenuationRadius(1400.f);
		Rect->SetBarnDoorAngle(60.f);
		Rect->SetLightColor(MovecadeKart::Art::Hex(TEXT("#f3ecdc")));
		Rect->SetCastShadows(X == 0.0);
		Rect->ComponentTags.Add(AlleyTag);
		AddInstanceComponent(Rect);
		Rect->RegisterComponent();
	}
}

void ABowlingAlley::BuildHouseRacks()
{
	// The four house lanes' racks; the played lane's pins are actors of the game (they move).
	for (int32 Lane = 0; Lane < UE_ARRAY_COUNT(LaneCenters); ++Lane)
	{
		UInstancedStaticMeshComponent* Rack = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("HouseRack")));
		Rack->SetStaticMesh(PinMesh);
		for (int32 Slot = 0; Slot < PinMaterials.Num(); ++Slot)
		{
			Rack->SetMaterial(Slot, PinMaterials[Slot]);
		}
		Rack->SetupAttachment(RootComponent);
		Rack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rack->SetMobility(EComponentMobility::Movable);
		Rack->ComponentTags.Add(AlleyTag);
		AddInstanceComponent(Rack);
		Rack->RegisterComponent();
		if (LaneCenters[Lane] != 0.0)
		{
			for (int32 Id = 1; Id <= 10; ++Id)
			{
				Rack->AddInstance(FTransform(ToWorld(PinSpot(Id, LaneCenters[Lane]) + FVector(0.0, PinHeight / 2.0, 0.0))), true);
			}
		}
		HouseRacks.Add(Rack);
	}
}
