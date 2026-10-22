#pragma once

#include "CoreMinimal.h"

// What a party game asks of its player avatar every frame (UArcadeAvatarComponent::Pose()): the pose input and its
// gesture, celebration and disappointment variants.

/** A gesture the avatar plays with its arms on top of the pose (FArcadePoseInput::Emote). */
enum class EArcadeEmote : uint8
{
	None,
	/** One hand up, waving side to side (EmoteHand). */
	Wave,
	/** One arm straight out toward PointAt, finger extended. */
	Point,
	/** Both hands meeting in front of the chest, in rhythm. */
	Clap,
	/** One fist pumped up and down. */
	FistPump,
	/** Shoulders up, palms out: "what?". */
	Shrug,
};

/** How a celebration looks (FArcadePoseInput::CheerStyle). Auto picks a fresh one per celebration, per avatar. */
enum class EArcadeCheer : uint8
{
	Auto,
	/** Both arms up in a V, waving (the original). */
	Wave,
	/** Both fists pumped, knees bouncing. */
	FistPump,
	/** Hopping on the spot, arms flung up. */
	Jump,
	/** Clapping overhead. */
	Clap,
	/** One fist punched to the sky, the other on the hip. */
	Victory,
	Count,
};

/** How a disappointment looks (FArcadePoseInput::SlumpStyle). Auto picks per reaction, per avatar. */
enum class EArcadeSlump : uint8
{
	Auto,
	/** Head down, shoulders dropped, arms hanging (the original). */
	Droop,
	/** Hands on the head: "no way". */
	HandsOnHead,
	/** Facepalm with one hand. */
	Facepalm,
	/** Arms flung out low, shoulders up: "why?". */
	ArmsOut,
	Count,
};

/** Which frame FArcadePoseInput::Hand is given in. */
enum class EArcadeHandSpace : uint8
{
	/** The posed torso's frame (lean and twist carry the hands): the original behaviour. */
	Torso,
	/** The avatar's own unposed frame, from the rest shoulder: hands stay where the game put them as the body moves. */
	Avatar,
};

/**
 * What a game asks of its standing player avatar each frame (the party games' share of the web's avatar motion
 * system, Docs/Port/05 §3). Directions are in the avatar's own frame: X forward, Y to its right, Z up. Hand
 * targets are relative to that side's shoulder, in arm lengths, so the same numbers fit every body.
 *
 * Every field has a neutral default, so a game sets only what it needs and the avatar does the rest: it breathes,
 * shifts its weight and glances about when idle, its hair and hems swing as it moves, its face follows its mood,
 * and its reactions wind up and overshoot.
 */
struct FArcadePoseInput
{
	float Time = 0.f;
	/** Bend at the waist, radians (+ forward). */
	float Lean = 0.f;
	/** Turn of the shoulders about the spine, radians (+ to the avatar's left). */
	float Twist = 0.f;
	/** Knees bent, 0..1. */
	float Crouch = 0.f;
	/** A walk: the step cycle's phase (radians) and how much of a stride, 0..1. */
	float StepPhase = 0.f;
	float Step = 0.f;
	/** The bowler's slide, 0..1: left leg forward and bent, right leg trailing behind. */
	float Lunge = 0.f;
	/** Off the ground (a jump), metres. */
	float Lift = 0.f;
	/** 0 left, 1 right. Weight 0 lets the arm hang at rest. */
	FVector Hand[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float HandWeight[2] = { 0.f, 0.f };
	/** Where to look, as a direction in the avatar frame, and how much. */
	FVector Look = FVector::ForwardVector;
	float LookWeight = 0.f;
	/** A celebration, 0..1 (its look: CheerStyle). */
	float Cheer = 0.f;
	/** A miss or a loss, 0..1 (its look: SlumpStyle). */
	float Slump = 0.f;
	/** Breath, weight shifts, glances and sway, 0..1. */
	float Idle = 1.f;

	// ---- Added for the richer avatar (all neutral by default). ----

	/** A gesture over the arms, and how much of it (0..1; the arms blend from the pose into it). */
	EArcadeEmote Emote = EArcadeEmote::None;
	float EmoteWeight = 0.f;
	/** The hand a one-handed emote uses: 0 left, 1 right. */
	uint8 EmoteHand = 1;
	/** Where Point points (and Wave faces), a direction in the avatar frame. */
	FVector PointAt = FVector::ForwardVector;
	/** The celebration and disappointment variants; Auto varies them per reaction. */
	EArcadeCheer CheerStyle = EArcadeCheer::Auto;
	EArcadeSlump SlumpStyle = EArcadeSlump::Auto;
	/** Straining (a charge, a heave), 0..1: teeth set, brows down, a tremble in the arms. */
	float Effort = 0.f;
	/** Where each elbow should point, a direction in the avatar frame; zero lets the avatar choose. */
	FVector ElbowHint[2] = { FVector::ZeroVector, FVector::ZeroVector };
	/** How closed each hand is, 0 open .. 1 fist; negative lets the avatar choose (relaxed, gripping, fists). */
	float Grip[2] = { -1.f, -1.f };
	/** The frame Hand is in. */
	EArcadeHandSpace HandSpace = EArcadeHandSpace::Torso;
	/** Scales the avatar's own secondary motion (hair, hems, reactions' wind-up and overshoot), 0..1+. */
	float Secondary = 1.f;
};
