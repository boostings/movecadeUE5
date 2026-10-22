#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * Track data for the kart sim, loaded from Data/Tracks/<id>.track.json.
 *
 * Everything here is in web units: metres on the three.js ground plane (x, z), radians, and
 * three.js yaw (rotation.y; a kart at yaw 0 faces -Z). Lateral offsets are positive on the
 * driver's right. MovecadeKart::Coords converts to UE space (Docs/Port/03 §1).
 */
namespace MovecadeKart
{
	/** One wall segment from Start to Start + Delta, with its length precomputed for the hot-path tests. */
	struct FWallSegment
	{
		FVector2D Start = FVector2D::ZeroVector;
		/** End - Start. */
		FVector2D Delta = FVector2D::ZeroVector;
		double LengthSq = 0.0;
		/** The face normal a three.js raycast reports. Harbor's barrier winding faces away from the road. */
		FVector2D FaceNormal = FVector2D::ZeroVector;
	};

	/** 2D wall segments, bucketed in a grid. Queries return segment indices in source order, so
	 *  sequential push-outs resolve exactly as the web build's full loop over every segment. */
	class MOVECADEKART_API FWallSet
	{
	public:
		void Reset(TArray<FWallSegment>&& InSegments, double InCellSize = 8.0);
		/** Segment indices near a box, sorted and unique. Inline storage sized for Violet's densest wall queries
		 *  (a sweep box there gathers up to ~400 raw indices), so the sim's hot path never touches the heap. */
		using FCandidates = TArray<int32, TInlineAllocator<512>>;
		void Query(const FVector2D& Min, const FVector2D& Max, FCandidates& Out) const;
		/** First wall hit along a horizontal ray, within Far; on an exact tie the lowest segment index wins.
		 *  Dir must be unit length. */
		[[nodiscard]] bool Raycast(const FVector2D& Origin, const FVector2D& Dir, double Far, double& OutDistance, FVector2D& OutNormal) const;
		const TArray<FWallSegment>& Segments() const { return WallSegments; }

	private:
		FIntPoint CellOf(const FVector2D& P) const;
		/** The cells covering a box, or false (with an ensure) for a box too far out to walk. */
		bool CellRange(const FVector2D& Min, const FVector2D& Max, FIntPoint& OutC0, FIntPoint& OutC1) const;

		TArray<FWallSegment> WallSegments;
		TMap<FIntPoint, TArray<int32>> Grid;
		double CellSize = 8.0;
	};

	/** Where the first floor under the kart is a dirt mesh (Violet: Object_11 "ground dirt"), rasterised offline. */
	struct MOVECADEKART_API FDirtGrid
	{
		double CellSize = 0.0;
		FVector2D Min = FVector2D::ZeroVector;
		int32 Cols = 0;
		int32 Rows = 0;
		TBitArray<> Cells;

		bool IsValid() const { return Cols > 0 && Rows > 0 && CellSize > 0.0; }
		bool IsDirt(const FVector2D& P) const;
	};

	struct FBoostPad
	{
		FVector2D Position = FVector2D::ZeroVector;
		double Yaw = 0.0;
		double Width = 0.0;
		double Length = 0.0;
		/** cos(Yaw) and sin(Yaw), filled by the loader so the per-step pad test does no trig. */
		double CosYaw = 1.0;
		double SinYaw = 0.0;
	};

	struct FCheckpoint
	{
		FString Id;
		FVector2D Position = FVector2D::ZeroVector;
		double Radius = 24.0;
		bool bFinish = false;
		/** The respawn pose for this checkpoint (legacy trackRecovery.ts getCheckpointApproachPose). */
		FVector2D ApproachPosition = FVector2D::ZeroVector;
		double ApproachYaw = 0.0;
		/** NearestWaypoint(Position), filled by the loader; INDEX_NONE until then. */
		int32 Waypoint = INDEX_NONE;
	};

	/** One closed-centreline segment, precomputed from Centre with the exact operations NearestCentre used inline. */
	struct FCentreSegment
	{
		FVector2D Start = FVector2D::ZeroVector;
		/** Centre[i + 1] - Start (wrapping). */
		FVector2D Delta = FVector2D::ZeroVector;
		double LengthSq = 0.0;
	};

	struct FSpawnSlot
	{
		FVector2D Position = FVector2D::ZeroVector;
		double Yaw = 0.0;
	};

	/** An intro/static camera shot, in three.js coordinates (metres, Y up). */
	struct FStaticCamera
	{
		FString Name;
		FVector Position = FVector::ZeroVector;
		FVector LookAt = FVector::ZeroVector;
	};

	/** A lateral band of collidable ground (a Harbor ribbon). A down ray hits the highest band first. */
	struct FGroundBand
	{
		double Inner = 0.0;
		double Outer = 0.0;
		double Y = 0.0;
		bool bDirt = false;
	};

	/**
	 * One track: route, walls, surfaces and presentation data. Loaded once (KartTrackJson.cpp), then shared
	 * read-only (TSharedPtr<const FTrack>) by every kart sim, the item world, the bots and the presentation.
	 * The defaults below are the loader's fallbacks for fields a file leaves out.
	 */
	struct MOVECADEKART_API FTrack
	{
		FString Id;
		FString Name;
		int32 Laps = 3;
		double GroundY = 0.0;
		double SuggestedRebaseY = 0.0;
		FVector SunDirection = FVector(0.5, 0.6, 0.6);

		/** Dense closed centreline (Harbor: 870 samples, Violet: 490). */
		TArray<FVector2D> Centre;
		/** Centre as segments, built once at load (RebuildCentreSegments); queries fall back to Centre if stale. */
		TArray<FCentreSegment> CentreSegments;
		TArray<FVector2D> CentreTangent;
		/** Racing-line waypoints (TRACK_PATH_POINTS) for recovery, wrong way and bots. */
		TArray<FVector2D> Waypoints;
		TArray<FCheckpoint> Checkpoints;
		int32 StartCheckpoint = 1;
		TArray<FSpawnSlot> Grid;
		TArray<FStaticCamera> StaticCameras;
		/** Item box centres (three.js x, y, z). */
		TArray<FVector> ItemBoxes;
		/** Minimap bounds in metres: X = minX..maxX, Y = minZ..maxZ. */
		FBox2D MinimapBounds = FBox2D(ForceInit);

		TArray<FBoostPad> BoostPads;
		double BoostMultiplier = 1.8;
		double BoostDuration = 1.5;
		double BoostCooldown = 2.0;
		double BoostPadding = 1.5;

		TArray<FGroundBand> GroundBands;
		FDirtGrid DirtGrid;
		/** The swept disc constraint's walls. */
		FWallSet Walls;
		/** The horizontal probe rays' walls when they differ from the sweep's (Violet: the GLB faces at the
		 *  probe height, y + 0.5). Empty means the probes use Walls (Harbor). */
		FWallSet ProbeWalls;
		const FWallSet& RayWalls() const { return ProbeWalls.Segments().Num() > 0 ? ProbeWalls : Walls; }
		/** Mesh walls (meshBoundary.ts) push the disc away from the closest point; Harbor's analytic
		 *  sweep (harborBoundary.ts) pushes along the segment normal. */
		bool bRadialWallPush = false;
		/** Harbor: the walls are offsets of the centreline, so the sweep also pulls back a start outside them. */
		bool bAnalyticWalls = false;
		double WallOffset = 16.3;
		double KartRadius = 1.15;
		double SweepSubstep = 0.65;
		double RecoveryDistance = 64.0;
		int32 RecoverySkipWaypoints = 3;

		/** Closest point on the closed centreline. Returns the distance; OutOffset is signed (+ = right). */
		double NearestCentre(const FVector2D& P, FVector2D& OutPoint, double& OutOffset) const;
		/** Exactly NearestCentre(P, ...) <= Limit, but stops at the first segment within Limit. Segments are tried
		 *  outward from InOutHint (any value is valid), which is left on the segment found. */
		[[nodiscard]] bool IsNearCentre(const FVector2D& P, double Limit, int32& InOutHint) const;
		[[nodiscard]] int32 NearestWaypoint(const FVector2D& P) const;
		/** Recomputes CentreSegments from Centre. */
		void RebuildCentreSegments();

		static bool LoadFromJsonFile(const FString& Path, FTrack& Out, FString& OutError);
		static bool LoadFromJsonString(const FString& Json, FTrack& Out, FString& OutError);
		/** For callers that also read the rest of the document: parse once, then load the sim track from the DOM. */
		static bool LoadFromJsonObject(const TSharedPtr<FJsonObject>& Root, FTrack& Out, FString& OutError);
	};
}
