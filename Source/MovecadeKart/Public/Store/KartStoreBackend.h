#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreTypes.h"

namespace MovecadeKart::Store
{
	/**
	 * Where ownership and checkout live. The store talks to exactly one backend: the Steam Inventory Service in a
	 * Steam build (FSteamInventoryBackend), or FMockStoreBackend offline and in tests. Every call is asynchronous;
	 * callbacks run on the game thread from Tick (or from the Steam callback pump). A backend owns no game state.
	 */
	class MOVECADEKART_API IKartStoreBackend
	{
	public:
		virtual ~IKartStoreBackend() = default;

		/** "mock", "steam". */
		virtual FName GetName() const = 0;
		/** False when the service can't be reached (Steam not running): the store then shows owned items only. */
		virtual bool IsOnline() const = 0;
		virtual void Tick(double DeltaSeconds) = 0;

		/** The def ids the player owns (Steam: GetAllItems). */
		virtual void RefreshInventory(TFunction<void(bool bOk, const TSet<int32>& Owned)> Done) = 0;
		/** Prices in the player's currency (Steam: RequestPrices + GetItemPrice). */
		virtual void RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool bOk, const TMap<int32, FPrice>& Prices)> Done) = 0;
		/**
		 * Opens checkout for one item (Steam: StartPurchase, then the overlay; the result arrives with the
		 * MicroTxnAuthorizationResponse). Success means the item is now in the inventory.
		 */
		virtual void StartPurchase(int32 DefId, TFunction<void(EPurchaseResult Result, const FString& Detail)> Done) = 0;
		/** Grants a free earned item (Steam: the achievement, then AddPromoItem). */
		virtual void GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool bOk)> Done) = 0;
		/** Asks the playtime generator for a drop (Steam: TriggerItemDrop). Granted lists new def ids (often none). */
		virtual void TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool bOk, const TArray<int32>& Granted)> Done) = 0;

		/** Inventory changed outside a call (a purchase finished in the overlay, a drop, a refund). */
		TFunction<void(const TSet<int32>& Owned)> OnInventoryChanged;
	};

	/** What the mock does with the next purchase. */
	enum class EMockOutcome : uint8
	{
		Succeed,
		Cancel,
		Fail,
	};

	/**
	 * The offline backend: a pretend Steam. Its inventory lives in its own save slot (as Steam's servers would
	 * hold it), so wiping the store's local cache and restoring works like the real thing. Calls finish after
	 * realistic delays, stepped by Tick so tests control time. Outcomes come from SetNextOutcome, or the
	 * -KartStoreMock=succeed|cancel|fail command line (default succeed).
	 */
	class MOVECADEKART_API FMockStoreBackend : public IKartStoreBackend
	{
	public:
		/** SaveSlot empty: memory only (nothing persists). */
		explicit FMockStoreBackend(const FString& InSaveSlot = TEXT("KartStoreMock"));

		virtual FName GetName() const override { return TEXT("mock"); }
		virtual bool IsOnline() const override { return bOnline; }
		virtual void Tick(double DeltaSeconds) override;
		virtual void RefreshInventory(TFunction<void(bool, const TSet<int32>&)> Done) override;
		virtual void RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool, const TMap<int32, FPrice>&)> Done) override;
		virtual void StartPurchase(int32 DefId, TFunction<void(EPurchaseResult, const FString&)> Done) override;
		virtual void GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool)> Done) override;
		virtual void TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool, const TArray<int32>&)> Done) override;

		// Test and debug controls -------------------------------------------------------------------------
		void SetNextOutcome(EMockOutcome Outcome) { NextOutcome = Outcome; }
		void SetOnline(bool bInOnline) { bOnline = bInOnline; }
		/** Seconds each kind of call takes. */
		double PurchaseSeconds = 1.8;
		double InventorySeconds = 0.45;
		double PriceSeconds = 0.35;
		const TSet<int32>& GetServerInventory() const { return Inventory; }
		/** Development: puts items in the pretend inventory (-KartStoreEquip). */
		void DevGrant(const TArray<int32>& DefIds) { Grant(DefIds); }
		/** A refund, as Steam support would do it: the item leaves the inventory. */
		void Revoke(int32 DefId);
		/** Forgets everything, including the save slot. */
		void Wipe();
		int32 NumPending() const { return Pending.Num(); }

	private:
		void After(double Seconds, TFunction<void()> Work);
		void Grant(const TArray<int32>& DefIds);
		void Load();
		void Save() const;

		struct FPendingCall
		{
			double At = 0.0;
			TFunction<void()> Work;
		};

		FString SaveSlot;
		TSet<int32> Inventory;
		TArray<FPendingCall> Pending;
		double Clock = 0.0;
		EMockOutcome NextOutcome = EMockOutcome::Succeed;
		bool bOnline = true;
	};

	/** The Steam backend when this build has Steamworks (MOVECADE_WITH_STEAM) and Steam is running; else null. */
	MOVECADEKART_API TUniquePtr<IKartStoreBackend> MakeSteamBackend(int32 DevAppId);
}
