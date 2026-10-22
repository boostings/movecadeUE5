#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Net/RoomTypes.h"
#include "Templates/SharedPointer.h"
#include "ArcadeGameModeBase.generated.h"

class FJsonObject;
class IArcadeGame;
class SArcadeOverlay;
class URoomHostSubsystem;
namespace MovecadeKart::Arcade { struct FGameInfo; }
namespace MovecadeKart::Net { struct FSensorFrame; }

/** A local launch's seats, resolved once in StartPlay (see AArcadeGameModeBase). */
struct MOVECADEKART_API FArcadeLaunchOptions
{
	/** People at this screen (keyboard, mouse, pads). */
	int32 Locals = 1;
	/** House bots. */
	int32 Bots = 1;
	/** -<Prefix>Auto: the local seats play themselves (soak tests, screenshots). Never set in Shipping. */
	bool bAutoPlay = false;

	/**
	 * The travel URL's ?Players=&Bots= (the Arcade picker, play again, next game), then the command line's
	 * -<Prefix>Players= -<Prefix>Bots= -<Prefix>Auto on top (tests), clamped to the game's catalog limits.
	 */
	static FArcadeLaunchOptions Parse(const MovecadeKart::Arcade::FGameInfo& Game, const FString& TravelOptions, const TCHAR* CommandLine,
		const FString& Prefix, int32 DefaultLocals, int32 DefaultBots);

	/** Adds house bots until there are at least MinSeats seats (a game that needs an opponent). Locals are kept. */
	void EnsureMinimumSeats(int32 MinSeats)
	{
		Bots = FMath::Max(Bots, MinSeats - Locals);
	}
};

/**
 * The level mode every party game shares (L_Bowling, L_Darts, ...): the loop around a game actor that implements
 * IArcadeGame. StartPlay finds or builds the venue, seats the players, spawns the game and attaches SArcadeOverlay
 * (pause, results, play again, next game) over it; EndPlay undoes all of it.
 *
 * Local play: seats come from FArcadeLaunchOptions and the launch is noted in UArcadeSession, so "play again" and
 * "next game" keep them. Party Mode: when URoomHostSubsystem is running a match of this game, the room's seats play
 * instead; this base binds the room (sensor frames and game events forwarded to the virtuals below, pause, the room's
 * return to its lobby), streams live scores at 1 Hz (only when they change), and offers CompleteRoomMatch and
 * RequestRematch to the game's callbacks. Test hooks (-KartShots, -KartQuitAfter) work in every game.
 *
 * A game derives, fills ArcadeId / CommandLinePrefix / DefaultBots in its constructor and overrides the pure
 * virtuals. Docs/Agents/ARCADE_API.md has the migration recipe.
 */
UCLASS(Abstract)
class MOVECADEKART_API AArcadeGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AArcadeGameModeBase();

	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	/** The game spawns and possesses whatever it needs; there is no default pawn. */
	virtual void RestartPlayer(AController* NewPlayer) override {}

	bool IsRoomMatch() const { return bRoomMatch; }
	const FArcadeLaunchOptions& GetLaunch() const { return Launch; }
	AActor* GetArcadeGameActor() const { return GameActor; }

protected:
	// ---- Per-game configuration: set these in the derived constructor.

	/** The catalog id ("bowling", ...): the room's gameId and the UArcadeSession launch id. */
	FString ArcadeId;
	/** -<Prefix>Players= / -<Prefix>Bots= / -<Prefix>Auto ("Bowl" for -BowlPlayers=). */
	FString CommandLinePrefix;
	/** Seats when neither the travel URL nor the command line says. */
	int32 DefaultLocals = 1;
	int32 DefaultBots = 1;
	/** Where "quit" and the room's return to its lobby go: the front end and the lobby live there. */
	FName FrontEndLevel = TEXT("L_SunsetHarbor");

	// ---- What each game provides.

	/** Finds the venue actor in the level, or builds one. Called first. */
	virtual void SpawnVenue() {}
	/**
	 * The game's own seat rules on top of the catalog's clamp (e.g. InOutLaunch.EnsureMinimumSeats(2)), applied before
	 * BuildLocalSeats and before the launch is noted in UArcadeSession, so "play again" / "next game" carry the seats
	 * that were actually played. Local launches only.
	 */
	virtual void AdjustLaunchSeats(FArcadeLaunchOptions& InOutLaunch) const {}
	/** Party Mode: one seat per room player (sorted by seat, none eliminated, at most the catalog's MaxPlayers). */
	virtual void BuildRoomSeats(TConstArrayView<FMcSeat> Players) PURE_VIRTUAL(AArcadeGameModeBase::BuildRoomSeats, );
	/** Local play: Launch.Locals people then Launch.Bots house bots. */
	virtual void BuildLocalSeats(const FArcadeLaunchOptions& InLaunch) PURE_VIRTUAL(AArcadeGameModeBase::BuildLocalSeats, );
	/** Spawns and sets up the game actor through SpawnArcadeGame<T>() (which registers it with this base). */
	virtual void SpawnGame() PURE_VIRTUAL(AArcadeGameModeBase::SpawnGame, );
	/** Party Mode: wires the game's own room callbacks (turns, toasts, prompts, completion). The room is bound. */
	virtual void BindRoomCallbacks(URoomHostSubsystem& Room) {}
	/** Party Mode: a phone's accepted sensor frame (Seat is the room seat it came from). */
	virtual void HandleSensorFrame(const FMcSeat& Seat, const MovecadeKart::Net::FSensorFrame& Frame) {}
	/** Party Mode: a phone's game event (bowling:release, pad:press, ...). Data may be null. */
	virtual void HandleGameEvent(const FString& FromPlayerId, const FString& Event, const TSharedPtr<FJsonObject>& Data) {}
	/**
	 * Party Mode: every player's score, in a stable order. Return false while there is nothing to report yet (a
	 * countdown); bFinal is the completion (CompleteRoomMatch) rather than a live update.
	 */
	virtual bool CollectScores(TArray<FMcScoreEntry>& OutScores, bool bFinal) const PURE_VIRTUAL(AArcadeGameModeBase::CollectScores, return false;);
	/** The room paused or resumed the match. The default pauses the game through IArcadeGame. */
	virtual void HandleRoomPaused(bool bPaused, const FString& Reason, const FString& PlayerId);

	// ---- Helpers for the game's callbacks.

	/** Spawns TGame (an AActor implementing IArcadeGame) and registers it as this mode's game. */
	template <class TGame>
	TGame* SpawnArcadeGame()
	{
		TGame* Game = GetWorld()->SpawnActor<TGame>();
		RegisterArcadeGame(Game, Game);
		return Game;
	}

	URoomHostSubsystem* GetRoom() const;
	/** Live scores now (also called at 1 Hz): sent only when they differ from the last accepted list. */
	void PushLiveScores();
	/** Party Mode: the final scores (score-update + complete-game, once per match). */
	void CompleteRoomMatch();
	/** Party Mode: the room goes back to its lobby to play again (everyone re-readies there). */
	void RequestRematch();
	/** Back to the front end level (local quit; the room's lobby lives there too). */
	void TravelToFrontEnd();
	/**
	 * Callbacks for a game's OnRematch / OnQuit (weak: harmless after this mode is gone). In Party Mode both rematch
	 * through the room; locally OnRematch is empty (the game restarts itself) and OnQuit travels to the front end.
	 */
	TFunction<void()> MakeRematchCallback();
	TFunction<void()> MakeQuitCallback();

private:
	void RegisterArcadeGame(AActor* Actor, IArcadeGame* Game);
	/** Room players this game seats, or empty when the room is not playing this game. */
	TArray<FMcSeat> GatherRoomPlayers() const;
	void BindRoom(URoomHostSubsystem& Room);
	void UnbindRoom();
	void SetGameInputMode() const;

	UFUNCTION()
	void HandleRoomLobby();

	UFUNCTION()
	void HandleRoomPauseChanged(bool bPaused, const FString& Reason, const FString& PlayerId);

	UPROPERTY()
	TObjectPtr<AActor> GameActor;
	/** GameActor as the interface (no RTTI in this module); valid while GameActor is. */
	IArcadeGame* ArcadeGame = nullptr;

	FArcadeLaunchOptions Launch;
	bool bRoomMatch = false;
	TSharedPtr<SArcadeOverlay> Overlay;

	TWeakObjectPtr<URoomHostSubsystem> BoundRoom;
	FDelegateHandle SensorHandle;
	FDelegateHandle GameEventHandle;
	FTimerHandle LiveScoreTimer;
	TArray<FTimerHandle> TestHookTimers;
	/** The last live list the room accepted (it drops repeats and anything inside its 1 s window). */
	TArray<FMcScoreEntry> LastLiveScores;
};
