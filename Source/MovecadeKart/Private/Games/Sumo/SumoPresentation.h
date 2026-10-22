#pragma once

#include "CoreMinimal.h"
#include "Games/Sumo/SumoRules.h"
#include "UObject/UObjectGlobals.h"

class UMaterialInstanceDynamic;
struct FKartMaterialCache;
class UMaterialInterface;
class UObject;

namespace MovecadeKart { struct FMeshBuilder; }

DECLARE_LOG_CATEGORY_EXTERN(LogSumo, Log, All);

/**
 * What Sumo Bumpers' presentation shares across ASumoGame's files, the arena and the HUD: the timings both the game
 * and the HUD follow, the puck's modelled size, the power-ups' colours, and the additive material the halos, bubbles
 * and warning washes are drawn with. Rules numbers live in SumoRules.h; these never touch the simulation.
 */
namespace MovecadeKart::Sumo::Presentation
{
	/** The intro card and the camera's sweep in from the sea (the rules hold meanwhile). */
	constexpr double IntroSeconds = 3.6;
	/** The champion's showcase holds this long before the room hears the result. */
	constexpr double ShowcaseSeconds = 6.0;
	/** A phone with no sensor frame for this long is quiet: its puck coasts and its panel asks for it. */
	constexpr double PhoneQuietSeconds = 1.0;

	/** The puck is modelled this many cm across the bumper and scaled to the rules' radius. */
	constexpr double PuckMeshRadius = 85.0;
	constexpr double PuckScale = PuckRadius * 100.0 / PuckMeshRadius;
	/** The deck the rider stands on, cm above the ring (in the puck mesh's units). */
	constexpr double DeckZ = 47.0;

	/** A rules-plane point (m) in UE cm, at height Z (cm). */
	inline FVector Cm(const FVector2D& Point, double Z = 0.0)
	{
		return FVector(Point.X * 100.0, Point.Y * 100.0, Z);
	}

	/** Hermite smoothstep of X between edges A and B (B < A runs it backwards). */
	inline double SmoothStep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Heavy gold, shield cyan, turbo pink (white for none). */
	FLinearColor PowerColor(EPower Power);
	/** The callout over a puck that took it: "HEAVY!", "SHIELD!", "TURBO!". */
	const TCHAR* PowerCallout(EPower Power);

	/** A plain material look as data: an sRGB hex colour and FKartMaterialCache::Plain's roughness, metallic, emissive. */
	struct FPlainLook
	{
		const TCHAR* Color;
		double Roughness;
		double Metallic = 0.0;
		double Emissive = 0.0;
	};

	/** The cached material for Look (one instance per look and cache). */
	UMaterialInstanceDynamic* CachedPlain(FKartMaterialCache& Cache, UObject* Outer, const FPlainLook& Look);

	/** Logs that an asset is missing and what falls back without it (a content problem, not a crash). */
	void WarnMissing(const TCHAR* Path, const TCHAR* Fallback);

	/** Loads an asset quietly; a miss logs one warning saying what falls back. Callers handle null. */
	template <class T>
	T* LoadAsset(const TCHAR* Path, const TCHAR* Fallback)
	{
		T* Asset = LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset)
		{
			WarnMissing(Path, Fallback);
		}
		return Asset;
	}

	/** The shared additive master (halos, bubbles, the warning wash), or null if the content is missing (warned once). */
	UMaterialInterface* AdditiveMaster();
	/** An additive instance of Color at Opacity, owned by Outer; null without the master. */
	UMaterialInstanceDynamic* MakeAdditive(UObject* Outer, const FLinearColor& Color, float Opacity);

	/**
	 * A flat ring facing up at height Z, as Steps quads into Section; UVs run 0..1 across and round. Radii and Z are in
	 * units of UnitCm (1 for cm, 100 for the rules' metres).
	 */
	void AddAnnulus(FMeshBuilder& Builder, int32 Section, double Inner, double Outer, double Z, int32 Steps, double UnitCm = 1.0);
}
