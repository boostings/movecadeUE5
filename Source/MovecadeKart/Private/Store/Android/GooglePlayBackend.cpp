// The Google Play backend: Google Play Billing (8.x) through the engine's OnlineSubsystemGooglePlay store and purchase
// interfaces. Built only for Android (UBT skips this folder elsewhere). Docs/Store/README.md "Mobile".
#include "Store/MobileStoreBackends.h"

#if PLATFORM_ANDROID

#include "HAL/PlatformProcess.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlinePurchaseInterface.h"
#include "Interfaces/OnlineStoreInterfaceV2.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "OnlineError.h"
#include "OnlineSubsystem.h"
#include "Store/GooglePlayReceipt.h"
#include "Store/KartStoreCatalog.h"
#include "Store/MobileStore.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartGooglePlay, Log, All);

// Launch/Private/Android/AndroidJNI.cpp: BillingClient.acknowledgePurchase for one purchase token. Called directly because
// the engine's FinalizePurchase consumes anything that isn't a subscription, and a consumed cosmetic would be gone.
extern bool AndroidThunkCpp_Iap_AcknowledgePurchase(const FString& ProductToken);

namespace MovecadeKart::Store
{
	/**
	 * Ownership is Google Play's purchase list (queryPurchasesAsync: refunded and voided purchases drop out of it), and a
	 * purchase counts only once GooglePlay::CheckValidationInfo verifies Google's signature with the app's license key,
	 * for this package, in the PURCHASED state. PENDING purchases (cash, slow banks) are reported as Pending and arrive
	 * through the engine's unexpected-purchase event when they clear. Every verified purchase is acknowledged (Google
	 * refunds unacknowledged ones after three days) and never consumed. Free items are the device's.
	 *
	 * The engine's interfaces answer on the game thread. Callbacks hold a weak token so an answer after the backend is
	 * gone does nothing.
	 */
	class FGooglePlayBackend final : public IKartStoreBackend
	{
	public:
		FGooglePlayBackend(IOnlineStoreV2Ptr InStore, IOnlinePurchasePtr InPurchase, FUniqueNetIdRef InUser, const FString& InLicenseKey)
			: Store(MoveTemp(InStore)), Purchases(MoveTemp(InPurchase)), User(MoveTemp(InUser)), LicenseKey(InLicenseKey),
			PackageName(FPlatformProcess::GetGameBundleId()), Free(MobileFreeItemsSlot)
		{
			TWeakPtr<bool> WeakAlive = Alive;
			// A pending purchase cleared, or one was made outside a checkout (a promo code in the Play Store).
			UnexpectedHandle = Purchases->AddOnUnexpectedPurchaseReceiptDelegate_Handle(FOnUnexpectedPurchaseReceiptDelegate::CreateLambda(
				[this, WeakAlive](const FUniqueNetId&) { if (WeakAlive.IsValid()) { ReportInventory(); } }));
			// Refunds while the game was in the background show on the next purchase list.
			ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddLambda(
				[this, WeakAlive]() { if (WeakAlive.IsValid()) { ReportInventory(); } });
		}

		virtual ~FGooglePlayBackend() override
		{
			*Alive = false;
			Purchases->ClearOnUnexpectedPurchaseReceiptDelegate_Handle(UnexpectedHandle);
			FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
		}

		virtual FName GetName() const override { return TEXT("googleplay"); }
		virtual EStorefront GetStorefront() const override { return EStorefront::GooglePlay; }
		virtual bool IsOnline() const override { return bOnline; }

		virtual void Tick(double DeltaSeconds) override
		{
			TArray<TFunction<void()>> Due = MoveTemp(Deferred);
			for (TFunction<void()>& Work : Due)
			{
				Work();
			}
		}

		virtual void RefreshInventory(TFunction<void(bool, const TSet<int32>&)> Done) override
		{
			// The engine runs one purchase query at a time: callers that ask meanwhile share the answer.
			Waiting.Add(MoveTemp(Done));
			if (bQuerying)
			{
				return;
			}
			bQuerying = true;
			TWeakPtr<bool> WeakAlive = Alive;
			Purchases->QueryReceipts(*User, false, FOnQueryReceiptsComplete::CreateLambda([this, WeakAlive](const FOnlineError& Result)
			{
				if (WeakAlive.IsValid())
				{
					FinishInventory(Result.WasSuccessful());
				}
			}));
		}

		virtual void RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool, const TMap<int32, FPrice>&)> Done) override
		{
			TArray<FUniqueOfferId> OfferIds;
			for (int32 Def : DefIds)
			{
				if (const FItem* Item = FindItem(Def); Item && IsSellable(*Item))
				{
					OfferIds.Add(ProductId(*Item));
				}
			}
			TWeakPtr<bool> WeakAlive = Alive;
			Store->QueryOffersById(*User, OfferIds, FOnQueryOnlineStoreOffersComplete::CreateLambda(
				[this, WeakAlive, Done = MoveTemp(Done)](bool bOk, const TArray<FUniqueOfferId>& Found, const FString& Error)
			{
				if (!WeakAlive.IsValid())
				{
					return;
				}
				bOnline = bOk;
				TMap<int32, FPrice> Prices;
				for (const FUniqueOfferId& Id : Found)
				{
					const TSharedPtr<FOnlineStoreOffer> Offer = Store->GetOffer(Id);
					const FItem* Item = FindItemByProductId(Id);
					if (Offer.IsValid() && Item)
					{
						// Google's formattedPrice, as the purchase sheet shows it; NumericPrice is already in minor units.
						Prices.Add(Item->DefId, Mobile::MakeStorePrice(Offer->NumericPrice, Offer->CurrencyCode, Offer->GetDisplayPrice()));
					}
				}
				UE_LOG(LogKartGooglePlay, Log, TEXT("Products: %s, %d priced %s"), bOk ? TEXT("ok") : TEXT("failed"), Prices.Num(), *Error);
				Done(bOk, Prices);
			}));
		}

		virtual void StartPurchase(int32 DefId, TFunction<void(EPurchaseResult, const FString&)> Done) override
		{
			const FItem* Item = FindItem(DefId);
			if (!Item || !IsSellable(*Item))
			{
				Deferred.Add([Done = MoveTemp(Done)]() { Done(EPurchaseResult::Unavailable, TEXT("not for sale")); });
				return;
			}
			FPurchaseCheckoutRequest Request;
			// One-time and never consumed (bIsConsumable false is informational for the engine; we never finalize).
			Request.AddPurchaseOffer(FOfferNamespace(), ProductId(*Item), 1, false);
			TWeakPtr<bool> WeakAlive = Alive;
			const FString Wanted = ProductId(*Item);
			Purchases->Checkout(*User, Request, FOnPurchaseCheckoutComplete::CreateLambda(
				[this, WeakAlive, Wanted, Done = MoveTemp(Done)](const FOnlineError& Result, const TSharedRef<FPurchaseReceipt>& Receipt)
			{
				if (WeakAlive.IsValid())
				{
					FinishPurchase(Wanted, Result, *Receipt, Done);
				}
			}));
		}

		virtual void GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool)> Done) override
		{
			// No platform inventory for free items: the device holds them (the goal was checked by FKartStore).
			const bool bOk = Free.GrantEarned(DefId);
			Deferred.Add([bOk, Done = MoveTemp(Done)]() { Done(bOk); });
		}

		virtual void TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool, const TArray<int32>&)> Done) override
		{
			TArray<int32> Granted = GeneratorDef == PlaytimeGeneratorDef ? Free.PlaytimeCheck() : TArray<int32>();
			Deferred.Add([Granted = MoveTemp(Granted), Done = MoveTemp(Done)]() { Done(true, Granted); });
		}

	private:
		/** The product ids a receipt proves, after the signature check; acknowledges what needs it. */
		TArray<FString> VerifiedProducts(const FPurchaseReceipt& Receipt, bool& bOutPending)
		{
			TArray<FString> Out;
			for (const FPurchaseReceipt::FReceiptOfferEntry& Offer : Receipt.ReceiptOffers)
			{
				for (const FPurchaseReceipt::FLineItemInfo& Line : Offer.LineItems)
				{
					// The engine leaves ValidationInfo empty while a purchase is PENDING.
					if (!Line.IsRedeemable())
					{
						bOutPending = true;
						continue;
					}
					GooglePlay::FPurchase Purchase;
					const GooglePlay::EReceiptCheck Check = GooglePlay::CheckValidationInfo(Line.ValidationInfo, LicenseKey, PackageName, Purchase);
					if (Check == GooglePlay::EReceiptCheck::Pending)
					{
						bOutPending = true;
						continue;
					}
					if (Check != GooglePlay::EReceiptCheck::Valid)
					{
						UE_LOG(LogKartGooglePlay, Warning, TEXT("Purchase %s refused: %s"), *Offer.OfferId, GooglePlay::CheckId(Check));
						continue;
					}
					Acknowledge(Purchase);
					Out.Append(Purchase.ProductIds);
				}
			}
			return Out;
		}

		void Acknowledge(const GooglePlay::FPurchase& Purchase)
		{
			if (!Purchase.bAcknowledged && !AcknowledgedTokens.Contains(Purchase.PurchaseToken))
			{
				AcknowledgedTokens.Add(Purchase.PurchaseToken);
				AndroidThunkCpp_Iap_AcknowledgePurchase(Purchase.PurchaseToken);
				UE_LOG(LogKartGooglePlay, Log, TEXT("Acknowledged %s"), *Purchase.OrderId);
			}
		}

		void FinishInventory(bool bOk)
		{
			bQuerying = false;
			TSet<int32> Owned;
			if (bOk)
			{
				TArray<FPurchaseReceipt> Receipts;
				Purchases->GetReceipts(*User, Receipts);
				TArray<FString> ProductIds;
				for (const FPurchaseReceipt& Receipt : Receipts)
				{
					bool bPending = false;
					ProductIds.Append(VerifiedProducts(Receipt, bPending));
				}
				Owned = Mobile::DefsForProducts(ProductIds);
				Owned.Append(Free.GetOwned());
				UE_LOG(LogKartGooglePlay, Log, TEXT("Purchases: %d receipts, %d verified products"), Receipts.Num(), ProductIds.Num());
			}
			bOnline = bOk;
			TArray<TFunction<void(bool, const TSet<int32>&)>> Answer = MoveTemp(Waiting);
			for (TFunction<void(bool, const TSet<int32>&)>& Done : Answer)
			{
				Done(bOk, Owned);
			}
		}

		void FinishPurchase(const FString& Wanted, const FOnlineError& Result, const FPurchaseReceipt& Receipt, const TFunction<void(EPurchaseResult, const FString&)>& Done)
		{
			switch (Receipt.TransactionState)
			{
			case EPurchaseTransactionState::Purchased:
			{
				bool bPending = false;
				const TArray<FString> Proven = VerifiedProducts(Receipt, bPending);
				if (Proven.Contains(Wanted))
				{
					Done(EPurchaseResult::Success, FString());
				}
				else if (bPending)
				{
					Done(EPurchaseResult::Pending, TEXT("payment pending"));
				}
				else
				{
					// Paid but unprovable (a missing or wrong license key, a tampered answer): not owned. Google refunds an
					// unacknowledged purchase after three days, so nobody pays for nothing.
					Done(EPurchaseResult::Failed, TEXT("the purchase couldn't be verified"));
				}
				break;
			}
			case EPurchaseTransactionState::Deferred:
				Done(EPurchaseResult::Pending, TEXT("payment pending"));
				break;
			case EPurchaseTransactionState::Canceled:
				Done(EPurchaseResult::Cancelled, TEXT("closed the purchase sheet"));
				break;
			default:
				Done(EPurchaseResult::Failed, Result.GetErrorMessage().ToString());
				// "Already owned" is a failure to the billing library: the purchase list then shows what is really owned.
				ReportInventory();
				break;
			}
		}

		/** Asks for the purchase list and reports it as the whole inventory. */
		void ReportInventory()
		{
			RefreshInventory([this](bool bOk, const TSet<int32>& Owned)
			{
				if (bOk && OnInventoryChanged)
				{
					OnInventoryChanged(Owned);
				}
			});
		}

		IOnlineStoreV2Ptr Store;
		IOnlinePurchasePtr Purchases;
		FUniqueNetIdRef User;
		/** Play Console's base64 license key (GooglePlayLicenseKey): without it nothing verifies, so nothing counts. */
		FString LicenseKey;
		FString PackageName;
		Mobile::FLocalFreeItems Free;
		TArray<TFunction<void(bool, const TSet<int32>&)>> Waiting;
		TArray<TFunction<void()>> Deferred;
		TSet<FString> AcknowledgedTokens;
		FDelegateHandle UnexpectedHandle;
		FDelegateHandle ForegroundHandle;
		bool bQuerying = false;
		bool bOnline = true;
		TSharedRef<bool> Alive = MakeShared<bool>(true);
	};

	TUniquePtr<IKartStoreBackend> MakeGooglePlayBackend()
	{
		IOnlineSubsystem* Subsystem = IOnlineSubsystem::Get(TEXT("GOOGLEPLAY"));
		IOnlineStoreV2Ptr Store = Subsystem ? Subsystem->GetStoreV2Interface() : nullptr;
		IOnlinePurchasePtr Purchases = Subsystem ? Subsystem->GetPurchaseInterface() : nullptr;
		IOnlineIdentityPtr Identity = Subsystem ? Subsystem->GetIdentityInterface() : nullptr;
		// Without a Play Games sign-in the identity is an empty id, which billing accepts.
		FUniqueNetIdPtr User = Identity ? Identity->GetUniquePlayerId(0) : nullptr;
		if (!Store || !Purchases || !User)
		{
			UE_LOG(LogKartGooglePlay, Error, TEXT("Google Play billing unavailable (OnlineSubsystemGooglePlay with bSupportsInAppPurchasing): the store is offline"));
			return nullptr;
		}
		FString LicenseKey;
		GConfig->GetString(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("GooglePlayLicenseKey"), LicenseKey, GEngineIni);
		if (LicenseKey.IsEmpty())
		{
			UE_LOG(LogKartGooglePlay, Error, TEXT("GooglePlayLicenseKey is not set: no purchase can be verified, so none will count"));
		}
		UE_LOG(LogKartGooglePlay, Display, TEXT("Google Play ready (%s)"), *FPlatformProcess::GetGameBundleId());
		return MakeUnique<FGooglePlayBackend>(MoveTemp(Store), MoveTemp(Purchases), User.ToSharedRef(), LicenseKey);
	}
}

#endif // PLATFORM_ANDROID
