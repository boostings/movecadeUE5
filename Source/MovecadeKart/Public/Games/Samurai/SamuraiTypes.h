#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"
#include "SamuraiTypes.generated.h"

/** Samurai Slice's shared vocabulary: what ASamuraiGame, its mode and its HUD pass between them. */

UENUM()
enum class ESamuraiStage : uint8
{
	/** The intro card and the camera's sweep onto the dojo; the round clock holds. */
	Intro,
	Match,
	Finished,
};

UENUM()
enum class ESwordKind : uint8
{
	/** The mouse or the right stick moves the blade's tip (M9). */
	Local,
	/** A phone is the hilt: its attitude is the blade, its sweep the intent. */
	Phone,
	Bot,
};

/** The onboarding moment in the intro: one practice apple hangs in the local swordsman's lane until it's cut. */
enum class ESamuraiPractice : uint8
{
	None,
	Waiting,
	TooSlow,
	Cut,
};

/** A presentation-only note floating over the dojo (never from the rules): coaching and the misses they don't call. */
struct FSamuraiNote
{
	/** UE cm. */
	FVector World = FVector::ZeroVector;
	FText Label;
	FLinearColor Color = FLinearColor::White;
	/** On the round's (slow-motion scaled) clock. */
	double BornAt = 0.0;
	double Life = 0.9;
	float Scale = 1.f;
};

/** One swordsman, as the mode seats them (a room player, the local player or a house bot). */
USTRUCT()
struct FSamuraiSeat
{
	GENERATED_BODY()

	/** The room's player id, or "seat-N" locally. */
	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	ESwordKind Kind = ESwordKind::Bot;
	/** Bots: 0 wild .. 1 sharp. */
	float Skill = 0.6f;
	FKartLook Look;
};

/**
 * Samurai Slice's own test switches, parsed once by the mode and handed to the game. (-SamuraiPlayers=,
 * -SamuraiBots= and -SamuraiAuto are the shared launch options: AArcadeGameModeBase parses those.) Every switch
 * here changes play, so Shipping builds never read them.
 */
struct FSamuraiDebugFlags
{
	/** -SamuraiPracticeDemo: the local blade sweeps through the practice apple by itself (screenshots, tests). */
	bool bPracticeDemo = false;

	static FSamuraiDebugFlags Parse(const TCHAR* CommandLine);
};
