#include "Store/MockStoreBackend.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Store/KartStoreCatalog.h"
#include "Store/KartStoreSave.h"
#include "Store/KartStoreSaveFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartStoreMock, Log, All);

namespace MovecadeKart::Store
{
	namespace
	{
		/** A call's work may queue more calls: at most this many run per tick, so a loop can't hang the game. */
		constexpr int32 MaxCallsPerTick = 64;
	}

	FMockStoreBackend::FMockStoreBackend(const FString& InSaveSlot) : SaveSlot(InSaveSlot)
	{
#if !UE_BUILD_SHIPPING
		// Development: -KartStoreMock=succeed|cancel|fail|pending picks how every checkout ends; =offline plays the store
		// as a Shipping build without its store sees it (nothing confirmed, nothing for sale).
		FString Outcome;
		if (FParse::Value(FCommandLine::Get(), TEXT("KartStoreMock="), Outcome))
		{
			NextOutcome = Outcome.Equals(TEXT("cancel"), ESearchCase::IgnoreCase) ? EMockOutcome::Cancel
				: Outcome.Equals(TEXT("fail"), ESearchCase::IgnoreCase) ? EMockOutcome::Fail
				: Outcome.Equals(TEXT("pending"), ESearchCase::IgnoreCase) ? EMockOutcome::Pending : EMockOutcome::Succeed;
			bOnline = !Outcome.Equals(TEXT("offline"), ESearchCase::IgnoreCase);
		}
		// -KartStoreFront=appstore|googleplay|steam: the storefront the mock plays (the phone store's words on PC).
		FString Front;
		if (FParse::Value(FCommandLine::Get(), TEXT("KartStoreFront="), Front))
		{
			Storefront = Front.Equals(TEXT("appstore"), ESearchCase::IgnoreCase) ? EStorefront::AppStore
				: Front.Equals(TEXT("googleplay"), ESearchCase::IgnoreCase) ? EStorefront::GooglePlay : EStorefront::Steam;
		}
#endif
		Load();
	}

	void FMockStoreBackend::Tick(double DeltaSeconds)
	{
		Clock += DeltaSeconds;
		// Run what's due, oldest first.
		for (int32 Ran = 0; Ran < MaxCallsPerTick; ++Ran)
		{
			const int32 Due = Pending.IndexOfByPredicate([this](const FPendingCall& C) { return C.At <= Clock; });
			if (Due == INDEX_NONE)
			{
				break;
			}
			TFunction<void()> Work = MoveTemp(Pending[Due].Work);
			Pending.RemoveAt(Due);
			Work();
		}
	}

	void FMockStoreBackend::After(double Seconds, TFunction<void()> Work)
	{
		Pending.Add({ Clock + Seconds, MoveTemp(Work) });
	}

	void FMockStoreBackend::RefreshInventory(TFunction<void(bool, const TSet<int32>&)> Done)
	{
		After(InventorySeconds, [this, Done = MoveTemp(Done)]()
		{
			Done(bOnline, bOnline ? Inventory : TSet<int32>());
		});
	}

	void FMockStoreBackend::RestorePurchases(TFunction<void(bool, const TSet<int32>&)> Done)
	{
		++RestoreSyncs;
		RefreshInventory(MoveTemp(Done));
	}

	void FMockStoreBackend::RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool, const TMap<int32, FPrice>&)> Done)
	{
		After(PriceSeconds, [this, DefIds, Done = MoveTemp(Done)]()
		{
			TMap<int32, FPrice> Prices;
			if (bOnline)
			{
				const bool bMobile = Storefront != EStorefront::Steam;
				for (int32 Def : DefIds)
				{
					if (const FItem* Item = FindItem(Def); Item && IsSellable(*Item))
					{
						// Steam gives numbers; the phone stores give their own formatted text as well (FPrice::Display).
						const int32 Cents = bMobile ? Item->MobilePriceCents : Item->PriceCents;
						FPrice Price{ static_cast<uint64>(Cents), static_cast<uint64>(Cents), TEXT("USD") };
						if (bMobile)
						{
							Price.Display = FText::AsCurrencyBase(Cents, TEXT("USD"));
						}
						Prices.Add(Def, Price);
					}
				}
			}
			Done(bOnline, Prices);
		});
	}

	void FMockStoreBackend::StartPurchase(int32 DefId, TFunction<void(EPurchaseResult, const FString&)> Done)
	{
		const FItem* Item = FindItem(DefId);
		if (!Item || Item->Source != ESource::Paid || Item->bComingSoon)
		{
			After(0.1, [Done = MoveTemp(Done)]() { Done(EPurchaseResult::Unavailable, TEXT("not for sale")); });
			return;
		}
		if (!bOnline)
		{
			After(0.6, [Done = MoveTemp(Done)]() { Done(EPurchaseResult::Failed, TEXT("offline")); });
			return;
		}
		const EMockOutcome Outcome = NextOutcome;
		UE_LOG(LogKartStoreMock, Display, TEXT("Mock %s checkout for %d (%s): %s"), StorefrontId(Storefront), DefId, *Item->Key.ToString(),
			Outcome == EMockOutcome::Succeed ? TEXT("approve") : Outcome == EMockOutcome::Cancel ? TEXT("cancel")
			: Outcome == EMockOutcome::Pending ? TEXT("pending") : TEXT("fail"));
		// Like the overlay: the player reads the checkout, then approves or closes it.
		After(PurchaseSeconds, [this, DefId, Outcome, Done = MoveTemp(Done)]()
		{
			switch (Outcome)
			{
			case EMockOutcome::Succeed:
			{
				const FItem* Bought = FindItem(DefId);
				Grant(Bought && Bought->Slot == ESlot::Bundle ? Bought->Contents : TArray<int32>{ DefId });
				Done(EPurchaseResult::Success, FString());
				if (OnInventoryChanged)
				{
					OnInventoryChanged(Inventory);
				}
				break;
			}
			case EMockOutcome::Cancel:
				Done(EPurchaseResult::Cancelled, TEXT("closed the checkout"));
				break;
			case EMockOutcome::Pending:
				AwaitingApproval.Add(DefId);
				Done(EPurchaseResult::Pending, TEXT("waiting for approval (mock)"));
				break;
			default:
				Done(EPurchaseResult::Failed, TEXT("k_EResultFail (mock)"));
				break;
			}
		});
	}

	void FMockStoreBackend::GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool)> Done)
	{
		After(InventorySeconds, [this, DefId, Done = MoveTemp(Done)]()
		{
			const FItem* Item = FindItem(DefId);
			const bool bOk = bOnline && Item && Item->Source == ESource::Earned;
			if (bOk)
			{
				Grant({ DefId });
			}
			Done(bOk);
		});
	}

	void FMockStoreBackend::TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool, const TArray<int32>&)> Done)
	{
		After(InventorySeconds, [this, GeneratorDef, Done = MoveTemp(Done)]()
		{
			TArray<int32> Granted;
			if (bOnline && GeneratorDef == PlaytimeGeneratorDef)
			{
				// Not random: the next gift the player doesn't have. (Steam's generator picks from the same pool.)
				for (int32 Def : DropPool())
				{
					if (!Inventory.Contains(Def))
					{
						Granted.Add(Def);
						break;
					}
				}
				Grant(Granted);
			}
			Done(bOnline, Granted);
		});
	}

	bool FMockStoreBackend::ResolvePending(int32 DefId, bool bApprove)
	{
		if (AwaitingApproval.Remove(DefId) == 0)
		{
			return false;
		}
		if (bApprove)
		{
			const FItem* Item = FindItem(DefId);
			Grant(Item && Item->Slot == ESlot::Bundle ? Item->Contents : TArray<int32>{ DefId });
			if (OnInventoryChanged)
			{
				OnInventoryChanged(Inventory);
			}
		}
		return true;
	}

	void FMockStoreBackend::Revoke(int32 DefId)
	{
		if (Inventory.Remove(DefId) > 0)
		{
			Save();
			if (OnInventoryChanged)
			{
				OnInventoryChanged(Inventory);
			}
		}
	}

	void FMockStoreBackend::Wipe()
	{
		Inventory.Reset();
		Pending.Reset();
		AwaitingApproval.Reset();
		if (!SaveSlot.IsEmpty() && UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
		}
	}

	void FMockStoreBackend::Grant(const TArray<int32>& DefIds)
	{
		if (DefIds.Num() == 0)
		{
			return;
		}
		for (int32 Def : DefIds)
		{
			Inventory.Add(Def);
		}
		Save();
	}

	void FMockStoreBackend::Load()
	{
		UKartStoreSave* Saved = nullptr;
		if (!SaveSlot.IsEmpty() && SaveFile::Read(SaveSlot, Saved) == SaveFile::EReadResult::Loaded)
		{
			Inventory = TSet<int32>(Saved->OwnedCache);
		}
	}

	void FMockStoreBackend::Save() const
	{
		if (SaveSlot.IsEmpty())
		{
			return;
		}
		// The mock's "server" reuses the store save type; only the owned list means anything here.
		UKartStoreSave* Out = Cast<UKartStoreSave>(UGameplayStatics::CreateSaveGameObject(UKartStoreSave::StaticClass()));
		Out->OwnedCache = Inventory.Array();
		SaveFile::Write(SaveSlot, *Out);
	}
}
