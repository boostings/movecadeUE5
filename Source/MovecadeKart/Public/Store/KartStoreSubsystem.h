#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Store/KartStore.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KartStoreSubsystem.generated.h"

/**
 * The game's cosmetics store (MovecadeKart::Store::FKartStore) for the life of the game instance. Picks the backend:
 * Steam Inventory when the build has Steamworks and Steam is running (-KartStoreBackend=mock forces the mock),
 * otherwise the offline mock (purchases disabled in Shipping). Ticks the store, counts play time, and takes the
 * play stats the games report (NoteArcadeGame, NoteRaceFinished).
 */
UCLASS()
class MOVECADEKART_API UKartStoreSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UKartStoreSubsystem* Get(const UObject* WorldContext);
	/** The live store of the running game, or the one a test installed; may be null (commandlets, tests). */
	static MovecadeKart::Store::FKartStore* Active();
	/** Tests: makes Store the one FKartCosmetics reads (null restores the subsystem's). */
	static void SetActiveForTests(MovecadeKart::Store::FKartStore* Store);

	/** Spacewar, Valve's public test app: what development builds run as until the real App ID exists. */
	static constexpr int32 DevAppId = 480;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	MovecadeKart::Store::FKartStore* GetStore() const { return Store.Get(); }

	/** A finished party game (the arcade session's results). bHumans: anyone but house bots played. */
	static void NoteArcadeGame(const UObject* WorldContext, const FString& GameId, bool bHumans);
	/** A finished race; bHumanWon: the winner was a player, not a bot. */
	static void NoteRaceFinished(const UObject* WorldContext, bool bHumans, bool bHumanWon);

private:
	bool Tick(float DeltaSeconds);

	TUniquePtr<MovecadeKart::Store::FKartStore> Store;
	FTSTicker::FDelegateHandle TickHandle;
};
