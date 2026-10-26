#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/Stacker/StackerTypes.h"
#include "StackerGameMode.generated.h"

class AStackerRooftop;
class AStackerGame;
namespace MovecadeKart::Stacker { struct FEffect; }

/**
 * Sky Stacker's level mode (L_Stacker): finds the rooftop (or builds one) and starts a round; AArcadeGameModeBase
 * does the rest of the arcade loop. Local play: one builder on a pad, the keys or the mouse (the game is
 * simultaneous, so one pair of hands) plus house bots, from ?Players=&Bots= or -StackerPlayers= -StackerBots=
 * -StackerAuto.
 *
 * Party Mode: every room seat is a lane: phones balance their towers (their attitude arrives in sensor frames) and
 * drop on stacker:drop, room bots build by the house AI. The mode announces simultaneous play (advance-turn null)
 * and the countdown, sends play:feedback for every drop and topple, and completes the match after the showcase.
 */
UCLASS()
class MOVECADEKART_API AStackerGameMode : public AArcadeGameModeBase
{
	GENERATED_BODY()

public:
	AStackerGameMode();

	AStackerGame* GetGame() const { return Game; }

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
	/** runtime.ts 540-549: the phone's toast (and haptic) for a drop or a topple. */
	void SendEffectFeedback(const MovecadeKart::Stacker::FEffect& Effect) const;

	UPROPERTY()
	TObjectPtr<AStackerRooftop> Rooftop;

	UPROPERTY()
	TObjectPtr<AStackerGame> Game;

	/** Filled by BuildRoomSeats / BuildLocalSeats, handed to the game in SpawnGame. */
	TArray<FStackerSeat> Seats;
};
