#pragma once

#include "CoreMinimal.h"
#include "Engine/PostProcessVolume.h"
#include "KartGradeVolume.generated.h"

class UDirectionalLightComponent;
class UKartGameUserSettings;

/**
 * The project-wide look: an unbound, low-priority post-process volume that UKartGradeSubsystem spawns in every
 * game world that doesn't already contain one. Per-track volumes (default priority 0) layer on top and win on any
 * setting they override, so a level's own lighting and grade still apply.
 *
 * Art direction: clean, saturated, soft and inviting, with realistic lighting under it. So: a filmic tonemapper
 * with lifted toes, gentle bloom, slightly cool shadows and warm highlights, a mild vignette, Lumen skylight
 * leaking for soft shadow fill, and motion blur that only fades in at speed.
 *
 * Exposure is histogram auto exposure clamped to a narrow window (ExposureWindow) centred on what the level's sun
 * calls for. The centre follows the sun's illuminance (one stop per doubling), so the grade stays right whether a
 * track uses the default 10 lux sun or physical sunlight, and the camera never visibly "pumps" between sky and road.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Kart Grade Volume"))
class MOVECADEKART_API AKartGradeVolume : public APostProcessVolume
{
	GENERATED_BODY()

public:
	AKartGradeVolume(const FObjectInitializer& ObjectInitializer);

	/** Sun illuminance (lux) at which ReferenceEV100 is the right exposure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Exposure", meta = (ClampMin = "0.01"))
	float ReferenceSunLux = 10.f;

	/** Exposure (EV100) that suits a scene lit by a ReferenceSunLux sun and its sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Exposure")
	float ReferenceEV100 = 1.f;

	/** Without a directional light, the exposure window is centred here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Exposure")
	float FallbackEV100 = 1.f;

	/** Auto exposure may adapt this many stops either side of the sun-derived centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Exposure", meta = (ClampMin = "0"))
	float ExposureWindow = 0.6f;

	/** Exposure compensation (stops): brighter than a neutral meter, for the bright, inviting look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Exposure")
	float ExposureCompensation = 1.f;

	/** Motion blur strength at and above BlurFullSpeed (UE motion blur amount, 0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Motion Blur", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxMotionBlur = 0.15f;

	/** Camera speed (cm/s) below which there's no motion blur at all. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Motion Blur")
	float BlurStartSpeed = 2200.f;

	/** Camera speed (cm/s) at which motion blur reaches MaxMotionBlur. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart Grade|Motion Blur")
	float BlurFullSpeed = 4200.f;

	/** The exposure window's current centre (EV100), for debugging. */
	float GetExposureCentre() const { return ExposureCentre; }

	//~ AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Re-centres the exposure window on the brightest sun in the world. */
	void UpdateExposure();
	/** Fades motion blur in with the fastest local camera. */
	void UpdateMotionBlur(float DeltaSeconds);
	void HandleGraphicsApplied(const UKartGameUserSettings& Settings);
	const UDirectionalLightComponent* FindSun() const;

	FDelegateHandle SettingsHandle;
	TArray<FVector> LastCameraLocations;
	float SmoothedSpeed = 0.f;
	float ExposureCentre = 0.f;
	float ExposureTimer = 0.f;
	bool bMotionBlurAllowed = true;
};
