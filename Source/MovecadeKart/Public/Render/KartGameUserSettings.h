#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "KartGameUserSettings.generated.h"

/** The four graphics presets the settings screen offers. Ultra is the target look (RTX 4070 SUPER class, 1440p 60). */
UENUM(BlueprintType)
enum class EKartGraphicsPreset : uint8
{
	/** Medium scalability, software Lumen-class costs, 67 % TSR input. For integrated and older GPUs. */
	Performance,
	/** High scalability, 75 % TSR input. */
	Balanced,
	/** Epic scalability, 87 % TSR input. */
	Quality,
	/** Epic scalability, native-resolution TSR, ray-traced (hit-lit) reflections. The default. */
	Ultra,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnKartGraphicsApplied, const class UKartGameUserSettings& /*Settings*/);

/**
 * The game's user settings (GameUserSettings.ini), selected by GameUserSettingsClassName in DefaultEngine.ini.
 *
 * On top of UGameUserSettings (resolution, window mode, vsync, frame limit, per-group scalability) it adds:
 *   - a graphics preset that sets every scalability group and the TSR resolution at once,
 *   - a hardware ray tracing switch (Lumen HWRT GI and reflections; off falls back to software Lumen),
 *   - a motion blur switch.
 *
 * Usage from a settings screen (all setters are deferred, like the engine's: nothing changes until ApplySettings):
 *
 *     UKartGameUserSettings* Settings = UKartGameUserSettings::Get();
 *     Settings->SetGraphicsPreset(EKartGraphicsPreset::Quality);
 *     Settings->SetRayTracingEnabled(true);
 *     Settings->SetMotionBlurEnabled(false);
 *     Settings->ApplySettings(false);   // applies the cvars and saves GameUserSettings.ini
 *
 * The base class API (SetScreenResolution, SetFullscreenMode, SetVSyncEnabled, SetFrameRateLimit, the per-group
 * Set*Quality calls) keeps working; changing a single group after picking a preset is allowed, and
 * GetGraphicsPreset() still reports the last preset picked.
 *
 * Split screen and the frame-time guard (UKartRenderQualitySubsystem) never write these settings: they derive a
 * temporary budget from them, so what the player chose is what gets saved.
 */
UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class MOVECADEKART_API UKartGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	/** The active settings object, or null if the engine is configured with another GameUserSettings class. */
	UFUNCTION(BlueprintPure, Category = "Kart|Settings")
	static UKartGameUserSettings* Get();

	/** Picks a preset: every scalability group, the TSR resolution and dynamic resolution follow it. Deferred. */
	UFUNCTION(BlueprintCallable, Category = "Kart|Settings")
	void SetGraphicsPreset(EKartGraphicsPreset Preset);

	UFUNCTION(BlueprintPure, Category = "Kart|Settings")
	EKartGraphicsPreset GetGraphicsPreset() const { return GraphicsPreset; }

	/** Hardware ray tracing for Lumen GI and reflections. Off: software Lumen (distance fields). Deferred. */
	UFUNCTION(BlueprintCallable, Category = "Kart|Settings")
	void SetRayTracingEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Kart|Settings")
	bool IsRayTracingEnabled() const { return bRayTracingEnabled; }

	/** False when the GPU or RHI can't ray trace; the settings screen should grey the switch out. */
	UFUNCTION(BlueprintPure, Category = "Kart|Settings")
	static bool IsRayTracingSupported();

	/** Camera motion blur (subtle, speed-driven). Deferred. */
	UFUNCTION(BlueprintCallable, Category = "Kart|Settings")
	void SetMotionBlurEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Kart|Settings")
	bool IsMotionBlurEnabled() const { return bMotionBlurEnabled; }

	/** Fired after ApplyNonResolutionSettings has pushed the cvars; the render quality subsystem re-derives its budget. */
	FOnKartGraphicsApplied OnGraphicsApplied;

	//~ UGameUserSettings
	virtual void SetToDefaults() override;
	virtual void ValidateSettings() override;
	virtual void ApplyNonResolutionSettings() override;
	virtual bool IsDirty() const override;
	virtual void ResetToCurrentSettings() override;

protected:
	virtual bool IsVersionValid() override;
	virtual void UpdateVersion() override;

private:
	/** Pushes the kart-specific cvars (ray tracing, motion blur, dynamic resolution bounds). */
	void ApplyKartRenderSettings();

	UPROPERTY(config)
	EKartGraphicsPreset GraphicsPreset = EKartGraphicsPreset::Ultra;

	UPROPERTY(config)
	bool bRayTracingEnabled = true;

	UPROPERTY(config)
	bool bMotionBlurEnabled = true;

	/** Bumped when the meaning of the saved values changes; older files are reset to defaults. */
	UPROPERTY(config)
	int32 KartSettingsVersion = 0;

	/** What was last applied, so IsDirty() can tell the settings screen there's something to apply. */
	EKartGraphicsPreset AppliedPreset = EKartGraphicsPreset::Ultra;
	bool bAppliedRayTracing = true;
	bool bAppliedMotionBlur = true;
};
