#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreBackend.h"
#include "Store/KartStoreCatalog.h"

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
		/** Paid item the local cache lists but the backend hasn't confirmed this session (offline): shown, not worn. */
		OwnedUnconfirmed,
		Owned,
		Equipped,
	};

	/**
	 * The cosmetics store: the catalog, what the player owns and wears, the purchase flow, restore, the free unlocks
	 * and their progress. Plain C++ around one IKartStoreBackend so the tests drive it with the mock and a manual
	 * clock; UKartStoreSubsystem owns the game's instance.
	 *
	 * Trust: the save's owned list is only a cache (anyone can edit it). Paid items are worn only once the backend
	 * has confirmed them this session (an inventory refresh, a checkout); until then they show as owned but
	 * unconfirmed, and the equip choice is kept for when Steam answers. Free items can be worn from the cache.
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
		/** Owned as far as this PC knows (the backend's answer, or the cache until it answers). */
		bool IsOwned(int32 DefId) const;
		/** Owned and safe to wear: free, or paid and confirmed by the backend this session. */
		bool IsConfirmed(int32 DefId) const;
		/** Bundles: how many of the contents are owned already. */
		int32 NumOwnedContents(int32 DefId) const;
		/** The player's choice for the slot (it may wait for confirmation; see GetEquippedItem). */
		bool IsEquipped(int32 DefId) const;
		/** The def id chosen for Slot, or 0. */
		int32 GetEquipped(ESlot Slot) const;
		/** What the games draw in Slot: the chosen item once it is confirmed, else null. */
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
		/** The backend's whole inventory: owned and confirmed become exactly this. */
		void ApplyInventory(const TSet<int32>& Inventory, bool bAnnounceNewFree);
		/** Items the backend just granted (checkout, unlock, drop): owned and confirmed from now on. */
		void AddConfirmed(const TArray<int32>& DefIds, bool bAnnounceNewFree);
		void SetOwned(const TSet<int32>& NewOwned, bool bAnnounceNewFree);
		void EvaluateUnlocks();
		void FinishPurchase(int32 DefId, EPurchaseResult Result, const FString& Detail);
		void Changed() { OnChanged.Broadcast(); }

		TUniquePtr<IKartStoreBackend> Backend;
		FString SaveSlot;
		TSet<int32> Owned;
		/** Def ids the backend confirmed this session; never read from disk. */
		TSet<int32> Confirmed;
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
