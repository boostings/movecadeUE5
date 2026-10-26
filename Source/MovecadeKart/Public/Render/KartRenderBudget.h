#pragma once

#include "CoreMinimal.h"
#include "Scalability.h"

enum class EKartGraphicsPreset : uint8;

/**
 * Pure rules that map the player's settings and the current screen layout to render budgets. No engine state is
 * touched here, so the rules are unit-tested (Private/Tests/KartRenderTests.cpp) and the subsystem only applies them.
 *
 * Scalability levels: 0 low, 1 medium, 2 high, 3 epic, 4 cinematic.
 */
namespace KartRender
{
	/** Every scalability group for a preset (ResolutionQuality is the preset's screen percentage). */
	MOVECADEKART_API Scalability::FQualityLevels QualityLevelsForPreset(EKartGraphicsPreset Preset);

	/** TSR input as a percentage of the output resolution: the dynamic-resolution ceiling. */
	MOVECADEKART_API float ScreenPercentageForPreset(EKartGraphicsPreset Preset);

	/** The lowest TSR input dynamic resolution may drop to under a given ceiling (~60 % of it, never below 50 %). */
	MOVECADEKART_API float MinScreenPercentageFor(float MaxScreenPercentage);

	/** What the renderer gets for the current frame: the player's choice, trimmed for split screen and the guard. */
	struct FRenderBudget
	{
		Scalability::FQualityLevels Levels;
		float MaxScreenPercentage = 100.f;
		float MinScreenPercentage = 50.f;
	};

	/** The most the frame-time guard steps down; each step trims the costliest groups by one level. */
	constexpr int32 MaxGuardSteps = 2;

	/**
	 * The budget for NumViews local views (1 = full screen, 2-4 = split screen) after GuardSteps frame-time guard
	 * steps. User.ResolutionQuality is the player's TSR ceiling. Never raises anything above the player's choice.
	 *
	 * Split screen renders the same pixel count as one full view but repeats every per-view pass (Lumen screen
	 * probes and reflections, shadow setup, TSR), so it caps Lumen GI, reflections and shadows at High; three and
	 * four views also cap anti-aliasing, post-processing and view distance and trim the TSR ceiling to 90 %.
	 * (Measured on an RTX 4070 SUPER: four views at the 1440p-equivalent cost ~12.5 ms GPU unbudgeted, ~9.5 ms with
	 * this budget.)
	 */
	MOVECADEKART_API FRenderBudget BudgetFor(const Scalability::FQualityLevels& User, int32 NumViews, int32 GuardSteps);

	/**
	 * The mobile renderer's budget (phones, and ES3.1 preview on PC). The player's levels there come from the device
	 * profile (Config/DefaultDeviceProfiles.ini); there is no Lumen to protect, so guard steps go down to Low:
	 * step 1 trims shadows, post-processing and effects, step 2 also view distance and foliage and lowers the
	 * resolution ceiling to MobileGuardScreenPercentage.
	 */
	MOVECADEKART_API FRenderBudget MobileBudgetFor(const Scalability::FQualityLevels& User, int32 GuardSteps);

	/** The resolution ceiling (percent) at the last mobile guard step. */
	constexpr float MobileGuardScreenPercentage = 85.f;

	/** Device thermal state, as iOS and Android report it (mirrors FCoreDelegates::ETemperatureSeverity). */
	enum class EThermalState : uint8
	{
		Unknown,
		Good,
		Bad,
		Serious,
		Critical,
	};

	/** What the renderer does about heat: a frame cap (0 = keep the player's) and a minimum guard step. */
	struct FThermalResponse
	{
		float MaxFps = 0.f;
		int32 MinGuardSteps = 0;

		bool operator==(const FThermalResponse& Other) const { return MaxFps == Other.MaxFps && MinGuardSteps == Other.MinGuardSteps; }
	};

	/** The frame rate the thermal policy falls back to: half of a 60 Hz panel, evenly paced. */
	constexpr float ThermalFallbackFps = 30.f;

	/**
	 * Thermal policy for phones: Good/Unknown change nothing; Bad trims one guard step (cheaper frames, same 60 fps);
	 * Serious also caps at 30 fps; Critical caps at 30 fps with every guard step. Low-power mode caps at 30 fps.
	 * A steady 30 beats a throttled 40-60 that stutters.
	 */
	MOVECADEKART_API FThermalResponse ThermalResponseFor(EThermalState State, bool bLowPowerMode);
}
