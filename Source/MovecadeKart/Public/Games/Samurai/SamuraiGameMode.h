#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/Samurai/SamuraiRules.h"
#include "Games/Samurai/SamuraiTypes.h"
#include "SamuraiGameMode.generated.h"

class ASamuraiDojo;
class ASamuraiGame;

/**
 * Samurai Slice's level mode (L_Samurai): finds the dojo (or builds one) and starts a round; AArcadeGameModeBase
 * runs the shared loop (launch options, room binding, live scores, overlay, test hooks). Local play: one swordsman
 * on the mouse or a pad (the game is simultaneous, so a second person on the same keyboard has nothing to hold;
 * the catalog's MaxLocal is 1) plus house bots, from ?Players=&Bots= or -SamuraiPlayers= -SamuraiBots= -SamuraiAuto.
 *
 * Party Mode: every room seat is a lane: phones swing their blades (their attitude and sweep arrive as sensor
 * frames), room bots cut by the house AI. The mode announces simultaneous play (advance-turn null) and the match
 * countdown, and sends play:feedback for every cut, bomb and miss.
 */
UCLASS()
class MOVECADEKART_API ASamuraiGameMode : public AArcadeGameModeBase
{
	GENERATED_BODY()

public:
	ASamuraiGameMode();

	ASamuraiGame* GetGame() const { return Game; }

protected:
	virtual void SpawnVenue() override;
	virtual void BuildRoomSeats(TConstArrayView<FMcSeat> Players) override;
	virtual void BuildLocalSeats(const FArcadeLaunchOptions& InLaunch) override;
	virtual void SpawnGame() override;
	virtual void BindRoomCallbacks(URoomHostSubsystem& Room) override;
	virtual void HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame) override;
	virtual bool CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const override;
	virtual TSharedPtr<MovecadeKart::Input::IDeviceControlScheme> MakeDeviceControls() override;

private:
	/** The phone's toast for a cut, bomb or miss (the room relays it as play:feedback). */
	void SendEffectFeedback(URoomHostSubsystem& Room, const MovecadeKart::Samurai::FEffect& Effect) const;

	UPROPERTY()
	TObjectPtr<ASamuraiDojo> Dojo;

	UPROPERTY()
	TObjectPtr<ASamuraiGame> Game;

	/** Filled by BuildRoomSeats / BuildLocalSeats, consumed by SpawnGame. */
	UPROPERTY()
	TArray<FSamuraiSeat> Seats;

	uint32 Seed = MovecadeKart::Samurai::DefaultSeed;
};
