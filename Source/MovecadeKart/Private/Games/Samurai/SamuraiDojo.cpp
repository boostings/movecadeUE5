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

	constexpr int32 MaxLanes = 4;
	/** The sky dome's scale (the engine sphere is 1 m across): the painted sky sits 10 km out. */
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

	// ---- Colours (sRGB hex, as the web's samuraiStage.tsx / SamuraiSky.tsx) ----
	namespace Palette
	{
		const TCHAR* const SunLight = TEXT("#ffb46e");
		const FColor SunShaftTint(255, 196, 140);
		const TCHAR* const RimLight = TEXT("#f49aa0");
		const TCHAR* const SkyLightTone = TEXT("#a7a6e0");
		const TCHAR* const GroundBounce = TEXT("#6b4a3c");
		const TCHAR* const SkyTop = TEXT("#2c2f6e");
		const TCHAR* const SkyMid = TEXT("#b4587c");
		const TCHAR* const SkyHorizon = TEXT("#ffa25a");
		const TCHAR* const SkyBelow = TEXT("#5a3a3a");
		const TCHAR* const SkySun = TEXT("#fff0c8");
		const TCHAR* const CloudLit = TEXT("#ffc08a");
		const TCHAR* const CloudShade = TEXT("#7a4a78");
		const FColor VolumetricAlbedo(255, 214, 180);
		const TCHAR* const PetalTones[] = { TEXT("#f7a8c0"), TEXT("#fbd0dc"), TEXT("#ff8fb1") };
		const TCHAR* const PetalFade = TEXT("#fff0f4");
	}

	/** The mood's grade: each moody value is base + per-Focus (+ per-Flash or per-Fringe) times the mood. */
	namespace GradeLook
	{
		constexpr float ExposureBias = 0.15f;
		constexpr double Saturation = 1.1;
		constexpr double SaturationFocus = 0.2;
		constexpr double Contrast = 1.08;
		constexpr double ContrastFocus = 0.05;
		/** Warm highlights (R, G, B gains), warmer under Focus. */
		constexpr double GainR = 1.06, GainRFocus = 0.06;
		constexpr double GainG = 1.0, GainGFocus = 0.02;
		constexpr double GainB = 0.9, GainBFocus = 0.05;
		/** Violet shadows. */
		const FVector4 ShadowOffset(0.012, 0.0, 0.03, 0.0);
		const FVector4 ShadowGamma(1.0, 0.98, 1.04, 1.0);
		constexpr float Bloom = 0.85f;
		constexpr float BloomFocus = 0.5f;
		constexpr float BloomFlash = 1.6f;
		/** -1: no threshold (physically based bloom), so every glow blooms by its brightness. */
		constexpr float BloomThreshold = -1.f;
		constexpr float Vignette = 0.42f;
		constexpr float VignetteFocus = 0.38f;
		constexpr float Fringe = 0.4f;
		constexpr float FringeImpact = 3.5f;
		/** A flash tints toward its colour (overdriven), and Focus warms the whole frame. */
		constexpr float FlashTintGain = 2.2f;
		constexpr float FlashTintShare = 0.5f;
		const FLinearColor FocusTint(0.07f, 0.04f, -0.05f, 0.f);
		constexpr float FilmGrain = 0.08f;
	}

	/** The drifting petals (FKartFxEmit recipe), their spawn volume over the stage, and the burst's spread. */
	namespace PetalRecipe
	{
		constexpr int32 RngSeed = 0x9e7a1;
		constexpr double SpawnWidthShare = 0.65;
		constexpr double SpawnMinY = 3.8, SpawnMaxY = 6.4;
		constexpr double SpawnMinZ = -3.5, SpawnMaxZ = 5.5;
		const FVector Breeze(-0.6, -0.25, 0.35);
		constexpr double DriftSpread = 0.6;
		constexpr double BurstSpread = 1.4;
		constexpr double BurstJitter = 0.4;
		const FVector2D Speed(0.3, 0.8);
		const FVector2D Life(3.5, 5.5);
		const FVector2D Size(0.05, 0.075);
		constexpr double SizeEnd = 0.85;
		constexpr double Intensity = 1.25;
	}

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
	PetalRng.Initialize(PetalRecipe::RngSeed);
}

FVector ASamuraiDojo::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
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

FName ASamuraiDojo::DojoTag()
{
	static const FName Tag(TEXT("SamuraiDojo"));
	return Tag;
}

void ASamuraiDojo::Clear()
{
	TArray<UActorComponent*> Old = GetComponentsByTag(UActorComponent::StaticClass(), DojoTag());
	for (UActorComponent* Component : Old)
	{
		RemoveInstanceComponent(Component);
		Component->DestroyComponent();
	}
	Groups.Reset();
	Lanterns.Reset();
}

void ASamuraiDojo::Build()
{
	bBuildPending = false;
	bBuilt = true;
	Clear();
	Groups.Init(this, TEXT("DojoGroup"), DojoTag(), EComponentMobility::Static);
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
	Sun->SetLightColor(Hex(Palette::SunLight));
	Sun->SetIntensity(SunIntensity);
	Sun->SetCastShadows(true);
	Sun->SetAtmosphereSunLight(false);
	Sun->SetVolumetricScatteringIntensity(SunVolumetricScattering);
	Sun->bEnableLightShaftBloom = true;
	Sun->BloomScale = SunShaftBloomScale;
	Sun->BloomThreshold = SunShaftBloomThreshold;
	Sun->BloomMaxBrightness = SunShaftBloomMax;
	Sun->BloomTint = Palette::SunShaftTint;
	Sun->LightSourceAngle = SunSourceAngle;
	Sun->MarkRenderStateDirty();
	Rim->SetWorldRotation((-DirToUE(RimFrom).GetSafeNormal()).Rotation());
	Rim->SetLightColor(Hex(Palette::RimLight));
	Rim->SetIntensity(RimIntensity);
	Rim->SetCastShadows(false);
	Rim->SetAtmosphereSunLight(false);
	Rim->SetVolumetricScatteringIntensity(0.f);
	// The sun is the one forward shading, translucency and fog use.
	Sun->SetForwardShadingPriority(1);
	Rim->SetForwardShadingPriority(0);
	SkyLight->SetIntensity(SkyLightIntensity);
	SkyLight->SetLightColor(FMath::Lerp(FLinearColor::White, Hex(Palette::SkyLightTone), SkyLightTint));
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->SetLowerHemisphereColor(Hex(Palette::GroundBounce) * LowerHemisphereScale);

	// A sunset that grades from indigo overhead through rose to a molten horizon, the sun low over the garden.
	const FLinearColor Top = Hex(Palette::SkyTop), Mid = Hex(Palette::SkyMid), Horizon = Hex(Palette::SkyHorizon);
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
	Sky->SetVectorParameterValue(TEXT("SkyBelow"), Hex(Palette::SkyBelow));
	const FVector SunDir = MovecadeKart::Coords::DirToUE(SkySun).GetSafeNormal();
	Sky->SetVectorParameterValue(TEXT("SunDir"), FLinearColor(SunDir.X, SunDir.Y, SunDir.Z, 0.0));
	Sky->SetVectorParameterValue(TEXT("SunColor"), Hex(Palette::SkySun) * SkySunGlow);
	Sky->SetScalarParameterValue(TEXT("SunCos"), FMath::Cos(SkySunAngle));
	Sky->SetScalarParameterValue(TEXT("SunGlow"), SkySunHalo);
	Sky->SetScalarParameterValue(TEXT("GlowPower"), SkyGlowPower);
	Sky->SetScalarParameterValue(TEXT("HorizonGlow"), SkyHorizonGlow);
	Sky->SetScalarParameterValue(TEXT("Brightness"), 1.0);
	if (UTexture* Clouds = LoadObject<UTexture>(nullptr, TEXT("/Game/Tracks/Shared/Textures/env_clouds.env_clouds"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Sky->SetTextureParameterValue(TEXT("CloudMap"), Clouds);
	}
	else
	{
		UE_LOG(LogSamurai, Warning, TEXT("Dojo: env_clouds is missing; the sky has no clouds"));
	}
	Sky->SetScalarParameterValue(TEXT("Clouds"), SkyClouds);
	Sky->SetScalarParameterValue(TEXT("CloudScale"), SkyCloudScale);
	Sky->SetVectorParameterValue(TEXT("CloudLit"), Hex(Palette::CloudLit) * SkyCloudLit);
	Sky->SetVectorParameterValue(TEXT("CloudShade"), Hex(Palette::CloudShade));
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
	Fog->SetDirectionalInscatteringColor(Hex(Palette::SunLight) * FogSunInscatter);
	Fog->SetDirectionalInscatteringExponent(FogSunExponent);
	Fog->SetVolumetricFog(true);
	Fog->SetVolumetricFogScatteringDistribution(VolumetricScattering);
	Fog->SetVolumetricFogAlbedo(Palette::VolumetricAlbedo);
	Fog->SetVolumetricFogExtinctionScale(VolumetricExtinction);
	Fog->SetVolumetricFogDistance(VolumetricDistance);
	Fog->SetVolumetricFogStartDistance(0.f);
}

void ASamuraiDojo::ApplyGrade()
{
	// The mood's grade: warm highlights, violet shadows, a little more colour and contrast, bloom on the glows and
	// a soft vignette. Mood layers Focus (gold, saturated, a pulsing vignette) and impacts (a flash, fringing).
	FPostProcessSettings& S = Grade->Settings;
	S = FPostProcessSettings();
	S.bOverride_AutoExposureBias = true;
	using namespace GradeLook;
	S.AutoExposureBias = ExposureBias;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.0, 1.0, 1.0, Saturation + SaturationFocus * MoodFocus);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.0, 1.0, 1.0, Contrast + ContrastFocus * MoodFocus);
	S.bOverride_ColorGainHighlights = true;
	S.ColorGainHighlights = FVector4(GainR + GainRFocus * MoodFocus, GainG + GainGFocus * MoodFocus, GainB - GainBFocus * MoodFocus, 1.0);
	S.bOverride_ColorOffsetShadows = true;
	S.ColorOffsetShadows = ShadowOffset;
	S.bOverride_ColorGammaShadows = true;
	S.ColorGammaShadows = ShadowGamma;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Bloom + BloomFocus * MoodFocus + BloomFlash * MoodFlash;
	S.bOverride_BloomThreshold = true;
	S.BloomThreshold = GradeLook::BloomThreshold;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = Vignette + VignetteFocus * MoodFocus;
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = Fringe + FringeImpact * MoodFringe;
	S.bOverride_SceneColorTint = true;
	S.SceneColorTint = FMath::Lerp(FLinearColor::White, MoodFlashColor * FlashTintGain, FlashTintShare * MoodFlash) + FocusTint * MoodFocus;
	S.SceneColorTint.A = 1.f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = FilmGrain;
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
		PetalScratch.Add(Petal(FVector(PetalRng.FRandRange(-W * PetalRecipe::SpawnWidthShare, W * PetalRecipe::SpawnWidthShare), PetalRng.FRandRange(PetalRecipe::SpawnMinY, PetalRecipe::SpawnMaxY),
			PetalRng.FRandRange(PetalRecipe::SpawnMinZ, PetalRecipe::SpawnMaxZ)), 1, DriftSpeed));
	}
	if (PetalScratch.Num() > 0)
	{
		Fx->Emit(PetalScratch);
	}
}

MovecadeKart::Fx::FKartFxEmit ASamuraiDojo::Petal(const FVector& Web, int32 Count, double Speed)
{
	static const FLinearColor Tones[] = { Hex(Palette::PetalTones[0]), Hex(Palette::PetalTones[1]), Hex(Palette::PetalTones[2]) };
	static const FLinearColor Pale = Hex(Palette::PetalFade);
	MovecadeKart::Fx::FKartFxEmit E;
	E.Field = MovecadeKart::Fx::EField::Confetti;
	E.Position = ToWorld(Web);
	E.Count = Count;
	E.Direction = ToWorld(PetalRecipe::Breeze).GetSafeNormal();
	E.Spread = Count > 1 ? PetalRecipe::BurstSpread : PetalRecipe::DriftSpread;
	// Speeds are given for the drift's 0.6 m/s and scaled to the asked speed.
	E.Speed = PetalRecipe::Speed * Speed / DriftSpeed;
	E.Life = PetalRecipe::Life;
	E.Size = PetalRecipe::Size;
	E.SizeEnd = PetalRecipe::SizeEnd;
	E.Color = Tones[PetalRng.RandRange(0, UE_ARRAY_COUNT(Tones) - 1)];
	E.ColorEnd = Pale;
	E.Intensity = PetalRecipe::Intensity;
	E.Jitter = Count > 1 ? PetalRecipe::BurstJitter : 0.0;
	return E;
}

void ASamuraiDojo::PetalBurst(const FVector& Web, int32 Count)
{
	if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this))
	{
		Fx->Emit(Petal(Web, Count, BurstSpeed));
	}
}
