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

namespace
{
	const TCHAR* const LakeTag = TEXT("FishingLake");
	const TCHAR* const LaneColors[] = { TEXT("#ff7a59"), TEXT("#2ec4b6"), TEXT("#ffc93c"), TEXT("#a083ff") };

	double SmoothStep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** The lake bed and its shores (cm): shelving from 1 m by the dock to 9 m out, rising to banks at the sides and far end. */
	double TerrainHeight(double X, double Y)
	{
		const double Out = (X - AFishingLake::EdgeX) / 100.0;
		const double Side = FMath::Abs(Y) / 100.0;
		const double Depth = 0.9 + 8.0 * (1.0 - FMath::Exp(-FMath::Max(0.0, Out) / 16.0));
		// The cove: the banks close in at the sides, wider further out; the far shore; the bank behind the dock.
		const double SideStart = 34.0 + FMath::Max(0.0, Out) * 0.35;
		const double Shore = FMath::Max(SmoothStep(SideStart, SideStart + 22.0, Side), SmoothStep(150.0, 185.0, Out));
		const double Hills = 1.6 * FMath::Sin(X * 0.0011 + 1.3) * FMath::Sin(Y * 0.0013) + 0.9 * FMath::Sin(X * 0.0031 + Y * 0.0023);
		const double Lake = (FMath::Lerp(-Depth, 1.4 + Hills * Shore, Shore) + 2.5 * FMath::Square(Shore) * SmoothStep(60.0, 120.0, Side + FMath::Max(0.0, Out - 140.0))) * 100.0;
		// The low bank the dock walks back onto (behind the camera): no hills there.
		return FMath::Lerp(Lake, 55.0, SmoothStep(-2.0, -9.0, Out));
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
	return MovecadeKart::Art::Hex(LaneColors[((Lane % 4) + 4) % 4]);
}

FVector AFishingLake::SunDirection()
{
	// Low over the far shore, a little right of centre: the glitter path runs up the lake toward the anglers.
	return FVector(1.0, 0.16, 0.085).GetSafeNormal();
}

void AFishingLake::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
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
	Floaters.Reset();
	FloaterRest.Reset();
	Lamps.Reset();
}

UInstancedStaticMeshComponent* AFishingLake::Group(UStaticMesh* Mesh, UMaterialInterface* Material, bool bShadows)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d"), *GetNameSafe(Mesh), *GetNameSafe(Material), bShadows ? 1 : 0);
	if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Groups.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("LakeGroup")));
	C->SetStaticMesh(Mesh);
	for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i)
	{
		C->SetMaterial(i, Material);
	}
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->ComponentTags.Add(LakeTag);
	AddInstanceComponent(C);
	C->RegisterComponent();
	Groups.Add(Key, C);
	return C;
}

void AFishingLake::Box(UMaterialInterface* Material, const FVector& Centre, const FVector& Size, const FRotator& Rotation, bool bShadows)
{
	Group(Cube, Material, bShadows)->AddInstance(FTransform(Rotation, Centre, Size / 100.0), true);
}

void AFishingLake::Instance(UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& Transform, bool bShadows)
{
	Group(Mesh, Material, bShadows)->AddInstance(Transform, true);
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

void AFishingLake::Build()
{
	Clear();
	BuildLook();
	BuildWater();
	BuildDock();
	BuildShores();
	BuildShallows();
	BuildProps();
}

void AFishingLake::BuildLook()
{
	using namespace MovecadeKart::Art;
	const FVector ToSun = SunDirection();
	Sun->SetWorldRotation((-ToSun).Rotation());
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
	const FLinearColor Top = Hex(TEXT("#34407a")), Horizon = Hex(TEXT("#ffa45c"));
	if (UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
		Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
		Sky->SetVectorParameterValue(TEXT("SkyMid"), Hex(TEXT("#c76a7a")));
		Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
		Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#7a5048")));
		Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(ToSun.X, ToSun.Y, ToSun.Z, 0.0));
		Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#fff0c8")) * 1.6f);
		Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(0.9f * 0.035f));
		Sky->SetScalarParameterValue(TEXT("SunGlow"), 1.1f);
		Sky->SetScalarParameterValue(TEXT("GlowPower"), 9.0f);
		Sky->SetScalarParameterValue(TEXT("HorizonGlow"), 0.85f);
		Sky->SetScalarParameterValue(TEXT("TopCurve"), 0.6f);
		Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0f);
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
	SkyLight->RecaptureSky();
	Fog->SetStartDistance(3500.f);
	Fog->SetFogDensity(0.035f);
	Fog->SetFogHeightFalloff(0.06f);
	Fog->SetFogMaxOpacity(0.75f);
	Fog->SetFogCutoffDistance(900000.f);
	Fog->SetFogInscatteringColor(Hex(TEXT("#e8936a")) * 0.8f);
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffbf7a")) * 0.6f);
	Fog->SetDirectionalInscatteringExponent(5.f);
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.45f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.85f;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.35f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.08f, 1.04f, 1.0f, 1.0f);
}

void AFishingLake::BuildWater()
{
	using namespace MovecadeKart::Art;
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
		M->SetScalarParameterValue(TEXT("WaveStrength"), 0.3f);
		M->SetScalarParameterValue(TEXT("WaveTileA"), 5.0f);
		M->SetScalarParameterValue(TEXT("WaveTileB"), 13.0f);
		M->SetScalarParameterValue(TEXT("WaveTileC"), 41.0f);
		M->SetVectorParameterValue(TEXT("Scattering"), FLinearColor(0.05f, 0.24f, 0.2f));
		M->SetVectorParameterValue(TEXT("Absorption"), FLinearColor(0.36f, 0.08f, 0.06f));
		M->SetScalarParameterValue(TEXT("PhaseG"), 0.3f);
		Water->SetMaterial(0, M);
	}
	// The lake bed and the banks around the cove (the water's colour comes from the depth over it).
	UStaticMesh* Ground = MovecadeKart::Fishing::Art::BuildTerrain(this, FVector2D(-2500.0, -16000.0), FVector2D(24000.0, 16000.0), 110, 96, &TerrainHeight);
	UMaterialInterface* Bed = Surface(this, { Hex(TEXT("#9a8a62")), TEXT("gravelly_sand"), 2.5, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.0 });
	UMaterialInterface* Bank = Surface(this, { Hex(TEXT("#6f8a45")), TEXT("leafy_grass"), 3.0, 0.45, 0.7, 0.35, 0.9, 0.0, 0.0, false, 0.1 });
	UStaticMeshComponent* G = Part(TEXT("LakeBed"), Ground, Bed, FTransform::Identity, true);
	G->SetMaterialByName(TEXT("Bank"), Bank);
}

void AFishingLake::BuildDock()
{
	using namespace MovecadeKart::Art;
	const double HalfW = DeckHalfWidth();
	const double Back = -420.0;
	UMaterialInterface* Plank = Surface(this, { Hex(TEXT("#b08560")), TEXT("dark_wooden_planks"), 1.2, 0.4, 0.8, 0.4, 0.8, 0.0, 0.0, true, 0.0 });
	UMaterialInterface* Beam = Surface(this, { Hex(TEXT("#6e5038")), TEXT("dark_wooden_planks"), 1.0, 0.35, 0.8, 0.4, 0.85, 0.0, 0.0, true, 0.0 });
	UMaterialInterface* Wet = Plain(this, Hex(TEXT("#3b3127")), 0.5);
	UMaterialInterface* Rope = Plain(this, Hex(TEXT("#c9b48a")), 0.9);
	UMaterialInterface* Iron = Plain(this, Hex(TEXT("#2a2a2e")), 0.4, 0.8);
	UMaterialInterface* Bulb = Glow(this, Hex(TEXT("#ffd89a")) * 7.0f);
	UMaterialInterface* LampGlass = Glow(this, Hex(TEXT("#ffc06a")) * 5.0f);
	// Planks across the dock (along Y) with small gaps, a little uneven, on two stringers.
	const int32 Planks = FMath::CeilToInt32((EdgeX - Back) / 24.0);
	FRandomStream R(0xd0c);
	for (int32 i = 0; i < Planks; ++i)
	{
		const double X = Back + 12.0 + i * 24.0;
		const double Lift = R.FRandRange(-0.6f, 0.6f);
		Box(Plank, FVector(X, R.FRandRange(-6.f, 6.f), DeckZ - 2.5 + Lift), FVector(22.0, HalfW * 2.0 + R.FRandRange(-10.f, 10.f), 5.0), FRotator(0.0, R.FRandRange(-0.6f, 0.6f), 0.0));
	}
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Beam, FVector((EdgeX + Back) / 2.0, Side * (HalfW - 20.0), DeckZ - 14.0), FVector(EdgeX - Back, 14.0, 18.0));
		Box(Beam, FVector((EdgeX + Back) / 2.0, Side * HalfW * 0.3, DeckZ - 14.0), FVector(EdgeX - Back, 12.0, 16.0), FRotator::ZeroRotator, false);
	}
	// The fishing edge: a kerb board along the front.
	Box(Beam, FVector(EdgeX + 6.0, 0.0, DeckZ - 8.0), FVector(12.0, HalfW * 2.0 + 20.0, 22.0));
	Box(Beam, FVector(EdgeX + 2.0, 0.0, DeckZ + 4.0), FVector(16.0, HalfW * 2.0 + 20.0, 8.0));
	// Posts into the water, dark where they are wet, with a rope wrap on the front ones.
	const int32 PostsY = FMath::Max(3, FMath::CeilToInt32(HalfW * 2.0 / 240.0) + 1);
	for (int32 i = 0; i < PostsY; ++i)
	{
		const double Y = -HalfW + 10.0 + i * (HalfW * 2.0 - 20.0) / (PostsY - 1);
		for (const double X : { EdgeX + 10.0, (EdgeX + Back) / 2.0, Back + 20.0 })
		{
			const bool bFront = X > EdgeX;
			const double Top = bFront ? DeckZ + 34.0 : DeckZ - 4.0;
			Group(Cylinder, Beam, true)->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Y, (Top - 260.0) / 2.0), FVector(0.26, 0.26, (Top + 260.0) / 100.0)), true);
			Group(Cylinder, Wet, false)->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Y, -6.0), FVector(0.275, 0.275, 0.3)), true);
			if (bFront)
			{
				Group(Cylinder, Rope, true)->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Y, DeckZ + 18.0), FVector(0.29, 0.29, 0.1)), true);
			}
		}
	}
	// The walkway back to the shore behind the camera.
	for (int32 i = 0; i < 18; ++i)
	{
		const double X = Back - 12.0 - i * 24.0;
		Box(Plank, FVector(X, 0.0, DeckZ - 2.5 + R.FRandRange(-0.6f, 0.6f)), FVector(22.0, 200.0, 5.0), FRotator(0.0, R.FRandRange(-0.8f, 0.8f), 0.0));
	}
	// Lamp posts at the four corners and string lights swagged between them along each side.
	TArray<FVector> Tops;
	for (const double Side : { -1.0, 1.0 })
	{
		for (const double X : { EdgeX - 6.0, Back + 20.0 })
		{
			const FVector Base(X, Side * (HalfW - 6.0), DeckZ);
			Group(Cylinder, Beam, true)->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0.0, 0.0, 130.0), FVector(0.14, 0.14, 2.6)), true);
			Box(Iron, Base + FVector(0.0, -Side * 14.0, 258.0), FVector(4.0, 34.0, 4.0));
			const FVector Lantern = Base + FVector(0.0, -Side * 28.0, 236.0);
			Box(Iron, Lantern + FVector(0.0, 0.0, 14.0), FVector(16.0, 16.0, 3.0));
			Box(LampGlass, Lantern, FVector(12.0, 12.0, 22.0), FRotator::ZeroRotator, false);
			Tops.Add(Base + FVector(0.0, 0.0, 262.0));
			UPointLightComponent* L = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("Lantern")));
			L->SetupAttachment(RootComponent);
			L->SetMobility(EComponentMobility::Movable);
			L->SetWorldLocation(Lantern);
			L->SetLightColor(Hex(TEXT("#ffb35c")));
			L->SetIntensity(5200.f);
			L->SetAttenuationRadius(700.f);
			L->SetCastShadows(false);
			L->ComponentTags.Add(LakeTag);
			AddInstanceComponent(L);
			L->RegisterComponent();
			Lamps.Add(L);
		}
	}
	for (int32 s = 0; s < 2; ++s)
	{
		const FVector A = Tops[s * 2], B = Tops[s * 2 + 1];
		const int32 Bulbs = 11;
		for (int32 k = 0; k <= 24; ++k)
		{
			const double U0 = k / 25.0, U1 = (k + 1) / 25.0;
			const FVector P0 = FMath::Lerp(A, B, U0) - FVector(0.0, 0.0, 70.0 * 4.0 * U0 * (1.0 - U0));
			const FVector P1 = FMath::Lerp(A, B, U1) - FVector(0.0, 0.0, 70.0 * 4.0 * U1 * (1.0 - U1));
			Box(Iron, (P0 + P1) / 2.0, FVector((P1 - P0).Size(), 0.8, 0.8), (P1 - P0).Rotation(), false);
		}
		for (int32 k = 1; k < Bulbs; ++k)
		{
			const double U = static_cast<double>(k) / Bulbs;
			const FVector P = FMath::Lerp(A, B, U) - FVector(0.0, 0.0, 70.0 * 4.0 * U * (1.0 - U) + 7.0);
			Group(Sphere, Bulb, false)->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(0.07, 0.07, 0.09)), true);
		}
	}
}

void AFishingLake::BuildShores()
{
	using namespace MovecadeKart::Art;
	namespace FA = MovecadeKart::Fishing::Art;
	UStaticMesh* Pine = FA::BuildPine(this);
	FRandomStream R(0x51ae);
	const TCHAR* const PineColors[] = { TEXT("#2f4a32"), TEXT("#3a5536"), TEXT("#28402e") };
	TArray<UMaterialInterface*> Needles;
	for (const TCHAR* C : PineColors)
	{
		Needles.Add(Plain(this, Hex(C), 0.85));
	}
	UMaterialInterface* Trunk = Plain(this, Hex(TEXT("#4a3626")), 0.9);
	auto Tree = [&](const FVector2D& At, double Scale)
	{
		const double Z = TerrainHeight(At.X, At.Y);
		if (Z < 20.0)
		{
			return;
		}
		const FTransform T(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector(At.X, At.Y, Z - 30.0), FVector(Scale));
		UInstancedStaticMeshComponent* G = Group(Pine, Needles[R.RandHelper(Needles.Num())], true);
		G->SetMaterial(0, Trunk);
		G->AddInstance(T, true);
	};
	// The far shore's tree line, thick, and the cove's sides thinning toward the dock.
	for (int32 i = 0; i < 150; ++i)
	{
		Tree(FVector2D(R.FRandRange(15500.f, 21000.f), R.FRandRange(-15000.f, 15000.f)), R.FRandRange(1.2f, 2.4f));
	}
	for (int32 i = 0; i < 220; ++i)
	{
		const double Side = R.FRand() < 0.5 ? -1.0 : 1.0;
		Tree(FVector2D(R.FRandRange(-1500.f, 15500.f), Side * R.FRandRange(4400.f, 15000.f)), R.FRandRange(0.8f, 1.8f));
	}
	// Rounded hills beyond, fading into the haze: three ranks, cooler and darker the farther they are.
	const TCHAR* const HillColors[] = { TEXT("#3c5236"), TEXT("#3d4b52"), TEXT("#4f5272") };
	for (int32 Rank = 0; Rank < 3; ++Rank)
	{
		UMaterialInterface* M = Plain(this, Hex(HillColors[Rank]), 0.95);
		for (int32 i = 0; i < 9; ++i)
		{
			const double X = 24000.0 + Rank * 14000.0 + R.FRandRange(-2000.f, 2000.f);
			const double Y = -42000.0 + i * 10500.0 + R.FRandRange(-2500.f, 2500.f);
			const double Rx = R.FRandRange(9000.f, 15000.f), Rz = R.FRandRange(2600.f, 5200.f) * (1.0 + Rank * 0.5);
			Group(Sphere, M, false)->AddInstance(FTransform(FRotator(0.0, R.FRandRange(0.f, 180.f), 0.0), FVector(X, Y, -Rz * 0.25), FVector(Rx / 50.0, Rx / 50.0 * 0.7, Rz / 50.0)), true);
		}
	}
	// Rocks along the waterline of the cove.
	UMaterialInterface* Rock = Surface(this, { Hex(TEXT("#9a9086")), TEXT("rock_face"), 3.0, 0.7, 1.0, 0.4, 0.9, 0.0, 0.0, true, 0.0 });
	for (int32 i = 0; i < 70; ++i)
	{
		const double Side = R.FRand() < 0.5 ? -1.0 : 1.0;
		const double X = R.FRandRange(-300.f, 12000.f);
		const double SideStart = (34.0 + FMath::Max(0.0, (X - EdgeX) / 100.0) * 0.35) * 100.0;
		const double Y = Side * (SideStart + R.FRandRange(300.f, 1100.f));
		const double S = R.FRandRange(0.6f, 2.2f);
		Group(Sphere, Rock, true)->AddInstance(FTransform(FRotator(R.FRandRange(-20.f, 20.f), R.FRandRange(0.f, 360.f), 0.0), FVector(X, Y, TerrainHeight(X, Y) - 10.0), FVector(S * 1.3, S, S * 0.6)), true);
	}
}

void AFishingLake::BuildShallows()
{
	using namespace MovecadeKart::Art;
	namespace FA = MovecadeKart::Fishing::Art;
	FRandomStream R(0x7eed);
	UStaticMesh* Reed = FA::BuildReed(this);
	UMaterialInterface* Stalk = Plain(this, Hex(TEXT("#6f8a3a")), 0.8);
	UMaterialInterface* Head = Plain(this, Hex(TEXT("#5a3a22")), 0.9);
	UInstancedStaticMeshComponent* Reeds = Group(Reed, Stalk, true);
	Reeds->SetMaterial(1, Head);
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
			const double SideStart = (34.0 + FMath::Max(0.0, (X - EdgeX) / 100.0) * 0.35) * 100.0;
			Clump(FVector2D(X, Side * (SideStart - R.FRandRange(100.f, 500.f))), 20, 220.0);
		}
	}
	// Lily pads, a few in flower, kept out of the lanes.
	UStaticMesh* Lily = FA::BuildLilyPad(this);
	UMaterialInterface* Pad = Plain(this, Hex(TEXT("#4f8a3c")), 0.5);
	UMaterialInterface* Flower = Plain(this, Hex(TEXT("#ffb6c8")), 0.6, 0.0, 0.15);
	UInstancedStaticMeshComponent* Flowers = Group(Lily, Pad, false);
	Flowers->SetMaterial(1, Flower);
	for (int32 i = 0; i < 70; ++i)
	{
		const double X = EdgeX + R.FRandRange(80.f, 1800.f);
		const double Y = R.FRandRange(-1.f, 1.f) * (HalfW + 900.0);
		bool bInLane = false;
		for (int32 L = 0; L < Lanes; ++L)
		{
			bInLane |= FMath::Abs(Y - WaterPoint(L, Lanes, (X - EdgeX) / VisualCmPerM).Y) < 110.0;
		}
		if (bInLane)
		{
			continue;
		}
		const double S = R.FRandRange(0.5f, 1.1f);
		const FTransform T(FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), FVector(X, Y, 0.3), FVector(S, S, 1.0));
		if (R.FRand() < 0.18)
		{
			Flowers->AddInstance(T, true);
		}
		else
		{
			Group(Cylinder, Pad, false)->AddInstance(FTransform(T.GetRotation(), T.GetLocation(), FVector(0.6 * S, 0.6 * S, 0.012)), true);
		}
	}
	// Band buoys between the lanes (and outside them) at each band's edge: green, yellow, red.
	const double Edges[] = { MovecadeKart::Fishing::ShallowsEndM, MovecadeKart::Fishing::ReedsEndM, MovecadeKart::Fishing::DeepEndM };
	const TCHAR* const BandColors[] = { TEXT("#5fd06a"), TEXT("#ffd23c"), TEXT("#ff5a4a") };
	UMaterialInterface* White = Plain(this, Hex(TEXT("#f2efe8")), 0.4);
	for (int32 b = 0; b < 3; ++b)
	{
		UMaterialInterface* Paint = Plain(this, Hex(BandColors[b]), 0.35, 0.0, 0.25);
		for (int32 g = 0; g <= Lanes; ++g)
		{
			const double Lateral = g == 0 ? -1.3 : (g == Lanes ? 1.3 : 0.0);
			const int32 L = FMath::Clamp(g == Lanes ? g - 1 : g, 0, Lanes - 1);
			FVector P = WaterPoint(L, Lanes, Edges[b], 0.0);
			if (g > 0 && g < Lanes)
			{
				P = (WaterPoint(g - 1, Lanes, Edges[b]) + WaterPoint(g, Lanes, Edges[b])) / 2.0;
			}
			else
			{
				P = WaterPoint(L, Lanes, Edges[b], Lateral);
			}
			UStaticMeshComponent* Float = Part(TEXT("BandBuoy"), Sphere, Paint, FTransform(FRotator::ZeroRotator, P, FVector(0.26, 0.26, 0.22)), true);
			Floaters.Add(Float);
			FloaterRest.Add(Float->GetComponentTransform());
			UStaticMeshComponent* Band = Part(TEXT("BandBuoyStripe"), Cylinder, White, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 0.0, 6.0), FVector(0.27, 0.27, 0.05)), false);
			Floaters.Add(Band);
			FloaterRest.Add(Band->GetComponentTransform());
			UStaticMeshComponent* Flag = Part(TEXT("BandBuoyFlag"), Cube, Paint, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 9.0, 34.0), FVector(0.01, 0.18, 0.12)), false);
			Floaters.Add(Flag);
			FloaterRest.Add(Flag->GetComponentTransform());
			UStaticMeshComponent* Pole = Part(TEXT("BandBuoyPole"), Cylinder, White, FTransform(FRotator::ZeroRotator, P + FVector(0.0, 0.0, 22.0), FVector(0.015, 0.015, 0.3)), false);
			Floaters.Add(Pole);
			FloaterRest.Add(Pole->GetComponentTransform());
		}
	}
}

void AFishingLake::BuildProps()
{
	using namespace MovecadeKart::Art;
	namespace FA = MovecadeKart::Fishing::Art;
	const double HalfW = DeckHalfWidth();
	// The rowboat moored off the left side of the dock, its rope to the corner post.
	UStaticMesh* BoatMesh = FA::BuildBoat(this);
	UStaticMeshComponent* Boat = Part(TEXT("Rowboat"), BoatMesh, Plain(this, Hex(TEXT("#3f6f8a")), 0.6), FTransform(FRotator(0.0, 12.0, 0.0), FVector(-120.0, -HalfW - 120.0, 6.0), FVector(1.0)), true);
	Boat->SetMaterialByName(TEXT("Trim"), Surface(this, { Hex(TEXT("#c2a07a")), TEXT("dark_wooden_planks"), 0.8, 0.4, 0.7, 0.4, 0.8 }));
	Boat->SetMaterialByName(TEXT("Seat"), Plain(this, Hex(TEXT("#8a6a4a")), 0.8));
	Floaters.Add(Boat);
	FloaterRest.Add(Boat->GetComponentTransform());
	// A bucket and a tackle box behind each angler; a bench and a crate at the back.
	UStaticMesh* BucketMesh = FA::BuildBucket(this);
	UMaterialInterface* Pail = Plain(this, Hex(TEXT("#8a96a0")), 0.35, 0.7);
	UMaterialInterface* Band = Plain(this, Hex(TEXT("#5a646c")), 0.3, 0.8);
	UMaterialInterface* BucketWater = Plain(this, Hex(TEXT("#2a5a66")), 0.05, 0.0, 0.05);
	for (int32 L = 0; L < Lanes; ++L)
	{
		UStaticMeshComponent* B = Part(TEXT("Bucket"), BucketMesh, Pail, FTransform(FRotator::ZeroRotator, BucketSpot(L, Lanes), FVector(1.0)), true);
		B->SetMaterialByName(TEXT("Band"), Band);
		B->SetMaterialByName(TEXT("Water"), BucketWater);
		const FVector Tackle = FVector(EdgeX - 150.0, LaneY(L, Lanes) - 80.0, DeckZ);
		Box(Plain(this, LaneColor(L) * 0.75f, 0.5), Tackle + FVector(0.0, 0.0, 9.0), FVector(26.0, 40.0, 18.0), FRotator(0.0, 8.0, 0.0));
		Box(Plain(this, Hex(TEXT("#2a2c33")), 0.5), Tackle + FVector(0.0, 0.0, 19.0), FVector(8.0, 14.0, 3.0), FRotator(0.0, 8.0, 0.0));
	}
	UMaterialInterface* Bench = Surface(this, { Hex(TEXT("#9a7250")), TEXT("dark_wooden_planks"), 1.0, 0.4, 0.7, 0.4, 0.8 });
	Box(Bench, FVector(-340.0, -HalfW + 90.0, DeckZ + 42.0), FVector(44.0, 140.0, 6.0));
	for (const double Dy : { -55.0, 55.0 })
	{
		Box(Bench, FVector(-340.0, -HalfW + 90.0 + Dy, DeckZ + 20.0), FVector(36.0, 6.0, 40.0));
	}
	Box(Surface(this, { Hex(TEXT("#b08a5a")), TEXT("dark_wooden_planks"), 0.6, 0.4, 0.7, 0.4, 0.8 }), FVector(-330.0, HalfW - 80.0, DeckZ + 26.0), FVector(52.0, 52.0, 52.0), FRotator(0.0, 14.0, 0.0));
	Group(Cylinder, Plain(this, Hex(TEXT("#c9b48a")), 0.9), true)->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-330.0, HalfW - 80.0, DeckZ + 60.0), FVector(0.4, 0.4, 0.14)), true);
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
		const double Bob = 2.2 * FMath::Sin(Clock * 1.3 + Phase);
		const FRotator Rock(1.6 * FMath::Sin(Clock * 1.1 + Phase), 0.0, 2.4 * FMath::Sin(Clock * 0.9 + Phase + 1.0));
		Floaters[i]->SetWorldLocationAndRotation(P + FVector(0.0, 0.0, Bob), (Rock + Rest.Rotator()).Quaternion());
	}
	for (int32 i = 0; i < Lamps.Num(); ++i)
	{
		if (Lamps[i])
		{
			Lamps[i]->SetIntensity(5200.f * (0.92f + 0.08f * FMath::Sin(static_cast<float>(Clock) * 7.3f + i * 2.1f) * FMath::Sin(static_cast<float>(Clock) * 3.1f + i)));
		}
	}
}
