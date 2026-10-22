#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Store/KartStoreBackend.h"
#include "Store/KartStoreCatalog.h"
#include "KartStore.generated.h"

/** The store's local state on disk (Saved/SaveGames/KartStore.sav): what's equipped, play stats, the owned cache. */
UCLASS()
class MOVECADEKART_API UKartStoreSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 Version = 1;

	/** The last inventory the backend reported, so owned items still show offline. Restore replaces it. */
	UPROPERTY()
	TArray<int32> OwnedCache;

	/** Slot id ("kart-paint") -> def id. */
	UPROPERTY()
	TMap<FName, int32> Equipped;

	UPROPERTY()
	TMap<FName, int32> Stats;

	UPROPERTY()
	double PlaySeconds = 0.0;
};

namespace MovecadeKart::Store
{
	/** How an item reads in the store. */
	enum class EItemState : uint8
	{
		ComingSoon,
		ForSale,
		/** A purchase or a grant is in flight. */
		Pending,
		/** Earned item still being earned (see GetProgress). */
		Locked,
		/** Drop item not dropped yet. */
		NotDropped,
		Owned,
		Equipped,
	};

	/**
	 * The cosmetics store: the catalog, what the player owns and wears, the purchase flow, restore, the free unlocks
	 * and their progress. Plain C++ around one IKartStoreBackend so the tests drive it with the mock and a manual
	 * clock; UKartStoreSubsystem owns the game's instance.
	 */
	class MOVECADEKART_API FKartStore
	{
	public:
		/** SaveSlot empty keeps everything in memory. */
		FKartStore(TUniquePtr<IKartStoreBackend> InBackend, const FString& InSaveSlot);
		~FKartStore();

		/** Loads the save, then refreshes the inventory and prices. */
		void Start();
		void Tick(double DeltaSeconds);

		// Queries ----------------------------------------------------------------------------------------------
		bool IsOwned(int32 DefId) const;
		/** Bundles: how many of the contents are owned already. */
		int32 NumOwnedContents(int32 DefId) const;
		bool IsEquipped(int32 DefId) const;
		/** The def id worn in Slot, or 0. */
		int32 GetEquipped(ESlot Slot) const;
		const FItem* GetEquippedItem(ESlot Slot) const;
		EItemState GetState(int32 DefId) const;
		/** True and the price when it can be bought now. */
		bool CanPurchase(int32 DefId) const;
		/** The backend gave a price for it. */
		bool HasPrice(int32 DefId) const { return Prices.Contains(DefId); }
		/** "$1.99", "Free", "Price unavailable". */
		FText GetPriceText(int32 DefId) const;
		/** Earned items: current value and goal of the stat. False for others. */
		bool GetProgress(int32 DefId, int32& OutCurrent, int32& OutTarget) const;
		int32 GetStat(FName Stat) const;
		bool IsPurchasing() const { return PurchasingDef != 0; }
		int32 GetPurchasingDef() const { return PurchasingDef; }
		bool IsRestoring() const { return bRestoring; }
		bool IsOnline() const { return Backend->IsOnline(); }
		/** True once the first inventory refresh came back. */
		bool HasInventory() const { return bHasInventory; }
		IKartStoreBackend& GetBackend() const { return *Backend; }
		double GetPlaySeconds() const { return PlaySeconds; }

		// Actions ----------------------------------------------------------------------------------------------
		/** Starts checkout; the end arrives through OnPurchaseFinished. False (and an immediate event) if it can't start. */
		bool Purchase(int32 DefId);
		/** Asks the backend for the whole inventory again (refunds leave, purchases on another PC arrive). */
		void Restore();
		bool Equip(int32 DefId);
		void Unequip(ESlot Slot);
		/** Counts towards the free unlocks. */
		void AddStat(FName Stat, int32 Delta = 1);
		/** Play time towards the playtime drops (checks every DropCheckSeconds). */
		void AddPlaytime(double Seconds);
		/** Forgets the local cache, equips and stats (not the backend's inventory). */
		void ResetLocal();
		void Save() const;

		static constexpr double DropCheckSeconds = 600.0;

		/** Anything visible changed (ownership, equips, progress, prices, pending). */
		TMulticastDelegate<void()> OnChanged;
		TMulticastDelegate<void(int32 DefId, EPurchaseResult Result, const FString& Detail)> OnPurchaseFinished;
		/** A free item arrived (earned or dropped). */
		TMulticastDelegate<void(int32 DefId)> OnUnlocked;
		TMulticastDelegate<void(bool bOk)> OnRestored;

	private:
		void Load();
		void SetOwned(const TSet<int32>& NewOwned, bool bAnnounceNewFree);
		void EvaluateUnlocks();
		void FinishPurchase(int32 DefId, EPurchaseResult Result, const FString& Detail);
		void Changed() { OnChanged.Broadcast(); }

		TUniquePtr<IKartStoreBackend> Backend;
		FString SaveSlot;
		TSet<int32> Owned;
		TMap<ESlot, int32> Equipped;
		TMap<FName, int32> Stats;
		TMap<int32, FPrice> Prices;
		/** Earned grants in flight. */
		TSet<int32> Granting;
		int32 PurchasingDef = 0;
		bool bRestoring = false;
		bool bHasInventory = false;
		double PlaySeconds = 0.0;
		double SinceDropCheck = 0.0;
		bool bDropInFlight = false;
		/** Shared with callbacks so a callback after destruction is a no-op. */
		TSharedRef<bool> Alive = MakeShared<bool>(true);
	};
}
