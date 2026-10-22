#include "Art/KartMaterialCache.h"

#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** Builds a cache key from the exact bits of each argument, after a prefix naming the kind of material. */
	struct FKeyBuilder
	{
		explicit FKeyBuilder(const TCHAR* Kind) : Key(Kind) {}

		FKeyBuilder& operator<<(double Value)
		{
			Key.Appendf(TEXT("|%016llx"), BitCast<uint64>(Value));
			return *this;
		}
		FKeyBuilder& operator<<(const FLinearColor& Color)
		{
			Key.Appendf(TEXT("|%08x%08x%08x%08x"), BitCast<uint32>(Color.R), BitCast<uint32>(Color.G), BitCast<uint32>(Color.B), BitCast<uint32>(Color.A));
			return *this;
		}
		FKeyBuilder& operator<<(const FString& Text)
		{
			Key.Appendf(TEXT("|%s"), *Text);
			return *this;
		}

		FString Key;
	};
}

UMaterialInterface* FKartMaterialCache::FindOrMake(const FString& Key, TFunctionRef<UMaterialInterface*()> Make)
{
	if (const TObjectPtr<UMaterialInterface>* Found = Materials.Find(Key))
	{
		return *Found;
	}
	UMaterialInterface* Made = Make();
	if (Made)
	{
		Materials.Add(Key, Made);
	}
	return Made;
}

UMaterialInstanceDynamic* FKartMaterialCache::FindOrMakeDynamic(const FString& Key, TFunctionRef<UMaterialInstanceDynamic*()> Make)
{
	return Cast<UMaterialInstanceDynamic>(FindOrMake(Key, [&Make]() -> UMaterialInterface* { return Make(); }));
}

UMaterialInstanceDynamic* FKartMaterialCache::Plain(UObject* Outer, const FLinearColor& Color, double Roughness, double Metallic, double Emissive)
{
	const FString Key = (FKeyBuilder(TEXT("plain")) << Color << Roughness << Metallic << Emissive).Key;
	return FindOrMakeDynamic(Key, [&]() { return MovecadeKart::Art::Plain(Outer, Color, Roughness, Metallic, Emissive); });
}

UMaterialInstanceDynamic* FKartMaterialCache::Surface(UObject* Outer, const MovecadeKart::Art::FSurfaceLook& Look)
{
	const FString Key = (FKeyBuilder(TEXT("surface")) << Look.Color << Look.Detail.ToString() << Look.TileM << Look.Albedo << Look.Normal << Look.Rough
		<< Look.Roughness << Look.Metallic << Look.Emissive << (Look.bTriplanar ? 1.0 : 0.0) << Look.Desaturate).Key;
	return FindOrMakeDynamic(Key, [&]() { return MovecadeKart::Art::Surface(Outer, Look); });
}

UMaterialInstanceDynamic* FKartMaterialCache::Glass(UObject* Outer, const FLinearColor& Color, double Opacity, double Roughness)
{
	const FString Key = (FKeyBuilder(TEXT("glass")) << Color << Opacity << Roughness).Key;
	return FindOrMakeDynamic(Key, [&]() { return MovecadeKart::Art::Glass(Outer, Color, Opacity, Roughness); });
}

UMaterialInstanceDynamic* FKartMaterialCache::Glow(UObject* Outer, const FLinearColor& Color)
{
	const FString Key = (FKeyBuilder(TEXT("glow")) << Color).Key;
	return FindOrMakeDynamic(Key, [&]() { return MovecadeKart::Art::Glow(Outer, Color); });
}

UMaterialInstanceDynamic* FKartMaterialCache::GlowOpaque(UObject* Outer, const FLinearColor& Color)
{
	const FString Key = (FKeyBuilder(TEXT("glowopaque")) << Color).Key;
	return FindOrMakeDynamic(Key, [&]() { return MovecadeKart::Art::GlowOpaque(Outer, Color); });
}

UMaterialInstanceDynamic* FKartMaterialCache::Unlit(UObject* Outer, const TCHAR* TexturePath, const FLinearColor& Tint)
{
	const FString Key = (FKeyBuilder(TEXT("unlit")) << FString(TexturePath) << Tint).Key;
	return FindOrMakeDynamic(Key, [&]() { return MovecadeKart::Art::Unlit(Outer, TexturePath, Tint); });
}
