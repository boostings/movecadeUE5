#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/Darts/DartsGameTypes.h"
#include "DartsGameMode.generated.h"

class ADartsGame;
class ADartsPub;

/**
 * Bullseye Bash's level mode (L_Darts): finds the pub (or builds one), seats the throwers and starts an ADartsGame.
 * AArcadeGameModeBase does the shared loop: ?Players=&Bots= or -DartsPlayers= -DartsBots= -DartsAuto, the test
 * hooks, the overlay, Party Mode's binding, live scores and completion. -DartsLocalAuto and -DartsShotBig are
 * Darts' own (FDartsDebugFlags).
 *
 * Party Mode: the room's seats throw: phones by their own release (darts:release), their motion driving the
 * reticle, room bots by the house AI. As on the web (runtime.ts), every phone streams (advance-turn null) and
 * darts:turn gates the throws; this mode sends darts:turn on change and every 250 ms, darts:aim every 80 ms, and
 * darts:result, darts:replay-outcome and play:feedback per dart, plus the match countdown.
 */
UCLASS()
class MOVECADEKART_API ADartsGameMode : public AArcadeGameModeBase
{
	GENERATED_BODY()

public:
	ADartsGameMode();

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	ADartsGame* GetGame() const { return Game; }

protected:
	virtual void SpawnVenue() override;
	virtual void BuildRoomSeats(TConstArrayView<FMcSeat> Players) override;
	virtual void BuildLocalSeats(const FArcadeLaunchOptions& InLaunch) override;
	virtual void SpawnGame() override;
	virtual void BindRoomCallbacks(URoomHostSubsystem& Room) override;
	virtual void HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame) override;
	virtual void HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data) override;
	virtual bool CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const override;

private:
	/** darts:turn (on a change, else at most every 250 ms) and darts:aim for the thrower up. */
	void PushTurnAndAim();
	/** -DartsLocalAuto: grips after a beat and lets go in the sweet spot, through the game's test grip. */
	void TickLocalAutoPlay();

	UPROPERTY()
	TObjectPtr<ADartsPub> Pub;

	UPROPERTY()
	TObjectPtr<ADartsGame> Game;

	/** Filled by BuildRoomSeats / BuildLocalSeats, consumed by SpawnGame. */
	TArray<FDartsSeat> Seats;

	FTimerHandle AimTimer;
	FString LastTurnJson;
	double TurnSentAt = -10.0;

	FTimerHandle LocalAutoTimer;
	double LocalAutoGripAt = -1.0;
};
