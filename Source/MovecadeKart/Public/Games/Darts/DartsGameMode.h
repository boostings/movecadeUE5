#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DartsGameMode.generated.h"

class ADartsPub;
class ADartsGame;

/**
 * Bullseye Bash's level mode (L_Darts): finds the pub (or builds one) and starts a game. Local play by default:
 * ?Players=N?Bots=N from the Arcade picker, or -DartsPlayers=N -DartsBots=N -DartsAuto on the command line (as
 * Bowling's), plus the Kart test hooks -KartShots= and -KartQuitAfter=.
 *
 * Party Mode: when the room host plays a darts match, the room's seats throw instead: phones by their own release
 * (darts:release), their motion driving the reticle, room bots by the house AI. As on the web (runtime.ts), every
 * phone streams (advance-turn null) and darts:turn gates the throws; the mode sends darts:turn on change and every
 * 250 ms, darts:aim every 80 ms, darts:result, darts:replay-outcome and play:feedback per dart, scores each second,
 * the match countdown and completion, and goes back to the lobby level with the room.
 */
UCLASS()
class MOVECADEKART_API ADartsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADartsGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	ADartsGame* GetGame() const { return Game; }

private:
	void SetupTestHooks();
	bool SetupRoom(TArray<struct FDartsSeat>& OutSeats);
	void BindRoom();
	void PushScores(bool bFinal);
	void PushTurnAndAim();

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	UPROPERTY()
	TObjectPtr<ADartsPub> Pub;

	UPROPERTY()
	TObjectPtr<ADartsGame> Game;

	bool bRoomMatch = false;
	FDelegateHandle GameEventHandle;
	FDelegateHandle SensorHandle;
	FTimerHandle ScoreTimer;
	FTimerHandle AimTimer;
	FString LastTurnKey;
	double TurnPushedAt = -10.0;
};
