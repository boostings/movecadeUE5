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
 * test hooks work here too: -KartShots=t1,t2 and -KartQuitAfter=N. The front end's Arcade picker passes
 * ?Players=N?Bots=N on the travel URL instead.
 *
 * Party Mode: when the room host (URoomHostSubsystem) is playing a bowling match, the room's seats bowl instead,
 * phones by their own release (bowling:release) with their motion driving the arm and guide, room bots by the
 * house AI. The mode announces each turn (advance-turn), sends the phones' toasts (play:feedback) and the replay
 * outcome, streams scores, completes the game, and goes back to the lobby level when the room does.
 */
UCLASS()
class MOVECADEKART_API ABowlingGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABowlingGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	ABowlingGame* GetGame() const { return Game; }

private:
	void SetupTestHooks();
	/** Seats the room's players and binds the room to the game; false when no bowling match is running. */
	bool SetupRoom(TArray<struct FBowlerSeat>& OutSeats);
	void BindRoom();
	void PushScores(bool bFinal);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	bool bRoomMatch = false;
	FDelegateHandle GameEventHandle;
	FDelegateHandle SensorHandle;
	FTimerHandle ScoreTimer;

	UPROPERTY()
	TObjectPtr<ABowlingAlley> Alley;

	UPROPERTY()
	TObjectPtr<ABowlingGame> Game;
};
