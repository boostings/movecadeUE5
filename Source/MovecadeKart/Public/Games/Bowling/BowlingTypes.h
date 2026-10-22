#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"
#include "Misc/Parse.h"
#include "BowlingTypes.generated.h"

/**
 * The vocabulary Bowling's game actor, game mode and HUD share: phases, who a seat is, how a roll reads, and the
 * debug flags. Presentation only: the rules (BowlingRules.h) and the sim (BowlingSim.h) know none of it.
 */

UENUM()
enum class EBowlingPhase : uint8
{
	/** The intro card: the lane at rest, nobody up yet. */
	Intro,
	Aiming,
	Delivery,
	Rolling,
	Settling,
	Reset,
	Finished,
};

UENUM()
enum class EBowlerKind : uint8
{
	/** Keyboard, mouse or a gamepad on this machine (turn-based: whoever is up uses any of them). */
	Local,
	/** A phone in the room: its own detector decides the throw (bowling:release). */
	Phone,
	Bot,
};

/** How a roll reads (presentation.ts classifyBowlingBeat), in that order of precedence. */
enum class EBowlingBeat : uint8
{
	None,
	Gutter,
	SplitConvert,
	Turkey,
	Strike,
	Spare,
	Split,
	Good,
	Open,
};

/** Where a line meets the rack, as the commentary reads it (presentation only). */
enum class EBowlingLine : uint8
{
	Pocket,
	/** A spare ball through the pins that are left. */
	OnTarget,
	Brooklyn,
	HeadOn,
	Light,
	Wide,
	Gutter,
};

namespace MovecadeKart::Bowling
{
	/** The beats the house celebrates at length (the orbit cam, the long reaction, the phones' celebration haptic). */
	inline bool IsCelebration(EBowlingBeat Beat)
	{
		return Beat == EBowlingBeat::Strike || Beat == EBowlingBeat::Turkey || Beat == EBowlingBeat::SplitConvert;
	}
}

/** One bowler: who they are, how they play, and their strike streak. Built by ABowlingGameMode, owned by the game. */
USTRUCT()
struct FBowlerSeat
{
	GENERATED_BODY()

	/** The room's player id, or "seat-N" locally. */
	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EBowlerKind Kind = EBowlerKind::Bot;
	/** Bots: 0 wild .. 1 steady. */
	float Skill = 0.6f;
	FKartLook Look;
	/** Strikes in a row for this bowler (W3: per player, not across the lane). */
	int32 Streak = 0;
};

/** The local bowler's live inputs (keyboard, gamepad, mouse), shown by the HUD's guide and meter. */
struct FLocalBowl
{
	double Aim = 0.0;
	double Curve = 0.0;
	bool bGripped = false;
	double GripAt = 0.0;
	/** 0..1: the oscillating meter (keys, pad) or the flick's speed (mouse). */
	double Power = 0.0;
	bool bMouseFlick = false;
};

/** How the last throw left the hand, for the HUD's swing read-out (presentation only). */
struct FBowlingSwing
{
	double At = -100.0;
	double Speed = 0.0;
	double Power = 0.0;
	double Curve = 0.0;
	EBowlingLine Line = EBowlingLine::Light;
};

/**
 * Bowling's debug command-line flags, parsed once when the game is set up. The seat flags (-BowlPlayers=, -BowlBots=,
 * -BowlAuto) are AArcadeGameModeBase's (CommandLinePrefix "Bowl"); these are the game's own:
 *   -BowlShowcase  bots throw ABowlingGame::ShowcaseShot (a strike) at every full rack: celebration screenshots, demos.
 * Every flag changes gameplay, so none of them exists in Shipping.
 */
struct FBowlingDebugFlags
{
	bool bShowcase = false;

	static FBowlingDebugFlags Parse(const TCHAR* CommandLine)
	{
		FBowlingDebugFlags Flags;
#if !UE_BUILD_SHIPPING
		Flags.bShowcase = CommandLine && FParse::Param(CommandLine, TEXT("BowlShowcase"));
#endif
		return Flags;
	}
};
