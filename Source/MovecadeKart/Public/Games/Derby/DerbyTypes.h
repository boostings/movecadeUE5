#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"
#include "DerbyTypes.generated.h"

namespace MovecadeKart::Derby
{
	/** Contact this good is called PERFECT (the HUD) and "perfect timing" (the phone's toast). */
	constexpr double PerfectCallQuality = 0.97;
}

/** Where a derby is in its loop. ADerbyGame moves through these; the HUD and the camera follow them. */
UENUM()
enum class EDerbyPhase : uint8
{
	/** The intro card and the crane over the park. */
	Intro,
	/** "Now batting": the batter steps in (the room hears whose turn it is). */
	StepIn,
	/** The pitcher on the rubber, waiting a moment (or for the batter's READY). */
	Set,
	/** The windup, then the release. */
	Windup,
	/** The ball on its way to the plate (and past it, if nobody hits it). */
	Pitch,
	/** Hit: the ball in the air. */
	Flight,
	/** The call: the distance, the count, the crowd. */
	Result,
	Finished,
};

/** Who swings for a seat. */
UENUM()
enum class EDerbyKind : uint8
{
	/** Keyboard, mouse or pad on this machine (turn-based: whoever is up uses any of them). */
	Local,
	/** A phone in the room: swing it like a bat. */
	Phone,
	Bot,
};

/** One batter, as the game mode seats them. Seats are in batting order, the same order as the match's batters. */
USTRUCT()
struct FDerbySeat
{
	GENERATED_BODY()

	FString Id;
	FString Name;
	FLinearColor Color = FLinearColor::White;
	EDerbyKind Kind = EDerbyKind::Bot;
	/** Bots: 0 wild .. 1 locked in. */
	float Skill = 0.6f;
	FKartLook Look;
};

/** The local batter's live inputs, for the swing and the HUD's aim and load meter. */
struct FDerbyLocalSwing
{
	/** -1 pull (left field) .. +1 opposite (right field). */
	double Aim = 0.0;
	bool bLoading = false;
	/** Game clock when the load started. */
	double LoadAt = 0.0;
	/** 0..1, how loaded the swing is. */
	double Load = 0.0;
};

/**
 * The derby's timing and feel: everything a designer would tune without touching the rules (DerbyRules owns the
 * ball physics and the scoring). Exposed on ADerbyGame; the defaults are the shipped feel.
 */
USTRUCT()
struct FDerbyTuning
{
	GENERATED_BODY()

	// ---- The loop.

	/** The intro card and the crane shot, seconds. */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double IntroSeconds = 3.4;

	/** "Now batting", seconds. */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double StepInSeconds = 2.3;

	/** The pitcher on the rubber before the windup, seconds. */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double SetSeconds = 0.7;

	/** A phone batter gets longer to settle before the pitch (or presses READY). */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double PhoneSetSeconds = 2.4;

	/** READY never starts the windup sooner than this into the set. */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double ReadyMinSetSeconds = 0.5;

	/** A take or a whiff is called this long after the ball reaches the plate (it gets to the backstop). */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double CallDelaySeconds = 0.32;

	/** How long the call stays up, by result. */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double MoonshotHoldSeconds = 3.8;

	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double HomeRunHoldSeconds = 2.8;

	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double InPlayHoldSeconds = 1.6;

	/** Fouls, strikes and whiffs. */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double OtherHoldSeconds = 1.2;

	/** Extra hold when the turn is over (a beat before the next batter). */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	double TurnOverHoldSeconds = 0.6;

	/** Longest frame the derby clock advances by: it keeps pace with real time (a phone's swing is real time). */
	UPROPERTY(EditAnywhere, Category = "Derby|Loop")
	float MaxFrameSeconds = 0.5f;

	// ---- The swing.

	/** The swing animation, from its start to the bat meeting the ball. */
	UPROPERTY(EditAnywhere, Category = "Derby|Swing")
	double SwingLeadSeconds = 0.11;

	/** The whole swing animation. */
	UPROPERTY(EditAnywhere, Category = "Derby|Swing")
	double SwingSeconds = 0.62;

	/** How long a full load takes to build while the button is held. */
	UPROPERTY(EditAnywhere, Category = "Derby|Swing")
	double LoadSeconds = 0.55;

	/** A released load swings with this strength empty .. this plus LoadStrength full. */
	UPROPERTY(EditAnywhere, Category = "Derby|Swing")
	double MinLoadStrength = 0.3;

	UPROPERTY(EditAnywhere, Category = "Derby|Swing")
	double LoadStrength = 0.7;

	/** With nobody swinging, the pitch is a take once it is this far past the contact window, seconds. */
	UPROPERTY(EditAnywhere, Category = "Derby|Swing")
	double TakeGraceSeconds = 0.02;

	// ---- Local aim.

	/** A/D and the arrows: aim per second. */
	UPROPERTY(EditAnywhere, Category = "Derby|Aim")
	double KeyAimRate = 1.6;

	/** Mouse: aim per pixel of horizontal movement. */
	UPROPERTY(EditAnywhere, Category = "Derby|Aim")
	double MouseAimPerPixel = 0.004;

	/** Sticks: ignored under this deflection, followed at this rate above it. */
	UPROPERTY(EditAnywhere, Category = "Derby|Aim")
	double StickDeadZone = 0.15;

	UPROPERTY(EditAnywhere, Category = "Derby|Aim")
	double StickAimSpeed = 8.0;

	// ---- Phones.

	/** A phone's swing arrives this much after it happened (the network), on top of the detector's own delay. */
	UPROPERTY(EditAnywhere, Category = "Derby|Phone")
	double PhoneLatencySeconds = 0.06;

	/** How long after the contact window a phone may still swing (its event is on the way). */
	UPROPERTY(EditAnywhere, Category = "Derby|Phone")
	double PhoneGraceSeconds = 0.35;

	/** A phone swing is never weaker than this. */
	UPROPERTY(EditAnywhere, Category = "Derby|Phone")
	double PhoneMinStrength = 0.25;

	/** The motion pad's button swings (no gyro, or no faith in it): its peak after the press, and its strength. */
	UPROPERTY(EditAnywhere, Category = "Derby|Phone")
	double ButtonSwingDelaySeconds = 0.07;

	UPROPERTY(EditAnywhere, Category = "Derby|Phone")
	double ButtonSwingStrength = 0.7;

	// ---- House bots.

	/** Chance a bot offers at a pitch at all. */
	UPROPERTY(EditAnywhere, Category = "Derby|Bots")
	double BotSwingChance = 0.96;

	/** Bot timing error (ms, one sigma): a locked-in hitter's, plus this much more for a wild one, plus the curve's. */
	UPROPERTY(EditAnywhere, Category = "Derby|Bots")
	double BotTimingSigmaMs = 16.0;

	UPROPERTY(EditAnywhere, Category = "Derby|Bots")
	double BotWildSigmaMs = 90.0;

	UPROPERTY(EditAnywhere, Category = "Derby|Bots")
	double BotCurveSigmaMs = 18.0;

	/** A changeup fools a wild hitter early by up to this much (ms). */
	UPROPERTY(EditAnywhere, Category = "Derby|Bots")
	double BotChangeupEarlyMs = 30.0;

	/** Bots aim within +-this. */
	UPROPERTY(EditAnywhere, Category = "Derby|Bots")
	float BotAimRange = 0.45f;

	// ---- The hit.

	/** Contact holds the frame a beat (seconds; the stronger one past HardContactQuality), the clock at this rate. */
	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double HitStopSeconds = 0.04;

	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double HardHitStopSeconds = 0.08;

	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double HardContactQuality = 0.85;

	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	float HitStopTimeScale = 0.08f;

	/** Moonshots hang in slow motion between these fractions of their flight; the rest play a touch fast. */
	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double MoonshotSlowFrom = 0.12;

	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double MoonshotSlowTo = 0.82;

	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double MoonshotTimeScale = 0.38;

	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double FlightTimeScale = 1.3;

	/** How fast the flight's time scale eases toward its target (1/s). */
	UPROPERTY(EditAnywhere, Category = "Derby|Hit")
	double TimeScaleResponse = 6.0;
};
