#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Templates/SharedPointer.h"
#include "FishingGameMode.generated.h"

class AFishingLake;
class AFishingGame;
class SArcadeOverlay;
namespace MovecadeKart::Fishing { struct FAngler; }

/**
 * Fishing Frenzy's level mode (L_Fishing): finds the lake (or builds one) and starts a round. Local play: one
 * angler on the keys, mouse or a pad (the game is simultaneous) plus house bots, from ?Players=&Bots= or
 * -FishingPlayers= -FishingBots= -FishingAuto (the local seat fishes by itself), -FishingSeed=, and the Kart test
 * hooks -KartShots= and -KartQuitAfter=.
 *
 * Party Mode (the generic motion pad): every seat is an angler. Phones stream sensor frames (a forward flick casts,
 * an upward jerk hooks, rolling the wrist reels) and the pad's button (pad:press / pad:release) holds a cast; room
 * bots fish by the house AI. The mode announces simultaneous play and the countdown, sends each phone a pad:prompt
 * when its angler's moment changes (mode "flick" to cast and hook, "swing" to reel), play:feedback with haptics on
 * the bite, the catch and the losses, live scores each second, completes the match after the weigh-in and follows
 * the room back to its lobby.
 */
UCLASS()
class MOVECADEKART_API AFishingGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFishingGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	AFishingGame* GetGame() const { return Game; }

	/** The pad:prompt for an angler's state (JSON), public for the tests. */
	static FString PromptJson(const MovecadeKart::Fishing::FAngler& Angler, const FLinearColor& Accent);
	/** The prompt's words and the phone's hint for an angler's moment. */
	static void PromptFields(const MovecadeKart::Fishing::FAngler& Angler, FString& Title, FString& Subtitle, FString& ButtonA, FString& Mode);

private:
	void SetupTestHooks();
	bool SetupRoom(TArray<struct FFishingSeat>& OutSeats);
	void BindRoom();
	void PushScores(bool bFinal);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	UPROPERTY()
	TObjectPtr<AFishingLake> Lake;

	UPROPERTY()
	TObjectPtr<AFishingGame> Game;

	bool bRoomMatch = false;
	TSharedPtr<SArcadeOverlay> Overlay;
	FDelegateHandle GameEventHandle;
	FDelegateHandle SensorHandle;
	FTimerHandle ScoreTimer;
	FString LastScores;
};
