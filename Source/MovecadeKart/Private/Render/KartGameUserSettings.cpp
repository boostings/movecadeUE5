#include "Render/KartGameUserSettings.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Render/KartRenderBudget.h"
#include "RenderUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartSettings, Log, All);

namespace
{
	/** Bump when a saved value changes meaning; older GameUserSettings.ini files are reset to the defaults. */
	constexpr int32 CurrentKartSettingsVersion = 1;

	/**
	 * Sets a cvar from code. ECVF_SetByCode outranks the project ini (RendererSettings), so a runtime switch such as
	 * r.Lumen.HardwareRayTracing actually changes, while the console can still override it for debugging.
	 */
	void SetCVarFromCode(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value, ECVF_SetByCode);
		}
		else
		{
			UE_LOG(LogKartSettings, Warning, TEXT("Unknown cvar %s"), Name);
		}
	}
}

UKartGameUserSettings* UKartGameUserSettings::Get()
{
	return GEngine ? Cast<UKartGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UKartGameUserSettings::SetGraphicsPreset(EKartGraphicsPreset Preset)
{
	GraphicsPreset = Preset;
	ScalabilityQuality = KartRender::QualityLevelsForPreset(Preset);
	// Dynamic resolution keeps TSR's input between the preset's floor and ceiling; ResolutionQuality (the
	// r.ScreenPercentage used when the player turns dynamic resolution off) is the ceiling.
	SetDynamicResolutionEnabled(true);
}

void UKartGameUserSettings::SetRayTracingEnabled(bool bEnabled)
{
	bRayTracingEnabled = bEnabled;
}

bool UKartGameUserSettings::IsRayTracingSupported()
{
	// Capable GPU and RHI, and ray tracing compiled into the project (r.RayTracing=1).
	return IsRayTracingAllowed();
}

void UKartGameUserSettings::SetMotionBlurEnabled(bool bEnabled)
{
	bMotionBlurEnabled = bEnabled;
}

void UKartGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	bRayTracingEnabled = true;
	bMotionBlurEnabled = true;
	SetGraphicsPreset(EKartGraphicsPreset::Ultra);
}

void UKartGameUserSettings::ValidateSettings()
{
	const bool bWasValid = IsVersionValid();
	Super::ValidateSettings();
	if (!bWasValid)
	{
		// A reset (first run, or an older settings file): the engine's reset copies the engine's dynamic resolution
		// state over ours after SetToDefaults, so re-apply the preset to turn it back on.
		SetGraphicsPreset(GraphicsPreset);
	}
}

bool UKartGameUserSettings::IsVersionValid()
{
	return Super::IsVersionValid() && KartSettingsVersion == CurrentKartSettingsVersion;
}

void UKartGameUserSettings::UpdateVersion()
{
	Super::UpdateVersion();
	KartSettingsVersion = CurrentKartSettingsVersion;
}

void UKartGameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings(); // validates, then pushes vsync, frame limit, dynamic resolution and scalability
	ApplyKartRenderSettings();
	OnGraphicsApplied.Broadcast(*this);
}

bool UKartGameUserSettings::IsDirty() const
{
	return Super::IsDirty() || GraphicsPreset != AppliedPreset || bRayTracingEnabled != bAppliedRayTracing
		|| bMotionBlurEnabled != bAppliedMotionBlur;
}

void UKartGameUserSettings::ResetToCurrentSettings()
{
	Super::ResetToCurrentSettings();
	GraphicsPreset = AppliedPreset;
	bRayTracingEnabled = bAppliedRayTracing;
	bMotionBlurEnabled = bAppliedMotionBlur;
}

void UKartGameUserSettings::ApplyKartRenderSettings()
{
	// Hardware ray tracing: Lumen GI and reflections trace the RT scene. Off, Lumen falls back to software tracing
	// against distance fields and the RT scene isn't built at all (r.RayTracing.EnableOnDemand=1 in
	// DefaultEngine.ini lets r.RayTracing.Enable change at runtime).
	const bool bRayTracing = bRayTracingEnabled && IsRayTracingSupported();
	SetCVarFromCode(TEXT("r.RayTracing.Enable"), bRayTracing ? 1 : 0);
	SetCVarFromCode(TEXT("r.Lumen.HardwareRayTracing"), bRayTracing ? 1 : 0);

	// The rest reaches the renderer through OnGraphicsApplied:
	//  - motion blur: the global grade volume (AKartGradeVolume) zeroes its blur amount, which skips the pass
	//    without fighting the PostProcessQuality group's cvars;
	//  - dynamic resolution bounds and the split-screen budget: UKartRenderQualitySubsystem.

	AppliedPreset = GraphicsPreset;
	bAppliedRayTracing = bRayTracingEnabled;
	bAppliedMotionBlur = bMotionBlurEnabled;
	UE_LOG(LogKartSettings, Log, TEXT("Applied graphics: preset %s, ray tracing %s%s, motion blur %s, dynamic resolution %s"),
		*UEnum::GetValueAsString(GraphicsPreset), bRayTracing ? TEXT("on") : TEXT("off"),
		bRayTracingEnabled && !bRayTracing ? TEXT(" (unsupported here)") : TEXT(""), bMotionBlurEnabled ? TEXT("on") : TEXT("off"),
		IsDynamicResolutionEnabled() ? TEXT("on") : TEXT("off"));
}
