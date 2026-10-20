#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Render/KartRenderBudget.h"
#include "Subsystems/GameInstanceSubsystem.h"
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
 *
 * The trimmed levels go through Scalability's temporary-levels mechanism, so the saved settings (and anything a
 * settings screen reads) remain the player's own. A perf summary is logged every kart.Render.LogInterval seconds
 * (LogKartRender), which is what the automated captures read.
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

	/** Re-derives and applies the budget from the player's settings, the view count and the guard. */
	void ApplyBudget();

private:
	bool Tick(float DeltaSeconds);
	int32 CountViews() const;
	void UpdateGuard(float DeltaSeconds, float GpuMs);
	void LogPerfWindow();
	void HandleGraphicsApplied(const UKartGameUserSettings& Settings);

	/** The player's own levels (what the settings screen shows and what gets saved). */
	Scalability::FQualityLevels GetUserLevels() const;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle SettingsHandle;

	KartRender::FRenderBudget Budget;
	int32 NumViews = 1;
	int32 GuardSteps = 0;
	bool bBudgetApplied = false;

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
};
