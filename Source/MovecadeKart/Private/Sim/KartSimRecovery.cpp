#include "Sim/KartSim.h"

#include "Sim/KartSimTuning.h"

// Getting a lost kart back on route: the off-track timer, the stuck and checkpoint-stall watchdogs, and the
// recovery pose they respawn to (legacy trackRecovery.ts, recoveryWatchdogs.ts).

namespace MovecadeKart
{
	using namespace KartTuning;

	const FCheckpoint* FKartSim::NextCheckpointOrNull() const
	{
		return TrackData->Checkpoints.IsValidIndex(Kart.NextCheckpoint) ? &TrackData->Checkpoints[Kart.NextCheckpoint] : nullptr;
	}

	void FKartSim::Respawn(const FVector2D& Position, double Yaw, const TCHAR* Reason)
	{
		Kart.Pos = Position;
		Kart.Yaw = Yaw;
		Kart.Speed = 0.0;
		Kart.DriftDirection = 0.0;
		Kart.DriftPower = 0.0;
		++Kart.TeleportCount;
		Kart.LastRespawnReason = Reason;
	}

	void FKartSim::UpdateOffTrack()
	{
		// Off-track: two seconds to steer back, then return to the next checkpoint's approach (:862-880).
		FVector2D RecoverPos;
		double RecoverYaw = 0.0;
		if (!RecoveryPose(Kart.Pos, NextCheckpointOrNull(), RecoverPos, RecoverYaw))
		{
			Kart.OffTrackSince = -1.0;
		}
		else if (Kart.OffTrackSince < 0.0)
		{
			Kart.OffTrackSince = Kart.Time;
		}
		else if (Kart.Time - Kart.OffTrackSince >= OffTrackRespawnSeconds)
		{
			Respawn(RecoverPos, RecoverYaw, TEXT("offtrack"));
			Kart.OffTrackSince = -1.0;
		}
	}

	bool FKartSim::RecoveryPose(const FVector2D& Position, const FCheckpoint* Next, FVector2D& OutPos, double& OutYaw) const
	{
		const int32 WaypointCount = TrackData->Waypoints.Num();
		if (WaypointCount < 2)
		{
			return false;
		}
		const int32 NearestIndex = TrackData->NearestWaypoint(Position);
		const double Distance = FVector2D::Distance(Position, TrackData->Waypoints[NearestIndex]);

		// On a figure-eight a kart can leave one branch and land on a later one (trackRecovery.ts:68-76).
		bool bSkipped = false;
		if (Next)
		{
			const int32 CheckpointWaypoint = Next->Waypoint != INDEX_NONE ? Next->Waypoint : TrackData->NearestWaypoint(Next->Position);
			const int32 Past = (NearestIndex - CheckpointWaypoint + WaypointCount) % WaypointCount;
			bSkipped = Past >= TrackData->RecoverySkipWaypoints && Past < WaypointCount / 2.0;
		}
		if (Distance <= TrackData->RecoveryDistance && !bSkipped)
		{
			return false;
		}
		if (Next)
		{
			OutPos = Next->ApproachPosition;
			OutYaw = Next->ApproachYaw;
		}
		else
		{
			const FVector2D Nearest = TrackData->Waypoints[NearestIndex];
			const FVector2D After = TrackData->Waypoints[(NearestIndex + 1) % WaypointCount];
			OutPos = Nearest;
			OutYaw = FMath::Atan2(Nearest.X - After.X, Nearest.Y - After.Y);
		}
		return true;
	}

	void FKartSim::UpdateWatchdogs(bool bPowered)
	{
		const FCheckpoint* Next = NextCheckpointOrNull();

		// Stuck anchor (recoveryWatchdogs.ts stepStuckAnchor): on the gas but within 4 m for 2.5 s.
		const bool bAnchorResumed = Kart.Time - Kart.AnchorSeen > WatchGapSeconds;
		if (bAnchorResumed || FVector2D::Distance(Kart.Pos, Kart.AnchorPos) >= StuckAnchorDistance || !bPowered)
		{
			Kart.AnchorPos = Kart.Pos;
			Kart.AnchorSince = Kart.AnchorSeen = Kart.Time;
		}
		else
		{
			Kart.AnchorSeen = Kart.Time;
			if (Kart.Time - Kart.AnchorSince >= StuckAnchorSeconds)
			{
				const FSpawnSlot& Spawn = TrackData->Grid[GridIndex];
				Respawn(Next ? Next->ApproachPosition : Spawn.Position, Next ? Next->ApproachYaw : Spawn.Yaw, TEXT("stuck"));
				Kart.AnchorPos = Kart.Pos;
				Kart.AnchorSince = Kart.AnchorSeen = Kart.Time;
			}
		}

		// Checkpoint stall (recoveryWatchdogs.ts stepCheckpointWatch): on the gas without reaching the next one.
		const bool bWatchResumed = Kart.Time - Kart.WatchSeen > WatchGapSeconds;
		if (bWatchResumed || Kart.WatchIndex != Kart.NextCheckpoint || !bPowered)
		{
			Kart.WatchIndex = Kart.NextCheckpoint;
			Kart.WatchSince = Kart.WatchSeen = Kart.Time;
		}
		else if (Kart.Time - Kart.WatchSince >= CheckpointStallSeconds && Next)
		{
			Kart.WatchSince = Kart.WatchSeen = Kart.Time;
			Respawn(Next->ApproachPosition, Next->ApproachYaw, TEXT("checkpoint-stall"));
			Kart.AnchorPos = Kart.Pos;
			Kart.AnchorSince = Kart.AnchorSeen = Kart.Time;
		}
		else
		{
			Kart.WatchSeen = Kart.Time;
		}
	}
}
