#pragma once

#include "CoreMinimal.h"

class AActor;
struct FTimerHandle;

/**
 * The test hooks every level mode shares (the kart race and each party game), parsed once from the command line:
 *   -KartShots=t1,t2,...  a screenshot with the UI ("Shot showui") that many seconds after the level starts;
 *   -KartQuitAfter=N      quit the game N seconds after the level starts.
 * Tools/ue/play.ps1 and Tools/net-sim/e2e.ps1 drive them. Compiled out of Shipping (Parse returns no hooks).
 */
struct MOVECADEKART_API FArcadeTestHooks
{
	/** Seconds after the level starts, in command-line order. */
	TArray<float> ShotTimes;
	/** <= 0: never quit. */
	float QuitAfterSeconds = 0.f;

	static FArcadeTestHooks Parse(const TCHAR* CommandLine);

	bool IsEmpty() const { return ShotTimes.Num() == 0 && QuitAfterSeconds <= 0.f; }

	/**
	 * Arms the hooks on Owner's world timers. The callbacks are weak (nothing fires once Owner is gone), and the
	 * handles land in OutHandles so the owner can clear them in EndPlay (the timer manager outlives level travel).
	 */
	void Schedule(AActor& Owner, TArray<FTimerHandle>& OutHandles) const;
};
