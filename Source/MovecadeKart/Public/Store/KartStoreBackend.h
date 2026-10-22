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
}
