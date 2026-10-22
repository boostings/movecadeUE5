#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UObject;

/**
 * Materials for scenery modelled in code (the game venues), from the same track masters ATrackBuilder instances
 * (Content/Tracks/Shared/Materials, Tools/ue/import_track_assets.py): a tinted surface over one of the CC0 detail
 * sets, a plain lit colour, glass, and unlit glow (translucent or opaque). Each call makes a new dynamic instance;
 * venues that reuse looks keep them in an FKartMaterialCache (Art/KartMaterialCache.h).
 */
namespace MovecadeKart::Art
{
	struct FSurfaceLook
	{
		FLinearColor Color = FLinearColor::White;
		/** A detail set in /Game/Tracks/Shared/Surfaces (asphalt_track, painted_concrete, leafy_grass, gravelly_sand,
		 *  rock_face, dark_wooden_planks, plastered_wall_02); none gives a plain lit colour. */
		FName Detail = NAME_None;
		/** World-space metres per detail tile. */
		double TileM = 2.0;
		double Albedo = 0.35;
		double Normal = 0.6;
		double Rough = 0.45;
		double Roughness = 0.8;
		double Metallic = 0.0;
		double Emissive = 0.0;
		/** Project the detail on the dominant axis (walls, props) rather than top-down (floors). */
		bool bTriplanar = true;
		/** Desaturate the detail before it modulates the colour (keeps a tint clean over a busy set). */
		double Desaturate = 0.0;
	};

	MOVECADEKART_API UMaterialInstanceDynamic* Surface(UObject* Outer, const FSurfaceLook& Look);
	MOVECADEKART_API UMaterialInstanceDynamic* Plain(UObject* Outer, const FLinearColor& Color, double Roughness = 0.6, double Metallic = 0.0, double Emissive = 0.0);
	MOVECADEKART_API UMaterialInstanceDynamic* Glass(UObject* Outer, const FLinearColor& Color, double Opacity = 0.2, double Roughness = 0.05);
	/**
	 * Unlit colour on the translucent master at opacity 1, for emissive strips, signs and lenses (Color can exceed 1
	 * to bloom). It draws in the translucency pass: prefer GlowOpaque for solid glowing surfaces.
	 */
	MOVECADEKART_API UMaterialInstanceDynamic* Glow(UObject* Outer, const FLinearColor& Color);
	/**
	 * Unlit colour on the opaque unlit master (M_TrackUnlitTexture over its default white map): the same colour as
	 * Glow, but drawn opaque, so no translucency pass or overdraw, correct depth sorting, and it shows in ray-traced
	 * reflections. For solid lamps, neon and lenses that never fade. Falls back to Glow if the master is missing.
	 */
	MOVECADEKART_API UMaterialInstanceDynamic* GlowOpaque(UObject* Outer, const FLinearColor& Color);

	/** An unlit texture (a backlit sign or panel), tinted (Tint can exceed 1 to bloom). */
	MOVECADEKART_API UMaterialInstanceDynamic* Unlit(UObject* Outer, const TCHAR* TexturePath, const FLinearColor& Tint = FLinearColor::White);

	/** sRGB hex ("#d8a46a" or "d8a46a") to linear. */
	MOVECADEKART_API FLinearColor Hex(const FString& Code);
}
