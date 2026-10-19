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
 * - B3: the shared boostPower fallback after a failed drift release is not ported.
 * - B5: fixed 60 Hz step instead of frame-rate-dependent handling.
 * Kart-to-kart contact (B2) is not here yet: the race layer will add it, behind a switch.
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

		void Init(const FTrack* InTrack, int32 GridSlot);
		void Step(const FKartInput& Input);
		void Teleport(const FVector2D& Position, double Yaw);

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

		void Respawn(const FVector2D& Position, double Yaw);
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
