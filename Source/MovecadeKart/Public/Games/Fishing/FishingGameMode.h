#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeGameModeBase.h"
#include "Games/Fishing/FishingDebugFlags.h"
#include "Games/Fishing/FishingGame.h"
#include "FishingGameMode.generated.h"

class AFishingLake;
namespace MovecadeKart::Fishing { struct FAngler; struct FEffect; }

/**
 * Fishing Frenzy's level mode (L_Fishing), on the shared AArcadeGameModeBase: finds the lake (or builds one), seats
 * the anglers and starts a round. Local play: one angler on the keys, mouse or a pad (the game is simultaneous) plus
 * house bots, from ?Players=&Bots= or -FishingPlayers= -FishingBots= -FishingAuto; the game's own debug flags are in
 * FFishingDebugFlags.
 *
 * Party Mode (the generic motion pad): every seat is an angler. Phones stream sensor frames (a forward flick casts,
 * an upward jerk hooks, rolling the wrist reels) and the pad's button (pad:press / pad:release) holds a cast; room
 * bots fish by the house AI. On top of the base's room loop this mode announces simultaneous play and the countdown,
 * sends each phone a pad:prompt whenever its angler's moment changes (mode "flick" to cast and hook, "swing" to reel)
 * and again when the horn goes, and play:feedback with haptics on the bite, the catch and the losses.
 */
UCLASS()
class MOVECADEKART_API AFishingGameMode : public AArcadeGameModeBase
{
	GENERATED_BODY()

public:
	AFishingGameMode();

	AFishingGame* GetGame() const { return Game; }

	/** The pad:prompt for an angler's state (JSON), public for the tests. */
	static FString PromptJson(const MovecadeKart::Fishing::FAngler& Angler, const FLinearColor& Accent);
	/** The prompt's words and the phone's hint for an angler's moment. */
	static void PromptFields(const MovecadeKart::Fishing::FAngler& Angler, FString& Title, FString& Subtitle, FString& ButtonA, FString& Mode);

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
	/** Sends Angler's prompt when its seat is a phone (bots and local seats have no phone to tell). */
	void SendPromptIfPhone(URoomHostSubsystem& Room, const MovecadeKart::Fishing::FAngler& Angler) const;
	/** The phone's toast and haptics for an effect on its angler. */
	void SendEffectFeedback(URoomHostSubsystem& Room, const MovecadeKart::Fishing::FEffect& Effect) const;
	bool IsPhone(const FString& PlayerId) const;

	UPROPERTY()
	TObjectPtr<AFishingLake> Lake;

	UPROPERTY()
	TObjectPtr<AFishingGame> Game;

	/** Filled by BuildRoomSeats / BuildLocalSeats, consumed by SpawnGame. */
	TArray<FFishingSeat> Seats;
	FFishingDebugFlags DebugFlags;
};
