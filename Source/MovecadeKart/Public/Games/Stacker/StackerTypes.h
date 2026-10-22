#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"
#include "StackerTypes.generated.h"

/**
 * The small value types Sky Stacker's game actor, game mode and HUD share: the round's stages, who drives a lane,
 * a seat as the mode hands it to the game, and the coach marks the HUD paints.
 */

UENUM()
enum class EStackerStage : uint8
{
	/** The intro card and the camera's drop onto the roof; the round clock holds. */
	Intro,
	Match,
	/** The showcase: the towers compress side by side, then the camera orbits the winner. */
	Finished,
};

UENUM()
enum class EBuilderKind : uint8
{
	/** The left stick, the keys or the mouse tilt the palm; A, Space or a click drops. */
	Local,
	/** A phone is the palm: its attitude balances the tower, its DROP button drops. */
	Phone,
	Bot,
};

/** One builder, as AStackerGameMode seats them (room players or local people and house bots). */
USTRUCT()
struct FStackerSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EBuilderKind Kind = EBuilderKind::Bot;
	/** Bots: 0 sloppy .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** The local builder's coach marks (the first round of a session teaches; later rounds stay quiet). */
struct FStackerCoach
{
	/** 0..1 how much each mark shows this frame. */
	float Drop = 0.f;
	float Lean = 0.f;
	/** The side to lean (-1 left, +1 right) against the wind. */
	float LeanSide = 1.f;
	/** The block would land perfectly if dropped now. */
	bool bAligned = false;
	/** The local tower (an index into the match's towers), or INDEX_NONE. */
	int32 Tower = INDEX_NONE;
};
