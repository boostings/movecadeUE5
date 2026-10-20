#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Sim/KartTrack.h"
#include "KartGameMode.generated.h"

class AKartRaceManager;
class ATrackBuilder;

/**
 * Sets up a race on the level's track: finds the ATrackBuilder (or spawns one for DefaultTrackId),
 * builds the roster, gives every kart its own viewport (the web host renders all karts, bots
 * included), and hands over to AKartRaceManager.
 *
 * Command line: -KartBots=N (default 3), -KartLocalPlayers=M (default 1; extra players use gamepads
 * 2..M), -KartSoloView (only seat 0 on screen), -KartSkipIntro, -KartAutoDrive (local seats drive
 * themselves), -KartLaps=N (shorter races for tests), -KartShots=2,8 (screenshots with UI at those
 * seconds), -KartQuitAfter=20.
 *
 * Without any of those flags (or -KartNoFrontEnd), a launch opens the front end on this level (UI/AKartFrontEnd).
 * UKartSessionSettings decides per level: menus, a local race from the menu's setup, or the phone room.
 */
UCLASS()
class MOVECADEKART_API AKartGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKartGameMode();

	/** Karts are spawned and possessed by the race manager, not here. */
	virtual void RestartPlayer(AController* NewPlayer) override {}
	virtual void StartPlay() override;

	ATrackBuilder* GetTrackBuilder();
	AKartRaceManager* GetRaceManager() const { return RaceManager; }

	UPROPERTY(EditAnywhere, Category = "Kart")
	FString DefaultTrackId = TEXT("sunset-harbor");

	UPROPERTY(EditAnywhere, Category = "Kart")
	int32 DefaultBots = 3;

private:
	void SetupTestHooks();
	void SetupLocalRace(TSharedPtr<const MovecadeKart::FTrack> Track);
	TArray<APlayerController*> MakeViewers(int32 Karts, int32 LocalPlayers, bool bSoloView);

	// Room mode (-KartRoom): phones join through the lobby; the room starts the race (spec 04 §5).
	void SetupRoom();
	void BuildRoomRace(TSharedPtr<const MovecadeKart::FTrack> Track);
	void SendLiveScores();

	UFUNCTION()
	void HandleRoomStart(const FString& MapId);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	bool bRoomMode = false;
	bool bRaceBuilt = false;
	/** Race seat -> room seat index (0..3), or -1 for a bot. */
	TArray<int32> RoomSeatOf;
	TArray<FString> RoomPlayerOf;
	FTimerHandle LiveScoreTimer;

	UPROPERTY()
	TObjectPtr<ATrackBuilder> TrackBuilder;

	UPROPERTY()
	TObjectPtr<AKartRaceManager> RaceManager;
};
