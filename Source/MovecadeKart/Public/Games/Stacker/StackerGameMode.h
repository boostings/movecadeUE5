#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StackerGameMode.generated.h"

class AStackerRooftop;
class AStackerGame;

/**
 * Sky Stacker's level mode (L_Stacker): finds the rooftop (or builds one) and starts a round. Local play: one
 * builder on a pad, the keys or the mouse (the game is simultaneous) plus house bots, from ?Players=&Bots= or
 * -StackerPlayers= -StackerBots= -StackerAuto, and the Kart test hooks -KartShots= and -KartQuitAfter=.
 *
 * Party Mode: when the room host plays a stacker match, every seat is a lane: phones balance their towers (their
 * attitude arrives in sensor frames) and drop on stacker:drop, room bots build by the house AI. The mode announces
 * simultaneous play (advance-turn null) and the countdown, sends play:feedback for every drop and topple, scores
 * each second, completes the match after the showcase and follows the room back to its lobby.
 */
UCLASS()
class MOVECADEKART_API AStackerGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AStackerGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	AStackerGame* GetGame() const { return Game; }

private:
	void SetupTestHooks();
	bool SetupRoom(TArray<struct FStackerSeat>& OutSeats);
	void BindRoom();
	void PushScores(bool bFinal);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	UPROPERTY()
	TObjectPtr<AStackerRooftop> Rooftop;

	UPROPERTY()
	TObjectPtr<AStackerGame> Game;

	bool bRoomMatch = false;
	FDelegateHandle GameEventHandle;
	FDelegateHandle SensorHandle;
	FTimerHandle ScoreTimer;
	/** Live scores go out only when they change (runtime.ts). */
	FString LastScores;
};
