#include "UI/KartRenderFeatures.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "UI/KartPlayerPrefs.h"

namespace
{
	/**
	 * Stand-in until UKartGameUserSettings exists: the preset is UGameUserSettings' overall scalability level,
	 * ray tracing is Lumen's hardware ray tracing switch, motion blur the r.MotionBlur.Amount override. The two
	 * toggles persist in UKartPlayerPrefs.
	 */
	class FKartDefaultRenderFeatures final : public IKartRenderFeatures
	{
	public:
		virtual void ApplySaved() override
		{
			const UKartPlayerPrefs* Prefs = GetDefault<UKartPlayerPrefs>();
			ApplyRayTracing(Prefs->bRayTracing);
			ApplyMotionBlur(Prefs->bMotionBlur);
		}

		virtual int32 GetPreset() const override
		{
			const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
			return Settings ? Settings->GetOverallScalabilityLevel() : -1;
		}

		virtual void ApplyPreset(int32 Preset) override
		{
			if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
			{
				Settings->SetOverallScalabilityLevel(FMath::Clamp(Preset, 0, NumPresets - 1));
				Settings->ApplyNonResolutionSettings();
				Settings->SaveSettings();
			}
		}

		virtual bool SupportsRayTracing() const override
		{
			const IConsoleVariable* Project = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing"));
			return Project && Project->GetInt() != 0 && FindHardwareRayTracing();
		}

		virtual bool IsRayTracingEnabled() const override { return GetDefault<UKartPlayerPrefs>()->bRayTracing; }

		virtual void SetRayTracingEnabled(bool bEnabled) override
		{
			UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
			Prefs->bRayTracing = bEnabled;
			Prefs->Save();
			ApplyRayTracing(bEnabled);
		}

		virtual bool IsMotionBlurEnabled() const override { return GetDefault<UKartPlayerPrefs>()->bMotionBlur; }

		virtual void SetMotionBlurEnabled(bool bEnabled) override
		{
			UKartPlayerPrefs* Prefs = GetMutableDefault<UKartPlayerPrefs>();
			Prefs->bMotionBlur = bEnabled;
			Prefs->Save();
			ApplyMotionBlur(bEnabled);
		}

	private:
		static IConsoleVariable* FindHardwareRayTracing()
		{
			return IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.HardwareRayTracing"));
		}

		static void ApplyRayTracing(bool bEnabled)
		{
			if (IConsoleVariable* Var = FindHardwareRayTracing())
			{
				Var->Set(bEnabled ? 1 : 0, ECVF_SetByGameSetting);
			}
		}

		static void ApplyMotionBlur(bool bEnabled)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlur.Amount")))
			{
				Var->Set(bEnabled ? 0.5f : 0.f, ECVF_SetByGameSetting);
			}
		}
	};
}

IKartRenderFeatures& IKartRenderFeatures::Get()
{
	// MERGE POINT: return the UKartGameUserSettings-backed implementation here (see KartRenderFeatures.h).
	static FKartDefaultRenderFeatures Default;
	return Default;
}
