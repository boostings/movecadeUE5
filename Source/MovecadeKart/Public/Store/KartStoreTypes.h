#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"

/**
 * The cosmetics store's data: what is for sale (or earned), where it applies, and what a purchase can end as.
 * Everything here is cosmetic. Nothing changes handling, scoring, items or any rule a game runs.
 */
namespace MovecadeKart::Store
{
	/** Where an item is worn. One item per slot is equipped at a time. */
	enum class ESlot : uint8
	{
		KartPaint,
		/** Jersey, pants and shoes (shows with the Classic Jersey outfit). */
		Colourway,
		/** Cap, shades or headband (the accessory slots in the avatar mesh). */
		Headwear,
		BowlingBall,
		Katana,
		StackerBlock,
		VictoryEmote,
		/** Not equipped: buying it grants its contents. */
		Bundle,
		Num,
	};

	/** How an item is obtained. */
	enum class ESource : uint8
	{
		/** Bought through the platform's checkout (Steam overlay, App Store sheet, Google Play sheet). */
		Paid,
		/** Free, earned by reaching a play stat (Steam: promo item tied to an achievement; mobile: kept on the device). */
		Earned,
		/** Free, dropped while you play (Steam: the playtime generator; mobile: the same rules, run on the device). */
		Drop,
	};

	/** Whose checkout sells the paid items: decides the words the store shows, the product ids and the price tiers. */
	enum class EStorefront : uint8
	{
		Steam,
		AppStore,
		GooglePlay,
	};

	enum class EPurchaseResult : uint8
	{
		Success,
		/** The player closed the checkout or declined: nothing was charged. */
		Cancelled,
		Failed,
		AlreadyOwned,
		/** Not for sale (coming soon, free item, unknown id, offline). */
		Unavailable,
		/**
		 * Mobile: the checkout ended without a charge yet (Ask to Buy waiting for a parent, a Google Play payment still
		 * clearing). Not owned; if it goes through, the item arrives later as an inventory change.
		 */
		Pending,
	};

	/** A free unlock's goal: reach Target on Stat. */
	struct FUnlockRule
	{
		FName Stat;
		int32 Target = 0;
		/** Steam achievement whose promo rule grants the item ("ach:<name>" in itemdefs.json). */
		FName Achievement;
	};

	/** The look data a slot needs. Unused fields stay at their defaults. */
	struct FVisual
	{
		FLinearColor Primary = FLinearColor::White;
		FLinearColor Secondary = FLinearColor::White;
		FLinearColor Tertiary = FLinearColor::White;
		float Roughness = 0.4f;
		float Metallic = 0.f;
		float Emissive = 0.f;
		EKartAccessory Accessory = EKartAccessory::None;
	};

	struct FItem
	{
		/** The Steam itemdefid (Docs/Store/itemdefs.json). Stable forever: never reuse or renumber. */
		int32 DefId = 0;
		/** A readable key for logs, tests and the command line ("paint.chrome"). */
		FName Key;
		FText Name;
		FText Description;
		ESlot Slot = ESlot::KartPaint;
		ESource Source = ESource::Paid;
		/** Mock and fallback price in US cents (Steam's own price, in the player's currency, wins when known). */
		int32 PriceCents = 0;
		/** The same tier on the App Store and Google Play, in US cents ($0.99 where Steam asks $1.00). */
		int32 MobilePriceCents = 0;
		/** Steam price tier ("1;VLV99"), written to itemdefs.json. */
		FString PriceCategory;
		FUnlockRule Unlock;
		FVisual Visual;
		/** Bundles: the def ids they grant. */
		TArray<int32> Contents;
		/** Shown but not sold yet (the game doesn't use it yet). Never take money for something that doesn't work. */
		bool bComingSoon = false;

		bool IsFree() const { return Source != ESource::Paid; }
		bool IsEquippable() const { return Slot != ESlot::Bundle && !bComingSoon; }
	};

	/** A price as the backend reports it: Amount in the currency's minor units. */
	struct FPrice
	{
		uint64 Amount = 0;
		uint64 BaseAmount = 0;
		FString Currency;
		/**
		 * The store's own formatted price ("1,09 €"), when it gives one (StoreKit's displayPrice, Google Play's
		 * formattedPrice). Shown as is: it is exactly what the checkout sheet will say. Empty on Steam.
		 */
		FText Display;
	};

	MOVECADEKART_API const TCHAR* SlotId(ESlot Slot);
	MOVECADEKART_API FText SlotName(ESlot Slot);
	MOVECADEKART_API const TCHAR* ResultId(EPurchaseResult Result);
	/** "steam", "appstore", "googleplay". */
	MOVECADEKART_API const TCHAR* StorefrontId(EStorefront Storefront);
	/** The store's name as the player reads it: "Steam", "the App Store", "Google Play". */
	MOVECADEKART_API FText StorefrontName(EStorefront Storefront);
	/** The storefront this build sells through: the App Store on iOS, Google Play on Android, else Steam. */
	MOVECADEKART_API EStorefront NativeStorefront();
	/** "$1.99", "1,99 €": the store's own text when it gave one, else the amount in its currency for the player's locale. */
	MOVECADEKART_API FText FormatPrice(const FPrice& Price);
}
