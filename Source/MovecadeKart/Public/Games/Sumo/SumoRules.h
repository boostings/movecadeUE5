#pragma once

#include "CoreMinimal.h"

/**
 * Sumo Bumpers' rules: 2-4 bumper pucks on a round floating ring. Each step reads every player's tilt (the
 * acceleration direction, a unit disc) and the DASH button; the pucks roll with drag, collide as circles with mass
 * and restitution, and a dash is a burst of speed that shoves whoever it meets. A puck whose centre leaves the ring
 * falls into the sea. The ring sheds a 1 m band at a time (warned first), power-ups spawn from a seeded RNG, the
 * last puck standing takes the round, two rounds take the match. Score: 3 a round win, 1 a knockout (a ring-out
 * within 3 s of your hit).
 *
 * Pure and deterministic: a fixed 60 Hz step, no UObjects, no clock, its own RNG. Plane coordinates in metres:
 * X away from the camera, Y to its right (UE's axes, so the presentation maps 1:1 at 100 cm a metre).
 */
namespace MovecadeKart::Sumo
{
	constexpr double StepSeconds = 1.0 / 60.0;
	constexpr double StepMs = 1000.0 / 60.0;
	constexpr int32 MaxPlayers = 4;
	constexpr int32 WinsForMatch = 2;
	constexpr int32 MaxRounds = 5;
	constexpr double CountdownMs = 3000.0;
	constexpr double RoundOverMs = 3600.0;

	// The ring --------------------------------------------------------------------------------------------------
	constexpr double ArenaRadius = 9.0;
	constexpr double BandWidth = 1.0;
	/** Bands that fall away before only the core (radius 2) is left. */
	constexpr int32 RingBands = 7;
	constexpr double CoreRadius = ArenaRadius - RingBands * BandWidth;
	constexpr double FirstShrinkMs = 14000.0;
	constexpr double ShrinkEveryMs = 8000.0;
	constexpr double ShrinkWarningMs = 3000.0;
	/** The core itself goes last: whoever is still on it then falls too (a drawn round). */
	constexpr double CollapseMs = FirstShrinkMs + RingBands * ShrinkEveryMs + 6000.0;
	constexpr double SpawnRadius = 5.2;

	// The pucks -------------------------------------------------------------------------------------------------
	constexpr double PuckRadius = 0.85;
	constexpr double PuckMass = 1.0;
	constexpr double HeavyMass = 2.4;
	/** m/s^2 at full tilt. */
	constexpr double Accel = 15.0;
	constexpr double HeavyAccel = 12.5;
	/** Velocity decay per second (rolling friction and air). */
	constexpr double Drag = 1.75;
	constexpr double FallingDrag = 0.4;
	constexpr double Restitution = 0.82;
	constexpr double ShieldRestitution = 1.35;
	constexpr double MaxSpeed = 17.0;
	constexpr double DashSpeed = 10.0;
	constexpr double DashMs = 320.0;
	constexpr double DashCooldownMs = 1500.0;
	constexpr double TurboCooldownMs = 450.0;
	/** A press up to this long before the dash is ready fires it as soon as it is. */
	constexpr double DashBufferMs = 160.0;
	/** The extra m/s a dash hit gives its victim (scaled by the mass ratio). */
	constexpr double DashShove = 4.5;
	/** Closing speed (m/s) under which a contact is a push, not an event. */
	constexpr double BumpEventSpeed = 1.5;
	constexpr double FallMs = 1300.0;
	constexpr double KoCreditMs = 3000.0;

	// Power-ups -------------------------------------------------------------------------------------------------
	constexpr double PowerFirstMs = 6000.0;
	constexpr double PowerEveryMinMs = 6500.0;
	constexpr double PowerEveryMaxMs = 10500.0;
	constexpr int32 MaxPowerUps = 2;
	constexpr double PowerPickupReach = PuckRadius + 0.45;
	constexpr double PowerLifeMs = 12000.0;
	constexpr double HeavyMs = 7000.0;
	constexpr double ShieldMs = 9000.0;
	constexpr double TurboMs = 6000.0;

	// Scoring ---------------------------------------------------------------------------------------------------
	constexpr int32 RoundWinPoints = 3;
	constexpr int32 KnockoutPoints = 1;

	enum class EPhase : uint8 { Countdown, Fight, RoundOver, MatchOver };
	enum class EPower : uint8 { None, Heavy, Shield, Turbo };

	enum class EEventKind : uint8
	{
		RoundStart,
		Fight,
		/** A contact closing at BumpEventSpeed or more. Strength: closing speed, m/s. */
		Bump,
		/** A dash that connected. Strength: closing speed. */
		DashHit,
		/** A shield took a hit and popped, bouncing the attacker. */
		ShieldBlock,
		Dash,
		/** A puck left the ring: A fell, B (if set) earned the knockout. */
		RingOut,
		/** A falling puck reached the sea. */
		Splash,
		PowerSpawn,
		PowerPickup,
		PowerExpire,
		/** The outer band is about to go: Strength is the radius it leaves. */
		ShrinkWarning,
		Shrink,
		RoundWin,
		RoundDraw,
		MatchOver,
	};

	struct FInput
	{
		/** The acceleration direction and amount: clamped to the unit disc. */
		FVector2D Tilt = FVector2D::ZeroVector;
		/** DASH pressed this step (an edge, not the held state). */
		bool bDash = false;
	};

	struct FPuck
	{
		FString Id;
		FString Name;
		int32 Slot = 0;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Vel = FVector2D::ZeroVector;
		/** The way it last rolled (the dash goes this way without a tilt). */
		FVector2D Facing = FVector2D(1.0, 0.0);
		FInput Input;
		/** On its feet this round (neither falling nor out). */
		bool bStanding = true;
		bool bFalling = false;
		double FallMs = 0.0;
		bool bOut = false;
		/** The active dash's remaining time, its cooldown, a buffered press. */
		double DashMs = 0.0;
		double DashCooldownMs = 0.0;
		double DashQueuedMs = 0.0;
		EPower Power = EPower::None;
		double PowerMs = 0.0;
		int32 LastHitBy = INDEX_NONE;
		double LastHitAtMs = -1e9;
		int32 RoundWins = 0;
		int32 Knockouts = 0;
		int32 Falls = 0;
		int32 Score = 0;

		double Mass() const { return Power == EPower::Heavy ? HeavyMass : PuckMass; }
		bool HasShield() const { return Power == EPower::Shield; }
		bool CanDash() const { return bStanding && DashCooldownMs <= 0.0; }
	};

	struct FPowerUp
	{
		int32 Id = 0;
		EPower Kind = EPower::Heavy;
		FVector2D Pos = FVector2D::ZeroVector;
		double AgeMs = 0.0;
	};

	struct FRingEvent
	{
		int32 Id = 0;
		EEventKind Kind = EEventKind::Bump;
		int32 A = INDEX_NONE;
		int32 B = INDEX_NONE;
		FVector2D Pos = FVector2D::ZeroVector;
		double Strength = 0.0;
		EPower Power = EPower::None;
		int32 Round = 1;
		double AtMs = 0.0;
	};

	struct FSeed
	{
		FString Id;
		FString Name;
	};

	struct FMatch
	{
		EPhase Phase = EPhase::Countdown;
		/** 1-based. */
		int32 Round = 1;
		int32 RoundsPlayed = 0;
		/** Time in the current phase, in the fight, and since the match began. */
		double PhaseMs = 0.0;
		double RoundMs = 0.0;
		double ElapsedMs = 0.0;
		int64 Steps = 0;
		double Radius = ArenaRadius;
		/** The radius after the warned shrink (== Radius when none is pending). */
		double PendingRadius = ArenaRadius;
		int32 ShrinksDone = 0;
		TArray<FPuck> Pucks;
		TArray<FPowerUp> PowerUps;
		/** The latest events (the oldest are dropped past MaxEvents); ids only grow. */
		TArray<FRingEvent> Events;
		int32 NextId = 1;
		double NextPowerMs = PowerFirstMs;
		uint32 Rng = 1;
		/** Per round: the winner's slot, or INDEX_NONE for a draw. */
		TArray<int32> RoundWinners;
		int32 RoundWinner = INDEX_NONE;
		int32 MatchWinner = INDEX_NONE;

		int32 StandingCount() const;
		const FPuck* Find(const FString& Id) const;
		int32 IndexOf(const FString& Id) const;
		bool IsShrinkPending() const { return PendingRadius < Radius; }
	};

	constexpr int32 MaxEvents = 64;

	MOVECADEKART_API FMatch CreateMatch(const TArray<FSeed>& Players, uint32 Seed);
	/** The input for the next step (Tilt is clamped to the unit disc; a dash press is kept until stepped). */
	MOVECADEKART_API void SetInput(FMatch& Match, int32 Index, const FInput& Input);
	/** One fixed 60 Hz step. */
	MOVECADEKART_API void StepMatch(FMatch& Match);

	/** When the next band falls (round time, ms) after Done shrinks; infinity once the core is gone. */
	MOVECADEKART_API double ShrinkAtMs(int32 Done);
	/** The ring's radius after Done shrinks. */
	MOVECADEKART_API double RadiusAfter(int32 Done);

	/** The circle contact between two pucks: separation, impulse, dash shove, shield; events into Match. */
	MOVECADEKART_API void ResolveContact(FMatch& Match, int32 A, int32 B);
	/** Places: round wins, then score, then knockouts (slot order breaks ties). Indices into Pucks. */
	MOVECADEKART_API TArray<int32> Standings(const FMatch& Match);
	/** A hash of the whole state, to prove two runs identical. */
	MOVECADEKART_API uint64 Fingerprint(const FMatch& Match);

	/** The rules' RNG (xorshift32): a uint in [0, 2^32) and a double in [0, 1). */
	MOVECADEKART_API uint32 NextRandom(uint32& State);
	MOVECADEKART_API double Random01(uint32& State);

	// The house players ----------------------------------------------------------------------------------------

	struct FBotBrain
	{
		/** 0 sloppy .. 1 sharp: reaction time, aim, edge sense, dodging, power-up greed. */
		double Skill = 0.6;
		int32 Target = INDEX_NONE;
		double RetargetMs = 0.0;
		double ThinkMs = 0.0;
		FVector2D Wander = FVector2D::ZeroVector;
		uint32 Rng = 1;
	};

	MOVECADEKART_API FBotBrain MakeBot(double Skill, uint32 Seed);
	/** A bot's input for this step: chase and line up behind a target to push it out, keep off the edge, dodge a
	 *  dash, take power-ups, dash when lined up (or to save itself). Deterministic in (Match, Brain). */
	MOVECADEKART_API FInput ThinkBot(const FMatch& Match, int32 Self, FBotBrain& Brain);
}
