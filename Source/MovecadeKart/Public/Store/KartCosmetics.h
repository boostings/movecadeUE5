#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"
#include "Store/KartStoreTypes.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UObject;

/**
 * What the games read to dress players in their cosmetics: one small, side-effect-free API over the store.
 * With no store (tests, commandlets) or nothing equipped, every call returns the game's own default.
 *
 * Who wears what: the kart and driver cosmetics (paint, colourway, headwear) are Player 1's, the person whose
 * Steam account owns them; the party-game skins (ball, katana, blocks) are worn by every player at this PC.
 * Phones and house bots keep the stock look.
 */
struct MOVECADEKART_API FKartCosmetics
{
	/** The item worn in Slot, or null. */
	static const MovecadeKart::Store::FItem* Equipped(MovecadeKart::Store::ESlot Slot);

	/** Paint, colourway and headwear onto a local player's look (LocalPlayer 0 only). */
	static void ApplyToLook(FKartLook& Look, int32 LocalPlayer);
	/** The equipped paint's name for Player 1's paint row, or Fallback. */
	static FText PaintName(int32 LocalPlayer, const FText& Fallback);
	/** The equipped paint's colour for Player 1, or Fallback. */
	static FLinearColor PaintColor(int32 LocalPlayer, const FLinearColor& Fallback);

	/** Colours the shared bowling ball material for the bowler who is up. */
	static void ApplyBowlingBall(UMaterialInstanceDynamic* BallMaterial, bool bLocalBowler, const FLinearColor& DefaultColor);
	/** The katana's material slots for a local swordsman (Default for anyone else, or with no skin). */
	static TArray<UMaterialInterface*> KatanaMaterials(UObject* Outer, bool bLocal, const TArray<UMaterialInterface*>& Default);
	/** A local builder's block colour. */
	static FLinearColor StackerBlockColor(bool bLocal, const FLinearColor& Default);
	/** The equipped victory emote's key, or NAME_None. */
	static FName VictoryEmote();
};
