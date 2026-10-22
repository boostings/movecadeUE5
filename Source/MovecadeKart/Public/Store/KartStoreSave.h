#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "KartStoreSave.generated.h"

/** The store's local state on disk (Saved/SaveGames/KartStore.sav): what's equipped, play stats, the owned cache. */
UCLASS()
class MOVECADEKART_API UKartStoreSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 Version = 1;

	/** The last inventory the backend reported, so owned items still show offline. Restore replaces it. */
	UPROPERTY()
	TArray<int32> OwnedCache;

	/** Slot id ("kart-paint") -> def id. */
	UPROPERTY()
	TMap<FName, int32> Equipped;

	UPROPERTY()
	TMap<FName, int32> Stats;

	UPROPERTY()
	double PlaySeconds = 0.0;
};
