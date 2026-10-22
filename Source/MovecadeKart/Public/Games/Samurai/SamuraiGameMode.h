#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/SharedPointer.h"
#include "SamuraiGameMode.generated.h"

class ASamuraiDojo;
class ASamuraiGame;
class SArcadeOverlay;

/**
 * Samurai Slice's level mode (L_Samurai): finds the dojo (or builds one) and starts a round. Local play: one
 * swordsman on the mouse or a pad (the game is simultaneous, so a second person on the same keyboard has nothing
 * to hold) plus house bots, from ?Players=&Bots= or -SamuraiPlayers= -SamuraiBots= -SamuraiAuto, and the Kart test
 * hooks -KartShots= and -KartQuitAfter=.
 *
 * Party Mode: when the room host plays a samurai match, every seat is a lane: phones swing their blades (their
 * attitude and sweep arrive as sensor frames), room bots cut by the house AI. The mode announces simultaneous play
 * (advance-turn null) and the match countdown, sends play:feedback for every cut, bomb and miss, scores each
 * second, completes the match and follows the room back to its lobby.
 */
UCLASS()
class MOVECADEKART_API ASamuraiGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASamuraiGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	ASamuraiGame* GetGame() const { return Game; }

private:
	void SetupTestHooks();
	bool SetupRoom(TArray<struct FSamuraiSeat>& OutSeats, uint32& OutSeed);
	void BindRoom();
	void PushScores(bool bFinal);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	UPROPERTY()
	TObjectPtr<ASamuraiDojo> Dojo;

	UPROPERTY()
	TObjectPtr<ASamuraiGame> Game;

	bool bRoomMatch = false;
	/** Pause, results and the play-again / next-game loop over the game. */
	TSharedPtr<SArcadeOverlay> Overlay;
	FDelegateHandle SensorHandle;
	FTimerHandle ScoreTimer;
	/** The last live scores sent ("id=score;..."), so unchanged scores aren't resent each second. */
	FString LastScores;
};
