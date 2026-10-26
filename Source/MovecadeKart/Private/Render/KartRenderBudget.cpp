#include "Render/KartRenderBudget.h"

#include "Render/KartGameUserSettings.h"

namespace KartRender
{
	namespace
	{
		constexpr int32 Medium = 1;
		constexpr int32 High = 2;
		constexpr int32 Epic = 3;

		/** Caps a group at Level without ever raising it. */
		void Cap(int32& Group, int32 Level)
		{
			Group = FMath::Min(Group, Level);
		}

		/** One guard step down, but never below Medium: Low turns Lumen GI and reflections off, which changes the look. */
		void StepDown(int32& Group)
		{
			Group = FMath::Max(FMath::Min(Group, Medium), Group - 1);
		}
	}

	Scalability::FQualityLevels QualityLevelsForPreset(EKartGraphicsPreset Preset)
	{
		Scalability::FQualityLevels Levels;
		switch (Preset)
		{
		case EKartGraphicsPreset::Performance:
			Levels.SetFromSingleQualityLevel(Medium);
			// Textures and view distance cost memory, not frame time; keep the art readable.
			Levels.TextureQuality = High;
			Levels.ViewDistanceQuality = High;
			Levels.AntiAliasingQuality = High;
			break;
		case EKartGraphicsPreset::Balanced:
			Levels.SetFromSingleQualityLevel(High);
			Levels.TextureQuality = Epic;
			break;
		case EKartGraphicsPreset::Quality:
		case EKartGraphicsPreset::Ultra:
		default:
			// Epic everywhere; Ultra differs in resolution (native TSR) and in hit-lit RT reflections, which the
			// project enables (r.Lumen.HardwareRayTracing.LightingMode=2) and GI Epic allows.
			Levels.SetFromSingleQualityLevel(Epic);
			if (Preset == EKartGraphicsPreset::Quality)
			{
				// TSR history at 100 % instead of 200 %: most of the AA quality for a fraction of the cost.
				Levels.AntiAliasingQuality = High;
			}
			break;
		}
		Levels.ResolutionQuality = ScreenPercentageForPreset(Preset);
		return Levels;
	}

	float ScreenPercentageForPreset(EKartGraphicsPreset Preset)
	{
		switch (Preset)
		{
		case EKartGraphicsPreset::Performance: return 66.7f; // 1440p output from 960p
		case EKartGraphicsPreset::Balanced: return 75.f;     // from 1080p
		case EKartGraphicsPreset::Quality: return 87.5f;     // from 1260p
		case EKartGraphicsPreset::Ultra:
		default: return 100.f;                               // native, TSR as anti-aliasing only
		}
	}

	float MinScreenPercentageFor(float MaxScreenPercentage)
	{
		// TSR still resolves a clean image from ~60 % of its target, so let dynamic resolution go that far before
		// the frame-time guard has to drop features.
		return FMath::Min(MaxScreenPercentage, FMath::Max(50.f, MaxScreenPercentage * 0.6f));
	}

	FRenderBudget BudgetFor(const Scalability::FQualityLevels& User, int32 NumViews, int32 GuardSteps)
	{
		FRenderBudget Budget;
		Budget.Levels = User;
		// A 0 or tiny ResolutionQuality means "engine default"; treat it as native.
		Budget.MaxScreenPercentage = User.ResolutionQuality >= 25.f ? FMath::Min(User.ResolutionQuality, 100.f) : 100.f;
		Scalability::FQualityLevels& L = Budget.Levels;

		if (NumViews >= 2)
		{
			// Per-view passes dominate: Lumen's screen probes and reflections, shadow setup, TSR.
			Cap(L.GlobalIlluminationQuality, High);
			Cap(L.ReflectionQuality, High);
			Cap(L.ShadowQuality, High);
		}
		if (NumViews >= 3)
		{
			// Quarter-screen views: the TSR 200 % history, cinematic post and far detail are invisible at that size.
			Cap(L.AntiAliasingQuality, High);
			Cap(L.PostProcessQuality, High);
			Cap(L.ViewDistanceQuality, High);
			// Quarter views upsampled from much below their size smear under motion; keep TSR's input near native and
			// let dynamic resolution trade it for frame time.
			Budget.MaxScreenPercentage = FMath::Min(Budget.MaxScreenPercentage, 90.f);
		}

		const int32 Steps = FMath::Clamp(GuardSteps, 0, MaxGuardSteps);
		if (Steps >= 1)
		{
			StepDown(L.GlobalIlluminationQuality);
			StepDown(L.ReflectionQuality);
		}
		if (Steps >= 2)
		{
			StepDown(L.ShadowQuality);
			StepDown(L.PostProcessQuality);
			StepDown(L.EffectsQuality);
		}

		Budget.MinScreenPercentage = MinScreenPercentageFor(Budget.MaxScreenPercentage);
		L.ResolutionQuality = Budget.MaxScreenPercentage;
		return Budget;
	}

	FRenderBudget MobileBudgetFor(const Scalability::FQualityLevels& User, int32 GuardSteps)
	{
		FRenderBudget Budget;
		Budget.Levels = User;
		Budget.MaxScreenPercentage = User.ResolutionQuality >= 25.f ? FMath::Min(User.ResolutionQuality, 100.f) : 100.f;
		Scalability::FQualityLevels& L = Budget.Levels;

		auto Lower = [](int32& Group) { Group = FMath::Max(0, Group - 1); };
		const int32 Steps = FMath::Clamp(GuardSteps, 0, MaxGuardSteps);
		if (Steps >= 1)
		{
			// The mobile frame's biggest movable costs: the CSM depth passes, bloom/DoF, and particle overdraw.
			Lower(L.ShadowQuality);
			Lower(L.PostProcessQuality);
			Lower(L.EffectsQuality);
		}
		if (Steps >= 2)
		{
			Lower(L.ViewDistanceQuality);
			Lower(L.FoliageQuality);
			Budget.MaxScreenPercentage = FMath::Min(Budget.MaxScreenPercentage, MobileGuardScreenPercentage);
		}
		// No dynamic resolution on the mobile path (see DefaultDeviceProfiles.ini): floor = ceiling.
		Budget.MinScreenPercentage = Budget.MaxScreenPercentage;
		L.ResolutionQuality = Budget.MaxScreenPercentage;
		return Budget;
	}

	FThermalResponse ThermalResponseFor(EThermalState State, bool bLowPowerMode)
	{
		FThermalResponse Response;
		switch (State)
		{
		case EThermalState::Bad:
			Response.MinGuardSteps = 1;
			break;
		case EThermalState::Serious:
			Response.MaxFps = ThermalFallbackFps;
			Response.MinGuardSteps = 1;
			break;
		case EThermalState::Critical:
			Response.MaxFps = ThermalFallbackFps;
			Response.MinGuardSteps = MaxGuardSteps;
			break;
		default:
			break;
		}
		if (bLowPowerMode)
		{
			Response.MaxFps = ThermalFallbackFps;
		}
		return Response;
	}
}
