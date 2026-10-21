#pragma once

#include "CoreMinimal.h"

class UObject;
class UStaticMesh;

/**
 * The track life meshes (ATrackLife), modelled in code like the item meshes: UE cm, X forward, Z up. Their
 * materials (Content/Tracks/Shared/Life, Tools/ue/make_track_life.py) read two UV channels:
 * - grass and flowers: UV0 = (across the blade, height 0 at the root .. 1 at the tip), UV1.x = the part
 *   (0 blade or stem, 1 petal, 2 flower heart);
 * - spectators: UV1.x = the part (0 shirt, 1 skin, 2 trousers, 3 hair, 4 shoes), UV1.y = the arm (-1 left,
 *   +1 right, 0 the rest), which the crowd material swings about the shoulders;
 * - butterflies: UV0 across the wing, UV1.y = the wing (-1, +1; 0 the body).
 */
namespace MovecadeKart::Life
{
	/** A lawn tuft: Blades curved, tapered blades leaning out from the origin, about Height cm tall. One slot. */
	MOVECADEKART_API UStaticMesh* BuildGrassTuftMesh(UObject* Outer, int32 Seed, int32 Blades, double Radius, double Height);
	/** A little lawn flower (a daisy or clover head): a stem, Petals round a heart, about 22 cm tall. One slot. */
	MOVECADEKART_API UStaticMesh* BuildFlowerMesh(UObject* Outer, int32 Petals);
	/** A cheerful spectator facing +X, feet at the origin, about 165 cm tall. One slot. */
	MOVECADEKART_API UStaticMesh* BuildSpectatorMesh(UObject* Outer);
	/** A butterfly facing +X, about 14 cm across the wings. One slot. */
	MOVECADEKART_API UStaticMesh* BuildButterflyMesh(UObject* Outer);

	/** The grandstand (the web's tracksideGeometry.ts grandstandGeometry) without its block crowd. */
	struct FGrandstandSpec
	{
		double Width = 30.0;
		int32 Tiers = 6;
		double TierDepth = 1.5;
		double TierRise = 0.9;
	};

	/**
	 * The stand in the imported prefab's mesh axes (three.js (x, y, z) metres -> (x, z, y) cm), so it drops onto
	 * the prefab's instance transforms. Slots: Concrete, Seats, Roof, Trim.
	 */
	MOVECADEKART_API UStaticMesh* BuildGrandstandMesh(UObject* Outer, const FGrandstandSpec& Spec = FGrandstandSpec());

	/**
	 * Where the spectators stand on that stand, in the same mesh axes (cm): one per seat place the web filled
	 * (its seeded generator skipped about a fifth of them), with a yaw (degrees) that faces them to the road.
	 */
	struct FSpectatorSpot
	{
		FVector Position = FVector::ZeroVector;
		double Yaw = 0.0;
		int32 Tier = 0;
	};
	MOVECADEKART_API TArray<FSpectatorSpot> GrandstandSpectators(const FGrandstandSpec& Spec = FGrandstandSpec());
}
