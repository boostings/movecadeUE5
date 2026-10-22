#include "Games/Samurai/SamuraiDojo.h"

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
#include "FX/KartFxSubsystem.h"
#include "Games/Samurai/SamuraiDojoMeshes.h"
#include "Games/Samurai/SamuraiLog.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart::Art;

namespace
{
	const FName DojoTag(TEXT("SamuraiDojo"));

	// ---- Layout (web metres) ----
	constexpr double LaneSpacing = 3.6;
	constexpr int32 MaxLanes = 4;
	/** The stage is at least this wide, else the lanes plus a margin. */
	constexpr double MinStageWidth = 13.0;
	constexpr double StageMargin = 4.0;
	constexpr double TatamiZ = 0.15;
	constexpr double WallZ = -3.6;
	/** The lanterns hang from the wall's beam to this height, this far in front of the wall. */
	constexpr double BeamY = 5.55;
	constexpr double LanternY = 4.55;
	constexpr double LanternZ = -1.3;
	constexpr double SkyDomeScale = 20000.0;

	// ---- Light (samuraiStage.tsx) ----
	/** The port's sun sits lower and further behind than the web's (13, 5.6, -3), near the painted sun. */
	const FVector SunFrom(6.0, 3.2, -10.0);
	/** The sky material's sun (the web's [.47, .195, -1]). */
	const FVector SkySun(0.47, 0.195, -1.0);
	constexpr float SunIntensity = 4.2f;
	constexpr float SunVolumetricScattering = 2.6f;
	constexpr float SunShaftBloomScale = 0.22f;
	constexpr float SunShaftBloomThreshold = 0.35f;
	constexpr float SunShaftBloomMax = 6.f;
	constexpr float SunSourceAngle = 1.2f;
	const FVector RimFrom(-9.0, 5.0, 10.0);
	constexpr float RimIntensity = 1.6f;
	constexpr float SkyLightIntensity = 1.05f;
	constexpr float SkyLightTint = 0.4f;
	constexpr float LowerHemisphereScale = 0.5f;

	// ---- Sky (M_TrackSky) ----
	constexpr float SkySunGlow = 3.f;
	/** The sun disc's angular radius (cos of 0.9 x 0.042 rad). */
	constexpr double SkySunAngle = 0.9 * 0.042;
	constexpr double SkySunHalo = 1.6;
	constexpr double SkyGlowPower = 9.0;
	constexpr double SkyHorizonGlow = 1.1;
	constexpr double SkyClouds = 0.5;
	constexpr double SkyCloudScale = 0.35;
	constexpr float SkyCloudLit = 1.4f;

	// ---- Haze (cm) ----
	constexpr float FogStart = 1800.f;
	constexpr float FogDensity = 0.045f;
	constexpr float FogHeightFalloff = 0.02f;
	constexpr float FogMaxOpacity = 0.8f;
	/** The painted sky (10 km out) stays clear of the haze. */
	constexpr float FogCutoff = 250000.f;
	constexpr float FogHorizonToMid = 0.35f;
	constexpr float FogInscatter = 0.85f;
	constexpr float FogSunInscatter = 0.6f;
	constexpr float FogSunExponent = 5.f;
	constexpr float VolumetricScattering = 0.75f;
	constexpr float VolumetricExtinction = 0.6f;
	constexpr float VolumetricDistance = 4000.f;

	// ---- Lamps (lumens, cm) ----
	constexpr float ToroLumens = 400.f;
	constexpr float ToroRadius = 350.f;
	constexpr float LanternLumens = 900.f;
	constexpr float LanternRadius = 520.f;
	constexpr float FireGlow = 3.f;

	// ---- Motion ----
	/** Each lantern sways on two beats (degrees of roll), offset per lantern. */
	constexpr double SwayRate = 0.9;
	constexpr double SwayPhase = 1.7;
	constexpr double SwayDegrees = 4.0;
	constexpr double FlutterRate = 2.3;
	constexpr double FlutterDegrees = 0.8;
	/** Petals per second drifting through the light, and their drift speed (m/s). */
	constexpr double PetalsPerSecond = 11.0;
	constexpr double DriftSpeed = 0.6;
	constexpr double BurstSpeed = 2.4;
	/** Mood changes smaller than this aren't worth a re-grade. */
	constexpr float MoodEpsilon = 0.01f;
}

ASamuraiDojo::ASamuraiDojo()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// The venue doesn't move: static geometry (cached shadows, no per-frame transform work). The lights, the sky
	// and the lantern pivots stay Movable children.
	RootComponent->SetMobility(EComponentMobility::Static);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Cube = CubeMesh.Object;
	Cylinder = CylinderMesh.Object;
	Sphere = SphereMesh.Object;

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Rim = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Rim"));
	Rim->SetupAttachment(RootComponent);
	Rim->SetMobility(EComponentMobility::Movable);
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	// Captured once after each build (RecaptureSky), not every frame.
	SkyLight->bRealTimeCapture = false;
	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(RootComponent);
	SkyDome->SetStaticMesh(SphereMesh.Object);
	SkyDome->SetRelativeScale3D(FVector(SkyDomeScale));
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCastShadow(false);
	SkyDome->SetMobility(EComponentMobility::Movable);
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetMobility(EComponentMobility::Movable);
	Grade = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Grade"));
	Grade->SetupAttachment(RootComponent);
	Grade->bUnbound = true;
	PetalRng.Initialize(0x9e7a1);
}

FVector ASamuraiDojo::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

double ASamuraiDojo::StageWidth() const
{
	return FMath::Max(MinStageWidth, Lanes * LaneSpacing + StageMargin);
}

void ASamuraiDojo::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SetActorTransform(FTransform::Identity);
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		// The round sets its lane count right after spawning: build once, then.
		bBuildPending = true;
		return;
	}
	Build();
}

void ASamuraiDojo::SetLanes(int32 LaneCount)
{
	LaneCount = FMath::Clamp(LaneCount, 1, MaxLanes);
	if (LaneCount != Lanes || bBuildPending || !bBuilt)
	{
		Lanes = LaneCount;
		Build();
	}
}

// ---- Building -----------------------------------------------------------------------------------------------------

template <class TComponent>
TComponent* ASamuraiDojo::AddDojoComponent(const TCHAR* Name, USceneComponent* Parent, EComponentMobility::Type Mobility)
{
	TComponent* Component = NewObject<TComponent>(this, MakeUniqueObjectName(this, TComponent::StaticClass(), Name));
	Component->SetupAttachment(Parent ? Parent : RootComponent.Get());
	Component->SetMobility(Mobility);
	Component->ComponentTags.Add(DojoTag);
	AddInstanceComponent(Component);
	return Component;
}

void ASamuraiDojo::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), DojoTag);
	for (UActorComponent* Component : Old)
	{
		RemoveInstanceComponent(Component);
		Component->DestroyComponent();
	}
	Groups.Reset();
	Lanterns.Reset();
}

void ASamuraiDojo::Box(UMaterialInterface* Material, const FVector& C, const FVector& S, double YawRad, bool bShadows)
{
	Groups.Find(Cube, Material, bShadows)->AddInstance(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(YawRad), 0.0), ToWorld(C), FVector(S.Z, S.X, S.Y)), true);
}

void ASamuraiDojo::Post(UMaterialInterface* Material, const FVector& Base, double Radius, double Height, bool bShadows)
{
	Groups.Find(Cylinder, Material, bShadows)->AddInstance(FTransform(FRotator::ZeroRotator, ToWorld(Base + FVector(0.0, Height / 2.0, 0.0)),
		FVector(Radius * 2.0, Radius * 2.0, Height)), true);
}

UStaticMeshComponent* ASamuraiDojo::Part(UStaticMesh* Mesh, const FTransform& World, bool bShadows)
{
	UStaticMeshComponent* Component = AddDojoComponent<UStaticMeshComponent>(TEXT("DojoPart"), nullptr, EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	Component->SetRelativeTransform(World);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(bShadows);
	Component->RegisterComponent();
	return Component;
}

void ASamuraiDojo::Build()
{
	bBuildPending = false;
	bBuilt = true;
	Clear();
	Groups.Init(this, TEXT("DojoGroup"), DojoTag, EComponentMobility::Static);
	BuildMeshes();
	if (!bLookBuilt)
	{
		// The lights, sky, fog and grade don't depend on the lanes.
		bLookBuilt = true;
		BuildLook();
	}
	BuildGround();
	BuildStage();
	BuildTorii();
	BuildWall();
	BuildGarden();
	BuildDistance();
	BuildLanterns();
	SkyLight->RecaptureSky();
}

void ASamuraiDojo::BuildMeshes()
{
	if (LanternMesh)
	{
		return;
	}
	using namespace MovecadeKart::Samurai;
	LanternMesh = DojoMeshes::Lantern(this);
	RidgeMesh = DojoMeshes::Ridge(this);
	PineMesh = DojoMeshes::Pine(this);
	StoneMesh = DojoMeshes::Stone(this);
	KasagiMesh = DojoMeshes::Kasagi(this);
}

// ---- The look: light, sky, haze and grade ---------------------------------------------------------------------

void ASamuraiDojo::BuildLook()
{
	using MovecadeKart::Coords::DirToUE;
	// The golden hour: a low sun from behind the right of the dojo throws long shadows toward the camera and rims
	// every swordsman and fruit; a rose fill from the front left keeps the faces and fruit readable.
	Sun->SetWorldRotation((-DirToUE(SunFrom).GetSafeNormal()).Rotation());
	Sun->SetLightColor(Hex(TEXT("#ffb46e")));
	Sun->SetIntensity(SunIntensity);
	Sun->SetCastShadows(true);
	Sun->SetAtmosphereSunLight(false);
	Sun->SetVolumetricScatteringIntensity(SunVolumetricScattering);
	Sun->bEnableLightShaftBloom = true;
	Sun->BloomScale = SunShaftBloomScale;
	Sun->BloomThreshold = SunShaftBloomThreshold;
	Sun->BloomMaxBrightness = SunShaftBloomMax;
	Sun->BloomTint = FColor(255, 196, 140);
	Sun->LightSourceAngle = SunSourceAngle;
	Sun->MarkRenderStateDirty();
	Rim->SetWorldRotation((-DirToUE(RimFrom).GetSafeNormal()).Rotation());
	Rim->SetLightColor(Hex(TEXT("#f49aa0")));
	Rim->SetIntensity(RimIntensity);
	Rim->SetCastShadows(false);
	Rim->SetAtmosphereSunLight(false);
	Rim->SetVolumetricScatteringIntensity(0.f);
	// The sun is the one forward shading, translucency and fog use.
	Sun->SetForwardShadingPriority(1);
	Rim->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(SkyLightIntensity);
	SkyLight->SetLightColor(FMath::Lerp(FLinearColor::White, Hex(TEXT("#a7a6e0")), SkyLightTint));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(TEXT("#6b4a3c")) * LowerHemisphereScale);

	// A sunset that grades from indigo overhead through rose to a molten horizon, the sun low over the garden.
	const FLinearColor Top = Hex(TEXT("#2c2f6e")), Mid = Hex(TEXT("#b4587c")), Horizon = Hex(TEXT("#ffa25a"));
	BuildSky(Top, Mid, Horizon);
	BuildFog(Mid, Horizon);
	ApplyGrade();
}

void ASamuraiDojo::BuildSky(const FLinearColor& Top, const FLinearColor& Mid, const FLinearColor& Horizon)
{
	UMaterialInterface* SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Tracks/Shared/Materials/M_TrackSky.M_TrackSky"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!SkyMaster)
	{
		UE_LOG(LogSamurai, Warning, TEXT("Dojo: M_TrackSky is missing; the dojo has no painted sky"));
		return;
	}
	UMaterialInstanceDynamic* Sky = UMaterialInstanceDynamic::Create(SkyMaster, this);
	Sky->SetVectorParameterValue(TEXT("SkyTop"), Top);
	Sky->SetVectorParameterValue(TEXT("SkyMid"), Mid);
	Sky->SetVectorParameterValue(TEXT("SkyHorizon"), Horizon);
	Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(TEXT("#5a3a3a")));
	const FVector SunDir = MovecadeKart::Coords::DirToUE(SkySun).GetSafeNormal();
	Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0));
	Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(TEXT("#fff0c8")) * SkySunGlow);
	Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(SkySunAngle));
	Sky->SetScalarParameterValue(TEXT("SunGlow"), SkySunHalo);
	Sky->SetScalarParameterValue(TEXT("GlowPower"), SkyGlowPower);
	Sky->SetScalarParameterValue(TEXT("HorizonGlow"), SkyHorizonGlow);
	Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0);
	if (UTexture* Clouds = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
	}
	Sky->SetScalarParameterValue(TEXT("Clouds"), SkyClouds);
	Sky->SetScalarParameterValue(TEXT("CloudScale"), SkyCloudScale);
	Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(TEXT("#ffc08a")) * SkyCloudLit);
	Sky->SetVectorParameterValue(TEXT("CloudShade"), Hex(TEXT("#7a4a78")));
	SkyDome->SetMaterial(0, Sky);
}

void ASamuraiDojo::BuildFog(const FLinearColor& Mid, const FLinearColor& Horizon)
{
	// Haze from 18 m in the horizon's peach, glowing toward the sun; volumetric fog catches the sun's shafts
	// through the torii, the posts and the open middle of the wall.
	Fog->SetStartDistance(FogStart);
	Fog->SetFogDensity(FogDensity);
	Fog->SetFogHeightFalloff(FogHeightFalloff);
	Fog->SetFogMaxOpacity(FogMaxOpacity);
	Fog->SetFogCutoffDistance(FogCutoff);
	Fog->SetFogInscatteringColor(FMath::Lerp(Horizon, Mid, FogHorizonToMid) * FogInscatter);
	Fog->SetDirectionalInscatteringColor(Hex(TEXT("#ffb46e")) * FogSunInscatter);
	Fog->SetDirectionalInscatteringExponent(FogSunExponent);
	Fog->SetVolumetricFog(true);
	Fog->SetVolumetricFogScatteringDistribution(VolumetricScattering);
	Fog->SetVolumetricFogAlbedo(FColor(255, 214, 180));
	Fog->SetVolumetricFogExtinctionScale(VolumetricExtinction);
	Fog->SetVolumetricFogDistance(VolumetricDistance);
	Fog->SetVolumetricFogStartDistance(0.f);
}

void ASamuraiDojo::ApplyGrade()
{
	// The mood's grade: warm highlights, violet shadows, a little more colour and contrast, bloom on the glows and
	// a soft vignette. Mood layers Focus (gold, saturated, a pulsing vignette) and impacts (a flash, fringing).
	// Each moody value reads base + per-Focus (+ per-Flash or per-Fringe) times the mood.
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.15f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.0, 1.0, 1.0, 1.1 + 0.2 * MoodFocus);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.0, 1.0, 1.0, 1.08 + 0.05 * MoodFocus);
	S.bOverride_ColorGainHighlights = true;
	S.ColorGainHighlights = FVector4(1.06 + 0.06 * MoodFocus, 1.0 + 0.02 * MoodFocus, 0.9 - 0.05 * MoodFocus, 1.0);
	S.bOverride_ColorOffsetShadows = true;
	S.ColorOffsetShadows = FVector4(0.012, 0.0, 0.03, 0.0);
	S.bOverride_ColorGammaShadows = true;
	S.ColorGammaShadows = FVector4(1.0, 0.98, 1.04, 1.0);
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.85f + 0.5f * MoodFocus + 1.6f * MoodFlash;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = -1.f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.42f + 0.38f * MoodFocus;
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.4f + 3.5f * MoodFringe;
	S.bOverride_SceneColorTint = true;
	S.SceneColorTint = FMath::Lerp(FLinearColor::White, MoodFlashColor * 2.2f, 0.5f * MoodFlash) + FLinearColor(0.07f, 0.04f, -0.05f, 0.f) * MoodFocus;
	S.SceneColorTint.A = 1.f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.08f;
}

void ASamuraiDojo::SetMood(float Focus, float Flash, const FLinearColor& FlashColor, float Fringe)
{
	// Re-grade only when the mood moved enough to see.
	if (FMath::Abs(Focus - MoodFocus) < MoodEpsilon && FMath::Abs(Flash - MoodFlash) < MoodEpsilon && FMath::Abs(Fringe - MoodFringe) < MoodEpsilon)
	{
		return;
	}
	MoodFocus = Focus;
	MoodFlash = Flash;
	MoodFlashColor = FlashColor;
	MoodFringe = Fringe;
	ApplyGrade();
}

// ---- The set (web metres; the coordinates are the set's modelling) ----------------------------------------------

void ASamuraiDojo::BuildGround()
{
	UMaterialInterface* Earth = Materials.Surface(this, { Hex(TEXT("#4a3c2c")), TEXT("gravelly_sand"), 2.0, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.4 });
	UMaterialInterface* Gravel = Materials.Surface(this, { Hex(TEXT("#b5aa95")), TEXT("gravelly_sand"), 0.8, 0.6, 0.8, 0.45, 0.92, 0.0, 0.0, false, 0.7 });
	UMaterialInterface* Moss = Materials.Surface(this, { Hex(TEXT("#3d5a33")), TEXT("leafy_grass"), 1.2, 0.5, 0.6, 0.4, 0.95, 0.0, 0.0, false, 0.2 });
	Box(Earth, FVector(0.0, -0.24, -6.0), FVector(90.0, 0.2, 40.0), 0.0, false);
	Box(Gravel, FVector(0.0, -0.13, -12.0), FVector(StageWidth() + 6.0, 0.02, 9.0), 0.0, false);
	// Raked lines across the gravel.
	UMaterialInterface* Rake = Materials.Surface(this, { Hex(TEXT("#9a907e")), TEXT("gravelly_sand"), 0.8, 0.6, 0.8, 0.45, 0.95, 0.0, 0.0, false, 0.7 });
	for (int32 i = 0; i < 12; ++i)
	{
		Box(Rake, FVector(0.0, -0.118, -8.2 - i * 0.62), FVector(StageWidth() + 5.0, 0.01, 0.07), 0.0, false);
	}
	for (const double Side : { -1.0, 1.0 })
	{
		Box(Moss, FVector(Side * (StageWidth() * 0.5 + 4.0), -0.12, -6.0), FVector(8.0, 0.03, 20.0), 0.0, false);
	}
}

void ASamuraiDojo::BuildStage()
{
	UMaterialInterface* Cedar = Materials.Surface(this, { Hex(TEXT("#8a5a3a")), TEXT("dark_wooden_planks"), 1.2, 0.5, 0.65, 0.4, 0.55, 0.0, 0.0, false });
	UMaterialInterface* CedarSide = Materials.Surface(this, { Hex(TEXT("#5e3a22")), TEXT("dark_wooden_planks"), 0.8, 0.4, 0.6, 0.4, 0.6 });
	UMaterialInterface* Tatami[] = {
		Materials.Surface(this, { Hex(TEXT("#c9b77e")), TEXT("leafy_grass"), 0.45, 0.35, 0.5, 0.3, 0.8, 0.0, 0.0, false, 1.0 }),
		Materials.Surface(this, { Hex(TEXT("#bfae74")), TEXT("leafy_grass"), 0.45, 0.35, 0.5, 0.3, 0.8, 0.0, 0.0, false, 1.0 }),
	};
	UMaterialInterface* Binding[] = { Materials.Plain(this, Hex(TEXT("#1f3b2b")), 0.6), Materials.Plain(this, Hex(TEXT("#2b1d33")), 0.6) };
	const double W = StageWidth();
	// The floor: a raised cedar stage, its front edge toward the camera.
	Box(Cedar, FVector(0.0, -0.075, 0.3), FVector(W, 0.15, 8.0));
	Box(CedarSide, FVector(0.0, -0.18, 4.3), FVector(W + 0.1, 0.14, 0.2));
	// A tatami per lane in alternating tones, bound in cloth on all four edges.
	for (int32 Lane = 0; Lane < Lanes; ++Lane)
	{
		const double X = (Lane - (Lanes - 1) / 2.0) * LaneSpacing;
		Box(Tatami[Lane % 2], FVector(X, 0.0, TatamiZ), FVector(LaneSpacing - 0.28, 0.04, 7.3));
		Box(Binding[Lane % 2], FVector(X - 1.66, 0.022, TatamiZ), FVector(0.07, 0.012, 7.3), 0.0, false);
		Box(Binding[Lane % 2], FVector(X + 1.66, 0.022, TatamiZ), FVector(0.07, 0.012, 7.3), 0.0, false);
		Box(Binding[Lane % 2], FVector(X, 0.022, TatamiZ - 3.61), FVector(LaneSpacing - 0.28, 0.012, 0.07), 0.0, false);
		Box(Binding[Lane % 2], FVector(X, 0.022, TatamiZ + 3.61), FVector(LaneSpacing - 0.28, 0.012, 0.07), 0.0, false);
	}
}

void ASamuraiDojo::BuildTorii()
{
	UMaterialInterface* Vermilion = Materials.Plain(this, Hex(TEXT("#d23c2a")), 0.42);
	UMaterialInterface* Lacquer = Materials.Plain(this, Hex(TEXT("#16120f")), 0.3, 0.1);
	// The pillars stand just outside the outer lanes.
	const double Half = Lanes * (LaneSpacing / 2.0) + 0.55;
	for (const double Side : { -1.0, 1.0 })
	{
		Post(Vermilion, FVector(Side * Half, 0.0, 0.0), 0.17, 4.2);
		Post(Lacquer, FVector(Side * Half, 0.0, 0.0), 0.2, 0.32);
	}
	// Shimaki and nuki (the beams), the kasagi on top with its upswept ends, the tablet between.
	Box(Vermilion, FVector(0.0, 4.02, 0.0), FVector(Half * 2.0 + 0.9, 0.24, 0.42));
	Box(Vermilion, FVector(0.0, 3.3, 0.0), FVector((Half * 2.0 + 0.9) * 0.88, 0.2, 0.32));
	UStaticMeshComponent* Kasagi = Part(KasagiMesh, FTransform(FRotator::ZeroRotator, ToWorld(FVector(0.0, 4.36, 0.0)), FVector(1.0, (Half + 0.85), 1.0)));
	Kasagi->SetMaterial(0, Lacquer);
	Box(Vermilion, FVector(0.0, 3.66, 0.0), FVector(0.22, 0.52, 0.26));
	Box(Lacquer, FVector(0.0, 3.68, 0.14), FVector(0.5, 0.42, 0.04));
}

void ASamuraiDojo::BuildWall()
{
	UMaterialInterface* Cedar = Materials.Surface(this, { Hex(TEXT("#6e4428")), TEXT("dark_wooden_planks"), 0.9, 0.45, 0.6, 0.4, 0.6 });
	UMaterialInterface* Paper = Materials.Plain(this, Hex(TEXT("#f3e6c8")), 0.9, 0.0, 0.18);
	UMaterialInterface* Kumiko = Materials.Plain(this, Hex(TEXT("#3b2414")), 0.6);
	const double W = StageWidth();
	// The beam over the wall, and posts every 2.4 m that leave the middle open onto the garden.
	Box(Cedar, FVector(0.0, BeamY, WallZ), FVector(W + 1.0, 0.34, 0.36));
	Box(Cedar, FVector(0.0, 0.25, WallZ), FVector(W + 1.0, 0.18, 0.3));
	const int32 Bays = FMath::Max(2, FMath::RoundToInt32(W / 2.4) / 2) * 2;
	const double Bay = W / Bays;
	for (int32 i = 0; i <= Bays; ++i)
	{
		const double X = -W / 2.0 + i * Bay;
		if (FMath::Abs(X) < Bay * 0.6)
		{
			continue;
		}
		Box(Cedar, FVector(X, 2.75, WallZ), FVector(0.2, 5.5, 0.22));
	}
	// Shoji between the posts (not across the open middle): glowing paper behind a kumiko lattice.
	for (int32 i = 0; i < Bays; ++i)
	{
		const double X = -W / 2.0 + (i + 0.5) * Bay;
		if (FMath::Abs(X) < Bay)
		{
			continue;
		}
		Box(Paper, FVector(X, 2.3, WallZ + 0.05), FVector(Bay - 0.24, 3.6, 0.02), 0.0, false);
		for (int32 r = 0; r <= 6; ++r)
		{
			Box(Kumiko, FVector(X, 0.5 + r * 0.6, WallZ + 0.08), FVector(Bay - 0.24, 0.035, 0.03), 0.0, false);
		}
		for (int32 c = 1; c < 3; ++c)
		{
			Box(Kumiko, FVector(X - (Bay - 0.24) / 2.0 + c * (Bay - 0.24) / 3.0, 2.3, WallZ + 0.08), FVector(0.035, 3.6, 0.03), 0.0, false);
		}
	}
}

void ASamuraiDojo::BuildGarden()
{
	UMaterialInterface* Stone = Materials.Surface(this, { Hex(TEXT("#7d7a72")), TEXT("rock_face"), 0.6, 0.6, 0.9, 0.45, 0.9 });
	const double W = StageWidth();
	for (const double Side : { -1.0, 1.0 })
	{
		// Stepping stones through the gravel, both sides of the open middle.
		for (int32 i = 0; i < 3; ++i)
		{
			UStaticMeshComponent* Step = Part(StoneMesh, FTransform(FRotator(0.0, i * 37.0, 0.0), ToWorld(FVector(Side * (1.6 + i * 0.18), -0.03, -5.2 - i * 2.1)), FVector(1.0 - i * 0.08)));
			Step->SetMaterial(0, Stone);
		}
		BuildStoneLantern(FVector(Side * 2.35, -0.13, -7.4));
		BuildBamboo(Side);
		// Garden pines behind the wall.
		for (int32 i = 0; i < 2; ++i)
		{
			UStaticMeshComponent* Pine = Part(PineMesh, FTransform(FRotator(0.0, i * 70.0 + (Side > 0 ? 20.0 : 0.0), 0.0), ToWorld(FVector(Side * (W * 0.37 + i * 2.2), -0.13, -5.2 - i * 3.0)),
				FVector(1.15 - i * 0.2)));
			Pine->SetMaterial(0, Materials.Plain(this, Hex(TEXT("#4a3426")), 0.85));
			Pine->SetMaterial(1, Materials.Plain(this, Hex(TEXT("#2f4a32")), 0.85));
		}
	}
}

void ASamuraiDojo::BuildStoneLantern(const FVector& T)
{
	// A stone lantern (toro): base, shaft, the lit fire box, the roof and its jewel.
	UMaterialInterface* Stone = Materials.Surface(this, { Hex(TEXT("#7d7a72")), TEXT("rock_face"), 0.6, 0.6, 0.9, 0.45, 0.9 });
	UMaterialInterface* StoneDark = Materials.Surface(this, { Hex(TEXT("#5d5a54")), TEXT("rock_face"), 0.5, 0.6, 0.9, 0.45, 0.9 });
	// The fire box's window is solid and never fades: opaque unlit, no translucency pass.
	UMaterialInterface* Fire = Materials.GlowOpaque(this, Hex(TEXT("#ffb35c")) * FireGlow);
	Post(StoneDark, T, 0.42, 0.22);
	Post(Stone, T + FVector(0.0, 0.22, 0.0), 0.16, 0.9);
	Box(Stone, T + FVector(0.0, 1.2, 0.0), FVector(0.66, 0.12, 0.66));
	Box(Stone, T + FVector(0.0, 1.5, 0.0), FVector(0.5, 0.46, 0.5));
	Box(Fire, T + FVector(0.0, 1.5, 0.255), FVector(0.24, 0.26, 0.01), 0.0, false);
	Box(StoneDark, T + FVector(0.0, 1.82, 0.0), FVector(0.92, 0.14, 0.92), UE_DOUBLE_PI / 4.0);
	Box(StoneDark, T + FVector(0.0, 1.94, 0.0), FVector(0.5, 0.12, 0.5), UE_DOUBLE_PI / 4.0);
	Post(Stone, T + FVector(0.0, 2.0, 0.0), 0.08, 0.16);
	UPointLightComponent* Light = AddDojoComponent<UPointLightComponent>(TEXT("ToroLight"), nullptr, EComponentMobility::Movable);
	Light->SetRelativeLocation(ToWorld(T + FVector(0.0, 1.5, 0.4)));
	Light->SetIntensityUnits(ELightUnits::Lumens);
	Light->SetIntensity(ToroLumens);
	Light->SetAttenuationRadius(ToroRadius);
	Light->SetLightColor(Hex(TEXT("#ffb35c")));
	Light->SetCastShadows(false);
	Light->RegisterComponent();
}

void ASamuraiDojo::BuildBamboo(double Side)
{
	// Bamboo at the stage's edges, in front of the wall: five canes of three heights, nodes and a few leaves.
	UMaterialInterface* Cane = Materials.Plain(this, Hex(TEXT("#5fa357")), 0.45);
	UMaterialInterface* Node = Materials.Plain(this, Hex(TEXT("#3f7a3a")), 0.5);
	UMaterialInterface* Leaf = Materials.Plain(this, Hex(TEXT("#4b8f44")), 0.6);
	const double EdgeX = Side * (StageWidth() / 2.0 - 0.25);
	for (int32 i = 0; i < 5; ++i)
	{
		const double X = EdgeX - Side * i * 0.16;
		const double H = 3.2 + (i % 3) * 0.6;
		const double Z = -1.75 + (i % 2) * 0.22;
		Post(Cane, FVector(X, 0.0, Z), 0.055, H);
		for (double Y = 0.7; Y < H; Y += 0.72)
		{
			Post(Node, FVector(X, Y, Z), 0.064, 0.04, false);
		}
		for (int32 k = 0; k < 3; ++k)
		{
			const double Y = H - 0.3 - k * 0.5;
			Box(Leaf, FVector(X - Side * 0.25, Y, Z + 0.05), FVector(0.5, 0.025, 0.09), Side * (0.5 + k * 0.4), false);
		}
	}
}

void ASamuraiDojo::BuildDistance()
{
	// Ridges fading into the haze: dusky blue to violet, each farther and taller.
	const TCHAR* const Colors[] = { TEXT("#3a4a5e"), TEXT("#4d5873"), TEXT("#6a6a88") };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Colors); ++i)
	{
		const double Z = -32.0 - i * 18.0;
		UStaticMeshComponent* Ridge = Part(RidgeMesh, FTransform(FRotator(0.0, 0.0, 0.0), ToWorld(FVector(i * 9.0 - 9.0, -0.5, Z)), FVector(1.0, 1.0 + i * 0.25, 0.75 + i * 0.35)), false);
		Ridge->SetMaterial(0, Materials.Plain(this, Hex(Colors[i]), 0.95));
	}
}

void ASamuraiDojo::BuildLanterns()
{
	// Paper lanterns hung on cords from the beam, each on a pivot so it can sway; they glow and light the stage.
	UMaterialInterface* Paper = Materials.Plain(this, Hex(TEXT("#e23d33")), 0.8, 0.0, 0.9);
	UMaterialInterface* Cap = Materials.Plain(this, Hex(TEXT("#141110")), 0.4);
	UMaterialInterface* Cord = Materials.Plain(this, Hex(TEXT("#141110")), 0.8);
	const double W = StageWidth();
	const double Xs[] = { -W * 0.3, 0.0, W * 0.3 };
	for (const double X : Xs)
	{
		USceneComponent* Pivot = AddDojoComponent<USceneComponent>(TEXT("LanternPivot"), nullptr, EComponentMobility::Movable);
		Pivot->SetRelativeLocation(ToWorld(FVector(X, BeamY, LanternZ)));
		Pivot->RegisterComponent();
		Lanterns.Add(Pivot);
		UStaticMeshComponent* Lantern = AddDojoComponent<UStaticMeshComponent>(TEXT("Lantern"), Pivot, EComponentMobility::Movable);
		Lantern->SetStaticMesh(LanternMesh);
		Lantern->SetMaterial(0, Paper);
		Lantern->SetMaterial(1, Cap);
		Lantern->SetRelativeLocation(FVector(0.0, 0.0, -(BeamY - LanternY) * 100.0));
		Lantern->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// Swaying casters would invalidate their shadow pages every frame.
		Lantern->SetCastShadow(false);
		Lantern->RegisterComponent();
		UStaticMeshComponent* Line = AddDojoComponent<UStaticMeshComponent>(TEXT("LanternCord"), Pivot, EComponentMobility::Movable);
		Line->SetStaticMesh(Cylinder);
		Line->SetMaterial(0, Cord);
		Line->SetRelativeLocation(FVector(0.0, 0.0, -25.0));
		Line->SetRelativeScale3D(FVector(0.015, 0.015, 0.5));
		Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Line->SetCastShadow(false);
		Line->RegisterComponent();
		UPointLightComponent* Glow = AddDojoComponent<UPointLightComponent>(TEXT("LanternLight"), Lantern, EComponentMobility::Movable);
		Glow->SetIntensityUnits(ELightUnits::Lumens);
		Glow->SetIntensity(LanternLumens);
		Glow->SetAttenuationRadius(LanternRadius);
		Glow->SetLightColor(Hex(TEXT("#ff9a5a")));
		Glow->SetCastShadows(false);
		Glow->RegisterComponent();
	}
}

// ---- Motion -----------------------------------------------------------------------------------------------------

void ASamuraiDojo::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBuildPending)
	{
		Build();
	}
	Clock += DeltaSeconds;
	// The lanterns sway on their cords, each on its own beat.
	for (int32 i = 0; i < Lanterns.Num(); ++i)
	{
		if (Lanterns[i])
		{
			const double Swing = FMath::Sin(Clock * SwayRate + i * SwayPhase) * SwayDegrees + FMath::Sin(Clock * FlutterRate + i) * FlutterDegrees;
			Lanterns[i]->SetRelativeRotation(FRotator(0.0, 0.0, Swing));
		}
	}
	DriftPetals(DeltaSeconds);
}

void ASamuraiDojo::DriftPetals(float DeltaSeconds)
{
	// Cherry petals drift down through the sunlight on the breeze, some close past the camera.
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	if (!Fx)
	{
		return;
	}
	PetalCarry += DeltaSeconds * PetalsPerSecond;
	PetalScratch.Reset();
	while (PetalCarry >= 1.0)
	{
		PetalCarry -= 1.0;
		const double W = StageWidth();
		PetalScratch.Add(Petal(FVector(PetalRng.FRandRange(-W * 0.65, W * 0.65), PetalRng.FRandRange(3.8, 6.4), PetalRng.FRandRange(-3.5, 5.5)), 1, DriftSpeed));
	}
	if (PetalScratch.Num() > 0)
	{
		Fx->Emit(PetalScratch);
	}
}

MovecadeKart::Fx::FKartFxEmit ASamuraiDojo::Petal(const FVector& Web, int32 Count, double Speed)
{
	static const FLinearColor Tones[] = { Hex(TEXT("#f7a8c0")), Hex(TEXT("#fbd0dc")), Hex(TEXT("#ff8fb1")) };
	static const FLinearColor Pale = Hex(TEXT("#fff0f4"));
	MovecadeKart::Fx::FKartFxEmit E;
	E.Field = MovecadeKart::Fx::EField::Confetti;
	E.Position = ToWorld(Web);
	E.Count = Count;
	E.Direction = ToWorld(FVector(-0.6, -0.25, 0.35)).GetSafeNormal();
	E.Spread = Count > 1 ? 1.4 : 0.6;
	// Speeds are given for the drift's 0.6 m/s and scaled to the asked speed.
	E.Speed = FVector2D(0.3, 0.8) * Speed / DriftSpeed;
	E.Life = FVector2D(3.5, 5.5);
	E.Size = FVector2D(0.05, 0.075);
	E.SizeEnd = 0.85;
	E.Color = Tones[PetalRng.RandRange(0, UE_ARRAY_COUNT(Tones) - 1)];
	E.ColorEnd = Pale;
	E.Intensity = 1.25;
	E.Jitter = Count > 1 ? 0.4 : 0.0;
	return E;
}

void ASamuraiDojo::PetalBurst(const FVector& Web, int32 Count)
{
	if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this))
	{
		Fx->Emit(Petal(Web, Count, BurstSpeed));
	}
}
