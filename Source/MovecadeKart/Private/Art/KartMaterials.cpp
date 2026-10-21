#include "Art/KartMaterials.h"

#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	UMaterialInterface* Master(const TCHAR* Name)
	{
		return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Tracks/Shared/Materials/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UTexture* SurfaceTexture(FName Set, const TCHAR* Kind)
	{
		const FString Name = FString::Printf(TEXT("T_%s_%s"), *Set.ToString(), Kind);
		return LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/Tracks/Shared/Surfaces/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** Each set's linear mean colour (the track exports' detailSurfaces[].mean), so a tint keeps its hue. */
	FLinearColor DetailMean(FName Set)
	{
		static const TMap<FName, FLinearColor> Means = {
			{ TEXT("asphalt_track"), FLinearColor(0.017f, 0.0137f, 0.0126f) },
			{ TEXT("painted_concrete"), FLinearColor(0.08f, 0.156f, 0.0881f) },
			{ TEXT("leafy_grass"), FLinearColor(0.3176f, 0.2342f, 0.1045f) },
			{ TEXT("gravelly_sand"), FLinearColor(0.2892f, 0.1605f, 0.0743f) },
			{ TEXT("rock_face"), FLinearColor(0.1206f, 0.0616f, 0.0315f) },
			{ TEXT("dark_wooden_planks"), FLinearColor(0.0877f, 0.0697f, 0.05f) },
			{ TEXT("plastered_wall_02"), FLinearColor(0.4932f, 0.4421f, 0.3683f) },
		};
		const FLinearColor* Mean = Means.Find(Set);
		return Mean ? *Mean : FLinearColor(0.2f, 0.2f, 0.2f);
	}
}

namespace MovecadeKart::Art
{
	FLinearColor Hex(const FString& Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code.Replace(TEXT("#"), TEXT(""))));
	}

	UMaterialInstanceDynamic* Plain(UObject* Outer, const FLinearColor& Color, double Roughness, double Metallic, double Emissive)
	{
		UMaterialInterface* Parent = Master(TEXT("M_TrackVertexColor"));
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		Mid->SetScalarParameterValue(TEXT("Metallic"), Metallic);
		Mid->SetScalarParameterValue(TEXT("Emissive"), Emissive);
		return Mid;
	}

	UMaterialInstanceDynamic* Surface(UObject* Outer, const FSurfaceLook& Look)
	{
		if (Look.Detail.IsNone())
		{
			return Plain(Outer, Look.Color, Look.Roughness, Look.Metallic, Look.Emissive);
		}
		UMaterialInterface* Parent = Master(Look.bTriplanar ? TEXT("M_TrackSurfaceWall") : TEXT("M_TrackSurface"));
		if (!Parent)
		{
			return Plain(Outer, Look.Color, Look.Roughness, Look.Metallic, Look.Emissive);
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
		Mid->SetVectorParameterValue(TEXT("Color"), Look.Color);
		Mid->SetScalarParameterValue(TEXT("Roughness"), Look.Roughness);
		Mid->SetScalarParameterValue(TEXT("Metallic"), Look.Metallic);
		Mid->SetScalarParameterValue(TEXT("Emissive"), Look.Emissive);
		if (UTexture* T = SurfaceTexture(Look.Detail, TEXT("color")))
		{
			Mid->SetTextureParameterValue(TEXT("DetailColor"), T);
		}
		if (UTexture* T = SurfaceTexture(Look.Detail, TEXT("normal")))
		{
			Mid->SetTextureParameterValue(TEXT("DetailNormal"), T);
		}
		if (UTexture* T = SurfaceTexture(Look.Detail, TEXT("rough")))
		{
			Mid->SetTextureParameterValue(TEXT("DetailRough"), T);
		}
		Mid->SetVectorParameterValue(TEXT("DetailMean"), DetailMean(Look.Detail));
		Mid->SetScalarParameterValue(TEXT("DetailTileM"), Look.TileM);
		Mid->SetScalarParameterValue(TEXT("DetailAlbedo"), Look.Albedo);
		Mid->SetScalarParameterValue(TEXT("DetailNormal"), Look.Normal);
		Mid->SetScalarParameterValue(TEXT("DetailRough"), Look.Rough);
		Mid->SetScalarParameterValue(TEXT("Desaturate"), Look.Desaturate);
		return Mid;
	}

	UMaterialInstanceDynamic* Glass(UObject* Outer, const FLinearColor& Color, double Opacity, double Roughness)
	{
		UMaterialInterface* Parent = Master(TEXT("M_TrackGlass"));
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetScalarParameterValue(TEXT("Opacity"), Opacity);
		Mid->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		return Mid;
	}

	UMaterialInstanceDynamic* Glow(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Parent = Master(TEXT("M_TrackTranslucent"));
		if (!Parent)
		{
			return nullptr;
		}
		// The translucent master is unlit: Color is emissive, Opacity 1 makes it a solid glowing strip.
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Mid->SetScalarParameterValue(TEXT("Opacity"), 1.0);
		return Mid;
	}

	UMaterialInstanceDynamic* Unlit(UObject* Outer, const TCHAR* TexturePath, const FLinearColor& Tint)
	{
		UMaterialInterface* Parent = Master(TEXT("M_TrackUnlitTexture"));
		UTexture* Map = LoadObject<UTexture>(nullptr, TexturePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Parent || !Map)
		{
			return Glow(Outer, Tint * 0.2f);
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Parent, Outer);
		Mid->SetTextureParameterValue(TEXT("Map"), Map);
		Mid->SetVectorParameterValue(TEXT("Color"), Tint);
		return Mid;
	}
}
