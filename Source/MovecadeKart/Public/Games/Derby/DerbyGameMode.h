#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/SharedPointer.h"
#include "DerbyGameMode.generated.h"

class ADerbyBallpark;
class ADerbyGame;
class SArcadeOverlay;

/**
 * Home Run Derby's level mode (L_Derby): finds the ballpark (or builds one) and starts a derby. Local play: 1-4
 * batters sharing the controls (turn-based) plus house bots, from ?Players=&Bots= or -DerbyPlayers= -DerbyBots=
 * -DerbyAuto (the local seats bat as bots) -DerbySeed=, and the Kart test hooks -KartShots= and -KartQuitAfter=.
 *
 * Party Mode (the motion pad): every seat bats in turn; the mode names the batter up (advance-turn, so only their
 * phone's frames come through), prompts the phones (pad:prompt, mode swing: "Swing your phone like a bat", READY on
 * the A button), turns the batter's sensor frames into swings, sends derby:pitch as each pitch leaves the hand (the
 * phones' timing hint), play:feedback for every pitch, live scores, and completes the match at the end.
 */
UCLASS()
class MOVECADEKART_API ADerbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADerbyGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	ADerbyGame* GetGame() const { return Game; }

private:
	void SetupTestHooks();
	bool SetupRoom(TArray<struct FDerbySeat>& OutSeats);
	void BindRoom();
	void PushScores(bool bFinal);
	void Prompt(const FString& BatterId);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	UPROPERTY()
	TObjectPtr<ADerbyBallpark> Park;

	UPROPERTY()
	TObjectPtr<ADerbyGame> Game;

	bool bRoomMatch = false;
	TSharedPtr<SArcadeOverlay> Overlay;
	FDelegateHandle GameEventHandle;
	FDelegateHandle SensorHandle;
	FTimerHandle ScoreTimer;
	FString LastScores;
};
