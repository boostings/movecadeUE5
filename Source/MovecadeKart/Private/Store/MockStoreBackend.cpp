#include "Store/MockStoreBackend.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Store/KartStoreCatalog.h"
#include "Store/KartStoreSave.h"

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
		// Development: -KartStoreMock=succeed|cancel|fail picks how every checkout ends.
		FString Outcome;
		if (FParse::Value(FCommandLine::Get(), TEXT("KartStoreMock="), Outcome))
		{
			NextOutcome = Outcome.Equals(TEXT("cancel"), ESearchCase::IgnoreCase) ? EMockOutcome::Cancel
				: Outcome.Equals(TEXT("fail"), ESearchCase::IgnoreCase) ? EMockOutcome::Fail : EMockOutcome::Succeed;
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

	void FMockStoreBackend::RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool, const TMap<int32, FPrice>&)> Done)
	{
		After(PriceSeconds, [this, DefIds, Done = MoveTemp(Done)]()
		{
			TMap<int32, FPrice> Prices;
			if (bOnline)
			{
				for (int32 Def : DefIds)
				{
					if (const FItem* Item = FindItem(Def); Item && Item->Source == ESource::Paid && !Item->bComingSoon)
					{
						Prices.Add(Def, { static_cast<uint64>(Item->PriceCents), static_cast<uint64>(Item->PriceCents), TEXT("USD") });
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
		UE_LOG(LogKartStoreMock, Display, TEXT("Mock checkout for %d (%s): %s"), DefId, *Item->Key.ToString(),
			Outcome == EMockOutcome::Succeed ? TEXT("approve") : Outcome == EMockOutcome::Cancel ? TEXT("cancel") : TEXT("fail"));
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
		if (SaveSlot.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
		{
			return;
		}
		if (const UKartStoreSave* Saved = Cast<UKartStoreSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0)))
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
		UGameplayStatics::SaveGameToSlot(Out, SaveSlot, 0);
	}
}
