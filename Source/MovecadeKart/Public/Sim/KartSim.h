#pragma once

#include "CoreMinimal.h"
#include "Sim/KartTrack.h"

/**
 * The kart simulation, ported line for line from the shipped web build
 * (legacy/Experience/kart/engine/MultiplayerKart.tsx; spec Docs/Port/01).
 *
 * It runs in web units (metres, speed units "SU", radians, three.js axes) at a fixed 60 Hz, which
 * is the 60 fps "ultra" reference feel. Constants that the web applies per frame are therefore
 * applied per step, unchanged.
 *
 * Deliberate differences from the web build, from the plan's bug table (Docs/Port/00-PLAN.md):
 * - B1: boost multipliers below 1 scale top speed (the web ignored them, so slowdowns did nothing).
 * - B2: kart-to-kart contact runs (the web never filled its list of other karts).
 * - B3: the shared boostPower fallback after a failed drift release is not ported.
 * - B4: slipstream thresholds and boosts are scaled by SlipstreamScale (0.4) to the 40 SU speed range.
 * - B5: fixed 60 Hz step instead of frame-rate-dependent handling.
 */
namespace MovecadeKart
{
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

	struct FDriftBoost
	{
		double Multiplier = 1.0;
		double Time = 0.0;
		/** e.g. "DRIFT_MINI", "DRIFT_CLEAN_SUPER", "DRIFT_APEX_ULTRA". */
		FString Label;
	};

	/** legacy performanceQuality.ts getRaceDriftExitBoostFeedback. DriftPower is seconds held. */
	MOVECADEKART_API bool DriftExitBoost(double DriftPower, double DriftDurationMs, double SteeringInput, double DriftDirection, FDriftBoost& Out);

	/** legacy performanceQuality.ts rocket start: how long the gas was held when the count hit GO. */
	MOVECADEKART_API bool RocketStartBoost(double HeldMs, FDriftBoost& Out);

	/** What the race knows that one kart's sim needs (MultiplayerKart.tsx:1112-1153, :1428-1434). */
	struct FKartRaceContext
	{
		int32 PlayerCount = 1;
		/** 1 = leader. */
		int32 RacePosition = 1;
		/** min(1, max(0, leaderScore - myScore) / 140), score = laps * 100 + progress %. */
		double LeaderGapRatio = 0.0;
		bool bRacing = true;
		/** The nearest kart ahead in race order, for slipstream. */
		bool bHasAhead = false;
		FVector2D AheadPosition = FVector2D::ZeroVector;
		struct FOther
		{
			int32 Seat = 0;
			FVector2D Position = FVector2D::ZeroVector;
		};
		int32 SelfSeat = 0;
		TArray<FOther> Others;
		bool bKartContact = true;
	};

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
		double Turbo = 0.0;
		double DriftDirection = 0.0;
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

		// Surfaces and walls
		bool bOnDirt = false;
		bool bWrongWay = false;
		bool bHitWallThisStep = false;
		uint64 Frame = 0;
		uint64 LastRadialFrame = 0;
		uint64 LastWrongWayFrame = 0;
		uint64 LastTerrainFrame = 0;
		FVector2D RadialPush = FVector2D::ZeroVector;
		double RadialStrength = 0.0;
		bool bRadialCritical = false;
		double NearestWall = 999.0;
		FVector2D LastCollisionNormal = FVector2D::ZeroVector;
		TArray<double> PadLastBoostTime;

		// Race progress
		double Time = 0.0;
		int32 NextCheckpoint = 1;
		bool bWasInsideNext = false;
		int32 LapsCompleted = 0;
		double ProgressPercent = 0.0;
		bool bFinished = false;
		double FinishTime = 0.0;

		// Recovery
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

	class MOVECADEKART_API FKartSim
	{
	public:
		static constexpr double StepSeconds = 1.0 / 60.0;
		static constexpr double SlipstreamScale = 0.4;

		void Init(const FTrack* InTrack, int32 GridSlot);
		void Step(const FKartInput& Input);
		void Step(const FKartInput& Input, const FKartRaceContext& Race);
		void Teleport(const FVector2D& Position, double Yaw);
		/** Sets a timed top-speed multiplier (boost pads, rocket start, items). Below 1 it slows the kart. */
		void ApplyBoost(double Multiplier, double Seconds, const FString& Label);

		const FKartState& State() const { return S; }
		FKartState& MutableState() { return S; }
		const FTrack* Track() const { return T; }

		/** Wall sweep for the frame's whole displacement (harborBoundary.ts constrainHarborMovement). */
		bool ConstrainMovement(const FVector2D& Start, FVector2D& InOutDesired) const;
		bool IsOnDirt(const FVector2D& Position) const;
		/** legacy trackRecovery.ts getKartRecoveryPose. Returns false when the kart is on route. */
		bool RecoveryPose(const FVector2D& Position, const FCheckpoint* Next, FVector2D& OutPos, double& OutYaw) const;

	private:
		struct FBarrier
		{
			bool bBlocked = false;
			double Distance = 0.0;
			bool bHasNormal = false;
			FVector2D Normal = FVector2D::ZeroVector;
			double Penetration = 0.0;
			double Assist = 0.0;
		};

		void Respawn(const FVector2D& Position, double Yaw, const TCHAR* Reason);
		double UpdateSlipstream(const FKartRaceContext& Race, double Dt);
		void ProbeRadial();
		FBarrier ProbeBarrier(const FVector2D& MoveDir);
		void UpdateWatchdogs(bool bPowered);
		void UpdateBoostPads();
		void UpdateCheckpoints();
		void UpdatePresentation(double Dt);

		const FTrack* T = nullptr;
		FKartState S;
		int32 Slot = 0;
	};
}
