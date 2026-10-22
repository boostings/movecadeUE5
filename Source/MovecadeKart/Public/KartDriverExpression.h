#pragma once

#include "CoreMinimal.h"

/**
 * The driver's face: blends the avatar4 expression morphs from what the race is doing to them. Pure functions, so
 * the mix is unit-tested (KartDriverTests); AKartPawn eases the result and writes the morph targets.
 */
namespace MovecadeKart::DriverFace
{
	enum class EMorph : uint8
	{
		Smile,
		CheekSquint,
		BrowDown,
		BrowInnerUp,
		BrowOuterUp,
		JawOpen,
		MouthPress,
		MouthFrown,
		EyeWide,
		EyeSquint,
		Count
	};

	constexpr int32 MorphCount = static_cast<int32>(EMorph::Count);

	/** The morph target's name on the avatar4 meshes. */
	MOVECADEKART_API FName MorphName(EMorph Morph);

	/** How strongly each mood applies right now, 0..1 each. */
	struct FMood
	{
		/** Charging a drift: focused, teeth set. */
		float Drift = 0.f;
		/** Boosting: a big grin. */
		float Boost = 0.f;
		/** Revving on the grid: eager. */
		float Rev = 0.f;
		/** Spun out, blown up or slammed into a wall: wide-eyed shock. */
		float Shock = 0.f;
		/** Finished on the podium: beaming. */
		float Cheer = 0.f;
		/** Finished off the podium: crestfallen. */
		float Slump = 0.f;
		/** Straining (a charge, a heave): teeth set, brows down, eyes narrowed. */
		float Effort = 0.f;
		/** "Meh" (a shrug): brows up, mouth pressed flat. */
		float Meh = 0.f;
	};

	/**
	 * The morph weights for a mood, each 0..1. Moods layer in priority order (drift, boost, rev, effort, shock, then the
	 * finish, then meh), each pulling every morph toward its own expression by its strength, so the strongest recent
	 * emotion wins without popping.
	 */
	MOVECADEKART_API void Mix(const FMood& Mood, float (&OutWeights)[MorphCount]);
}
