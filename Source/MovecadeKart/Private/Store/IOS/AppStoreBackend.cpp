// The App Store backend: StoreKit 2 through KartStoreKit.swift. Built only for iOS (UBT skips this folder elsewhere);
// compiled as Objective-C++ like every .cpp on Apple platforms. Docs/Store/README.md "Mobile".
#include "Store/MobileStoreBackends.h"

#if PLATFORM_IOS

#include "Containers/Queue.h"
#include "Store/KartStoreCatalog.h"
#include "Store/MobileStore.h"

#import <Foundation/Foundation.h>
#include "MovecadeKart-Swift.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartAppStore, Log, All);

namespace MovecadeKart::Store
{
	namespace
	{
		/** MovecadeStoreKitResult in KartStoreKit.swift. */
		enum class EAppStorePurchase : int32
		{
			Purchased = 0,
			Cancelled = 1,
			Pending = 2,
			Failed = 3,
			Unverified = 4,
		};

		NSArray<NSString*>* ToNSArray(const TArray<FString>& Strings)
		{
			NSMutableArray<NSString*>* Out = [NSMutableArray arrayWithCapacity:Strings.Num()];
			for (const FString& S : Strings)
			{
				[Out addObject:S.GetNSString()];
			}
			return Out;
		}

		TArray<FString> FromNSArray(NSArray<NSString*>* Strings)
		{
			TArray<FString> Out;
			for (NSString* S in Strings)
			{
				Out.Add(FString(S));
			}
			return Out;
		}
	}

	/**
	 * Ownership is StoreKit's current entitlements: Apple-signed transactions, verified on the device, minus refunds and
	 * revocations. Restore purchases runs AppStore.sync first (it may ask the player to sign in), the quiet refresh at
	 * launch never does. Ask to Buy ends a checkout Pending; the approval arrives through Transaction.updates, which
	 * also brings refunds, and both become an inventory change. Free items are the device's (Mobile::FLocalFreeItems).
	 *
	 * Swift answers on the main thread, which on iOS isn't the game thread: every answer is queued and handled in Tick.
	 * Callbacks wait in this object, keyed by request, so an answer after the backend is gone does nothing.
	 */
	class FAppStoreBackend final : public IKartStoreBackend
	{
	public:
		FAppStoreBackend() : Free(MobileFreeItemsSlot)
		{
			TWeakPtr<FInbox, ESPMode::ThreadSafe> WeakInbox = Inbox;
			[MovecadeStoreKit startListening:^{
				Post(WeakInbox, [](FAppStoreBackend& Self) { Self.OnTransactionsChanged(); });
			}];
		}

		virtual ~FAppStoreBackend() override
		{
			[MovecadeStoreKit stopListening];
		}

		virtual FName GetName() const override { return TEXT("appstore"); }
		virtual EStorefront GetStorefront() const override { return EStorefront::AppStore; }
		virtual bool IsOnline() const override { return bOnline; }

		virtual void Tick(double DeltaSeconds) override
		{
			TFunction<void(FAppStoreBackend&)> Work;
			while (Inbox->Work.Dequeue(Work))
			{
				Work(*this);
			}
		}

		virtual void RefreshInventory(TFunction<void(bool, const TSet<int32>&)> Done) override
		{
			const int32 Id = Remember(InventoryCalls, MoveTemp(Done));
			TWeakPtr<FInbox, ESPMode::ThreadSafe> WeakInbox = Inbox;
			[MovecadeStoreKit currentEntitlements:^(NSArray<NSString*>* ProductIds) {
				TArray<FString> Owned = FromNSArray(ProductIds);
				Post(WeakInbox, [Id, Owned = MoveTemp(Owned)](FAppStoreBackend& Self) { Self.FinishInventory(Id, true, Owned); });
			}];
		}

		virtual void RestorePurchases(TFunction<void(bool, const TSet<int32>&)> Done) override
		{
			const int32 Id = Remember(InventoryCalls, MoveTemp(Done));
			TWeakPtr<FInbox, ESPMode::ThreadSafe> WeakInbox = Inbox;
			[MovecadeStoreKit sync:^(BOOL bSynced) {
				if (!bSynced)
				{
					Post(WeakInbox, [Id](FAppStoreBackend& Self) { Self.FinishInventory(Id, false, {}); });
					return;
				}
				[MovecadeStoreKit currentEntitlements:^(NSArray<NSString*>* ProductIds) {
					TArray<FString> Owned = FromNSArray(ProductIds);
					Post(WeakInbox, [Id, Owned = MoveTemp(Owned)](FAppStoreBackend& Self) { Self.FinishInventory(Id, true, Owned); });
				}];
			}];
		}

		virtual void RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool, const TMap<int32, FPrice>&)> Done) override
		{
			TArray<FString> ProductIds;
			for (int32 Def : DefIds)
			{
				if (const FItem* Item = FindItem(Def); Item && IsSellable(*Item))
				{
					ProductIds.Add(ProductId(*Item));
				}
			}
			const int32 Id = Remember(PriceCalls, MoveTemp(Done));
			TWeakPtr<FInbox, ESPMode::ThreadSafe> WeakInbox = Inbox;
			[MovecadeStoreKit requestProducts:ToNSArray(ProductIds) completion:^(NSArray<MovecadeStoreKitProduct*>* Products, BOOL bOk) {
				TMap<int32, FPrice> Prices;
				for (MovecadeStoreKitProduct* Product in Products)
				{
					if (const FItem* Item = FindItemByProductId(FString(Product.productId)))
					{
						Prices.Add(Item->DefId, Mobile::MakeStorePrice(Product.priceMinorUnits, FString(Product.currencyCode), FText::FromString(FString(Product.displayPrice))));
					}
				}
				const bool bAnswered = bOk;
				Post(WeakInbox, [Id, bAnswered, Prices = MoveTemp(Prices)](FAppStoreBackend& Self) { Self.FinishPrices(Id, bAnswered, Prices); });
			}];
		}

		virtual void StartPurchase(int32 DefId, TFunction<void(EPurchaseResult, const FString&)> Done) override
		{
			const FItem* Item = FindItem(DefId);
			if (!Item || !IsSellable(*Item))
			{
				Later([Done = MoveTemp(Done)]() { Done(EPurchaseResult::Unavailable, TEXT("not for sale")); });
				return;
			}
			if (![MovecadeStoreKit canMakePayments])
			{
				Later([Done = MoveTemp(Done)]() { Done(EPurchaseResult::Failed, TEXT("purchases are turned off on this device (Screen Time)")); });
				return;
			}
			const int32 Id = Remember(PurchaseCalls, MoveTemp(Done));
			TWeakPtr<FInbox, ESPMode::ThreadSafe> WeakInbox = Inbox;
			[MovecadeStoreKit purchase:ProductId(*Item).GetNSString() completion:^(NSInteger Code, NSString* Detail) {
				const int32 Result = static_cast<int32>(Code);
				FString Why(Detail);
				Post(WeakInbox, [Id, Result, Why = MoveTemp(Why)](FAppStoreBackend& Self) { Self.FinishPurchase(Id, static_cast<EAppStorePurchase>(Result), Why); });
			}];
		}

		virtual void GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool)> Done) override
		{
			// No platform inventory for free items: the device holds them (the goal was checked by FKartStore).
			const bool bOk = Free.GrantEarned(DefId);
			Later([bOk, Done = MoveTemp(Done)]() { Done(bOk); });
		}

		virtual void TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool, const TArray<int32>&)> Done) override
		{
			TArray<int32> Granted = GeneratorDef == PlaytimeGeneratorDef ? Free.PlaytimeCheck() : TArray<int32>();
			Later([Granted = MoveTemp(Granted), Done = MoveTemp(Done)]() { Done(true, Granted); });
		}

	private:
		struct FInbox
		{
			TQueue<TFunction<void(FAppStoreBackend&)>, EQueueMode::Mpsc> Work;
		};

		/** From any thread: runs Work on the game thread in the next Tick, if the backend still exists. */
		static void Post(const TWeakPtr<FInbox, ESPMode::ThreadSafe>& WeakInbox, TFunction<void(FAppStoreBackend&)> Work)
		{
			if (const TSharedPtr<FInbox, ESPMode::ThreadSafe> Pinned = WeakInbox.Pin())
			{
				Pinned->Work.Enqueue(MoveTemp(Work));
			}
		}

		/** Answers a call from the next Tick (calls are always asynchronous). */
		void Later(TFunction<void()> Work)
		{
			Inbox->Work.Enqueue([Work = MoveTemp(Work)](FAppStoreBackend&) { Work(); });
		}

		template <typename FnType>
		int32 Remember(TMap<int32, FnType>& Calls, FnType Done)
		{
			const int32 Id = ++LastCallId;
			Calls.Add(Id, MoveTemp(Done));
			return Id;
		}

		/** The device's whole inventory: the verified paid products (bundles as their contents) and the free items. */
		TSet<int32> InventoryFrom(const TArray<FString>& ProductIds) const
		{
			TSet<int32> Owned = Mobile::DefsForProducts(ProductIds);
			Owned.Append(Free.GetOwned());
			return Owned;
		}

		void FinishInventory(int32 Id, bool bOk, const TArray<FString>& ProductIds)
		{
			TFunction<void(bool, const TSet<int32>&)> Done;
			if (InventoryCalls.RemoveAndCopyValue(Id, Done) && Done)
			{
				UE_LOG(LogKartAppStore, Log, TEXT("Entitlements: %s, %d products"), bOk ? TEXT("ok") : TEXT("sync failed"), ProductIds.Num());
				Done(bOk, bOk ? InventoryFrom(ProductIds) : TSet<int32>());
			}
		}

		void FinishPrices(int32 Id, bool bOk, const TMap<int32, FPrice>& Prices)
		{
			bOnline = bOk;
			TFunction<void(bool, const TMap<int32, FPrice>&)> Done;
			if (PriceCalls.RemoveAndCopyValue(Id, Done) && Done)
			{
				UE_LOG(LogKartAppStore, Log, TEXT("Products: %s, %d priced"), bOk ? TEXT("ok") : TEXT("App Store unreachable"), Prices.Num());
				Done(bOk, Prices);
			}
		}

		void FinishPurchase(int32 Id, EAppStorePurchase Result, const FString& Detail)
		{
			TFunction<void(EPurchaseResult, const FString&)> Done;
			if (!PurchaseCalls.RemoveAndCopyValue(Id, Done) || !Done)
			{
				return;
			}
			switch (Result)
			{
			case EAppStorePurchase::Purchased: Done(EPurchaseResult::Success, FString()); break;
			case EAppStorePurchase::Cancelled: Done(EPurchaseResult::Cancelled, TEXT("closed the purchase sheet")); break;
			case EAppStorePurchase::Pending: Done(EPurchaseResult::Pending, TEXT("Ask to Buy or payment pending")); break;
			case EAppStorePurchase::Unverified:
				// StoreKit couldn't verify Apple's signature: never counts (a jailbroken device, a tampered receipt).
				UE_LOG(LogKartAppStore, Warning, TEXT("Unverified transaction refused: %s"), *Detail);
				Done(EPurchaseResult::Failed, TEXT("the purchase couldn't be verified"));
				break;
			default: Done(EPurchaseResult::Failed, Detail); break;
			}
		}

		/** Transaction.updates: an approval, a purchase from another device, a refund. Report the whole inventory. */
		void OnTransactionsChanged()
		{
			RefreshInventory([this](bool bOk, const TSet<int32>& Owned)
			{
				if (bOk && OnInventoryChanged)
				{
					OnInventoryChanged(Owned);
				}
			});
		}

		TSharedRef<FInbox, ESPMode::ThreadSafe> Inbox = MakeShared<FInbox, ESPMode::ThreadSafe>();
		Mobile::FLocalFreeItems Free;
		TMap<int32, TFunction<void(bool, const TSet<int32>&)>> InventoryCalls;
		TMap<int32, TFunction<void(bool, const TMap<int32, FPrice>&)>> PriceCalls;
		TMap<int32, TFunction<void(EPurchaseResult, const FString&)>> PurchaseCalls;
		int32 LastCallId = 0;
		/** The last product request reached the App Store. */
		bool bOnline = true;
	};

	TUniquePtr<IKartStoreBackend> MakeAppStoreBackend()
	{
		// Always the App Store on iOS, even when the device can't pay (Screen Time): ownership and restore still work.
		UE_LOG(LogKartAppStore, Display, TEXT("App Store ready (StoreKit 2, payments %s)"), [MovecadeStoreKit canMakePayments] ? TEXT("allowed") : TEXT("restricted"));
		return MakeUnique<FAppStoreBackend>();
	}
}

#endif // PLATFORM_IOS
