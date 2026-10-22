#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UObject;

/**
 * Instances of the shared FX masters in Content/FX/Materials (Tools/ue/make_fx_materials.py), set up the same way
 * wherever an item or a glow is drawn. Each returns null when the master is missing (FX content not built).
 */
namespace MovecadeKart::Fx
{
	/** The clear-coated item paint (M_KartItem). An unset ClearCoat keeps the master's. */
	MOVECADEKART_API UMaterialInstanceDynamic* MakeItemMaterial(UObject* Outer, const FLinearColor& Base, const FLinearColor& Emissive, float EmissiveStrength,
		float Metallic, float Roughness, TOptional<float> ClearCoat = {});

	/** The soft additive glow sphere (M_FX_Glow), with its falloff (Power) and hot core; colour and intensity stay the
	 *  master's until set. */
	MOVECADEKART_API UMaterialInstanceDynamic* MakeGlowMaterial(UObject* Outer, float Power, float Core);
}
