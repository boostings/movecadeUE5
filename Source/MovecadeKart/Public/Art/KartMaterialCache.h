#pragma once

#include "CoreMinimal.h"
#include "Art/KartMaterials.h"
#include "KartMaterialCache.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * One dynamic material instance per look for a venue: the same arguments return the same instance, so a venue that
 * asks for "#ffd89a glow" in ten places makes one material, and instanced groups keyed by material merge. Keys use
 * the exact bits of every argument (no rounding), so two looks only share an instance when they are identical.
 *
 * The owning actor holds it as a UPROPERTY (that keeps the materials alive) and calls Reset() when it rebuilds.
 * Instances are shared: don't change their parameters per use (make a separate Art:: material for that).
 */
USTRUCT()
struct MOVECADEKART_API FKartMaterialCache
{
	GENERATED_BODY()

	// The MovecadeKart::Art looks, cached. Outer owns any instance made by the call.
	UMaterialInstanceDynamic* Plain(UObject* Outer, const FLinearColor& Color, double Roughness = 0.6, double Metallic = 0.0, double Emissive = 0.0);
	UMaterialInstanceDynamic* Surface(UObject* Outer, const MovecadeKart::Art::FSurfaceLook& Look);
	UMaterialInstanceDynamic* Glass(UObject* Outer, const FLinearColor& Color, double Opacity = 0.2, double Roughness = 0.05);
	UMaterialInstanceDynamic* Glow(UObject* Outer, const FLinearColor& Color);
	UMaterialInstanceDynamic* GlowOpaque(UObject* Outer, const FLinearColor& Color);
	UMaterialInstanceDynamic* Unlit(UObject* Outer, const TCHAR* TexturePath, const FLinearColor& Tint = FLinearColor::White);

	/** Any other material under the caller's own key: Make runs only the first time (a null result isn't kept). */
	UMaterialInterface* FindOrMake(const FString& Key, TFunctionRef<UMaterialInterface*()> Make);

	int32 Num() const { return Materials.Num(); }
	void Reset() { Materials.Reset(); }

private:
	UMaterialInstanceDynamic* FindOrMakeDynamic(const FString& Key, TFunctionRef<UMaterialInstanceDynamic*()> Make);

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInterface>> Materials;
};
