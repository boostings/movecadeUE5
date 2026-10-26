#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/Derby/DerbyTypes.h"
#include "DerbyGameMode.generated.h"

class ADerbyBallpark;
class ADerbyGame;

namespace MovecadeKart::Derby
{
	struct FOutcome;
	struct FPitch;
}

/**
 * Home Run Derby's level mode (L_Derby): finds the ballpark (or builds one) and starts a derby. The arcade base does
 * the launch (seats from ?Players=&Bots= or -DerbyPlayers= -DerbyBots= -DerbyAuto, the test hooks, the overlay);
 * the derby's own flags are in FDerbyDebugFlags. Local play: 1-4 batters sharing the controls (turn-based) plus
 * house bots.
 *
 * Party Mode (the motion pad): every seat bats in turn; the mode names the batter up (advance-turn, so only their
 * phone's frames come through), prompts the phones (pad:prompt, mode swing: "Swing your phone like a bat", READY on
 * the A button), turns the batter's sensor frames into swings, sends derby:pitch as each pitch leaves the hand (the
 * phones' timing hint), play:feedback for every pitch, live scores, and completes the match at the end.
 */
UCLASS()
class MOVECADEKART_API ADerbyGameMode : public AArcadeGameModeBase
{
	GENERATED_BODY()

public:
	ADerbyGameMode();

	ADerbyGame* GetGame() const { return Game; }

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
	/** The match seed: the room's (every host of a room's match pitches the same) or the local one. */
	uint32 MatchSeed() const;
	/** The phones' pad prompts for the batter up: the swing prompt for them, "X is batting" for the rest. */
	void PromptPhones(URoomHostSubsystem& Room, const FString& BatterId) const;
	void SendPitch(URoomHostSubsystem& Room, const FString& BatterId, const MovecadeKart::Derby::FPitch& Pitch) const;
	void SendCall(URoomHostSubsystem& Room, const FString& BatterId, const MovecadeKart::Derby::FOutcome& Outcome) const;

	UPROPERTY()
	TObjectPtr<ADerbyBallpark> Park;

	UPROPERTY()
	TObjectPtr<ADerbyGame> Game;

	/** Filled by BuildRoomSeats / BuildLocalSeats, handed to the game in SpawnGame. */
	TArray<FDerbySeat> Seats;
};
