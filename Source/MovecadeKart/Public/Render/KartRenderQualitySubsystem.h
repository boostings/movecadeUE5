#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Render/KartRenderBudget.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include <atomic>
#include "KartRenderQualitySubsystem.generated.h"

class UKartGameUserSettings;

/**
 * Keeps the frame rate at the target while the player's graphics settings stay what they chose.
 *
 *  - Split screen: counts the local views (one per ULocalPlayer while split screen is on, see
 *    UKartGameViewportClient) and applies KartRender::BudgetFor, which trims the per-view costs (Lumen, shadows,
 *    TSR ceiling) for 2-4 views.
 *  - Dynamic resolution: the engine's GPU-time heuristic scales TSR's input between the budget's floor and
 *    ceiling to hold kart.Render.TargetFPS (r.DynamicRes.FrameTimeBudget).
 *  - Frame-time guard: if the GPU still misses the budget with dynamic resolution at its floor for a few seconds,
 *    it steps the costliest groups down (at most KartRender::MaxGuardSteps), and back up after sustained headroom.
 *  - Mobile renderer (phones, and -featureleveles31 preview on PC): the levels come from the device profile
 *    (Config/DefaultDeviceProfiles.ini), the guard uses KartRender::MobileBudgetFor, the frame cap is
 *    kart.Render.MobileFrameCap, and the OS thermal and low-power notifications apply KartRender::ThermalResponseFor
 *    (guard steps, then a 30 fps cap while the phone is hot).
 *
 * The trimmed levels go through Scalability's temporary-levels mechanism, so the saved settings (and anything a
 * settings screen reads) remain the player's own. A perf summary is logged every kart.Render.LogInterval seconds
 * (LogKartRender), with draw calls, primitives, memory and map load times: that is what the automated captures read
 * (Tools/ue/mobile_audit.ps1).
 */
UCLASS()
class MOVECADEKART_API UKartRenderQualitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Local views being rendered (1 = full screen). */
	int32 GetNumViews() const { return NumViews; }

	/** Frame-time guard steps currently applied (0 = none). */
	int32 GetGuardSteps() const { return GuardSteps; }

	/** The budget currently applied. */
	const KartRender::FRenderBudget& GetBudget() const { return Budget; }

	/** True when the scene renders with the mobile renderer (ES3.1 feature level). */
	bool IsMobileRenderer() const { return bMobileRenderer; }

	/** The thermal response applied now (mobile renderer only; always the neutral response on PC). */
	const KartRender::FThermalResponse& GetThermalResponse() const { return Thermal; }

	/** Re-derives and applies the budget from the player's settings, the view count and the guard. */
	void ApplyBudget();

private:
	bool Tick(float DeltaSeconds);
	int32 CountViews() const;
	void UpdateGuard(float DeltaSeconds, float GpuMs);
	void LogPerfWindow();
	void HandleGraphicsApplied(const UKartGameUserSettings& Settings);

	/** The frame time (ms) to hold: the desktop rules, or the mobile frame cap and the thermal response. */
	float ComputeBudgetMs() const;
	/** Mobile: pushes the frame cap to the frame pacer (rhi.SyncInterval) and t.MaxFPS. */
	void ApplyMobileFrameCap();
	/** Mobile: applies thermal and low-power changes posted by the OS callbacks (which may arrive on any thread). */
	void UpdateThermal();
	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* World);

	/** The player's own levels (what the settings screen shows and what gets saved). */
	Scalability::FQualityLevels GetUserLevels() const;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle SettingsHandle;
	FDelegateHandle TemperatureHandle;
	FDelegateHandle LowPowerHandle;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;

	KartRender::FRenderBudget Budget;
	int32 NumViews = 1;
	int32 GuardSteps = 0;
	bool bBudgetApplied = false;
	bool bProfileDumped = false;
	/** The frame time held (ms): kart.Render.TargetFPS, or the frame cap / refresh rate (see TargetFrameMs). */
	float BudgetMs = 1000.f / 60.f;

	bool bMobileRenderer = false;
	KartRender::FThermalResponse Thermal;
	/** Last values posted by the OS callbacks; read on the game thread in Tick. */
	std::atomic<uint8> PendingThermalState{ 0 };
	std::atomic<bool> bPendingLowPower{ false };
	uint8 AppliedThermalState = 0;
	bool bAppliedLowPower = false;

	/** Map load timing for the perf log. */
	double MapLoadStartSeconds = 0.0;
	FString LoadingMap;

	/** Guard state: seconds spent over budget at the resolution floor, and under budget with headroom. */
	float OverBudgetSeconds = 0.f;
	float HeadroomSeconds = 0.f;
	float GuardCooldown = 0.f;

	/** Perf window samples (milliseconds), reset every log interval. */
	TArray<float> FrameMs;
	TArray<float> GpuMs;
	TArray<float> GameMs;
	TArray<float> RenderMs;
	float WindowSeconds = 0.f;
	float ResolutionSum = 0.f;
	double DrawCallSum = 0.0;
	double PrimitiveSum = 0.0;
};
