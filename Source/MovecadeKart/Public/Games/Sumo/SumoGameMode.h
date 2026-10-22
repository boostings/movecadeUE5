#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/SharedPointer.h"
#include "SumoGameMode.generated.h"

class ASumoArena;
class ASumoGame;
class SArcadeOverlay;

/**
 * Sumo Bumpers' level mode (L_Sumo): finds the arena (or builds one) and starts a match. Local play: one player on
 * a pad or the keys (the game is simultaneous) plus house bots, from ?Players=&Bots= or -SumoPlayers= -SumoBots=
 * -SumoAuto (the local seat plays itself) -SumoSkill= (the bots' skill, 0..1), and the Kart test hooks -KartShots=
 * and -KartQuitAfter=.
 *
 * Party Mode (the generic motion pad): every seat is a puck; phones roll by their tilt (sensor frames) and dash on
 * pad:press, room bots by the house AI. The mode announces simultaneous play and the countdown, sends each phone a
 * pad:prompt ("Tilt to roll · A: dash") every round, play:feedback for knockouts, round wins and power-ups, haptics
 * on hits, live scores each second, and completes the match after the showcase.
 */
UCLASS()
class MOVECADEKART_API ASumoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASumoGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	ASumoGame* GetGame() const { return Game; }

private:
	void SetupTestHooks();
	bool SetupRoom(TArray<struct FSumoSeat>& OutSeats, uint32& OutSeed);
	void BindRoom();
	void SendPrompts();
	void PushScores(bool bFinal);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	UPROPERTY()
	TObjectPtr<ASumoArena> Arena;

	UPROPERTY()
	TObjectPtr<ASumoGame> Game;

	bool bRoomMatch = false;
	TSharedPtr<SArcadeOverlay> Overlay;
	FDelegateHandle GameEventHandle;
	FDelegateHandle SensorHandle;
	FTimerHandle ScoreTimer;
	FString LastScores;
};
