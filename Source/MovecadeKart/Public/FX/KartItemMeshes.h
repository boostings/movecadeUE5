#pragma once

#include "CoreMinimal.h"

class UObject;
class UStaticMesh;

/**
 * Item meshes for the powerups, modelled in code (UE cm, X forward, Z up, origin at the item centre) so
 * they need no import pipeline. Each part is its own material slot, in the order listed per mesh.
 *
 * Sizes follow the web primitives (MultiplayerItems.tsx:234-263): a shell is a 0.3 m sphere, a bomb a
 * 0.4 m sphere, the banana a 0.4 m curved tube. The shapes add the details the web left out: the shell's
 * rim and belly, the banana's tips, the bomb's cap and fuse, the rocket's nose, band and fins. The items the web
 * never showed in the world (nitro, prism, lightning, ink) get chunky, IP-neutral shapes of their own, about
 * 0.6 m tall, for the pop-up over a kart that uses one.
 */
namespace MovecadeKart::Fx
{
	/** Slots: Dome, Rim, Belly. */
	MOVECADEKART_API UStaticMesh* BuildShellMesh(UObject* Outer);
	/** The sky seeker: a shell with two swept wings. Slots: Dome, Rim, Belly, Wings. */
	MOVECADEKART_API UStaticMesh* BuildWingedShellMesh(UObject* Outer);
	/** Slots: Peel, Tips. */
	MOVECADEKART_API UStaticMesh* BuildBananaMesh(UObject* Outer);
	/** Slots: Body, Cap, Fuse. The fuse tip is at FuseTip(). */
	MOVECADEKART_API UStaticMesh* BuildBombMesh(UObject* Outer);
	MOVECADEKART_API FVector BombFuseTip();
	/** About 3 m long, 1.3 m wide, along +X. Slots: Body, Band, Fins, Exhaust. The exhaust is at RocketExhaust(). */
	MOVECADEKART_API UStaticMesh* BuildRocketMesh(UObject* Outer);
	MOVECADEKART_API FVector RocketExhaust();
	/** A cube of edge 1 m with chamfered edges (the item box's inner core). Slot: Core. */
	MOVECADEKART_API UStaticMesh* BuildChamferCubeMesh(UObject* Outer);
	/**
	 * The item box's "?" glyphs: a quad just outside each face of a 1 m cube, UVs upright on the four sides (and
	 * readable from outside on all six). Slot: Label.
	 */
	MOVECADEKART_API UStaticMesh* BuildBoxLabelsMesh(UObject* Outer);
	/** A nitro canister: body, white band and nozzle cap, valve. Slots: Body, Band, Metal. */
	MOVECADEKART_API UStaticMesh* BuildNitroMesh(UObject* Outer);
	/** Three canisters in a fan (the triple nitro). Slots: Body, Band, Metal. */
	MOVECADEKART_API UStaticMesh* BuildTripleNitroMesh(UObject* Outer);
	/** A faceted crystal: an eight-sided bipyramid with a girdle, flat shaded. Slot: Crystal. */
	MOVECADEKART_API UStaticMesh* BuildPrismMesh(UObject* Outer);
	/** A chunky lightning bolt glyph, bevelled, in the XZ plane. Slots: Bolt, Edge. */
	MOVECADEKART_API UStaticMesh* BuildLightningMesh(UObject* Outer);
	/** An ink drop with two big eyes. Slots: Ink, Eyes, Pupils. */
	MOVECADEKART_API UStaticMesh* BuildInkMesh(UObject* Outer);
	/**
	 * A lightning strike from Height cm straight down to the origin: a jagged ribbon of boxes with two forks,
	 * different for every Seed. Slot: Bolt.
	 */
	MOVECADEKART_API UStaticMesh* BuildStrikeMesh(UObject* Outer, int32 Seed, double Height);
}
