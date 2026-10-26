#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreBackend.h"

namespace MovecadeKart::Store
{
	/** What the mock does with the next purchase. */
	enum class EMockOutcome : uint8
	{
		Succeed,
		Cancel,
		Fail,
		/** Mobile: Ask to Buy, or a payment still clearing. ResolvePending ends it later. */
		Pending,
	};

	/**
	 * The offline backend: a pretend store. Its inventory lives in its own save slot (as Steam's or Apple's servers
	 * would hold it), so wiping the store's local cache and restoring works like the real thing. Calls finish after
	 * realistic delays, stepped by Tick so tests control time. Outcomes come from SetNextOutcome, or the
	 * -KartStoreMock=succeed|cancel|fail|pending|offline command line (default succeed; development builds).
	 *
	 * It pretends to be the build's own storefront (Steam on PC, the App Store on iOS, Google Play on Android), or the
	 * one SetStorefront / -KartStoreFront=steam|appstore|googleplay picks, so the mobile words, the mobile price
	 * tiers and the mobile flows (pending approval, restore, refund) run in the editor and the tests.
	 */
	class MOVECADEKART_API FMockStoreBackend : public IKartStoreBackend
	{
	public:
		/** SaveSlot empty: memory only (nothing persists). */
		explicit FMockStoreBackend(const FString& InSaveSlot = TEXT("KartStoreMock"));

		virtual FName GetName() const override { return TEXT("mock"); }
		virtual EStorefront GetStorefront() const override { return Storefront; }
		virtual bool IsOnline() const override { return bOnline; }
		virtual void Tick(double DeltaSeconds) override;
		virtual void RefreshInventory(TFunction<void(bool, const TSet<int32>&)> Done) override;
		virtual void RestorePurchases(TFunction<void(bool, const TSet<int32>&)> Done) override;
		virtual void RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool, const TMap<int32, FPrice>&)> Done) override;
		virtual void StartPurchase(int32 DefId, TFunction<void(EPurchaseResult, const FString&)> Done) override;
		virtual void GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool)> Done) override;
		virtual void TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool, const TArray<int32>&)> Done) override;

		// Test and debug controls -------------------------------------------------------------------------
		void SetNextOutcome(EMockOutcome Outcome) { NextOutcome = Outcome; }
		void SetOnline(bool bInOnline) { bOnline = bInOnline; }
		void SetStorefront(EStorefront InStorefront) { Storefront = InStorefront; }
		/**
		 * Ends a purchase that finished Pending. Approved (a parent said yes, the payment cleared): it lands in the
		 * inventory and the change is reported, as StoreKit's Transaction.updates or Play's purchase update would.
		 * Declined: forgotten (neither store says anything then). False if DefId wasn't pending.
		 */
		bool ResolvePending(int32 DefId, bool bApprove);
		const TSet<int32>& GetAwaitingApproval() const { return AwaitingApproval; }
		/** How often a Restore purchases the player asked for reached the store (StoreKit would sync, maybe with a sign-in). */
		int32 NumRestoreSyncs() const { return RestoreSyncs; }
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
		EStorefront Storefront = NativeStorefront();
		TSet<int32> AwaitingApproval;
		int32 RestoreSyncs = 0;
		bool bOnline = true;
	};
}
