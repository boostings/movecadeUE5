#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"

class UMaterialInstanceDynamic;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Dresses an avatar4 driver (Content/Avatars, Tools/ue/import_karts.py) in an FKartLook: the face and body mesh,
 * one hair style (none under a cap or beanie), the accessory (a mesh section, or a prop built in code on the head), a paid outfit leader-posed over the stock clothes, and the
 * tints (skin, lips, hair, jersey, trousers, shoes, eyes). Shared by the karts' drivers and every party game's
 * player avatar, so a look reads the same in all of them.
 */
namespace MovecadeKart::Avatar
{
	enum class EDressResult : uint8
	{
		Unchanged,
		/** A new face or body mesh went on: the caller places it and gives it its anim instance. */
		NewMesh,
		/** The mesh isn't imported. */
		Missing,
	};

	struct MOVECADEKART_API FDressing
	{
		/** Puts Look's mesh, outfit and visible sections on the components (rebuilding only what changed). */
		EDressResult Dress(USkeletalMeshComponent* Driver, USkeletalMeshComponent* Outfit, const FKartLook& Look);
		/** Writes Look's colours into the tinted slots. */
		void Colour(const FKartLook& Look) const;
		/** Forget what was built, so the next Dress rebuilds everything. */
		void Invalidate() { BuiltDriver.Reset(); bRebuildOutfit = true; }

		/** The rim colour that keeps the avatar readable at a distance (the player's colour, lifted), and its strength. */
		static FLinearColor RimColour(const FKartLook& Look);

	private:
		/** Puts the code-built prop on the head (or takes it off). */
		void DressProp(USkeletalMeshComponent* Driver, const FKartLook& Look);

		TWeakObjectPtr<UStaticMeshComponent> Prop;
		EKartAccessory BuiltProp = EKartAccessory::None;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>, TInlineAllocator<3>> PropTints;
		TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>> DriverTints;
		TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>> OutfitTints;
		FString BuiltDriver;
		EKartOutfit BuiltOutfit = EKartOutfit::Classic;
		bool bRebuildOutfit = true;
	};

	/**
	 * The rotation that turns the avatar's own forward and up (from its rest skeleton) onto Forward and Up in its
	 * parent's space: the mesh's import axes differ from the scene's.
	 */
	MOVECADEKART_API FQuat Facing(const USkeletalMesh* Mesh, const FVector& Forward, const FVector& Up);

	/** appearanceColors.ts lipHex: the skin pulled 42 % toward a warm rose, then 0.9 (linear light). */
	MOVECADEKART_API FLinearColor LipColour(const FLinearColor& Skin);
	/** Each face its own eye colour. */
	MOVECADEKART_API FLinearColor IrisColour(EKartFace Face);
	/** The jersey darkened: the waistband and headband (appearanceColors.ts detailHex). */
	MOVECADEKART_API FLinearColor DetailColour(const FLinearColor& Jersey);
}
