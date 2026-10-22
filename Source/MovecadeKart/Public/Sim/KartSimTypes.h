#pragma once

#include "CoreMinimal.h"

/**
 * The plain data the kart sim reads and writes each step: one kart's controls, what the race tells it, and its
 * whole state. All in web units (metres, speed units "SU", seconds, radians, three.js axes); see Sim/KartSim.h.
 */
namespace MovecadeKart
{
	/** One step of controls, from a player, a phone or a bot. */
	struct FKartInput
	{
		/** Analog steering in [-1, 1]. Positive steers LEFT (the web convention). */
		double SteeringX = 0.0;
		/** Digital steering keys, added on top of SteeringX. */
		bool bLeft = false;
		bool bRight = false;
		bool bForward = false;
		/** Brake and reverse share one pedal. */
		bool bBackward = false;
		/** Hop / drift. */
		bool bJump = false;
		/** Item button (held). The race layer turns it into a press edge; the kart sim ignores it. */
		bool bUseItem = false;
	};

	/** A timed top-speed boost earned by a drift release or a rocket start. */
	struct FDriftBoost
	{
		double Multiplier = 1.0;
		/** Seconds. */
		double Time = 0.0;
		/** e.g. "DRIFT_MINI", "DRIFT_CLEAN_SUPER", "DRIFT_APEX_ULTRA". */
		FString Label;
	};

	/** What the race knows that one kart's sim needs (MultiplayerKart.tsx:1112-1153, :1428-1434). */
	struct FKartRaceContext
	{
		/** Another kart, for kart-to-kart contact. */
		struct FOther
		{
			int32 Seat = 0;
			FVector2D Position = FVector2D::ZeroVector;
		};

		int32 PlayerCount = 1;
		/** 1 = leader. */
		int32 RacePosition = 1;
		/** min(1, max(0, leaderScore - myScore) / 140), score = laps * 100 + progress %. */
		double LeaderGapRatio = 0.0;
		bool bRacing = true;
		/** The nearest kart ahead in race order, for slipstream. */
		bool bHasAhead = false;
		FVector2D AheadPosition = FVector2D::ZeroVector;
		int32 SelfSeat = 0;
		TArray<FOther, TInlineAllocator<8>> Others;
		bool bKartContact = true;
	};

	/** One kart's complete sim state. Everything here is deterministic; the presentation only reads it. */
	struct FKartState
	{
		// Pose and motion
		FVector2D Pos = FVector2D::ZeroVector;
		/** three.js rotation.y. Forward is (-sin, -cos). */
		double Yaw = 0.0;
		double Speed = 0.0;
		/** Lagged, unnormalised travel direction (smoothedDirectionRef). */
		FVector2D Dir = FVector2D(0.0, -1.0);

		// Drift and boost
		/** Seconds of mini-turbo left; positive adds the turbo bonus to top speed. */
		double Turbo = 0.0;
		/** +-DriftTurn while drifting (positive = left), else 0. Also adds to the turn. */
		double DriftDirection = 0.0;
		/** Seconds the current drift has been held. */
		double DriftPower = 0.0;
		bool bJumpHeld = false;
		/** Seconds into the 0.25 s hop, or -1 when not hopping. */
		double HopTime = -1.0;
		double BoostMultiplier = 1.0;
		double BoostTimeRemaining = 0.0;
		FString ActiveBoost;

		// Race assists
		double RubberBandBoost = 0.0;
		double SlipstreamBoost = 0.0;
		/** 0..1: how long the kart has sat in a slipstream. */
		double SlipstreamCharge = 0.0;

		// Collision feedback (MultiplayerKart.tsx:1457-1569), for haptics, audio and FX
		bool bCollidingBarrier = false;
		double CollisionIntensity = 0.0;
		/** > 0 on a step that fires a barrier event (first contact, or sustained every 150 ms). */
		double BarrierEvent = 0.0;
		bool bBarrierEventSustained = false;
		double LastBarrierEventTime = -1000.0;
		/** > 0 on a step where this kart bumped another. */
		double BumpEvent = 0.0;
		/** Count up with each barrier and bump event, so presentation running at any frame rate sees each once. */
		uint32 BarrierEventCount = 0;
		uint32 BumpEventCount = 0;
		/** The latest event's normal, pointing away from what was hit (zero if unknown). */
		FVector2D ImpactNormal = FVector2D::ZeroVector;

		// Surfaces and walls
		bool bOnDirt = false;
		bool bWrongWay = false;
		bool bHitWallThisStep = false;
		/** Steps simulated; the throttled checks below run on it. */
		uint64 Frame = 0;
		uint64 LastRadialFrame = 0;
		uint64 LastWrongWayFrame = 0;
		uint64 LastTerrainFrame = 0;
		/** The last radial probe's push, re-applied every step until the next probe (as on the web). */
		FVector2D RadialPush = FVector2D::ZeroVector;
		double RadialStrength = 0.0;
		bool bRadialCritical = false;
		double NearestWall = 999.0;
		FVector2D LastCollisionNormal = FVector2D::ZeroVector;
		/** Per boost pad: sim time it last fired. */
		TArray<double> PadLastBoostTime;

		// Race progress
		/** Sim seconds since Init. */
		double Time = 0.0;
		int32 NextCheckpoint = 1;
		bool bWasInsideNext = false;
		int32 LapsCompleted = 0;
		double ProgressPercent = 0.0;
		bool bFinished = false;
		double FinishTime = 0.0;

		// Recovery watchdogs (recoveryWatchdogs.ts): "Since" starts a timer, "Seen" is when it was last stepped
		double OffTrackSince = -1.0;
		FVector2D AnchorPos = FVector2D::ZeroVector;
		double AnchorSince = 0.0;
		double AnchorSeen = -1000.0;
		int32 WatchIndex = -1;
		double WatchSince = 0.0;
		double WatchSeen = -1000.0;
		/** Increments on every teleport, so the presentation can snap instead of interpolate. */
		uint32 TeleportCount = 0;
		/** Why the last teleport happened: "offtrack", "stuck", "checkpoint-stall" or "teleport". */
		FString LastRespawnReason;

		// Presentation (Kart.jsx visual layers), driven from the sim so they stay deterministic
		double InputTurn = 0.0;
		double SlipYaw = 0.0;
		double SteerVisual = 0.0;
		double BodyYaw = 0.0;
		double GroupYaw = 0.0;
		double Lean = 0.0;
		double HopOffset = 0.0;
		FString LastDriftBoost;
		uint32 DriftBoostCount = 0;
	};
}
