#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KartGameMode.generated.h"

class ATrackBuilder;

/**
 * Graybox driving mode: finds the level's ATrackBuilder (or spawns one for DefaultTrackId) and puts
 * each player's kart on the start grid. Race flow (countdown, results) arrives in phase 4.
 */
UCLASS()
class MOVECADEKART_API AKartGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKartGameMode();

	virtual void RestartPlayer(AController* NewPlayer) override;
	/** Test hooks from the command line: -KartShots=2,8 (screenshots at those seconds), -KartQuitAfter=20. */
	virtual void StartPlay() override;

	ATrackBuilder* GetTrackBuilder();

	UPROPERTY(EditAnywhere, Category = "Kart")
	FString DefaultTrackId = TEXT("sunset-harbor");

private:
	UPROPERTY()
	TObjectPtr<ATrackBuilder> TrackBuilder;

	int32 NextGridSlot = 0;
};
