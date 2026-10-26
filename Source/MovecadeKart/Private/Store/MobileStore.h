#pragma once

#include "CoreMinimal.h"
#include "Store/KartStoreTypes.h"

/**
 * What the App Store and Google Play backends share, kept apart from the platform code so it builds and is tested on
 * every platform (MovecadeKart.Store.Mobile*): turning the products a store says you own into def ids, store prices,
 * and the free items a phone holds itself.
 */
namespace MovecadeKart::Store::Mobile
{
	/** The def ids a set of owned product ids stands for: items as themselves, bundles as their contents. Unknown ids are dropped. */
	TSet<int32> DefsForProducts(const TArray<FString>& ProductIds);

	/** A price from a mobile store: minor units, ISO currency, and the store's own formatted text (shown as is). */
	FPrice MakeStorePrice(int64 MinorUnits, const FString& Currency, const FText& Display);

	/**
	 * The free cosmetics on a phone. Steam holds earned and dropped items in the player's inventory; the App Store and
	 * Google Play have nothing like it (only paid products), so the device keeps them, in their own save slot
	 * (KartStoreFree.sav, with the store save's checked header). Same trust as Steam's free unlocks (F-04): free items
	 * are counted on the device, so editing the file gets free items early and nothing else.
	 *
	 * Gift drops follow Steam's playtime generator (itemdefs.json 990): one gift per about two hours of play, at most one
	 * per 24 hours, the next one the player doesn't have, three in total. Never random, never sold.
	 */
	class FLocalFreeItems
	{
	public:
		/** SaveSlot empty: memory only. Clock is UTC now (tests pass their own). */
		explicit FLocalFreeItems(const FString& InSaveSlot, TFunction<FDateTime()> InClock = []() { return FDateTime::UtcNow(); });

		const TSet<int32>& GetOwned() const { return Owned; }
		/** Grants an earned item (its goal is checked by FKartStore). False for anything that isn't an earned item. */
		bool GrantEarned(int32 DefId);
		/** One playtime check (FKartStore asks every DropCheckSeconds of play): the gift it drops, or none. */
		TArray<int32> PlaytimeCheck();
		/** Forgets everything, including the save slot. */
		void Wipe();

		/** Checks per gift: 12 checks of 10 minutes is Steam's drop_interval of 120 minutes. */
		static constexpr int32 ChecksPerDrop = 12;
		/** Steam's drop_window (1440 minutes) with drop_max_per_window 1. */
		static constexpr int32 MinMinutesBetweenDrops = 24 * 60;

	private:
		void Load();
		void Save() const;
		/** Minutes since 2020-01-01 UTC: an int32 for the save's stat map. */
		int32 NowMinutes() const;

		FString SaveSlot;
		TFunction<FDateTime()> Clock;
		TSet<int32> Owned;
		int32 ChecksSinceDrop = 0;
		/** NowMinutes of the last gift, or a value a day in the past when none dropped yet. */
		int32 LastDropMinutes = TNumericLimits<int32>::Lowest() / 2;
	};
}
