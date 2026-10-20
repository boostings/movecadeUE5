#pragma once

#include "CoreMinimal.h"

/**
 * Game feel: the presentation layer that sits on top of the deterministic sim and never feeds back into it.
 *
 * The sim already drives the web build's visual layers (slip yaw, body yaw, lean, hop). FKartFeel adds what a
 * console kart racer has on top of those: a sprung chassis that pitches under throttle and brake, rolls under
 * lateral load and bounces on landings and wall hits; squash and stretch; and a camera that kicks its FOV and
 * pulls back on boosts, swings wide in drifts, banks into corners and dips on landings.
 *
 * It is plain C++ (no UObjects), steps at any frame rate, and is covered by automation tests (KartFeelTests).
 * -KartWebFeel turns every extra off, for side-by-side comparisons with the web build.
 */
namespace MovecadeKart
{
	/** A damped spring toward a target, integrated in fixed substeps so it behaves the same at 30 or 240 fps. */
	struct MOVECADEKART_API FFeelSpring
	{
		double Value = 0.0;
		double Velocity = 0.0;

		/** Frequency in Hz; Damping is the damping ratio (1 = critical, < 1 overshoots). */
		void Step(double Target, double Frequency, double Damping, double DeltaSeconds);
		void Kick(double Impulse) { Velocity += Impulse; }
		void Reset(double InValue = 0.0) { Value = InValue; Velocity = 0.0; }
	};

	/** What the feel layer reads each frame: the sim's state, already in web units. */
	struct FKartFeelInput
	{
		double DeltaSeconds = 0.0;
		/** m/s (SU), signed. */
		double Speed = 0.0;
		/** Render yaw (three.js radians). */
		double Yaw = 0.0;
		/** The hop's height above the road, metres. */
		double HopOffset = 0.0;
		/** -1, 0 or +1 while drifting (+1 = drifting left, the sim's convention). */
		double DriftDirection = 0.0;
		/** 0..10 charge while drifting. */
		double DriftPower = 0.0;
		double BoostMultiplier = 1.0;
		double BoostTimeRemaining = 0.0;
		bool bOnDirt = false;
		/** > 0 on a frame with a wall hit (the sim's collision event intensity). */
		double WallImpact = 0.0;
		/** Which side the wall was hit on: +1 right, -1 left (from the collision normal). */
		double WallSide = 0.0;
		/** > 0 on a frame this kart bumped another. */
		double BumpImpact = 0.0;
		/** Counts up on every drift boost; a change means a mini-turbo just fired. */
		uint32 DriftBoostCount = 0;
		/** Counts up on every teleport; a change resets the springs instead of springing across the map. */
		uint32 TeleportCount = 0;
		/** The race clock, for the road rumble's noise. */
		double Time = 0.0;
	};

	/** The pose and camera offsets for this frame. Angles in degrees, lengths in metres. */
	struct FKartFeelPose
	{
		/** Nose up is positive. */
		double ChassisPitch = 0.0;
		/** Right side down is positive. */
		double ChassisRoll = 0.0;
		double ChassisHeave = 0.0;
		/** Scale along up (squash < 1) and along the length (stretch > 1); the width keeps the volume. */
		double SquashUp = 1.0;
		double StretchForward = 1.0;

		double CameraFovKick = 0.0;
		double CameraPullBack = 0.0;
		/** Sideways eye offset; positive is to the kart's right. */
		double CameraSide = 0.0;
		/** Extra yaw of the look target; positive turns the view right. */
		double CameraYaw = 0.0;
		double CameraRoll = 0.0;
		double CameraDip = 0.0;
		/** High-frequency eye jitter, metres (road texture at speed, dirt). */
		double CameraRumble = 0.0;
	};

	/** Feel tuning. The defaults are the shipped look; every value is a presentation constant. */
	struct FKartFeelTuning
	{
		// Chassis
		double PitchPerAccel = 0.22;      // deg per m/s^2 of longitudinal acceleration (nose up on throttle)
		double MaxPitch = 4.5;
		double RollPerLateralG = 2.6;     // deg per g of lateral load (body rolls to the outside)
		double MaxRoll = 6.0;
		double ChassisFrequency = 2.6;    // Hz
		double ChassisDamping = 0.42;
		double LandingHeaveKick = -0.2;   // m/s into the heave spring per m/s of landing speed
		double WallHeaveKick = -0.35;
		double WallRollKick = 40.0;       // deg/s

		// Squash and stretch
		double LandingSquash = 0.7;       // squash spring velocity per m/s of landing speed
		double BoostStretch = 0.10;
		double SquashFrequency = 4.2;
		double SquashDamping = 0.35;

		// Camera
		double BoostFovKick = 9.0;        // deg, scaled by how strong the boost is
		double BoostPullBack = 0.9;
		double DriftSide = 0.55;
		double DriftYaw = 7.0;
		double TurnRoll = 2.2;            // deg of camera bank per g
		double LandingDip = 0.75;
		double RumbleAtTopSpeed = 0.012;
		double DirtRumble = 0.03;
	};

	class MOVECADEKART_API FKartFeel
	{
	public:
		FKartFeelTuning Tuning;
		/** False gives the web build's look: every output stays at rest. */
		bool bEnabled = true;

		void Reset();
		const FKartFeelPose& Update(const FKartFeelInput& In);
		const FKartFeelPose& Pose() const { return Out; }

	private:
		FFeelSpring Pitch;
		FFeelSpring Roll;
		FFeelSpring Heave;
		FFeelSpring Squash;
		FFeelSpring Stretch;
		FFeelSpring Fov;
		FFeelSpring PullBack;
		FFeelSpring Side;
		FFeelSpring LookYaw;
		FFeelSpring Bank;
		FFeelSpring Dip;

		bool bPrimed = false;
		double LastSpeed = 0.0;
		double LastYaw = 0.0;
		double LastHop = 0.0;
		double HopVelocity = 0.0;
		double LastBoost = 0.0;
		/** Smoothed accelerations, so one frame's spike doesn't snap the chassis. */
		double LongAccel = 0.0;
		double LateralG = 0.0;
		uint32 LastDriftBoostCount = 0;
		uint32 LastTeleportCount = 0;
		FKartFeelPose Out;
	};
}
