#pragma once

#include "CoreMinimal.h"

/**
 * The graphics settings the settings screen can't express through plain UGameUserSettings: the quality
 * preset, hardware ray tracing and motion blur.
 *
 * ============================================================================================
 *  MERGE POINT (rendering workstream): this is the only place the UI touches render features.
 *  When UKartGameUserSettings lands, add an implementation that forwards to it
 *      UKartGameUserSettings* S = Cast<UKartGameUserSettings>(GEngine->GetGameUserSettings());
 *      ApplyPreset(L)        -> S->SetGraphicsPreset(static_cast<EKartGraphicsPreset>(L)); S->ApplySettings(false);
 *      SetRayTracingEnabled  -> S->SetRayTracingEnabled(b); S->ApplySettings(false);
 *      SetMotionBlurEnabled  -> S->SetMotionBlurEnabled(b); S->ApplySettings(false);
 *  and return it from IKartRenderFeatures::Get() (KartRenderFeatures.cpp). Nothing else changes.
 * ============================================================================================
 */
class MOVECADEKART_API IKartRenderFeatures
{
public:
	virtual ~IKartRenderFeatures() = default;

	/** Presets 0..4: Low, Medium, High, Epic, Cinematic. */
	static constexpr int32 NumPresets = 5;

	/** The active implementation (FKartDefaultRenderFeatures until the merge above). */
	static IKartRenderFeatures& Get();

	/** Re-applies the saved choices once at startup. */
	virtual void ApplySaved() = 0;

	/** -1 when the individual scalability groups don't match a single preset ("Custom"). */
	virtual int32 GetPreset() const = 0;
	virtual void ApplyPreset(int32 Preset) = 0;

	virtual bool SupportsRayTracing() const = 0;
	virtual bool IsRayTracingEnabled() const = 0;
	virtual void SetRayTracingEnabled(bool bEnabled) = 0;

	virtual bool IsMotionBlurEnabled() const = 0;
	virtual void SetMotionBlurEnabled(bool bEnabled) = 0;
};
