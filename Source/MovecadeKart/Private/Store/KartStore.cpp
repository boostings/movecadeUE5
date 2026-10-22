#include "Store/KartStore.h"

#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartStore, Log, All);

#define LOCTEXT_NAMESPACE "KartStore"

namespace MovecadeKart::Store
{
	namespace
	{
		ESlot SlotFromId(FName Id)
		{
			for (uint8 i = 0; i < static_cast<uint8>(ESlot::Num); ++i)
			{
				if (Id == SlotId(static_cast<ESlot>(i)))
				{
					return static_cast<ESlot>(i);
				}
			}
			return ESlot::Num;
		}

		TArray<int32> PaidDefs()
		{
			TArray<int32> Out;
			for (const FItem& I : Catalog())
			{
				if (I.Source == ESource::Paid && !I.bComingSoon)
				{
					Out.Add(I.DefId);
				}
			}
			return Out;
		}
	}

	FKartStore::FKartStore(TUniquePtr<IKartStoreBackend> InBackend, const FString& InSaveSlot)
		: Backend(MoveTemp(InBackend)), SaveSlot(InSaveSlot)
	{
		check(Backend.IsValid());
		TWeakPtr<bool> WeakAlive = Alive;
		Backend->OnInventoryChanged = [this, WeakAlive](const TSet<int32>& NewOwned)
		{
			if (WeakAlive.IsValid())
			{
				SetOwned(NewOwned, true);
			}
		};
	}

	FKartStore::~FKartStore()
	{
		*Alive = false;
		Backend->OnInventoryChanged = nullptr;
	}

	void FKartStore::Start()
	{
		Load();
		Restore();
		TWeakPtr<bool> WeakAlive = Alive;
		Backend->RequestPrices(PaidDefs(), [this, WeakAlive](bool bOk, const TMap<int32, FPrice>& NewPrices)
		{
			if (!WeakAlive.IsValid())
			{
				return;
			}
			if (bOk)
			{
				Prices = NewPrices;
			}
			UE_LOG(LogKartStore, Log, TEXT("Prices: %s (%d)"), bOk ? TEXT("ok") : TEXT("unavailable"), Prices.Num());
			Changed();
		});
	}

	void FKartStore::Tick(double DeltaSeconds)
	{
		Backend->Tick(DeltaSeconds);
	}

	// ----------------------------------------------------------------------------------------------------- Queries

	bool FKartStore::IsOwned(int32 DefId) const
	{
		const FItem* Item = FindItem(DefId);
		if (Item && Item->Slot == ESlot::Bundle)
		{
			return Item->Contents.Num() > 0 && NumOwnedContents(DefId) == Item->Contents.Num();
		}
		return Owned.Contains(DefId);
	}

	int32 FKartStore::NumOwnedContents(int32 DefId) const
	{
		int32 N = 0;
		if (const FItem* Item = FindItem(DefId))
		{
			for (int32 Def : Item->Contents)
			{
				N += Owned.Contains(Def) ? 1 : 0;
			}
		}
		return N;
	}

	bool FKartStore::IsEquipped(int32 DefId) const
	{
		const FItem* Item = FindItem(DefId);
		return Item && GetEquipped(Item->Slot) == DefId;
	}

	int32 FKartStore::GetEquipped(ESlot Slot) const
	{
		const int32* Found = Equipped.Find(Slot);
		return Found ? *Found : 0;
	}

	const FItem* FKartStore::GetEquippedItem(ESlot Slot) const
	{
		const int32 Def = GetEquipped(Slot);
		return Def ? FindItem(Def) : nullptr;
	}

	EItemState FKartStore::GetState(int32 DefId) const
	{
		const FItem* Item = FindItem(DefId);
		if (!Item || Item->bComingSoon)
		{
			return EItemState::ComingSoon;
		}
		if (PurchasingDef == DefId || Granting.Contains(DefId))
		{
			return EItemState::Pending;
		}
		if (IsOwned(DefId))
		{
			return IsEquipped(DefId) ? EItemState::Equipped : EItemState::Owned;
		}
		switch (Item->Source)
		{
		case ESource::Earned: return EItemState::Locked;
		case ESource::Drop: return EItemState::NotDropped;
		default: return EItemState::ForSale;
		}
	}

	bool FKartStore::CanPurchase(int32 DefId) const
	{
		return GetState(DefId) == EItemState::ForSale && PurchasingDef == 0 && Backend->IsOnline() && Prices.Contains(DefId);
	}

	FText FKartStore::GetPriceText(int32 DefId) const
	{
		const FItem* Item = FindItem(DefId);
		if (!Item)
		{
			return FText::GetEmpty();
		}
		if (Item->bComingSoon)
		{
			return LOCTEXT("Soon", "Coming soon");
		}
		if (Item->IsFree())
		{
			return LOCTEXT("Free", "Free");
		}
		// Only Steam's own number is shown: a price the checkout then contradicts is worse than none.
		if (const FPrice* Price = Prices.Find(DefId))
		{
			return FormatPrice(*Price);
		}
		return LOCTEXT("NoPrice", "Price unavailable");
	}

	bool FKartStore::GetProgress(int32 DefId, int32& OutCurrent, int32& OutTarget) const
	{
		const FItem* Item = FindItem(DefId);
		if (!Item || Item->Source != ESource::Earned)
		{
			return false;
		}
		OutTarget = Item->Unlock.Target;
		OutCurrent = FMath::Min(GetStat(Item->Unlock.Stat), OutTarget);
		return true;
	}

	int32 FKartStore::GetStat(FName Stat) const
	{
		const int32* Found = Stats.Find(Stat);
		return Found ? *Found : 0;
	}

	// ----------------------------------------------------------------------------------------------------- Actions

	bool FKartStore::Purchase(int32 DefId)
	{
		const FItem* Item = FindItem(DefId);
		EPurchaseResult Refused = EPurchaseResult::Success;
		FString Why;
		if (!Item || Item->bComingSoon || Item->IsFree())
		{
			Refused = EPurchaseResult::Unavailable;
			Why = TEXT("not for sale");
		}
		else if (IsOwned(DefId))
		{
			Refused = EPurchaseResult::AlreadyOwned;
		}
		else if (PurchasingDef != 0)
		{
			Refused = EPurchaseResult::Unavailable;
			Why = TEXT("another purchase is open");
		}
		else if (!Backend->IsOnline())
		{
			Refused = EPurchaseResult::Failed;
			Why = TEXT("offline");
		}
		else if (!Prices.Contains(DefId))
		{
			Refused = EPurchaseResult::Unavailable;
			Why = TEXT("no price");
		}
		if (Refused != EPurchaseResult::Success)
		{
			UE_LOG(LogKartStore, Log, TEXT("Purchase %d refused: %s %s"), DefId, ResultId(Refused), *Why);
			OnPurchaseFinished.Broadcast(DefId, Refused, Why);
			return false;
		}
		PurchasingDef = DefId;
		UE_LOG(LogKartStore, Log, TEXT("Purchase %d (%s) started on %s"), DefId, *Item->Key.ToString(), *Backend->GetName().ToString());
		Changed();
		TWeakPtr<bool> WeakAlive = Alive;
		Backend->StartPurchase(DefId, [this, WeakAlive, DefId](EPurchaseResult Result, const FString& Detail)
		{
			if (WeakAlive.IsValid())
			{
				FinishPurchase(DefId, Result, Detail);
			}
		});
		return true;
	}

	void FKartStore::FinishPurchase(int32 DefId, EPurchaseResult Result, const FString& Detail)
	{
		PurchasingDef = 0;
		if (Result == EPurchaseResult::Success)
		{
			// The backend only reports success once the items are in the inventory; take them now, and refresh so the
			// cache matches the backend exactly.
			const FItem* Item = FindItem(DefId);
			TSet<int32> NewOwned = Owned;
			if (Item && Item->Slot == ESlot::Bundle)
			{
				NewOwned.Append(Item->Contents);
			}
			else
			{
				NewOwned.Add(DefId);
			}
			SetOwned(NewOwned, false);
		}
		UE_LOG(LogKartStore, Log, TEXT("Purchase %d: %s %s"), DefId, ResultId(Result), *Detail);
		Changed();
		OnPurchaseFinished.Broadcast(DefId, Result, Detail);
	}

	void FKartStore::Restore()
	{
		if (bRestoring)
		{
			return;
		}
		bRestoring = true;
		Changed();
		TWeakPtr<bool> WeakAlive = Alive;
		Backend->RefreshInventory([this, WeakAlive](bool bOk, const TSet<int32>& NewOwned)
		{
			if (!WeakAlive.IsValid())
			{
				return;
			}
			bRestoring = false;
			if (bOk)
			{
				bHasInventory = true;
				SetOwned(NewOwned, false);
			}
			UE_LOG(LogKartStore, Log, TEXT("Inventory refresh: %s, %d owned"), bOk ? TEXT("ok") : TEXT("failed (kept the cache)"), Owned.Num());
			EvaluateUnlocks();
			Changed();
			OnRestored.Broadcast(bOk);
		});
	}

	bool FKartStore::Equip(int32 DefId)
	{
		const FItem* Item = FindItem(DefId);
		if (!Item || !Item->IsEquippable() || !IsOwned(DefId))
		{
			return false;
		}
		Equipped.Add(Item->Slot, DefId);
		Save();
		Changed();
		return true;
	}

	void FKartStore::Unequip(ESlot Slot)
	{
		if (Equipped.Remove(Slot) > 0)
		{
			Save();
			Changed();
		}
	}

	void FKartStore::AddStat(FName Stat, int32 Delta)
	{
		if (Stat.IsNone() || Delta == 0)
		{
			return;
		}
		Stats.FindOrAdd(Stat) += Delta;
		Save();
		EvaluateUnlocks();
		Changed();
	}

	void FKartStore::AddPlaytime(double Seconds)
	{
		PlaySeconds += Seconds;
		SinceDropCheck += Seconds;
		if (SinceDropCheck < DropCheckSeconds || bDropInFlight)
		{
			return;
		}
		SinceDropCheck = 0.0;
		bDropInFlight = true;
		Save();
		TWeakPtr<bool> WeakAlive = Alive;
		Backend->TriggerPlaytimeDrop(PlaytimeGeneratorDef, [this, WeakAlive](bool bOk, const TArray<int32>& Granted)
		{
			if (!WeakAlive.IsValid())
			{
				return;
			}
			bDropInFlight = false;
			if (bOk && Granted.Num() > 0)
			{
				TSet<int32> NewOwned = Owned;
				NewOwned.Append(Granted);
				SetOwned(NewOwned, true);
				Changed();
			}
		});
	}

	void FKartStore::ResetLocal()
	{
		Owned.Reset();
		Equipped.Reset();
		Stats.Reset();
		PlaySeconds = 0.0;
		bHasInventory = false;
		Save();
		Changed();
	}

	void FKartStore::SetOwned(const TSet<int32>& NewOwned, bool bAnnounceNewFree)
	{
		TArray<int32> Arrived;
		for (int32 Def : NewOwned)
		{
			if (!Owned.Contains(Def))
			{
				Arrived.Add(Def);
			}
		}
		Owned = NewOwned;
		// A refund (or a restore on a fresh PC) can take items away: don't keep wearing them.
		for (auto It = Equipped.CreateIterator(); It; ++It)
		{
			if (!Owned.Contains(It.Value()))
			{
				It.RemoveCurrent();
			}
		}
		Save();
		if (bAnnounceNewFree)
		{
			for (int32 Def : Arrived)
			{
				if (const FItem* Item = FindItem(Def); Item && Item->IsFree())
				{
					OnUnlocked.Broadcast(Def);
				}
			}
		}
		Changed();
	}

	void FKartStore::EvaluateUnlocks()
	{
		for (const FItem& Item : Catalog())
		{
			if (Item.Source != ESource::Earned || Owned.Contains(Item.DefId) || Granting.Contains(Item.DefId) || GetStat(Item.Unlock.Stat) < Item.Unlock.Target)
			{
				continue;
			}
			Granting.Add(Item.DefId);
			const int32 Def = Item.DefId;
			UE_LOG(LogKartStore, Log, TEXT("Earned %d (%s): granting"), Def, *Item.Key.ToString());
			TWeakPtr<bool> WeakAlive = Alive;
			Backend->GrantEarned(Def, Item.Unlock.Achievement, [this, WeakAlive, Def](bool bOk)
			{
				if (!WeakAlive.IsValid())
				{
					return;
				}
				Granting.Remove(Def);
				if (bOk)
				{
					TSet<int32> NewOwned = Owned;
					NewOwned.Add(Def);
					SetOwned(NewOwned, true);
				}
				else
				{
					UE_LOG(LogKartStore, Log, TEXT("Grant %d failed; retried on the next refresh"), Def);
				}
				Changed();
			});
		}
	}

	// --------------------------------------------------------------------------------------------------- Persistence

	void FKartStore::Load()
	{
		if (SaveSlot.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
		{
			return;
		}
		const UKartStoreSave* Saved = Cast<UKartStoreSave>(UGameplayStatics::LoadGameFromSlot(SaveSlot, 0));
		if (!Saved)
		{
			return;
		}
		Owned = TSet<int32>(Saved->OwnedCache);
		Stats = Saved->Stats;
		PlaySeconds = Saved->PlaySeconds;
		Equipped.Reset();
		for (const TPair<FName, int32>& Pair : Saved->Equipped)
		{
			const ESlot Slot = SlotFromId(Pair.Key);
			const FItem* Item = FindItem(Pair.Value);
			if (Slot != ESlot::Num && Item && Item->Slot == Slot && Owned.Contains(Pair.Value))
			{
				Equipped.Add(Slot, Pair.Value);
			}
		}
	}

	void FKartStore::Save() const
	{
		if (SaveSlot.IsEmpty())
		{
			return;
		}
		UKartStoreSave* Out = Cast<UKartStoreSave>(UGameplayStatics::CreateSaveGameObject(UKartStoreSave::StaticClass()));
		Out->OwnedCache = Owned.Array();
		Out->OwnedCache.Sort();
		for (const TPair<ESlot, int32>& Pair : Equipped)
		{
			Out->Equipped.Add(SlotId(Pair.Key), Pair.Value);
		}
		Out->Stats = Stats;
		Out->PlaySeconds = PlaySeconds;
		UGameplayStatics::SaveGameToSlot(Out, SaveSlot, 0);
	}
}

#undef LOCTEXT_NAMESPACE
