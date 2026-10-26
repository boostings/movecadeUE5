#include "Store/MobileStore.h"

#include "Kismet/GameplayStatics.h"
#include "Store/KartStoreCatalog.h"
#include "Store/KartStoreSave.h"
#include "Store/KartStoreSaveFile.h"

namespace MovecadeKart::Store::Mobile
{
	namespace
	{
		/** The ledger reuses the store save type: OwnedCache is the free items, two stats the drop clock. */
		const FName ChecksStat(TEXT("drop.checks"));
		const FName LastDropStat(TEXT("drop.lastMinute"));
		const FDateTime MinuteEpoch(2020, 1, 1);
	}

	TSet<int32> DefsForProducts(const TArray<FString>& ProductIds)
	{
		TSet<int32> Out;
		for (const FString& Id : ProductIds)
		{
			const FItem* Item = FindItemByProductId(Id);
			if (!Item)
			{
				continue;
			}
			if (Item->Slot == ESlot::Bundle)
			{
				Out.Append(Item->Contents);
			}
			else
			{
				Out.Add(Item->DefId);
			}
		}
		return Out;
	}

	FPrice MakeStorePrice(int64 MinorUnits, const FString& Currency, const FText& Display)
	{
		FPrice Price;
		Price.Amount = static_cast<uint64>(FMath::Max<int64>(MinorUnits, 0));
		Price.BaseAmount = Price.Amount;
		Price.Currency = Currency;
		Price.Display = Display;
		return Price;
	}

	FLocalFreeItems::FLocalFreeItems(const FString& InSaveSlot, TFunction<FDateTime()> InClock)
		: SaveSlot(InSaveSlot), Clock(MoveTemp(InClock))
	{
		check(Clock);
		Load();
	}

	bool FLocalFreeItems::GrantEarned(int32 DefId)
	{
		const FItem* Item = FindItem(DefId);
		if (!Item || Item->Source != ESource::Earned)
		{
			return false;
		}
		if (!Owned.Contains(DefId))
		{
			Owned.Add(DefId);
			Save();
		}
		return true;
	}

	TArray<int32> FLocalFreeItems::PlaytimeCheck()
	{
		TArray<int32> Granted;
		++ChecksSinceDrop;
		const int32 Now = NowMinutes();
		// 64-bit: the stored minute comes from a file and may be anything.
		const bool bDue = ChecksSinceDrop >= ChecksPerDrop && static_cast<int64>(Now) - LastDropMinutes >= MinMinutesBetweenDrops;
		if (bDue)
		{
			for (int32 Def : DropPool())
			{
				if (!Owned.Contains(Def))
				{
					Granted.Add(Def);
					Owned.Add(Def);
					ChecksSinceDrop = 0;
					LastDropMinutes = Now;
					break;
				}
			}
		}
		Save();
		return Granted;
	}

	void FLocalFreeItems::Wipe()
	{
		Owned.Reset();
		ChecksSinceDrop = 0;
		LastDropMinutes = TNumericLimits<int32>::Lowest() / 2;
		if (!SaveSlot.IsEmpty() && UGameplayStatics::DoesSaveGameExist(SaveSlot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(SaveSlot, 0);
		}
	}

	int32 FLocalFreeItems::NowMinutes() const
	{
		return static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>((Clock() - MinuteEpoch).GetTotalMinutes()), 0, TNumericLimits<int32>::Max()));
	}

	void FLocalFreeItems::Load()
	{
		UKartStoreSave* Saved = nullptr;
		if (SaveSlot.IsEmpty() || SaveFile::Read(SaveSlot, Saved) != SaveFile::EReadResult::Loaded)
		{
			return;
		}
		for (int32 Def : Saved->OwnedCache)
		{
			// Only free items live here: a paid def id in this file is ignored (paid ownership is the store's word).
			if (const FItem* Item = FindItem(Def); Item && Item->IsFree())
			{
				Owned.Add(Def);
			}
		}
		if (const int32* Checks = Saved->Stats.Find(ChecksStat))
		{
			ChecksSinceDrop = FMath::Max(*Checks, 0);
		}
		if (const int32* Last = Saved->Stats.Find(LastDropStat))
		{
			LastDropMinutes = *Last;
		}
	}

	void FLocalFreeItems::Save() const
	{
		if (SaveSlot.IsEmpty())
		{
			return;
		}
		UKartStoreSave* Out = Cast<UKartStoreSave>(UGameplayStatics::CreateSaveGameObject(UKartStoreSave::StaticClass()));
		Out->OwnedCache = Owned.Array();
		Out->OwnedCache.Sort();
		Out->Stats.Add(ChecksStat, ChecksSinceDrop);
		Out->Stats.Add(LastDropStat, LastDropMinutes);
		SaveFile::Write(SaveSlot, *Out);
	}
}
