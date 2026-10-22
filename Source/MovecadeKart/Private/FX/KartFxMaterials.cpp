#include "FX/KartFxMaterials.h"

#include "KartAssetLoad.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace MovecadeKart::Fx
{
	namespace
	{
		const TCHAR* const ItemMaster = TEXT("/Game/FX/Materials/M_KartItem.M_KartItem");
		const TCHAR* const GlowMaster = TEXT("/Game/FX/Materials/M_FX_Glow.M_FX_Glow");
	}

	UMaterialInstanceDynamic* MakeItemMaterial(UObject* Outer, const FLinearColor& Base, const FLinearColor& Emissive, float EmissiveStrength,
		float Metallic, float Roughness, TOptional<float> ClearCoat)
	{
		UMaterialInterface* Parent = LoadKartAsset<UMaterialInterface>(ItemMaster, AssetSource::FxMaterials);
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, Outer);
		M->SetVectorParameterValue(TEXT("BaseColor"), Base);
		M->SetVectorParameterValue(TEXT("Emissive"), Emissive);
		M->SetScalarParameterValue(TEXT("EmissiveStrength"), EmissiveStrength);
		M->SetScalarParameterValue(TEXT("Metallic"), Metallic);
		M->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		if (ClearCoat.IsSet())
		{
			M->SetScalarParameterValue(TEXT("ClearCoat"), ClearCoat.GetValue());
		}
		return M;
	}

	UMaterialInstanceDynamic* MakeGlowMaterial(UObject* Outer, float Power, float Core)
	{
		UMaterialInterface* Parent = LoadKartAsset<UMaterialInterface>(GlowMaster, AssetSource::FxMaterials);
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, Outer);
		M->SetScalarParameterValue(TEXT("Power"), Power);
		M->SetScalarParameterValue(TEXT("Core"), Core);
		return M;
	}
}
