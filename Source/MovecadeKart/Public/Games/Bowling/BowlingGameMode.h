#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/Bowling/BowlingTypes.h"
#include "BowlingGameMode.generated.h"

class ABowlingAlley;
class ABowlingGame;

/**
 * Bowling's level mode (L_Bowling): finds the alley (or builds one) and starts a game on it. The loop around the game
 * (seats from ?Players=&Bots= or -BowlPlayers= / -BowlBots= / -BowlAuto, the overlay, the test hooks, the room
 * binding, live scores) is AArcadeGameModeBase's. Local bowlers share the keyboard, mouse and pads, since only one
 * bowls at a time.
 *
 * Party Mode: the room's seats bowl, phones by their own release (bowling:release) with their motion driving the arm
 * and guide, room bots by the house AI. This mode announces each turn (advance-turn) and sends the phones' toasts
 * (play:feedback) and the replay outcome after every roll.
 */
UCLASS()
class MOVECADEKART_API ABowlingGameMode : public AArcadeGameModeBase
{
	GENERATED_BODY()

public:
	ABowlingGameMode();

	ABowlingGame* GetGame() const { return Game; }

protected:
	virtual void SpawnVenue() override;
	virtual void BuildRoomSeats(TConstArrayView<FMcSeat> Players) override;
	virtual void BuildLocalSeats(const FArcadeLaunchOptions& InLaunch) override;
	virtual void SpawnGame() override;
	virtual void BindRoomCallbacks(URoomHostSubsystem& Room) override;
	virtual void HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame) override;
	virtual void HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data) override;
	virtual bool CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceControlScheme> MakeDeviceControls() override;

private:
	/** The phone's toast and haptic for a committed roll, and the replay outcome for every phone. */
	void SendRollFeedback(URoomHostSubsystem& Room, const FString& PlayerId, int32 Pins, EBowlingBeat Beat) const;

	/** Filled by Build*Seats, handed to the game in SpawnGame. */
	TArray<FBowlerSeat> Seats;

	UPROPERTY()
	TObjectPtr<ABowlingAlley> Alley;

	UPROPERTY()
	TObjectPtr<ABowlingGame> Game;
};
