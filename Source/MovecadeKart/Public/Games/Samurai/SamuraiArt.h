#pragma once

#include "CoreMinimal.h"
#include "Games/Samurai/SamuraiRules.h"

class UMaterialInterface;
class UObject;
class UStaticMesh;

/**
 * Samurai Slice's props modelled in code (movecade-v2 samurai/{fruitGeometry,targetClipping,katanaGeometry}.ts,
 * Docs/Port/08-samurai.md §4-§5): each target whole and as the two halves a cut leaves (split on local Y = 0, the
 * cut faces capped with flesh, pith or a dark interior), the katana, and the rings the halo and the bomb's
 * telegraph use. Meshes are unit radius (100 cm) for the targets; the game scales them by radius * .75.
 */
namespace MovecadeKart::Samurai::Art
{
	enum class EPart : uint8
	{
		Whole,
		/** The half on local +Y (it flies toward +Y). */
		Plus,
		Minus,
	};

	MOVECADEKART_API UStaticMesh* TargetMesh(UObject* Outer, EVariant Variant, EPart Part);
	/** One material per slot of TargetMesh (the same for the whole and its halves). */
	MOVECADEKART_API TArray<UMaterialInterface*> TargetMaterials(UObject* Outer, EVariant Variant);

	/** The katana along +X from the hilt end: grip, guard, then the curved blade, the edge to +Z. Real size (m * 100). */
	MOVECADEKART_API UStaticMesh* KatanaMesh(UObject* Outer);
	MOVECADEKART_API TArray<UMaterialInterface*> KatanaMaterials(UObject* Outer);

	/** A flat ring facing -X (toward the camera), radii in cm; Segments 8 gives the bomb's octagon. */
	MOVECADEKART_API UStaticMesh* RingMesh(UObject* Outer, double InnerCm, double OuterCm, int32 Segments, bool bBar = false);
}
