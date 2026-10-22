#pragma once

#include "CoreMinimal.h"

/**
 * What the pieces of AStackerGame (StackerGame*.cpp) share about the scene: the web-unit layout of a lane, the
 * instance batches a lane's blocks are drawn in, and a few easing and transform helpers. Lengths are web units (1 =
 * one metre, x across the lanes, y up, z toward the camera); AStackerGame::W turns them into UE centimetres.
 */
namespace MovecadeKart::Stacker::Scene
{
	/** StackerScene.tsx: a row is half a unit tall; the showcase compresses each tower to .15 a row. */
	constexpr double RowHeight = 0.5;
	constexpr double CompareRowHeight = 0.15;
	/** The block at width 1: a rounded box 1 across, .43 tall, 1.5 deep (web x, y, z), corner radius .11. */
	const FVector BlockSize(1.0, 0.43, 1.5);
	/** How long the showcase takes to compress the towers (and then dims the losers). */
	constexpr double CompressSeconds = 1.2;

	/** The builder's lift stands beside its lane, on the deck, a little toward the camera. */
	constexpr double FloorY = -0.7;
	constexpr double BuilderZ = 2.2;
	constexpr double BuilderSide = 2.6;
	/** The builder faces the tower at this yaw (degrees). */
	constexpr double BuilderYawDeg = 35.0;
	/** The sweeping block rides this many rows above the top block. */
	constexpr double CursorRowsAbove = 1.35;

	/**
	 * Per lane, one instance batch per block look and one for the lane's shards, all of the block mesh: a lane draws
	 * in BatchesPerLane draws however tall it grows. A block's slot in its batch is its row; a shard's is its id.
	 */
	enum class EBlockBatch : int32
	{
		Even,
		Odd,
		/** A perfect landing: lighter than the lane's paint. */
		Perfect,
		/** The showcase's losers. */
		Dimmed,
		/** The newest perfect block's flash as it lands. */
		Flashing,
		Shards,
	};
	constexpr int32 BlockLooks = static_cast<int32>(EBlockBatch::Shards);
	constexpr int32 BatchesPerLane = BlockLooks + 1;

	constexpr int32 BatchIndex(int32 Lane, EBlockBatch Batch)
	{
		return Lane * BatchesPerLane + static_cast<int32>(Batch);
	}

	/** GLSL smoothstep. */
	inline double Smooth(double Edge0, double Edge1, double X)
	{
		const double T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Exponential approach factor for a rate per second over Dt: frame-rate independent smoothing. */
	inline double Approach(double RatePerSecond, double Dt)
	{
		return 1.0 - FMath::Exp(-RatePerSecond * Dt);
	}

	/** Where an unused instance slot waits: shrunk to nothing under the deck (near, so the batch bounds stay tight). */
	inline const FTransform& Parked()
	{
		static const FTransform Spot(FQuat::Identity, FVector(0.0, 0.0, -150.0), FVector(0.001));
		return Spot;
	}

	inline bool IsParked(const FTransform& T)
	{
		return T.GetScale3D().X < 0.002;
	}

	/** A roll about the depth axis that tips the top toward +web x by Degrees. */
	inline FQuat Tip(double Degrees)
	{
		return FQuat(FVector::ForwardVector, -FMath::DegreesToRadians(Degrees));
	}
}
