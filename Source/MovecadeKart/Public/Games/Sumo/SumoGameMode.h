#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/Sumo/SumoDebugFlags.h"
#include "Games/Sumo/SumoGameTypes.h"
#include "SumoGameMode.generated.h"

class ASumoArena;
class ASumoGame;
namespace MovecadeKart::Sumo { struct FRingEvent; }

/**
 * Sumo Bumpers' level mode (L_Sumo). AArcadeGameModeBase runs the loop (seats, room binding, live scores, the arcade
 * overlay, test hooks); this mode finds or builds the arena, seats the wrestlers and spawns ASumoGame.
 *
 * Local play: one player on a pad or the keys (the game is simultaneous) plus house bots of mixed skill, from
 * ?Players=&Bots= or -SumoPlayers= -SumoBots= -SumoAuto (the local seat plays itself), and FSumoDebugFlags
 * (-SumoSkill=, -SumoSeed=). A sumo needs an opponent: a launch with fewer than two seats gets house bots up to two.
 *
 * Party Mode (the generic motion pad): every room seat is a puck; phones roll by their tilt (sensor frames) and dash
 * on pad:press, room bots by the house AI, and a lone phone gets a house sparring partner. The mode announces the
 * simultaneous turn and the countdown, sends each phone a pad:prompt ("Tilt to roll · A: dash") every round,
 * play:feedback for knockouts, round wins and power-ups, haptics on hits, and completes the match after the showcase.
 */
UCLASS()
class MOVECADEKART_API ASumoGameMode : public AArcadeGameModeBase
{
	GENERATED_BODY()

public:
	ASumoGameMode();

	virtual void StartPlay() override;

	ASumoGame* GetGame() const { return Game; }

protected:
	virtual void SpawnVenue() override;
	virtual void AdjustLaunchSeats(FArcadeLaunchOptions& InOutLaunch) const override;
	virtual void BuildRoomSeats(TConstArrayView<FMcSeat> Players) override;
	virtual void BuildLocalSeats(const FArcadeLaunchOptions& InLaunch) override;
	virtual void SpawnGame() override;
	virtual void BindRoomCallbacks(URoomHostSubsystem& Room) override;
	virtual void HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame) override;
	virtual void HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data) override;
	virtual bool CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceControlScheme> MakeDeviceControls() override;

private:
	void StartRoomTurn() const;
	/** The motion pad's prompt for every phone, in its colour, at a round's countdown. */
	void SendPrompts() const;
	/** The room's side of a rules event: haptics for hits, toasts for knockouts, round wins and power-ups. */
	void SendEventFeedback(const MovecadeKart::Sumo::FRingEvent& Event) const;
	void SendHitHaptics(URoomHostSubsystem& Room, const MovecadeKart::Sumo::FRingEvent& Event) const;
	void SendRingOutFeedback(URoomHostSubsystem& Room, const MovecadeKart::Sumo::FRingEvent& Event) const;
	void SendRoundWinFeedback(URoomHostSubsystem& Room, const MovecadeKart::Sumo::FRingEvent& Event) const;
	void SendPowerFeedback(URoomHostSubsystem& Room, const MovecadeKart::Sumo::FRingEvent& Event) const;
	bool IsPhone(int32 Slot) const;

	UPROPERTY()
	TObjectPtr<ASumoArena> Arena;

	UPROPERTY()
	TObjectPtr<ASumoGame> Game;

	/** Filled by BuildRoomSeats / BuildLocalSeats, handed to the game by SpawnGame. */
	TArray<FSumoSeat> Seats;
	uint32 Seed = 0;
	/** Parsed once in StartPlay. */
	FSumoDebugFlags DebugFlags;
};
