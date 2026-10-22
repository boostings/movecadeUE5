#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"

class UStaticMesh;

/**
 * Accessories modelled in code (FMeshBuilder), worn on the avatar4 head bone: a crown, a party hat, headphones and a
 * beanie. They are built once per kind (transient meshes), in the head bone's own space (the rig's import axes:
 * X to the avatar's right, -Y forward, Z up, origin at the base of the skull), so they ride every head and follow the
 * cartoon head scale of the anim proxies.
 */
namespace MovecadeKart::Avatar
{
	/** Material slots of a prop mesh, in the order the meshes create them. */
	enum class EPropSlot : uint8
	{
		Main,
		Trim,
		Accent,
		Count,
	};

	/** Whether Accessory is one of the props built here (rather than a section of the avatar4 mesh). */
	MOVECADEKART_API bool IsBuiltProp(EKartAccessory Accessory);

	/** Whether Accessory covers the hair (the cap and the beanie hide every hair style). */
	MOVECADEKART_API bool HidesHair(EKartAccessory Accessory);

	/** The prop's mesh (built on first use, then shared), or null for the mesh accessories and None. */
	MOVECADEKART_API UStaticMesh* PropMesh(EKartAccessory Accessory);

	/** The prop's colour in each slot for a look (linear), and whether that slot is metal. */
	MOVECADEKART_API FLinearColor PropColour(EKartAccessory Accessory, EPropSlot Slot, const FKartLook& Look);
	MOVECADEKART_API bool PropIsCloth(EKartAccessory Accessory, EPropSlot Slot);
}
