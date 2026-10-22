#pragma once

#include "CoreMinimal.h"

/**
 * Every tuning number of the kart sim, in web units (metres, speed units "SU", seconds, radians), grouped by the
 * phase that uses it. They are port-parity values: each group names the web source it came from, and changing
 * one changes the shipped feel (and the determinism fingerprint). Per-step factors are applied once per 60 Hz
 * step, exactly as the web applied them once per 60 fps frame.
 */
namespace MovecadeKart::KartTuning
{
	/** The web build's reference frame rate, for per-frame factors the port applies per step. */
	constexpr double WebFramesPerSecond = 60.0;
	/** What a probe that hit nothing reports as its distance (MultiplayerKart.tsx). */
	constexpr double NoHitDistance = 999.0;

	// ---- Speed (legacy/Experience/kart/engine/constants.ts:2-20, MultiplayerKart.tsx:1108-1181) ----
	constexpr double MaxSpeed = 40.0;
	constexpr double ReverseSpeed = -15.0;
	constexpr double LambdaThrottle = 1.8;
	constexpr double LambdaBrake = 6.0;
	constexpr double LambdaCoast = 1.1;
	constexpr double TurboBonus = 40.0;
	constexpr double DirtMultiplier = 0.75;

	// ---- Rubber band (MultiplayerKart.tsx:1112-1129): at most +2 SU for the last kart ----
	constexpr double RubberBandScale = 0.05;
	constexpr double RubberBandTwoPlayerScale = 0.4;
	constexpr double RubberBandThreePlayerScale = 0.7;
	constexpr double RubberBandFullScale = 1.0;

	// ---- Steering and heading (MultiplayerKart.tsx:1188-1199, :1249-1263) ----
	constexpr double TurnPerStep = 0.018;
	constexpr double TurnSpeedReference = 40.0;
	/** Analog steering beyond this picks a drift side, or counts as steering into a wall. */
	constexpr double SteerDeadZone = 0.1;
	/** B11: the turn speed a kart wedged on a wall still gets, so it can steer away. */
	constexpr double WedgedMinTurnSpeed = 10.0;
	constexpr double DirLerp = 12.0;

	// ---- Hop and drift (constants.ts, MultiplayerKart.tsx:1202-1244) ----
	constexpr double DriftMinSpeed = 20.0;
	constexpr double DriftTurn = 1.4;
	constexpr double DriftPowerCap = 10.0;
	constexpr double HopDuration = 0.25;
	constexpr double HopHeight = 0.3;
	/** A kart this high off the ground still counts as hopping (no wall probes). */
	constexpr double HopAirborneOffset = 0.05;

	// ---- Wall probes and response (MultiplayerKart.tsx:88-109, :611-652, :1280-1425) ----
	constexpr double BarrierCollisionDistance = 2.5;
	constexpr double SideProbeReach = 0.8;
	constexpr double DiagonalProbeReach = 0.9;
	constexpr double CollisionPushStrength = 0.8;
	constexpr double CollisionSoftDistance = 0.85;
	constexpr double CollisionHardDistance = 0.7;
	constexpr double CollisionCriticalDistance = 0.4;
	/** A side wall pushes back at half the strength of one dead ahead. */
	constexpr double SidePenetrationWeight = 0.5;
	constexpr double SteeringAssistStrength = 0.01;
	constexpr double AssistNearWallBoost = 1.2;
	/** The assist barely acts while the player steers into the wall on purpose. */
	constexpr double AssistOpposedScale = 0.15;
	constexpr double MinCollisionSpeedMult = 0.3;
	constexpr double NearWallMinSpeedMult = 0.1;
	constexpr double NearWallClosenessPenalty = 0.5;
	constexpr double ClosenessPenalty = 0.6;
	/** Closeness used when the probe reported a hit at distance zero. */
	constexpr double UnknownCloseness = 0.5;
	constexpr double BounceStrength = 1.2;
	constexpr double LegacyBounceMinSpeed = 30.0;
	constexpr double NearWallSlideRate = 0.15;
	constexpr double SlideRate = 0.4;
	/** B11: how much of the slide a head-on hit loses (a glancing one keeps it all). */
	constexpr double HeadOnSlideLoss = 0.7;
	constexpr double ContinuousProbeDistance = 1.5;
	constexpr int32 NumRadialProbes = 4;
	constexpr double EmergencyPushStrength = 1.5;
	/** A hard-zone probe counts half towards the radial push strength. */
	constexpr double HardPushStrengthWeight = 0.5;
	/** The speed a critical (two-probe) wall contact keeps each step. */
	constexpr double CriticalWallSpeedKeep = 0.3;
	constexpr uint64 ContinuousCheckInterval = 6;
	constexpr uint64 FastCheckInterval = 3;
	/** The swept constraint's speed decay per second while it pushes the kart out (:1440-1445). */
	constexpr double WallSpeedDecay = 8.0;
	/** Swept wall query box, in kart radii: sequential push-outs can move the kart up to a radius per segment. */
	constexpr double SweepQueryMarginRadii = 4.0;
	/** Sanity limit on one step's sweep (about 650 m at Harbor's 0.65 m substep). */
	constexpr double MaxSweepSubsteps = 1000.0;
	/** Below this the disc centre is on the wall and is pushed along the face normal instead. */
	constexpr double RadialPushEpsilon = 1e-6;

	// ---- Hops into walls (MultiplayerKart.tsx) ----
	constexpr double HopWallProbeDistance = 1.0;
	constexpr double HopWallStopDistance = 0.6;
	constexpr double HopWallSpeedKeep = 0.3;

	// ---- Kart contact (MultiplayerKart.tsx:658-675, :1428-1434, kartContact.ts) ----
	constexpr double KartContactDistance = 1.8;
	constexpr double KartBumpForce = 0.5;
	/** Below this separation the push direction falls back to seat order. */
	constexpr double KartContactCoincident = 0.0001;
	constexpr double KartContactSpeedKeepPerFrame = 0.8;
	constexpr double BumpEventSpeedWeight = 0.8;
	constexpr double BumpEventFloor = 0.2;

	// ---- Collision feedback for haptics, audio and FX (MultiplayerKart.tsx:1457-1569) ----
	constexpr double IntensityPenetrationWeight = 2.0;
	constexpr double IntensitySpeedWeight = 0.5;
	constexpr double IntensityCriticalBonus = 1.0;
	constexpr double IntensityNearWallBonus = 0.3;
	constexpr double FirstContactEventFloor = 0.3;
	constexpr double SustainedEventInterval = 0.15;
	constexpr double SustainedEventFloor = 0.2;
	constexpr double SustainedEventScale = 0.6;

	// ---- Wrong way and terrain (MultiplayerKart.tsx:1571-1610) ----
	constexpr uint64 WrongWayInterval = 15;
	constexpr double WrongWayMinSpeed = 5.0;
	constexpr double WrongWayMinLookahead = 0.1;
	/** Facing more than 120 degrees away from the next waypoint is wrong way. */
	constexpr double WrongWayDot = -0.5;
	constexpr uint64 TerrainInterval = 10;

	// ---- Recovery (trackRecovery.ts, recoveryWatchdogs.ts) ----
	constexpr double OffTrackRespawnSeconds = 2.0;
	constexpr double StuckAnchorDistance = 4.0;
	constexpr double StuckAnchorSeconds = 2.5;
	constexpr double CheckpointStallSeconds = 8.0;
	/** A watchdog not stepped for this long starts over instead of counting the gap. */
	constexpr double WatchGapSeconds = 1.0;

	// ---- Race progress (useCheckpointDetection.ts) ----
	constexpr double MaxProgressPercent = 99.9;

	// ---- Visual layers (Kart.jsx:194-242 and the slip layer in MultiplayerKart.tsx:1249-1263) ----
	constexpr double SlipYawFromSlipAngle = 1.3;
	constexpr double SlipYawFromDrift = 0.1;
	constexpr double SlipYawLambda = 6.0;
	constexpr double SteerVisualLambda = 14.0;
	/** Speed at which body yaw and lean reach full strength. */
	constexpr double BodyFullLeanSpeed = 50.0;
	constexpr double BodyYawGain = 6.0;
	constexpr double BodyYawLambda = 6.0;
	constexpr double GroupYawDriftGain = 0.4;
	constexpr double GroupYawLambda = 4.0;
	constexpr double LeanGain = 3.0;
	constexpr double LeanLambda = 5.0;
}
