#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreTypes.h"

namespace MovecadeKart::Store
{
	/**
	 * Where ownership and checkout live. The store talks to exactly one backend: the Steam Inventory Service on
	 * desktop (FSteamInventoryBackend), StoreKit 2 on iOS, Google Play Billing on Android, or FMockStoreBackend in the
	 * editor, tests and offline. Every call is asynchronous; callbacks run on the game thread from Tick (or from the
	 * Steam callback pump). A backend owns no game state.
	 *
	 * Ownership a backend reports is the platform's word: an item is in an inventory only once the platform confirmed
	 * it (Steam's signed inventory, a StoreKit-verified entitlement, a signed Google Play purchase). Docs/Store/README.md.
	 */
	class MOVECADEKART_API IKartStoreBackend
	{
	public:
		virtual ~IKartStoreBackend() = default;

		/** "mock", "steam", "appstore", "googleplay". */
		virtual FName GetName() const = 0;
		/** Whose checkout this is (the mock pretends to be the build's own: see FMockStoreBackend::SetStorefront). */
		virtual EStorefront GetStorefront() const = 0;
		/** False when the service can't be reached (Steam not running): the store then shows owned items only. */
		virtual bool IsOnline() const = 0;
		virtual void Tick(double DeltaSeconds) = 0;

		/**
		 * The def ids the player owns, quietly (Steam: GetAllItems; StoreKit: current entitlements; Play: queryPurchases).
		 * Bundles arrive as their contents. Free items the device holds are included.
		 */
		virtual void RefreshInventory(TFunction<void(bool bOk, const TSet<int32>& Owned)> Done) = 0;
		/**
		 * The player pressed Restore purchases: the same answer as RefreshInventory, but the backend may ask the platform
		 * to sync first, which can show a sign-in sheet (StoreKit: AppStore.sync). Never called without the player asking.
		 */
		virtual void RestorePurchases(TFunction<void(bool bOk, const TSet<int32>& Owned)> Done) { RefreshInventory(MoveTemp(Done)); }
		/** Prices in the player's currency (Steam: RequestPrices + GetItemPrice). */
		virtual void RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool bOk, const TMap<int32, FPrice>& Prices)> Done) = 0;
		/**
		 * Opens checkout for one item (Steam: StartPurchase, then the overlay; the result arrives with the
		 * MicroTxnAuthorizationResponse). Success means the item is now in the inventory. Pending (mobile) means no
		 * charge yet: the item arrives through OnInventoryChanged if it is approved.
		 */
		virtual void StartPurchase(int32 DefId, TFunction<void(EPurchaseResult Result, const FString& Detail)> Done) = 0;
		/** Grants a free earned item (Steam: the achievement, then AddPromoItem). */
		virtual void GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool bOk)> Done) = 0;
		/** Asks the playtime generator for a drop (Steam: TriggerItemDrop). Granted lists new def ids (often none). */
		virtual void TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool bOk, const TArray<int32>& Granted)> Done) = 0;

		/** The whole inventory, changed outside a call (a purchase finished in the overlay, an approval, a drop, a refund). */
		TFunction<void(const TSet<int32>& Owned)> OnInventoryChanged;
	};
}
