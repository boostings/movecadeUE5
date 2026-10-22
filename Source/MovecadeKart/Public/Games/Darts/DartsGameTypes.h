#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"
#include "DartsGameTypes.generated.h"

/**
 * The value types ADartsGame shares with its game mode and HUD: the stage, who sits at each seat, the local
 * thrower's live inputs and the finished visit the HUD's visit card shows.
 */

UENUM()
enum class EDartsStage : uint8
{
	/** The intro card: the pub, the board and the line-up; the match clock holds. */
	Intro,
	/** The engine runs: its own 3-2-1, then the visits. */
	Match,
	/** The engine ended: final scores. */
	Finished,
};

UENUM()
enum class EThrowerKind : uint8
{
	/** Keyboard, mouse or a gamepad on this machine (whoever is up uses any of them). */
	Local,
	/** A phone in the room: its own detector decides the throw (darts:release). */
	Phone,
	Bot,
};

/** One thrower, as the game mode seats them (local, room phone or bot). */
USTRUCT()
struct FDartsSeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EThrowerKind Kind = EThrowerKind::Bot;
	/** Bots: 0 wild .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** The local thrower's live inputs (keyboard, gamepad, mouse), shown by the reticle and the HUD's meter. */
struct FLocalThrow
{
	/** Aim, -1..1 each way (the reticle; a landing point is Aim * AimReach). */
	FVector2D Aim = FVector2D::ZeroVector;
	/** The aim's smoothed speed (units/s), for steadiness. */
	double AimSpeed = 0.0;
	bool bGripped = false;
	double GripAt = 0.0;
	/** 0..1: the oscillating meter (keys, pad) or the flick's speed (mouse). */
	double Power = 0.0;
	bool bMouseFlick = false;
	double FlickSide = 0.0;
};

/** A finished visit, for the HUD's visit card (shown once its third dart sticks). */
struct FDartsVisitSummary
{
	FString PlayerId;
	int32 Round = 0;
	/** The three darts: label ("T20", "MISS") and points. */
	TArray<FString, TInlineAllocator<3>> Labels;
	TArray<int32, TInlineAllocator<3>> Points;
	int32 Total = 0;
	int32 Bonus = 0;
	int32 Bounties = 0;
	/** The checkout-style call ("ONE HUNDRED AND EIGHTY!", "TON!", ...). */
	FText Call;
	/** Clock when it was shown; < 0 when there is none. */
	double At = -10.0;
	/** Clock when the next dart landed (the card makes way for it). */
	double EndAt = 1e9;
};
