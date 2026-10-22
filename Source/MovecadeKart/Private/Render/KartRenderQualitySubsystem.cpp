#include "Render/KartRenderQualitySubsystem.h"

#include "DynamicResolutionState.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Render/KartGameUserSettings.h"
#include "RenderTimer.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartRender, Log, All);

namespace
{
	TAutoConsoleVariable<float> CVarTargetFPS(
		TEXT("kart.Render.TargetFPS"), 0.f,
		TEXT("Frame rate the dynamic resolution heuristic and the frame-time guard hold. 0 (default): the player's frame cap, ")
		TEXT("else the display's refresh rate with vsync on, else 60; clamped to 30-144."));

	/** The frame time (ms) the budget holds: kart.Render.TargetFPS, else derived from the frame cap and the display. */
	float TargetFrameMs()
	{
		float Fps = CVarTargetFPS.GetValueOnGameThread();
		if (Fps <= 0.f)
		{
			Fps = 60.f;
			if (const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
			{
				const float Refresh = static_cast<float>(FPlatformMisc::GetMaxRefreshRate());
				if (Settings->GetFrameRateLimit() > 0.f)
				{
					Fps = Settings->GetFrameRateLimit();
					if (Settings->IsVSyncEnabled() && Refresh > 0.f)
					{
						Fps = FMath::Min(Fps, Refresh);
					}
				}
				else if (Settings->IsVSyncEnabled() && Refresh > 0.f)
				{
					Fps = Refresh;
				}
			}
		}
		return 1000.f / FMath::Clamp(Fps, 30.f, 144.f);
	}

	TAutoConsoleVariable<bool> CVarGuard(
		TEXT("kart.Render.Guard"), true,
		TEXT("Frame-time guard: step the costliest scalability groups down when dynamic resolution alone can't hold the target."));

	TAutoConsoleVariable<float> CVarLogInterval(
		TEXT("kart.Render.LogInterval"), 5.f,
		TEXT("Seconds between LogKartRender perf summaries (0 = off)."));

	TAutoConsoleVariable<int32> CVarForceViews(
		TEXT("kart.Render.ForceViews"), 0,
		TEXT("Debug: budget as if this many views were on screen (0 = count the local players)."));

	/** GPU over budget for this long, with dynamic resolution at its floor, steps the guard down. */
	constexpr float GuardStepDownSeconds = 2.f;
	/** GPU under 70 % of the budget for this long steps the guard back up. */
	constexpr float GuardStepUpSeconds = 10.f;
	constexpr float GuardHeadroom = 0.7f;
	/** Settle time after any change before the guard judges again (resolution and Lumen histories reconverge). */
	constexpr float GuardCooldownSeconds = 4.f;

	void SetFromCode(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			CVar->Set(Value, ECVF_SetByCode);
		}
	}

	float Percentile(TArray<float>& Samples, float Q)
	{
		if (Samples.IsEmpty())
		{
			return 0.f;
		}
		Samples.Sort();
		return Samples[FMath::Clamp(FMath::FloorToInt32(Q * Samples.Num()), 0, Samples.Num() - 1)];
	}

	float Average(const TArray<float>& Samples)
	{
		float Sum = 0.f;
		for (float S : Samples)
		{
			Sum += S;
		}
		return Samples.IsEmpty() ? 0.f : Sum / Samples.Num();
	}

	/**
	 * The primary TSR input as a percentage of the output, as the renderer currently uses it, and whether dynamic
	 * resolution is choosing it (false: the static r.ScreenPercentage).
	 */
	float CurrentScreenPercentage(bool* bOutDynamic = nullptr)
	{
		if (GEngine)
		{
			FDynamicResolutionStateInfos Infos;
			GEngine->GetDynamicResolutionCurrentStateInfos(Infos);
			if (Infos.Status == EDynamicResolutionStatus::Enabled)
			{
				if (bOutDynamic)
				{
					*bOutDynamic = true;
				}
				return Infos.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction] * 100.f;
			}
		}
		if (bOutDynamic)
		{
			*bOutDynamic = false;
		}
		static const IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		const float Static = ScreenPercentage ? ScreenPercentage->GetFloat() : 100.f;
		return Static > 0.f ? Static : 100.f;
	}
}

bool UKartRenderQualitySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Nothing to budget without a renderer (dedicated server, -nullrhi automation).
	return FApp::CanEverRender() && Super::ShouldCreateSubsystem(Outer);
}

void UKartRenderQualitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (UKartGameUserSettings* Settings = UKartGameUserSettings::Get())
	{
		SettingsHandle = Settings->OnGraphicsApplied.AddUObject(this, &UKartRenderQualitySubsystem::HandleGraphicsApplied);
	}
	// Hold the target frame rate with the engine's dynamic resolution heuristic (r.DynamicRes.OperationMode=1 lets the
	// player's dynamic resolution setting turn it off).
	BudgetMs = TargetFrameMs();
	SetFromCode(TEXT("r.DynamicRes.FrameTimeBudget"), BudgetMs);

	NumViews = CountViews();
	ApplyBudget();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UKartRenderQualitySubsystem::Tick));
}

void UKartRenderQualitySubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (UKartGameUserSettings* Settings = UKartGameUserSettings::Get())
	{
		Settings->OnGraphicsApplied.Remove(SettingsHandle);
	}
	if (Scalability::IsTemporaryQualityLevelActive())
	{
		Scalability::ToggleTemporaryQualityLevels(false); // hand the player's own levels back
	}
	Super::Deinitialize();
}

Scalability::FQualityLevels UKartRenderQualitySubsystem::GetUserLevels() const
{
	// In temporary mode Scalability reports the backed-up (player's) levels, never our trimmed ones.
	return Scalability::GetQualityLevels();
}

int32 UKartRenderQualitySubsystem::CountViews() const
{
	if (const int32 Forced = CVarForceViews.GetValueOnGameThread(); Forced > 0)
	{
		return FMath::Clamp(Forced, 1, 4);
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!Viewport || Viewport->GetCurrentSplitscreenConfiguration() == ESplitScreenType::None)
	{
		return 1;
	}
	return FMath::Clamp(GameInstance->GetNumLocalPlayers(), 1, 4);
}

void UKartRenderQualitySubsystem::ApplyBudget()
{
	const Scalability::FQualityLevels User = GetUserLevels();
	Budget = KartRender::BudgetFor(User, NumViews, GuardSteps);

	// Swap the trimmed levels in through Scalability's temporary mode, so SaveSettings and the settings screen
	// still see the player's levels. Re-entering refreshes the override.
	if (Scalability::IsTemporaryQualityLevelActive())
	{
		Scalability::ToggleTemporaryQualityLevels(false);
	}
	if (Budget.Levels != User)
	{
		Scalability::ToggleTemporaryQualityLevels(true, Budget.Levels);
		Scalability::SetQualityLevels(Budget.Levels, /*bForce*/ true);
	}

	SetFromCode(TEXT("r.DynamicRes.MaxScreenPercentage"), Budget.MaxScreenPercentage);
	SetFromCode(TEXT("r.DynamicRes.MinScreenPercentage"), Budget.MinScreenPercentage);
	bBudgetApplied = true;

	UE_LOG(LogKartRender, Log, TEXT("Budget for %d view(s), guard %d: GI %d, reflections %d, shadows %d, post %d, AA %d, TSR input %.0f-%.0f %%"),
		NumViews, GuardSteps, Budget.Levels.GlobalIlluminationQuality, Budget.Levels.ReflectionQuality, Budget.Levels.ShadowQuality,
		Budget.Levels.PostProcessQuality, Budget.Levels.AntiAliasingQuality, Budget.MinScreenPercentage, Budget.MaxScreenPercentage);
}

void UKartRenderQualitySubsystem::HandleGraphicsApplied(const UKartGameUserSettings& Settings)
{
	// The player changed something: start the guard over from their new choice.
	GuardSteps = 0;
	OverBudgetSeconds = HeadroomSeconds = 0.f;
	GuardCooldown = GuardCooldownSeconds;
	// The frame cap or vsync may have changed: hold the new frame time.
	BudgetMs = TargetFrameMs();
	SetFromCode(TEXT("r.DynamicRes.FrameTimeBudget"), BudgetMs);
	ApplyBudget();
}

bool UKartRenderQualitySubsystem::Tick(float DeltaSeconds)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_KartRenderQuality_Tick);

	const int32 Views = CountViews();
	if (Views != NumViews)
	{
		NumViews = Views;
		GuardSteps = 0;
		OverBudgetSeconds = HeadroomSeconds = 0.f;
		GuardCooldown = GuardCooldownSeconds;
		ApplyBudget();
	}

	// Hitches (loading, alt-tab, the first frames after a level opens) say nothing about the steady-state cost.
	if (DeltaSeconds <= 0.f || DeltaSeconds > 0.25f)
	{
		return true;
	}

	const float Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	UpdateGuard(DeltaSeconds, Gpu);

	if (CVarLogInterval.GetValueOnGameThread() > 0.f)
	{
		FrameMs.Add(DeltaSeconds * 1000.f);
		GpuMs.Add(Gpu);
		GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		ResolutionSum += CurrentScreenPercentage();
		WindowSeconds += DeltaSeconds;
		if (WindowSeconds >= CVarLogInterval.GetValueOnGameThread())
		{
			LogPerfWindow();
		}
	}
	return true;
}

void UKartRenderQualitySubsystem::UpdateGuard(float DeltaSeconds, float Gpu)
{
	GuardCooldown = FMath::Max(0.f, GuardCooldown - DeltaSeconds);
	if (!CVarGuard.GetValueOnGameThread() || GuardCooldown > 0.f || Gpu <= 0.f)
	{
		return;
	}

	// Dynamic resolution is the first line of defence; only step features down once it has nothing left to give
	// (or right away when the player has turned it off).
	bool bDynamic = false;
	const float Resolution = CurrentScreenPercentage(&bDynamic);
	const bool bResolutionAtFloor = !bDynamic || Resolution <= Budget.MinScreenPercentage + 2.f;

	OverBudgetSeconds = (Gpu > BudgetMs * 1.03f && bResolutionAtFloor) ? OverBudgetSeconds + DeltaSeconds : FMath::Max(0.f, OverBudgetSeconds - DeltaSeconds);
	HeadroomSeconds = Gpu < BudgetMs * GuardHeadroom ? HeadroomSeconds + DeltaSeconds : 0.f;

	int32 NewSteps = GuardSteps;
	if (OverBudgetSeconds > GuardStepDownSeconds && GuardSteps < KartRender::MaxGuardSteps)
	{
		++NewSteps;
	}
	else if (HeadroomSeconds > GuardStepUpSeconds && GuardSteps > 0)
	{
		--NewSteps;
	}
	if (NewSteps != GuardSteps)
	{
		UE_LOG(LogKartRender, Log, TEXT("Frame-time guard %s: GPU %.1f ms against a %.1f ms budget"),
			NewSteps > GuardSteps ? TEXT("steps down") : TEXT("steps back up"), Gpu, BudgetMs);
		GuardSteps = NewSteps;
		OverBudgetSeconds = HeadroomSeconds = 0.f;
		GuardCooldown = GuardCooldownSeconds;
		ApplyBudget();
	}
}

void UKartRenderQualitySubsystem::LogPerfWindow()
{
	const float AvgFrame = Average(FrameMs);
	UE_LOG(LogKartRender, Log, TEXT("Perf %.0fs | %d view(s) | %.1f fps | frame avg %.2f p95 %.2f ms | GPU avg %.2f p95 %.2f ms | game %.2f | render %.2f | TSR input %.0f %% | guard %d"),
		WindowSeconds, NumViews, AvgFrame > 0.f ? 1000.f / AvgFrame : 0.f, AvgFrame, Percentile(FrameMs, 0.95f), Average(GpuMs),
		Percentile(GpuMs, 0.95f), Average(GameMs), Average(RenderMs), FrameMs.IsEmpty() ? 0.f : ResolutionSum / FrameMs.Num(), GuardSteps);
	FrameMs.Reset();
	GpuMs.Reset();
	GameMs.Reset();
	RenderMs.Reset();
	WindowSeconds = 0.f;
	ResolutionSum = 0.f;
}
