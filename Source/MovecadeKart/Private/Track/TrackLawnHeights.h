#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MovecadeKart::LifeShared
{
	/**
	 * Data/Tracks/<id>.lawn.json (Tools/ue/bake_lawn_heights.py): the lawn's top surface, UE cm over three.js x, z, which
	 * ATrackLife's grass stands on. Parse rejects a file whose header doesn't describe a real raster (a cell size
	 * outside (0, 10] m, a corner off the map, more than MaxCells cells, or a height blob of the wrong size).
	 */
	struct FLawnHeights
	{
		static constexpr int16 NoData = -32768;
		static constexpr int32 MaxSide = 1 << 15;
		static constexpr int64 MaxCells = 1 << 26;

		double Cell = 0.0;
		FVector2D Min = FVector2D::ZeroVector;
		int32 Cols = 0;
		int32 Rows = 0;
		TArray<int16> Heights;

		/** Reads Data/Tracks/<TrackId>.lawn.json; false (and empty) when it is missing or invalid. */
		bool Load(const FString& TrackId);
		bool Parse(const TSharedPtr<FJsonObject>& Root);

		/** Bilinear height under a three.js (x, z) point, over the cells that have lawn; false off the lawn. */
		bool Sample(const FVector2D& P, double& OutCm) const;
	};
}
