// ATrackBuilder's scenery materials: a JSON material hint ({shader, color, texture, detail, params, ...}) becomes a
// dynamic instance of the matching master in Content/Tracks/Shared/Materials, cached by everything that sets it.
#include "TrackBuilder.h"

#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "KartAssetLoad.h"
#include "Track/TrackBuildHelpers.h"

using namespace MovecadeKart;
using namespace MovecadeKart::TrackBuild;

namespace
{
	/** M_TrackSurface* detail texture slots, by the detailSurfaces[] set's field. */
	struct FDetailSlot
	{
		const TCHAR* Field;
		const TCHAR* Parameter;
	};
	const FDetailSlot DetailSlots[] = {
		{ TEXT("color"), TEXT("DetailColor") },
		{ TEXT("normal"), TEXT("DetailNormal") },
		{ TEXT("rough"), TEXT("DetailRough") },
	};

	/** Every input that shapes the instance, so equal hints share one. */
	FString HintKey(const TSharedPtr<FJsonObject>& Hint, const FString& Shader, const FString& Color, const FString& Texture, const FString& TextureAsset,
		const FString& Detail, double Roughness, double Metallic, double Emissive, double Opacity, const FString& ParamsKey)
	{
		return FString::Printf(TEXT("%s|%s|%s|%s|%s|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.3f"), *Shader, *Color, *Texture, *TextureAsset, *Detail,
			Roughness, Metallic, Emissive, Opacity, Num(Hint, TEXT("tileM")), Num(Hint, TEXT("albedo")), Num(Hint, TEXT("normal")), Num(Hint, TEXT("rough")),
			Num(Hint, TEXT("desaturate")), Num(Hint, TEXT("pulse"))) + FString::Printf(TEXT("|%.3f|"), Num(Hint, TEXT("baseDesaturate"))) + ParamsKey;
	}
}

UTexture* ATrackBuilder::TextureNamed(const FString& Name)
{
	for (const TSharedPtr<FJsonValue>& V : Arr(Json, TEXT("textures")))
	{
		const TSharedPtr<FJsonObject> T = V->AsObject();
		if (Str(T, TEXT("name")) == Name)
		{
			return LoadAsset<UTexture>(Str(T, TEXT("ueAsset")));
		}
	}
	return nullptr;
}

UMaterialInterface* ATrackBuilder::MaterialFor(const TSharedPtr<FJsonObject>& Hint, const FString& TintOverride, int32 Variant)
{
	FString Shader = Str(Hint, TEXT("shader"));
	bool bTriplanar = false;
	if (Hint.IsValid() && Hint->TryGetBoolField(TEXT("triplanar"), bTriplanar) && bTriplanar && Shader == TEXT("surface"))
	{
		Shader = TEXT("surfaceWall");
	}
	const FString Color = TintOverride.IsEmpty() ? Str(Hint, TEXT("color")) : TintOverride;
	const FString Texture = Str(Hint, TEXT("texture")).Replace(TEXT("{variant}"), *FString::FromInt(Variant));
	const FString TextureAsset = Str(Hint, TEXT("textureAsset"));
	const FString Detail = Str(Hint, TEXT("detail"));
	const double Roughness = Num(Hint, TEXT("roughness"), 0.8);
	const double Metallic = Num(Hint, TEXT("metalness"), 0.0);
	const double Emissive = Num(Hint, TEXT("emissive"), 0.0);
	const double Opacity = Num(Hint, TEXT("opacity"), 1.0);
	// Free-form master parameters (hint.params): numbers, "#rrggbb" colours, [r, g, b] vectors, texture names.
	const TSharedPtr<FJsonObject> Params = Obj(Hint, TEXT("params"));
	FString ParamsKey;
	if (Params.IsValid())
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ParamsKey);
		FJsonSerializer::Serialize(Params.ToSharedRef(), Writer);
	}
	const FString Key = HintKey(Hint, Shader, Color, Texture, TextureAsset, Detail, Roughness, Metallic, Emissive, Opacity, ParamsKey);
	if (TObjectPtr<UMaterialInterface>* Found = MaterialCache.Find(Key))
	{
		return *Found;
	}
	UMaterialInterface* Parent = LoadKartAsset<UMaterialInterface>(*MaterialPath(Shader), AssetSource::TrackAssets);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
	MID->SetVectorParameterValue(TEXT("Color"), HexColor(Color));
	MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	MID->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	MID->SetScalarParameterValue(TEXT("Emissive"), Emissive);
	MID->SetScalarParameterValue(TEXT("Opacity"), Opacity);
	UTexture* Map = !TextureAsset.IsEmpty() ? LoadAsset<UTexture>(TextureAsset) : (!Texture.IsEmpty() ? TextureNamed(Texture) : nullptr);
	if (Map)
	{
		MID->SetTextureParameterValue(TEXT("Map"), Map);
	}
	ApplyDetailSurface(MID, Hint, Detail);
	if (Params.IsValid())
	{
		ApplyHintParams(MID, Params);
	}
	if (Shader == TEXT("boostPad"))
	{
		// boostPadLook: chevrons scroll forward (texture up) and pulse.
		MID->SetScalarParameterValue(TEXT("PanV"), Num(Hint, TEXT("scrollPerS"), 1.6)); // + moves the art toward v = 0, the image top = forward
		MID->SetScalarParameterValue(TEXT("Pulse"), Num(Hint, TEXT("pulse"), 0.5));
		MID->SetScalarParameterValue(TEXT("PulseRate"), Num(Hint, TEXT("pulseRadPerS"), 6.0));
	}
	MaterialCache.Add(Key, MID);
	return MID;
}

void ATrackBuilder::ApplyDetailSurface(UMaterialInstanceDynamic* MID, const TSharedPtr<FJsonObject>& Hint, const FString& Detail)
{
	// World-space CC0 detail (M_TrackSurface*): the set's maps and linear mean from detailSurfaces[].
	const TSharedPtr<FJsonObject> Set = Obj(Obj(Json, TEXT("detailSurfaces")), *Detail);
	if (!Set.IsValid())
	{
		return;
	}
	for (const FDetailSlot& Slot : DetailSlots)
	{
		if (UTexture* T = LoadAsset<UTexture>(Str(Set, Slot.Field)))
		{
			MID->SetTextureParameterValue(Slot.Parameter, T);
		}
	}
	const FVector Mean = Vec3(Arr(Set, TEXT("mean")));
	MID->SetVectorParameterValue(TEXT("DetailMean"), FLinearColor(Mean.X, Mean.Y, Mean.Z, 1.0));
	MID->SetScalarParameterValue(TEXT("DetailTileM"), Num(Hint, TEXT("tileM"), 4.0));
	MID->SetScalarParameterValue(TEXT("DetailAlbedo"), Num(Hint, TEXT("albedo"), 0.5));
	MID->SetScalarParameterValue(TEXT("DetailNormal"), Num(Hint, TEXT("normal"), 0.5));
	MID->SetScalarParameterValue(TEXT("DetailRough"), Num(Hint, TEXT("rough"), 0.5));
	MID->SetScalarParameterValue(TEXT("Desaturate"), Num(Hint, TEXT("desaturate"), 0.0));
	MID->SetScalarParameterValue(TEXT("BaseDesaturate"), Num(Hint, TEXT("baseDesaturate"), 0.0));
}

void ATrackBuilder::ApplyHintParams(UMaterialInstanceDynamic* MID, const TSharedPtr<FJsonObject>& Params)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Params->Values)
	{
		const FName Name(*P.Key);
		FString Text;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (P.Value->Type == EJson::Number)
		{
			MID->SetScalarParameterValue(Name, P.Value->AsNumber());
		}
		else if (P.Value->TryGetArray(Values) && Values->Num() >= 3)
		{
			MID->SetVectorParameterValue(Name, FLinearColor((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber(), Values->Num() > 3 ? (*Values)[3]->AsNumber() : 1.0));
		}
		else if (P.Value->TryGetString(Text))
		{
			if (Text.StartsWith(TEXT("#")))
			{
				MID->SetVectorParameterValue(Name, HexColor(Text));
			}
			else if (UTexture* T = TextureNamed(Text))
			{
				MID->SetTextureParameterValue(Name, T);
			}
		}
	}
}
