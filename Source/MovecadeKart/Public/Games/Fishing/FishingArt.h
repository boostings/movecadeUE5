#pragma once

#include "CoreMinimal.h"

struct FKartMaterialCache;
class UMaterialInterface;
class UObject;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Fishing Frenzy's props built in code (FMeshBuilder): every species of fish (a body of rings banded into back,
 * flank, belly and pattern sections, so perch get bars, trout their pink stripe, pike their spots and carp their
 * scales, plus tail, dorsal and pectoral fins and eyes), the junk (a boot, a bike tyre, a rubber duck), the rod
 * (cork handle and reel, then a tapered blank in segments so it can bend), the line, the bobber, and the lake's
 * scenery shapes (pines, reeds, lily pads, the rowboat, the bucket).
 *
 * Meshes are UE cm, +X forward (a fish's head, a rod's tip), slot order as noted per builder.
 */
namespace MovecadeKart::Fishing::Art
{
	struct FFishLook
	{
		/** Back, flank, belly, pattern, fin, (metallic 0..1 for the trophy). */
		const TCHAR* Back;
		const TCHAR* Flank;
		const TCHAR* Belly;
		const TCHAR* Pattern;
		const TCHAR* Fin;
		double Metallic;
		/** Grams = Density x length(cm)^3: a slim fish is longer for its weight. */
		double Density;
	};

	/** The look for a species (by its index in Fishing::Species()). */
	MOVECADEKART_API const FFishLook& LookOf(int32 Species);
	/** A mesh 100 cm long (nose at +50 X) for the species: slots Back, Flank, Belly, Pattern, Fin, Eye. */
	MOVECADEKART_API UStaticMesh* BuildFish(UObject* Outer, int32 Species);
	/** Dresses a fish mesh component with the species' materials (made once per look in Materials, on Outer). */
	MOVECADEKART_API void DressFish(FKartMaterialCache& Materials, UObject* Outer, UStaticMeshComponent* Component, int32 Species);
	/** Length in cm of a catch. */
	MOVECADEKART_API double LengthCm(int32 Species, int32 WeightG);

	/** The rod's handle, butt at the origin pointing +X, 42 cm: slots Cork, Reel, Metal. */
	MOVECADEKART_API UStaticMesh* BuildRodHandle(UObject* Outer);
	/** A unit taper from radius 1 to 0.82 over 100 cm along +X, with a guide ring at the far end: slots Blank, Guide. */
	MOVECADEKART_API UStaticMesh* BuildRodSegment(UObject* Outer);
	/** A unit cylinder along +X, 100 cm long, radius 1. */
	MOVECADEKART_API UStaticMesh* BuildRodLine(UObject* Outer);
	/** The bobber, its waterline at the origin: slots Top (the lane colour), Bottom (white), Stick. */
	MOVECADEKART_API UStaticMesh* BuildBobber(UObject* Outer);

	MOVECADEKART_API UStaticMesh* BuildPine(UObject* Outer);
	MOVECADEKART_API UStaticMesh* BuildReed(UObject* Outer);
	MOVECADEKART_API UStaticMesh* BuildLilyPad(UObject* Outer);
	/** Slots Hull, Trim, Seat. */
	MOVECADEKART_API UStaticMesh* BuildBoat(UObject* Outer);
	/** Slots Pail, Band, Water. */
	MOVECADEKART_API UStaticMesh* BuildBucket(UObject* Outer);
	/** The lake bed and its shores as one grid (height in cm from HeightAt): slots Bed, Bank. */
	MOVECADEKART_API UStaticMesh* BuildTerrain(UObject* Outer, const FVector2D& Min, const FVector2D& Max, int32 CellsX, int32 CellsY, TFunctionRef<double(double, double)> HeightAt);
}
