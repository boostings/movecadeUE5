#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BowlingGameMode.generated.h"

class ABowlingAlley;
class ABowlingGame;

/**
 * Bowling's level mode (L_Bowling): finds the alley (or builds one) and starts a game. Local play by default:
 * -BowlPlayers=N local bowlers (default 1; they share the keyboard, mouse and pads, since only one bowls at a
 * time), -BowlBots=N house bowlers (default 1), -BowlAuto (everyone is a bot: soak tests, screenshots). The Kart
 * test hooks work here too: -KartShots=t1,t2 and -KartQuitAfter=N.
 */
UCLASS()
class MOVECADEKART_API ABowlingGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABowlingGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}

	ABowlingGame* GetGame() const { return Game; }

private:
	void SetupTestHooks();

	UPROPERTY()
	TObjectPtr<ABowlingAlley> Alley;

	UPROPERTY()
	TObjectPtr<ABowlingGame> Game;
};
