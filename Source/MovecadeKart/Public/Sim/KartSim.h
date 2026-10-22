#pragma once

#include "CoreMinimal.h"
#include "Sim/KartSimTypes.h"
#include "Sim/KartTrack.h"

/**
 * The kart simulation, ported line for line from the shipped web build
 * (legacy/Experience/kart/engine/MultiplayerKart.tsx; spec Docs/Port/01).
 *
 * It runs in web units (metres, speed units "SU", radians, three.js axes) at a fixed 60 Hz, which
 * is the 60 fps "ultra" reference feel. Constants that the web applies per frame are therefore
 * applied per step, unchanged. Every result is a pure function of the track, the inputs and the race
 * context, bit for bit (MovecadeKart.Sim.Determinism and .Fingerprint guard this).
 *
 * Deliberate differences from the web build, from the plan's bug table (Docs/Port/00-PLAN.md):
 * - B1: boost multipliers below 1 scale top speed (the web ignored them, so slowdowns did nothing).
 * - B2: kart-to-kart contact runs (the web never filled its list of other karts).
 * - B3: the shared boostPower fallback after a failed drift release is not ported.
 * - B4: slipstream thresholds and boosts are scaled by SlipstreamScale (0.4) to the 40 SU speed range.
 * - B5: fixed 60 Hz step instead of frame-rate-dependent handling.
 * - B11: wall hits scale with the angle of impact, and a wedged kart can turn away (bLegacyStickyWalls
 *   restores the web's behaviour).
 *
 * The implementation is split by phase: KartSim.cpp (step order, speed, steering, drift, pads, checkpoints,
 * visual layers), KartSimCollision.cpp (walls and kart contact), KartSimRecovery.cpp (respawn watchdogs) and
 * KartBoosts.cpp (drift, rocket-start and slipstream boosts). Tuning lives in Private/Sim/KartSimTuning.h.
 */
namespace MovecadeKart
{
	/** legacy performanceQuality.ts getRaceDriftExitBoostFeedback. DriftPower is seconds held. */
	MOVECADEKART_API bool DriftExitBoost(double DriftPower, double DriftDurationMs, double SteeringInput, double DriftDirection, FDriftBoost& Out);

	/** legacy performanceQuality.ts rocket start: how long the gas was held when the count hit GO. */
	MOVECADEKART_API bool RocketStartBoost(double HeldMs, FDriftBoost& Out);

	/**
	 * One kart's sim. Owned by value by its UKartMovementComponent (and by tests); it points at a shared,
	 * immutable FTrack that must outlive it. Call Init once, then Step once per 60 Hz tick.
	 */
	class MOVECADEKART_API FKartSim
	{
	public:
		static constexpr double StepSeconds = 1.0 / 60.0;
		static constexpr double SlipstreamScale = 0.4;

		/** B11: true keeps the web's sticky walls (a wall within 2.5 m ahead stops forward motion). */
		bool bLegacyStickyWalls = false;

		/** Places the kart on the track's spawn grid (clamped to the grid) and resets all state. */
		void Init(const FTrack* InTrack, int32 GridSlot);
		/** One 60 Hz step outside a race (no rivals, no rubber band). */
		void Step(const FKartInput& Input);
		void Step(const FKartInput& Input, const FKartRaceContext& Race);
		void Teleport(const FVector2D& Position, double Yaw);
		/** Sets a timed top-speed multiplier (boost pads, rocket start, items). Below 1 it slows the kart. */
		void ApplyBoost(double Multiplier, double Seconds, const FString& Label);

		const FKartState& State() const { return Kart; }
		/** For the item world and tests, which change speed or position directly. */
		FKartState& MutableState() { return Kart; }
		const FTrack* Track() const { return TrackData; }

		/** Wall sweep for the frame's whole displacement (harborBoundary.ts constrainHarborMovement). */
		bool ConstrainMovement(const FVector2D& Start, FVector2D& InOutDesired) const;
		bool IsOnDirt(const FVector2D& Position) const;
		/** legacy trackRecovery.ts getKartRecoveryPose. Returns false when the kart is on route. */
		bool RecoveryPose(const FVector2D& Position, const FCheckpoint* Next, FVector2D& OutPos, double& OutYaw) const;

	private:
		/** What the forward and side probes found this step. */
		struct FBarrier
		{
			bool bBlocked = false;
			double Distance = 0.0;
			bool bHasNormal = false;
			FVector2D Normal = FVector2D::ZeroVector;
			double Penetration = 0.0;
			/** Yaw nudge away from a close side wall. */
			double Assist = 0.0;
		};

		/** This step's controls, clamped and decoded once. */
		struct FStepControls
		{
			double SteeringX = 0.0;
			/** Keyboard steering: +1 left, -1 right. */
			double Keys = 0.0;
			bool bLeft = false;
			bool bRight = false;
			bool bForward = false;
			bool bBackward = false;
			bool bJump = false;
		};

		/** How the kart moves this step, decided before the collision phases. */
		struct FStepMotion
		{
			FVector2D MoveDir = FVector2D::ZeroVector;
			bool bReversing = false;
			bool bHopping = false;
			bool bNearWall = false;
		};

		// The phases of Step, in order.
		void BeginStep(double Dt);
		void UpdateOffTrack();
		void UpdateWatchdogs(bool bPowered);
		void UpdateSpeed(const FStepControls& Controls, const FKartRaceContext& Race, double Dt);
		void UpdateYaw(const FStepControls& Controls);
		void UpdateHopAndDrift(const FStepControls& Controls, double Dt);
		void UpdateHeading(double Dt);
		void ApplyRadialPush(const FStepMotion& Motion);
		void ApplyBarrierResponse(const FBarrier& Barrier, const FStepMotion& Motion, const FStepControls& Controls);
		void Advance(const FBarrier& Barrier, const FStepMotion& Motion, double Dt);
		void ApplyKartContact(const FKartRaceContext& Race, double Dt);
		void UpdateCollisionFeedback(const FBarrier& Barrier, const FStepMotion& Motion);
		void UpdateBoostPads();
		void UpdateWrongWayAndTerrain();
		void UpdateCheckpoints();
		void UpdatePresentation(double Dt);

		// Helpers of the phases.
		static FStepControls DecodeControls(const FKartInput& Input);
		void Respawn(const FVector2D& Position, double Yaw, const TCHAR* Reason);
		/** The checkpoint the kart is heading for, or null if the index is somehow out of range. */
		const FCheckpoint* NextCheckpointOrNull() const;
		double UpdateSlipstream(const FKartRaceContext& Race, double Dt);
		void ProbeRadial();
		FBarrier ProbeBarrier(const FVector2D& MoveDir);
		void AdvanceAirborne(const FStepMotion& Motion, double Dt);
		/** The wall response when the forward probe hit: slow down and slide along the wall. */
		void ApplyBlockedResponse(const FBarrier& Barrier, const FStepMotion& Motion, double AbsSpeed, double Dt);
		/** Harbor: a start outside the analytic walls is pulled back inside them first. True if it was. */
		bool PullInsideAnalyticWalls(const FVector2D& Start, FVector2D& InOutP) const;

		const FTrack* TrackData = nullptr;
		FKartState Kart;
		int32 GridIndex = 0;
		/** Where the last centreline search succeeded: a starting point only, never part of the result. */
		mutable int32 CentreHint = 0;
	};
}
