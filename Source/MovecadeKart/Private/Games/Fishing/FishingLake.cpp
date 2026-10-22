#include "Games/Fishing/FishingLake.h"

#include "Art/KartMaterials.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Games/Fishing/FishingArt.h"
#include "Games/Fishing/FishingRules.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogFishingLake, Log, All);

using namespace MovecadeKart::Art;
namespace FA = MovecadeKart::Fishing::Art;

namespace
{
	/** Every part the build makes carries this tag, so a rebuild removes exactly those. */
	const FName LakeTag = TEXT("FishingLake");
	const TCHAR* const LaneColors[] = { TEXT("#ff7a59"), TEXT("#2ec4b6"), TEXT("#ffc93c"), TEXT("#a083ff") };

	/** Fixed scenery seeds: the same lake every time. */
	constexpr int32 DockSeed = 0xd0c;
	constexpr int32 ShoreSeed = 0x51ae;
	constexpr int32 ShallowsSeed = 0x7eed;

	/** The dock runs back to here (cm), planked across at this pitch. */
	constexpr double DockBackX = -420.0;
	constexpr double PlankPitchCm = 24.0;
	/** The walkway back to the shore: planks, and its width (cm). */
	constexpr int32 WalkwayPlanks = 18;
	constexpr double WalkwayWidthCm = 200.0;
	/** Posts along each row at most this far apart (cm). */
	constexpr double PostSpacingCm = 240.0;
	/** The lanterns: brightness (candela-ish units), reach (cm), and the flicker's depth. */
	constexpr float LanternIntensity = 5200.f;
	constexpr float LanternRadius = 700.f;
	constexpr float LanternSteady = 0.92f;
	constexpr float LanternFlicker = 0.08f;
	/** The string lights between the lamp posts: bulbs per side and how far the wire swags (cm). */
	constexpr int32 StringBulbs = 11;
	constexpr int32 StringWirePieces = 25;
	constexpr double StringSagCm = 70.0;
	/** Trees: the far shore's tree line and the cove's sides. */
	constexpr int32 FarTrees = 150;
	constexpr int32 SideTrees = 220;
	/** Three ranks of hills beyond, nine across. */
	constexpr int32 HillRanks = 3;
	constexpr int32 HillsPerRank = 9;
	constexpr int32 Rocks = 70;
	constexpr int32 LilyPads = 70;
	/** Lily pads stay this far (cm) from every lane's line. */
	constexpr double LilyLaneClearCm = 110.0;
	constexpr double FlowerOdds = 0.18;
	/** The swell everything afloat rides: height (cm), roll and pitch (degrees). */
	constexpr double SwellCm = 2.2;
	constexpr double SwellPitchDeg = 1.6;
	constexpr double SwellRollDeg = 2.4;

	double SmoothStep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Where the cove's side banks begin (m from the centre line), widening further out (OutM from the dock's edge). */
	double CoveSideStartM(double OutM)
	{
		return 34.0 + FMath::Max(0.0, OutM) * 0.35;
	}

	/** The lake bed and its shores (cm): shelving from 1 m by the dock to 9 m out, rising to banks at the sides and far end. */
	double TerrainHeight(double X, double Y)
	{
		const double Out = (X - AFishingLake::EdgeX) / 100.0;
		const double Side = FMath::Abs(Y) / 100.0;
		const double Depth = 0.9 + 8.0 * (1.0 - FMath::Exp(-FMath::Max(0.0, Out) / 16.0));
		// The cove: the banks close in at the sides, wider further out; the far shore; the bank behind the dock.
		const double SideStart = CoveSideStartM(Out);
		const double Shore = FMath::Max(SmoothStep(SideStart, SideStart + 22.0, Side), SmoothStep(150.0, 185.0, Out));
		const double Hills = 1.6 * FMath::Sin(X * 0.0011 + 1.3) * FMath::Sin(Y * 0.0013) + 0.9 * FMath::Sin(X * 0.0031 + Y * 0.0023);
		const double Lake = (FMath::Lerp(-Depth, 1.4 + Hills * Shore, Shore) + 2.5 * FMath::Square(Shore) * SmoothStep(60.0, 120.0, Side + FMath::Max(0.0, Out - 140.0))) * 100.0;
		// The low bank the dock walks back onto (behind the camera): no hills there.
		return FMath::Lerp(Lake, 55.0, SmoothStep(-2.0, -9.0, Out));
	}

	/** The cove's side bank at X (cm from the centre line). */
	double CoveSideStartCm(double X)
	{
		return CoveSideStartM((X - AFishingLake::EdgeX) / 100.0) * 100.0;
	}

	/** A point SagCm under the chord A-B at U (a parabola: the swag of a wire or string). */
	FVector Swag(const FVector& A, const FVector& B, double U, double SagCm)
	{
		return FMath::Lerp(A, B, U) - FVector(0.0, 0.0, SagCm * 4.0 * U * (1.0 - U));
	}
}

AFishingLake::AFishingLake()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sphere = SphereMesh.Object;
	Plane = PlaneMesh.Object;
	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Fill = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Fill"));
	Fill->SetupAttachment(RootComponent);
	Fill->SetMobility(EComponentMobility::Movable);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(RootComponent);
	SkyDome->SetStaticMesh(SphereMesh.Object);
	SkyDome->SetRelativeScale3D(FVector(20000.0));
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCastShadow(false);
	SkyDome->SetMobility(EComponentMobility::Movable);
	Water = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Water"));
	Water->SetupAttachment(RootComponent);
	Water->SetStaticMesh(PlaneMesh.Object);
	Water->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Water->SetCastShadow(false);
	Water->SetMobility(EComponentMobility::Movable);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetMobility(EComponentMobility::Movable);
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
}

FVector AFishingLake::AnglerSpot(int32 Lane, int32 LaneCount)
{
	return FVector(EdgeX - 62.0, LaneY(Lane, LaneCount), DeckZ);
}

FVector AFishingLake::WaterPoint(int32 Lane, int32 LaneCount, double DistanceM, double Lateral)
{
	// The lanes fan out a little with distance so four lines never cross.
	const double Y = LaneY(Lane, LaneCount);
	return FVector(EdgeX + DistanceM * VisualCmPerM, Y * (1.0 + DistanceM * 0.03) + Lateral * (60.0 + DistanceM * 4.0), 0.0);
}

FVector AFishingLake::BucketSpot(int32 Lane, int32 LaneCount)
{
	return FVector(EdgeX - 120.0, LaneY(Lane, LaneCount) + 78.0, DeckZ);
}

FLinearColor AFishingLake::LaneColor(int32 Lane)
{
	return Hex(LaneColors[((Lane % 4) + 4) % 4]);
}

FVector AFishingLake::SunDirection()
{
	// Low over the far shore, a little right of centre: the glitter path runs up the lake toward the anglers.
	return FVector(1.0, 0.17, 0.12).GetSafeNormal();
}

void AFishingLake::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The helpers' coordinates are world coordinates: the lake always sits at the origin.
	SetActorTransform(FTransform::Identity);
	Build();
}

void AFishingLake::SetLanes(int32 LaneCount)
{
	LaneCount = FMath::Clamp(LaneCount, 1, 4);
	if (LaneCount != Lanes)
	{
		Lanes = LaneCount;
		Build();
	}
}

void AFishingLake::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), LakeTag);
	for (UActorComponent* C : Old)
	{
		RemoveInstanceComponent(C);
		C->DestroyComponent();
	}
	Groups.Reset();
	Materials.Reset();
	Floaters.Reset();
	FloaterRest.Reset();
	Lamps.Reset();
}

void AFishingLake::Box(UMaterialInterface* Material, const FVector& Centre, const FVector& Size, const FRotator& Rotation, bool bShadows)
{
	Instance(Cube, Material, FTransform(Rotation, Centre, Size / 100.0), bShadows);
}

void AFishingLake::Instance(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows)
{
	Groups.Find(Mesh, Material, bShadows)->AddInstance(Transform, true);
}

UStaticMeshComponent* AFishingLake::Part(const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
	C->SetStaticMesh(Mesh);
	if (Material)
	{
		C->SetMaterial(0, Material);
	}
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->SetWorldTransform(Transform);
	C->ComponentTags.Add(LakeTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

void AFishingLake::Float(UStaticMeshComponent* Floater)
{
	Floaters.Add(Floater);
	FloaterRest.Add(Floater->GetComponentTransform());
}

void AFishingLake::Build()
{
	Clear();
	// Movable: the lake is rebuilt at runtime for the lane count, and nothing here is lightmapped.
	Groups.Init(this, TEXT("LakeGroup"), LakeTag, EComponentMobility::Movable);
	BuildLighting();
	BuildSky();
	BuildAtmosphere();
	BuildWater();
	BuildDeck();
	BuildPosts();
	BuildLamps();
	BuildShores();
	BuildShallows();
	BuildProps();
}

void AFishingLake::BuildLighting()
{
	Sun->SetWorldRotation((-SunDirection()).Rotation());
	Sun->SetLightColor(Hex(TEXT("#ffb46a")));
	Sun->SetIntensity(3.4f);
	Sun->SetCastShadows(true);
	Sun->SetAtmosphereSunLight(false);
	Sun->SetForwardShadingPriority(1);
	// A cool fill from behind the camera so the anglers' backs aren't black against the sunset.
	Fill->SetWorldRotation(FVector(1.0, -0.25, -0.55).GetSafeNormal().Rotation());
	Fill->SetLightColor(Hex(TEXT("#a9b8ff")));
	Fill->SetIntensity(0.9f);
	Fill->SetCastShadows(false);
	Fill->SetAtmosphereSunLight(false);
	Fill->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(1.25f);
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(TEXT("#5a4a52")) * 0.6f);
}

void AFishingLake::BuildSky()
{
	// The sunset on the track sky master: navy overhead through rose to orange at the horizon, the sun's disc and
	// lit clouds. Without the master (a stripped build) the dome stays plain; the sky light still captures it.
	UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (SkyMaster)
	{
		const FVector ToSun = SunDirection();
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Hex(TEXT("#1c2a70")));
		Sky->SetVectorParameterValue(TEXT("SkyMid"), Hex(TEXT("#d65a78")));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Hex(TEXT("#ff8a3c")));
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#7a5048")));
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(ToSun.X, ToSun.Y, ToSun.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#fff0c8")) * 1.6f);
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.9f * 0.035f));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 0.5f);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 20.0f);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.35f);
		Sky->SetScalarParameterValue(TEXT("TopCurve"), 0.6f);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 0.55f);
		if (UTexture* Clouds = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
		}
		Sky->SetScalarParameterValue(TEXT("Clouds"), 0.5f);
		Sky->SetScalarParameterValue(TEXT("CloudScale"), 0.3f);
		Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#ffc58a")));
		Sky->SetVectorParameterValue(TEXT("CloudShade"), Hex(TEXT("#7d5878")));
		SkyDome->SetMaterial(0, Sky);
	}
	else
	{
		UE_LOG(LogFishingLake, Warning, TEXT("FishingLake: M_TrackSky is missing; the sky dome stays unlit"));
	}
	SkyLight->RecaptureSky();
}

void AFishingLake::BuildAtmosphere()
{
	// Warm haze far out (the hills fade into it), and a grade that keeps the sunset rich without crushing it.
	Fog->SetStartDistance(8000.f);
	Fog->SetFogDensity(0.012f);
	Fog->SetFogHeightFalloff(0.25f);
	Fog->SetFogMaxOpacity(0.75f);
	Fog->SetFogCutoffDistance(900000.f);
	Fog->SetFogInscatteringColor(Hex(TEXT("#f0a27a")) * 0.55f);
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffbf7a")) * 0.2f);
	Fog->SetDirectionalInscatteringExponent(5.f);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.45f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.85f;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = -0.15f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.08f, 1.04f, 1.0f, 1.0f);
}

void AFishingLake::BuildWater()
{
	// Single layer water (the harbour's master): a calm lake with longer, gentler swells than the sea.
	Water->SetWorldLocation(FVector(14000.0, 0.0, 0.0));
	Water->SetWorldScale3D(FVector(480.0, 700.0, 1.0));
	if (UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackWater.M_TrackWater"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Master, this);
		M->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("#0a1412")));
		M->SetScalarParameterValue(TEXT("Roughness"), 0.03f);
		if (UTexture* Waves = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_waves_normal.env_waves_normal"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			M->SetTextureParameterValue(TEXT("WaveNormal"), Waves);
		}
		M->SetScalarParameterValue(TEXT("WaveStrength"), 0.2f);
		M->SetScalarParameterValue(TEXT("WaveTileA"), 9.0f);
		M->SetScalarParameterValue(TEXT("WaveTileB"), 23.0f);
		M->SetScalarParameterValue(TEXT("WaveTileC"), 71.0f);
		M->SetVectorParameterValue(TEXT("Scattering"), FLinearColor(0.05f, 0.24f, 0.2f));
		M->SetVectorParameterValue(TEXT("Absorption"), FLinearColor(0.36f, 0.08f, 0.06f));
		M->SetScalarParameterValue(TEXT("PhaseG"), 0.3f);
		Water->SetMaterial(0, M);
	}
	else
	{
		UE_LOG(LogFishingLake, Warning, TEXT("FishingLake: M_TrackWater is missing; the lake has no water surface material"));
	}
	// The lake bed and the banks around the cove (the water's colour comes from the depth over it).
	UStaticMesh* Ground = FA::BuildTerrain(this, FVector2D(-2500.0, -16000.0), FVector2D(24000.0, 16000.0), 110, 96, &TerrainHeight);
	UMaterialInterface* Bed = Materials.Surface(this, { Hex(TEXT("#9a8a62")), TEXT("gravelly_sand"), 2.5, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.0 });
	UMaterialInterface* Bank = Materials.Surface(this, { Hex(TEXT("#6f8a45")), TEXT("leafy_grass"), 3.0, 0.45, 0.7, 0.35, 0.9, 0.0, 0.0, false, 0.1 });
	UStaticMeshComponent* G = Part(TEXT("LakeBed"), Ground, Bed, FTransform::Identity, true);
	G->SetMaterialByName(TEXT("Bank"), Bank);
}

void AFishingLake::BuildDeck()
{
	const double HalfW = DeckHalfWidth();
	UMaterialInterface* Plank = Materials.Surface(this, { Hex(TEXT("#b08560")), TEXT("dark_wooden_planks"), 1.2, 0.4, 0.8, 0.4, 0.8, 0.0, 0.0, true, 0.0 });
	UMaterialInterface* Beam = Materials.Surface(this, { Hex(TEXT("#6e5038")), TEXT("dark_wooden_planks"), 1.0, 0.35, 0.8, 0.4, 0.85, 0.0, 0.0, true, 0.0 });
	// Planks across the dock (along Y) with small gaps, a little uneven, on two stringers.
	const int32 Planks = FMath::CeilToInt32((EdgeX - DockBackX) / PlankPitchCm);
	FRandomStream R(DockSeed);
	for (int32 i = 0; i < Planks; ++i)
	{
		const double X = DockBackX + 12.0 + i * PlankPitchCm;
		const double Lift = R.FRandRange(-0.6f, 0.6f);
		Box(Plank, FVector(X, R.FRandRange(-6.f, 6.f), DeckZ - 2.5 + Lift), FVector(23.2, HalfW * 2.0 + R.FRandRange(-10.f, 10.f), 5.0), FRotator(0.0, R.FRandRange(-0.6f, 0.6f), 0.0));
	}
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Beam, FVector((EdgeX + DockBackX) / 2.0, Side * (HalfW - 20.0), DeckZ - 14.0), FVector(EdgeX - DockBackX, 14.0, 18.0));
		Box(Beam, FVector((EdgeX + DockBackX) / 2.0, Side * HalfW * 0.3, DeckZ - 14.0), FVector(EdgeX - DockBackX, 12.0, 16.0), FRotator::ZeroRotator, false);
	}
	// The fishing edge: a kerb board along the front.
	Box(Beam, FVector(EdgeX + 6.0, 0.0, DeckZ - 8.0), FVector(12.0, HalfW * 2.0 + 20.0, 22.0));
	Box(Beam, FVector(EdgeX + 2.0, 0.0, DeckZ + 4.0), FVector(16.0, HalfW * 2.0 + 20.0, 8.0));
	// The walkway back to the shore behind the camera.
	for (int32 i = 0; i < WalkwayPlanks; ++i)
	{
		const double X = DockBackX - 12.0 - i * PlankPitchCm;
		Box(Plank, FVector(X, 0.0, DeckZ - 2.5 + R.FRandRange(-0.6f, 0.6f)), FVector(22.0, WalkwayWidthCm, 5.0), FRotator(0.0, R.FRandRange(-0.8f, 0.8f), 0.0));
	}
}

void AFishingLake::BuildPosts()
{
	const double HalfW = DeckHalfWidth();
	UMaterialInterface* Beam = Materials.Surface(this, { Hex(TEXT("#6e5038")), TEXT("dark_wooden_planks"), 1.0, 0.35, 0.8, 0.4, 0.85, 0.0, 0.0, true, 0.0 });
	UMaterialInterface* Wet = Materials.Plain(this, Hex(TEXT("#3b3127")), 0.5);
	UMaterialInterface* Rope = Materials.Plain(this, Hex(TEXT("#c9b48a")), 0.9);
	// Posts into the water in three rows, dark where they are wet, the front row taller with a rope wrap.
	const int32 PostsY = FMath::Max(3, FMath::CeilToInt32(HalfW * 2.0 / PostSpacingCm) + 1);
	for (int32 i = 0; i < PostsY; ++i)
	{
		const double Y = -HalfW + 10.0 + i * (HalfW * 2.0 - 20.0) / (PostsY - 1);
		for (const double X : { EdgeX + 10.0, (EdgeX + DockBackX) / 2.0, DockBackX + 20.0 })
		{
			const bool bFront = X > EdgeX;
			const double Top = bFront ? DeckZ + 34.0 : DeckZ - 4.0;
			Instance(Cylinder, Beam, FTransform(FRotator::ZeroRotator, FVector(X, Y, (Top - 260.0) / 2.0), FVector(0.26, 0.26, (Top + 260.0) / 100.0)));
			Instance(Cylinder, Wet, FTransform(FRotator::ZeroRotator, FVector(X, Y, -6.0), FVector(0.275, 0.275, 0.3)), false);
			if (bFront)
			{
				Instance(Cylinder, Rope, FTransform(FRotator::ZeroRotator, FVector(X, Y, DeckZ + 18.0), FVector(0.29, 0.29, 0.1)));
			}
		}
	}
}

UPointLightComponent* AFishingLake::Lantern(const FVector& At)
{
	UPointLightComponent* L = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("Lantern")));
	L->SetupAttachment(RootComponent);
	L->SetMobility(EComponentMobility::Movable);
	L->SetWorldLocation(At);
	L->SetLightColor(Hex(TEXT("#ffb35c")));
	L->SetIntensity(LanternIntensity);
	L->SetAttenuationRadius(LanternRadius);
	L->SetCastShadows(false);
	L->ComponentTags.Add(LakeTag);
	AddInstanceComponent(L);
	L->RegisterComponent();
	return L;
}

void AFishingLake::BuildLamps()
{
	const double HalfW = DeckHalfWidth();
	UMaterialInterface* Beam = Materials.Surface(this, { Hex(TEXT("#6e5038")), TEXT("dark_wooden_planks"), 1.0, 0.35, 0.8, 0.4, 0.85, 0.0, 0.0, true, 0.0 });
	UMaterialInterface* Iron = Materials.Plain(this, Hex(TEXT("#2a2a2e")), 0.4, 0.8);
	// Solid glowing glass and bulbs: opaque unlit, so they bloom without a translucency pass.
	UMaterialInterface* Bulb = Materials.GlowOpaque(this, Hex(TEXT("#ffd89a")) * 7.0f);
	UMaterialInterface* LampGlass = Materials.GlowOpaque(this, Hex(TEXT("#ffc06a")) * 5.0f);
	// Lamp posts at the four corners, each with a hanging lantern and its light.
	TArray<FVector> Tops;
	for (const double Side : { -1.0, 1.0 })
	{
		for (const double X : { EdgeX - 6.0, DockBackX + 20.0 })
		{
			const FVector Base(X, Side * (HalfW - 6.0), DeckZ);
			Instance(Cylinder, Beam, FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, 130.0), FVector(0.14, 0.14, 2.6)));
			Box(Iron, Base + FVector(0.0, -Side * 14.0, 258.0), FVector(4.0, 34.0, 4.0));
			const FVector At = Base + FVector(0.0, -Side * 28.0, 236.0);
			Box(Iron, At + FVector(0.0, 0.0, 14.0), FVector(16.0, 16.0, 3.0));
			Box(LampGlass, At, FVector(12.0, 12.0, 22.0), FRotator::ZeroRotator, false);
			Tops.Add(Base + FVector(0.0, 0.0, 262.0));
			Lamps.Add(Lantern(At));
		}
	}
	// String lights swagged between the posts along each side: the wire in short pieces, then the bulbs.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector A = Tops[Side * 2], B = Tops[Side * 2 + 1];
		for (int32 k = 0; k < StringWirePieces; ++k)
		{
			const FVector P0 = Swag(A, B, k / static_cast<double>(StringWirePieces), StringSagCm);
			const FVector P1 = Swag(A, B, (k + 1) / static_cast<double>(StringWirePieces), StringSagCm);
			Box(Iron, (P0 + P1) / 2.0, FVector((P1 - P0).Size(), 0.8, 0.8), (P1 - P0).Rotation(), false);
		}
		for (int32 k = 1; k < StringBulbs; ++k)
		{
			const double U = static_cast<double>(k) / StringBulbs;
			// Each bulb hangs just under the wire.
			const FVector P = FMath::Lerp(A, B, U) - FVector(0.0, 0.0, StringSagCm * 4.0 * U * (1.0 - U) + 7.0);
			Instance(Sphere, Bulb, FTransform(FRotator::ZeroRotator, P, FVector(0.07, 0.07, 0.09)), false);
		}
	}
}

void AFishingLake::BuildShores()
{
	// One stream for the whole shore, in this order: the trees, the hills, then the rocks.
	FRandomStream R(ShoreSeed);
	BuildTrees(R);
	BuildHills(R);
	BuildRocks(R);
}

void AFishingLake::BuildTrees(FRandomStream& R)
{
	UStaticMesh* Pine = FA::BuildPine(this);
	const TCHAR* const PineColors[] = { TEXT("#2f4a32"), TEXT("#3a5536"), TEXT("#28402e") };
	TArray<UMaterialInterface*, TInlineAllocator<3>> Needles;
	for (const TCHAR* C : PineColors)
	{
		Needles.Add(Materials.Plain(this, Hex(C), 0.85));
	}
	UMaterialInterface* Trunk = Materials.Plain(this, Hex(TEXT("#4a3626")), 0.9);
	auto Tree = [&](const FVector2D& At, double Scale)
	{
		// Only on dry land.
		const double Z = TerrainHeight(At.X, At.Y);
		if (Z < 20.0)
		{
			return;
		}
		const FTransform T(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector(At.X, At.Y, Z - 30.0), FVector(Scale));
		// The pine's slots: Trunk, then Needles.
		Groups.Find(Pine, Trunk, true, nullptr, Needles[R.RandHelper(Needles.Num())])->AddInstance(T, true);
	};
	// The far shore's tree line, thick, and the cove's sides thinning toward the dock.
	for (int32 i = 0; i < FarTrees; ++i)
	{
		Tree(FVector2D(R.FRandRange(15500.f, 21000.f), R.FRandRange(-15000.f, 15000.f)), R.FRandRange(1.2f, 2.4f));
	}
	for (int32 i = 0; i < SideTrees; ++i)
	{
		const double Side = R.FRand() < 0.5 ? -1.0 : 1.0;
		Tree(FVector2D(R.FRandRange(-1500.f, 15500.f), Side * R.FRandRange(4400.f, 15000.f)), R.FRandRange(0.8f, 1.8f));
	}
}

void AFishingLake::BuildHills(FRandomStream& R)
{
	// Rounded hills beyond, fading into the haze: three ranks, cooler and darker the farther they are.
	const TCHAR* const HillColors[] = { TEXT("#2c3a3a"), TEXT("#363c5c"), TEXT("#4c4c78") };
	for (int32 Rank = 0; Rank < HillRanks; ++Rank)
	{
		UMaterialInterface* M = Materials.Plain(this, Hex(HillColors[Rank]), 0.95);
		for (int32 i = 0; i < HillsPerRank; ++i)
		{
			const double X = 30000.0 + Rank * 16000.0 + R.FRandRange(-2000.f, 2000.f);
			const double Y = -42000.0 + i * 10500.0 + R.FRandRange(-2500.f, 2500.f);
			const double Rx = R.FRandRange(9000.f, 15000.f), Rz = R.FRandRange(1100.f, 2200.f) * (1.0 + Rank * 0.7);
			Instance(Sphere, M, FTransform(FRotator(0.0, R.FRandRange(0.f, 180.f), 0.0), FVector(X, Y, -Rz * 0.25), FVector(Rx / 50.0, Rx / 50.0 * 0.7, Rz / 50.0)), false);
		}
	}
}

void AFishingLake::BuildRocks(FRandomStream& R)
{
	// Rocks along the waterline of the cove.
	UMaterialInterface* Rock = Materials.Surface(this, { Hex(TEXT("#9a9086")), TEXT("rock_face"), 3.0, 0.7, 1.0, 0.4, 0.9, 0.0, 0.0, true, 0.0 });
	for (int32 i = 0; i < Rocks; ++i)
	{
		const double Side = R.FRand() < 0.5 ? -1.0 : 1.0;
		const double X = R.FRandRange(-300.f, 12000.f);
		const double Y = Side * (CoveSideStartCm(X) + R.FRandRange(300.f, 1100.f));
		const double S = R.FRandRange(0.6f, 2.2f);
		Instance(Sphere, Rock, FTransform(FRotator(R.FRandRange(-20.f, 20.f), R.FRandRange(0.f, 360.f), 0.0), FVector(X, Y, TerrainHeight(X, Y) - 10.0), FVector(S * 1.3, S, S * 0.6)));
	}
}

void AFishingLake::BuildShallows()
{
	// One stream for the shallows: the reeds, then the lily pads.
	FRandomStream R(ShallowsSeed);
	BuildReeds(R);
	BuildLilies(R);
	BuildBuoys();
}

void AFishingLake::BuildReeds(FRandomStream& R)
{
	UStaticMesh* Reed = FA::BuildReed(this);
	UMaterialInterface* Stalk = Materials.Plain(this, Hex(TEXT("#6f8a3a")), 0.8);
	UMaterialInterface* Head = Materials.Plain(this, Hex(TEXT("#5a3a22")), 0.9);
	UInstancedStaticMeshComponent* Reeds = Groups.Find(Reed, Stalk, true, nullptr, Head);
	const double HalfW = DeckHalfWidth();
	auto Clump = [&](const FVector2D& C, int32 N, double Spread)
	{
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D P = C + FVector2D(R.FRandRange(-1.f, 1.f), R.FRandRange(-1.f, 1.f)) * Spread;
			Reeds->AddInstance(FTransform(FRotator(R.FRandRange(-8.f, 8.f), R.FRandRange(0.f, 360.f), R.FRandRange(-8.f, 8.f)), FVector(P.X, P.Y, 0.0), FVector(R.FRandRange(0.7f, 1.3f))), true);
		}
	};
	// Beside the dock, and in drifts along the cove's sides.
	for (const double Side : { -1.0, 1.0 })
	{
		Clump(FVector2D(EdgeX - 60.0, Side * (HalfW + 160.0)), 26, 110.0);
		Clump(FVector2D(EdgeX + 260.0, Side * (HalfW + 300.0)), 18, 120.0);
		for (int32 k = 0; k < 9; ++k)
		{
			const double X = 500.0 + k * 1200.0 + R.FRandRange(-300.f, 300.f);
			Clump(FVector2D(X, Side * (CoveSideStartCm(X) - R.FRandRange(100.f, 500.f))), 20, 220.0);
		}
	}
}

void AFishingLake::BuildLilies(FRandomStream& R)
{
	// Lily pads, a few in flower, kept out of the lanes.
	const double HalfW = DeckHalfWidth();
	UStaticMesh* Lily = FA::BuildLilyPad(this);
	UMaterialInterface* Pad = Materials.Plain(this, Hex(TEXT("#4f8a3c")), 0.5);
	UMaterialInterface* Flower = Materials.Plain(this, Hex(TEXT("#ffb6c8")), 0.6, 0.0, 0.15);
	UInstancedStaticMeshComponent* Flowers = Groups.Find(Lily, Pad, false, nullptr, Flower);
	for (int32 i = 0; i < LilyPads; ++i)
	{
		const double X = EdgeX + R.FRandRange(80.f, 1800.f);
		const double Y = R.FRandRange(-1.f, 1.f) * (HalfW + 900.0);
		bool bInLane = false;
		for (int32 L = 0; L < Lanes; ++L)
		{
			bInLane |= FMath::Abs(Y - WaterPoint(L, Lanes, (X - EdgeX) / VisualCmPerM).Y) < LilyLaneClearCm;
		}
		if (bInLane)
		{
			continue;
		}
		const double S = R.FRandRange(0.5f, 1.1f);
		const FTransform T(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector(X, Y, 0.3), FVector(S, S, 1.0));
		if (R.FRand() < FlowerOdds)
		{
			Flowers->AddInstance(T, true);
		}
		else
		{
			// A plain pad: a flat disc.
			Instance(Cylinder, Pad, FTransform(T.GetRotation(), T.GetLocation(), FVector(0.6 * S, 0.6 * S, 0.012)), false);
		}
	}
}

void AFishingLake::BuildBuoys()
{
	// Band buoys outside the outer lanes at each band's edge: green, yellow, red.
	const double Edges[] = { MovecadeKart::Fishing::ShallowsEndM, MovecadeKart::Fishing::ReedsEndM, MovecadeKart::Fishing::DeepEndM };
	const TCHAR* const BandColors[] = { TEXT("#5fd06a"), TEXT("#ffd23c"), TEXT("#ff5a4a") };
	UMaterialInterface* White = Materials.Plain(this, Hex(TEXT("#f2efe8")), 0.4);
	for (int32 Band = 0; Band < 3; ++Band)
	{
		UMaterialInterface* Paint = Materials.Plain(this, Hex(BandColors[Band]), 0.35, 0.0, 0.25);
		for (const double Lateral : { -1.3, 1.3 })
		{
			const int32 Lane = Lateral < 0.0 ? 0 : Lanes - 1;
			const FVector P = WaterPoint(Lane, Lanes, Edges[Band], Lateral);
			Float(Part(TEXT("BandBuoy"), Sphere, Paint, FTransform(FRotator::ZeroRotator, P, FVector(0.26, 0.26, 0.22)), true));
			Float(Part(TEXT("BandBuoyStripe"), Cylinder, White, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 0.0, 6.0), FVector(0.27, 0.27, 0.05)), false));
			Float(Part(TEXT("BandBuoyFlag"), Cube, Paint, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 9.0, 34.0), FVector(0.01, 0.18, 0.12)), false));
			Float(Part(TEXT("BandBuoyPole"), Cylinder, White, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 0.0, 22.0), FVector(0.015, 0.015, 0.3)), false));
		}
	}
}

void AFishingLake::BuildProps()
{
	const double HalfW = DeckHalfWidth();
	// The rowboat moored off the left side of the dock.
	UStaticMesh* BoatMesh = FA::BuildBoat(this);
	UStaticMeshComponent* Boat = Part(TEXT("Rowboat"), BoatMesh, Materials.Plain(this, Hex(TEXT("#3f6f8a")), 0.6), FTransform(FRotator(0.0, 12.0, 0.0), FVector(-120.0, -HalfW - 120.0, 6.0), FVector(1.0)), true);
	Boat->SetMaterialByName(TEXT("Trim"), Materials.Surface(this, { Hex(TEXT("#c2a07a")), TEXT("dark_wooden_planks"), 0.8, 0.4, 0.7, 0.4, 0.8 }));
	Boat->SetMaterialByName(TEXT("Seat"), Materials.Plain(this, Hex(TEXT("#8a6a4a")), 0.8));
	Float(Boat);
	// A bucket and a tackle box behind each angler; a bench and a crate at the back.
	UStaticMesh* BucketMesh = FA::BuildBucket(this);
	UMaterialInterface* Pail = Materials.Plain(this, Hex(TEXT("#8a96a0")), 0.35, 0.7);
	UMaterialInterface* Band = Materials.Plain(this, Hex(TEXT("#5a646c")), 0.3, 0.8);
	UMaterialInterface* BucketWater = Materials.Plain(this, Hex(TEXT("#2a5a66")), 0.05, 0.0, 0.05);
	UMaterialInterface* Latch = Materials.Plain(this, Hex(TEXT("#2a2c33")), 0.5);
	for (int32 L = 0; L < Lanes; ++L)
	{
		UStaticMeshComponent* B = Part(TEXT("Bucket"), BucketMesh, Pail, FTransform(FRotator::ZeroRotator, BucketSpot(L, Lanes), FVector(1.0)), true);
		B->SetMaterialByName(TEXT("Band"), Band);
		B->SetMaterialByName(TEXT("Water"), BucketWater);
		const FVector Tackle = FVector(EdgeX - 150.0, LaneY(L, Lanes) - 80.0, DeckZ);
		Box(Materials.Plain(this, LaneColor(L) * 0.75f, 0.5), Tackle + FVector(0.0, 0.0, 9.0), FVector(26.0, 40.0, 18.0), FRotator(0.0, 8.0, 0.0));
		Box(Latch, Tackle + FVector(0.0, 0.0, 19.0), FVector(8.0, 14.0, 3.0), FRotator(0.0, 8.0, 0.0));
	}
	UMaterialInterface* Bench = Materials.Surface(this, { Hex(TEXT("#9a7250")), TEXT("dark_wooden_planks"), 1.0, 0.4, 0.7, 0.4, 0.8 });
	Box(Bench, FVector(-340.0, -HalfW + 90.0, DeckZ + 42.0), FVector(44.0, 140.0, 6.0));
	for (const double Dy : { -55.0, 55.0 })
	{
		Box(Bench, FVector(-340.0, -HalfW + 90.0 + Dy, DeckZ + 20.0), FVector(36.0, 6.0, 40.0));
	}
	// The crate, with a coil of rope on it.
	Box(Materials.Surface(this, { Hex(TEXT("#b08a5a")), TEXT("dark_wooden_planks"), 0.6, 0.4, 0.7, 0.4, 0.8 }), FVector(-330.0, HalfW - 80.0, DeckZ + 26.0), FVector(52.0, 52.0, 52.0), FRotator(0.0, 14.0, 0.0));
	Instance(Cylinder, Materials.Plain(this, Hex(TEXT("#c9b48a")), 0.9), FTransform(FRotator::ZeroRotator, FVector(-330.0, HalfW - 80.0, DeckZ + 60.0), FVector(0.4, 0.4, 0.14)));
}

void AFishingLake::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	for (int32 i = 0; i < Floaters.Num(); ++i)
	{
		if (!Floaters[i] || !FloaterRest.IsValidIndex(i))
		{
			continue;
		}
		// Everything afloat bobs on the same slow swell, phased by where it is.
		const FTransform& Rest = FloaterRest[i];
		const FVector P = Rest.GetLocation();
		const double Phase = P.X * 0.004 + P.Y * 0.003;
		const double Bob = SwellCm * FMath::Sin(Clock * 1.3 + Phase);
		const FRotator Rock(SwellPitchDeg * FMath::Sin(Clock * 1.1 + Phase), 0.0, SwellRollDeg * FMath::Sin(Clock * 0.9 + Phase + 1.0));
		Floaters[i]->SetWorldLocationAndRotation(P + FVector(0.0, 0.0, Bob), (Rock + Rest.Rotator()).Quaternion());
	}
	// The lanterns flicker gently, each out of step with the others.
	const float T = static_cast<float>(Clock);
	for (int32 i = 0; i < Lamps.Num(); ++i)
	{
		if (Lamps[i])
		{
			Lamps[i]->SetIntensity(LanternIntensity * (LanternSteady + LanternFlicker * FMath::Sin(T * 7.3f + i * 2.1f) * FMath::Sin(T * 3.1f + i)));
		}
	}
}
