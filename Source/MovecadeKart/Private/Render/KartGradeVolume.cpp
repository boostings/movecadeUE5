#include "Render/KartGradeVolume.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Render/KartGameUserSettings.h"
#include "UObject/UObjectIterator.h"

namespace
{
	TAutoConsoleVariable<float> CVarExposureBias(
		TEXT("kart.Grade.ExposureBias"), 0.f,
		TEXT("Extra exposure compensation (stops) on top of the grade's sun-derived exposure, for look development."));

	TAutoConsoleVariable<bool> CVarGradeEnabled(
		TEXT("kart.Grade.Enable"), true,
		TEXT("Blend the project-wide grade volume in (1) or out (0), for before/after comparisons."));
}

AKartGradeVolume::AKartGradeVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	// After the cameras have moved this frame, so the speed estimate matches what's about to be rendered.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	bUnbound = true;
	BlendWeight = 1.f;
	// Below a level's own volumes (priority 0 by default), which then win wherever they override something.
	Priority = -100.f;

	FPostProcessSettings& S = Settings;

	// Exposure: histogram metering, but only within ExposureWindow stops of the sun-derived centre (UpdateExposure).
	// The highlight percentile ignores the brightest sky so turning towards the sun doesn't darken the track.
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Histogram;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = ExposureCompensation;
	S.bOverride_AutoExposureMinBrightness = true;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMinBrightness = ReferenceEV100 - ExposureWindow;
	S.AutoExposureMaxBrightness = ReferenceEV100 + ExposureWindow;
	S.bOverride_AutoExposureLowPercent = true;
	S.AutoExposureLowPercent = 50.f;
	S.bOverride_AutoExposureHighPercent = true;
	S.AutoExposureHighPercent = 85.f;
	S.bOverride_AutoExposureSpeedUp = true;
	S.AutoExposureSpeedUp = 1.5f;
	S.bOverride_AutoExposureSpeedDown = true;
	S.AutoExposureSpeedDown = 1.f;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = 0;

	// Tonemapper: the filmic ACES-style curve, with a softer toe so shadows stay readable and coloured, and a
	// slightly longer shoulder so bright paint and sky keep their hue instead of clipping to white.
	S.bOverride_FilmSlope = true;
	S.FilmSlope = 0.86f;
	S.bOverride_FilmToe = true;
	S.FilmToe = 0.48f;
	S.bOverride_FilmShoulder = true;
	S.FilmShoulder = 0.3f;
	S.bOverride_FilmBlackClip = true;
	S.FilmBlackClip = 0.f;
	S.bOverride_FilmWhiteClip = true;
	S.FilmWhiteClip = 0.04f;

	// Local exposure compresses the sun/shade contrast a little: soft, toy-like lighting.
	S.bOverride_LocalExposureHighlightContrastScale = true;
	S.LocalExposureHighlightContrastScale = 0.8f;
	S.bOverride_LocalExposureShadowContrastScale = true;
	S.LocalExposureShadowContrastScale = 0.75f;

	// Grade: saturated and clean. White balance a touch cool so asphalt reads grey and skies blue under warm suns,
	// cool-violet shadows, warm highlights.
	S.bOverride_WhiteTemp = true;
	S.WhiteTemp = 6200.f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.18f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.08f);
	S.bOverride_ColorSaturationShadows = true;
	S.ColorSaturationShadows = FVector4(1.f, 1.f, 1.f, 1.08f);
	S.bOverride_ColorGainShadows = true;
	S.ColorGainShadows = FVector4(0.97f, 0.98f, 1.05f, 1.f);
	S.bOverride_ColorGainHighlights = true;
	S.ColorGainHighlights = FVector4(1.03f, 1.01f, 0.97f, 1.f);

	// Lumen: let a little skylight into occluded areas, so shadows are soft and coloured rather than black.
	S.bOverride_LumenSkylightLeaking = true;
	S.LumenSkylightLeaking = 0.12f;

	// Screen-space AO only shows when Lumen is off (Performance on non-RT hardware); keep it soft there too.
	S.bOverride_AmbientOcclusionIntensity = true;
	S.AmbientOcclusionIntensity = 0.4f;
	S.bOverride_AmbientOcclusionRadius = true;
	S.AmbientOcclusionRadius = 120.f;

	// Lens: gentle bloom, mild vignette, nothing that reads as a dirty or cheap lens.
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.45f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.25f;
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.f;
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.f;

	// Motion blur: off at rest; UpdateMotionBlur fades it in with camera speed. Capped short, ignore small objects.
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.f;
	S.bOverride_MotionBlurMax = true;
	S.MotionBlurMax = 0.6f;
	S.bOverride_MotionBlurTargetFPS = true;
	S.MotionBlurTargetFPS = 30;
	S.bOverride_MotionBlurPerObjectSize = true;
	S.MotionBlurPerObjectSize = 0.f;
}

void AKartGradeVolume::BeginPlay()
{
	Super::BeginPlay();
	if (UKartGameUserSettings* UserSettings = UKartGameUserSettings::Get())
	{
		bMotionBlurAllowed = UserSettings->IsMotionBlurEnabled();
		SettingsHandle = UserSettings->OnGraphicsApplied.AddUObject(this, &AKartGradeVolume::HandleGraphicsApplied);
	}
	UpdateExposure();
}

void AKartGradeVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UKartGameUserSettings* UserSettings = UKartGameUserSettings::Get())
	{
		UserSettings->OnGraphicsApplied.Remove(SettingsHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AKartGradeVolume::HandleGraphicsApplied(const UKartGameUserSettings& UserSettings)
{
	bMotionBlurAllowed = UserSettings.IsMotionBlurEnabled();
}

void AKartGradeVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BlendWeight = CVarGradeEnabled.GetValueOnGameThread() ? 1.f : 0.f;
	Settings.AutoExposureBias = ExposureCompensation + CVarExposureBias.GetValueOnGameThread();
	UpdateMotionBlur(DeltaSeconds);

	// Suns change rarely (a level script, a time-of-day tweak); once a second is plenty.
	ExposureTimer += DeltaSeconds;
	if (ExposureTimer >= 1.f)
	{
		ExposureTimer = 0.f;
		UpdateExposure();
	}
}

const UDirectionalLightComponent* AKartGradeVolume::FindSun() const
{
	const UWorld* World = GetWorld();
	const UDirectionalLightComponent* Best = nullptr;
	for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
	{
		const UDirectionalLightComponent* Light = *It;
		if (Light->GetWorld() != World || !Light->IsRegistered() || !Light->IsVisible() || !Light->bAffectsWorld)
		{
			continue;
		}
		// The atmosphere's sun is the key light; otherwise the brightest one.
		const bool bBetter = !Best
			|| (Light->IsUsedAsAtmosphereSunLight() && !Best->IsUsedAsAtmosphereSunLight())
			|| (Light->IsUsedAsAtmosphereSunLight() == Best->IsUsedAsAtmosphereSunLight() && Light->Intensity > Best->Intensity);
		if (bBetter)
		{
			Best = Light;
		}
	}
	return Best;
}

void AKartGradeVolume::UpdateExposure()
{
	// One stop per doubling of the sun: a 10 lux sun and a 100 000 lux sun need exposures ~13 stops apart, and
	// the sky (lit by the same sun through the atmosphere) scales with it.
	const UDirectionalLightComponent* Sun = FindSun();
	ExposureCentre = (Sun && Sun->Intensity > 0.f)
		? ReferenceEV100 + FMath::Log2(Sun->Intensity / FMath::Max(ReferenceSunLux, 0.01f))
		: FallbackEV100;
	Settings.AutoExposureMinBrightness = ExposureCentre - ExposureWindow;
	Settings.AutoExposureMaxBrightness = ExposureCentre + ExposureWindow;
}

void AKartGradeVolume::UpdateMotionBlur(float DeltaSeconds)
{
	// The fastest local camera drives the blur (in split screen one shared volume serves every view).
	float Fastest = 0.f;
	int32 Index = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager)
		{
			continue;
		}
		const FVector Location = PC->PlayerCameraManager->GetCameraLocation();
		if (LastCameraLocations.IsValidIndex(Index) && DeltaSeconds > 0.f)
		{
			Fastest = FMath::Max(Fastest, static_cast<float>(FVector::Dist(Location, LastCameraLocations[Index])) / DeltaSeconds);
		}
		if (!LastCameraLocations.IsValidIndex(Index))
		{
			LastCameraLocations.Add(Location);
		}
		LastCameraLocations[Index++] = Location;
	}

	// Smooth over ~0.25 s, and treat teleports (respawns, camera cuts) as standing still.
	const float Measured = Fastest > BlurFullSpeed * 4.f ? 0.f : Fastest;
	SmoothedSpeed = FMath::FInterpTo(SmoothedSpeed, Measured, DeltaSeconds, 4.f);

	const float Ramp = FMath::SmoothStep(BlurStartSpeed, BlurFullSpeed, SmoothedSpeed);
	Settings.MotionBlurAmount = bMotionBlurAllowed ? MaxMotionBlur * Ramp : 0.f;
}
