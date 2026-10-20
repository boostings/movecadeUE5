#pragma once

#include "CoreMinimal.h"

/**
 * Track data for the kart sim, loaded from Data/Tracks/<id>.track.json.
 *
 * Everything here is in web units: metres on the three.js ground plane (x, z), radians, and
 * three.js yaw (rotation.y; a kart at yaw 0 faces -Z). Lateral offsets are positive on the
 * driver's right. MovecadeKart::Coords converts to UE space (Docs/Port/03 §1).
 */
namespace MovecadeKart
{
	struct FWallSegment
	{
		FVector2D A = FVector2D::ZeroVector;
		/** B - A. */
		FVector2D D = FVector2D::ZeroVector;
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
		void Query(const FVector2D& Min, const FVector2D& Max, TArray<int32>& Out) const;
		/** First wall hit along a horizontal ray, within Far. Dir must be unit length. */
		bool Raycast(const FVector2D& Origin, const FVector2D& Dir, double Far, double& OutDistance, FVector2D& OutNormal) const;
		const TArray<FWallSegment>& Segments() const { return Segments_; }

	private:
		FIntPoint CellOf(const FVector2D& P) const;

		TArray<FWallSegment> Segments_;
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
		int32 NearestWaypoint(const FVector2D& P) const;

		static bool LoadFromJsonFile(const FString& Path, FTrack& Out, FString& OutError);
		static bool LoadFromJsonString(const FString& Json, FTrack& Out, FString& OutError);
	};
}
