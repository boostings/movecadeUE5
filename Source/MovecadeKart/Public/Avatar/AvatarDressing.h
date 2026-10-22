#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"

class UMaterialInstanceDynamic;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/**
 * Dresses an avatar4 driver (Content/Avatars, Tools/ue/import_karts.py) in an FKartLook: the face and body mesh,
 * one hair style (none under a cap or beanie), the accessory (a mesh section, or a prop built in code on the head),
 * a paid outfit leader-posed over the stock clothes, and the tints (skin, lips, hair, jersey, trousers, shoes, eyes).
 * Shared by the karts' drivers and every party game's player avatar, so a look reads the same in all of them.
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

	/** The skin's subsurface profile family (M_AvatarSkin's tone instances, Tools/ue/import_karts.py). */
	enum class ESkinTone : uint8
	{
		Light,
		Medium,
		Deep,
		Count
	};

	/** How a skin colour is shaded: its profile family and the tuning that varies smoothly with its depth. */
	struct FSkinTuning
	{
		ESkinTone Tone = ESkinTone::Medium;
		/** How far the light scatters under the skin (the material's Opacity), 0..1. */
		float Scatter = 0.8f;
		/** How strongly the cheeks, nose and ears flush. */
		float Flush = 1.f;
		/** The warm rim's strength and the lift in shadow (the master's Rim and Lift). */
		float Rim = 0.12f;
		float Lift = 0.008f;
		/** How much of the colour's saturation shows (deep tones are held back from turning maroon under the scatter). */
		float Saturation = 0.9f;
	};

	MOVECADEKART_API FSkinTuning SkinTuning(const FLinearColor& Skin);

	/**
	 * Where a face's eyes sit in the mesh's rest space (both bodies share the head), for the eye material: the right
	 * eyeball's centre (+X; the left mirrors it) and its radius, cm.
	 */
	struct FEyeRig
	{
		FVector Centre = FVector::ZeroVector;
		float Radius = 0.f;
	};

	MOVECADEKART_API FEyeRig EyeRig(EKartFace Face);

	struct MOVECADEKART_API FDressing
	{
		/** Puts Look's mesh, outfit and visible sections on the components (rebuilding only what changed). */
		EDressResult Dress(USkeletalMeshComponent* Driver, USkeletalMeshComponent* Outfit, const FKartLook& Look);
		/** Writes Look's colours into the tinted slots. */
		void Colour(const FKartLook& Look) const;
		/** Forget what was built, so the next Dress rebuilds everything. */
		void Invalidate() { BuiltDriver.Reset(); bRebuildOutfit = true; }
		/**
		 * Turns the irises (painted by the eye material) toward Direction, given in the mesh's rest space relative to
		 * the head. Writes only when it moved visibly. Undriven, the eyes look straight ahead out of the head.
		 */
		void AimEyes(const FVector& MeshDirection);

		/** The rim colour that keeps the avatar readable at a distance (the player's colour, lifted), and its strength. */
		static FLinearColor RimColour(const FKartLook& Look);

	private:
		/** Puts the code-built prop on the head (or takes it off). */
		void DressProp(USkeletalMeshComponent* Driver, const FKartLook& Look);
		/** Gives a skin slot its own instance of the skin tone's material (its subsurface profile), keeping the mesh's maps. */
		static UMaterialInstanceDynamic* MakeSkin(USkeletalMeshComponent* Mesh, int32 Index, ESkinTone Tone);
		/** Rebuilds the skin instances when the skin tone moves to another profile. */
		void DressSkin(USkeletalMeshComponent* Driver, USkeletalMeshComponent* Outfit, const FKartLook& Look);
		/**
		 * Gives brows and lashes their own material, and hands the eyeballs to the eye material, which paints (and turns)
		 * the irises in place of the modelled ones.
		 */
		void DressFace(USkeletalMeshComponent* Driver, EKartFace Face);

		TWeakObjectPtr<UStaticMeshComponent> Prop;
		EKartAccessory BuiltProp = EKartAccessory::None;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>, TInlineAllocator<3>> PropTints;
		TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>> DriverTints;
		TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>> OutfitTints;
		FString BuiltDriver;
		EKartOutfit BuiltOutfit = EKartOutfit::Classic;
		bool bRebuildOutfit = true;
		/** The skin tone the skin instances were made for (Count: none yet). */
		ESkinTone BuiltTone = ESkinTone::Count;
		/** The eye material's instance, and the direction last written to it. */
		TWeakObjectPtr<UMaterialInstanceDynamic> Eyes;
		FVector AimedEyes = FVector::ZeroVector;
	};

	/**
	 * The rotation that turns the avatar's own forward and up (from its rest skeleton) onto Forward and Up in its
	 * parent's space: the mesh's import axes differ from the scene's.
	 */
	MOVECADEKART_API FQuat Facing(const USkeletalMesh* Mesh, const FVector& Forward, const FVector& Up);

	/** appearanceColors.ts lipHex: the skin pulled 42 % toward a warm rose, then 0.9 (linear light). */
	MOVECADEKART_API FLinearColor LipColour(const FLinearColor& Skin);
	/** Brows and lashes: the hair's colour, deeper (fine hairs over skin read darker than a full head of it). */
	MOVECADEKART_API FLinearColor BrowColour(const FLinearColor& Hair);
	/** Each face its own eye colour. */
	MOVECADEKART_API FLinearColor IrisColour(EKartFace Face);
	/** The jersey darkened: the waistband and headband (appearanceColors.ts detailHex). */
	MOVECADEKART_API FLinearColor DetailColour(const FLinearColor& Jersey);
}
