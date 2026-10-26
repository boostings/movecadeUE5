#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Sim/KartTrack.h"
#include "KartGameMode.generated.h"

class AKartRaceManager;
class ATrackBuilder;
class URoomHostSubsystem;
struct FKartResult;
struct FKartSeatSpec;
namespace MovecadeKart::Input { class FKartDeviceSeat; }

/**
 * Sets up a race on the level's track: finds the ATrackBuilder (or spawns one for DefaultTrackId),
 * builds the roster, gives every kart its own viewport (the web host renders all karts, bots
 * included), and hands over to AKartRaceManager.
 *
 * Command line: -KartBots=N (default 3), -KartLocalPlayers=M (default 1; extra players use gamepads
 * 2..M), -KartSoloView (only seat 0 on screen), -KartSkipIntro, -KartAutoDrive (local seats drive
 * themselves), -KartLaps=N (shorter races for tests), and the shared test hooks (FArcadeTestHooks)
 * -KartShots=2,8 (screenshots with UI at those seconds), -KartQuitAfter=20.
 *
 * Without any of those flags (or -KartNoFrontEnd), a launch opens the front end on this level (UI/AKartFrontEnd).
 * UKartSessionSettings decides per level: menus, a local race from the menu's setup, or the phone room.
 *
 * Party Mode (-KartRoom, or the menus' Party entry): the lobby (UKartLobbyWidget) gathers the phones; when the room
 * starts a kart match this mode builds the race from the room's seats (plus -KartBots house karts), feeds it the
 * phones' input, publishes phases, haptics and live scores, and submits the results. A party game's match travels
 * to that game's level instead.
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
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	ATrackBuilder* GetTrackBuilder();
	AKartRaceManager* GetRaceManager() const { return RaceManager; }
	/**
	 * A local race played with this device's own controls (Input/KartDeviceControls.h): the first local seat's tilt
	 * and touch buttons, which AKartPlayerController adds to its keys. Null without device controls.
	 */
	MovecadeKart::Input::FKartDeviceSeat* GetDeviceSeat() const { return DeviceSeat.Get(); }

	UPROPERTY(EditAnywhere, Category = "Kart")
	FString DefaultTrackId = TEXT("sunset-harbor");

	UPROPERTY(EditAnywhere, Category = "Kart")
	int32 DefaultBots = 3;

private:
	void SetupLocalRace(TSharedPtr<const MovecadeKart::FTrack> Track);
	void ActivateDeviceControls();
	TArray<APlayerController*> MakeViewers(int32 Karts, int32 LocalPlayers, bool bSoloView);
	URoomHostSubsystem* GetRoom() const;

	// Party Mode: phones join through the lobby; the room starts the race (spec 04 §5).
	void SetupRoom();
	/** Leaving this level: unbinds from the room, which outlives it. */
	void StopListeningToRoom();
	void BuildRoomRace(TSharedPtr<const MovecadeKart::FTrack> Track);
	/** The room's players (up to the grid), then Bots house karts; fills the race-seat -> room maps. */
	TArray<FKartSeatSpec> BuildRoomSeats(const URoomHostSubsystem& Room, int32 GridSize, int32 Bots);
	/** The race manager's input, phase, haptic and result callbacks, wired to the room. */
	void BindRaceToRoom(URoomHostSubsystem& Room);
	/** The room seat (0..3) racing in RaceSeat, or INDEX_NONE for a house kart. */
	int32 RoomSeatFor(int32 RaceSeat) const;
	void SubmitRoomResults(const TArray<FKartResult>& Results);
	void SendLiveScores();

	UFUNCTION()
	void HandleRoomStart(const FString& MapId);

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPause(bool bPaused, const FString& Reason, const FString& PlayerId);

	bool bRoomMode = false;
	bool bRaceBuilt = false;
	/** Race seat -> room seat index (0..3), or INDEX_NONE for a house kart. */
	TArray<int32> RoomSeatOf;
	/** Race seat -> room player id, or empty for a house kart. */
	TArray<FString> RoomPlayerOf;
	FTimerHandle LiveScoreTimer;
	TArray<FTimerHandle> TestHookTimers;
	TSharedPtr<MovecadeKart::Input::FKartDeviceSeat> DeviceSeat;

	UPROPERTY()
	TObjectPtr<ATrackBuilder> TrackBuilder;

	UPROPERTY()
	TObjectPtr<AKartRaceManager> RaceManager;
};
