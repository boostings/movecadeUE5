#pragma once

#include "CoreMinimal.h"

class UObject;
class UStaticMesh;

/** The dojo's own props, modelled in code with FMeshBuilder (ASamuraiDojo builds each once). Sizes in UE cm. */
namespace MovecadeKart::Samurai::DojoMeshes
{
	/** A chochin: ribbed paper about a black top and bottom (slots Paper, Cap). */
	UStaticMesh* Lantern(UObject* Outer);
	/** A layered ridge: a jagged silhouette 120 m wide, facing the camera. */
	UStaticMesh* Ridge(UObject* Outer);
	/** A garden pine: a trunk and four flat tiers (slots Trunk, Needles). */
	UStaticMesh* Pine(UObject* Outer);
	/** A stepping stone: a flattened, lumpy disc. */
	UStaticMesh* Stone(UObject* Outer);
	/** The kasagi: the torii's top beam, its ends sweeping up (half length 100 cm, scaled per torii). */
	UStaticMesh* Kasagi(UObject* Outer);
}
