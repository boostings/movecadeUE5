#pragma once

// Shared by AFishingGame's implementation files (FishingGame*.cpp) only: the tunables more than one of them reads,
// the easing helpers, and the per-frame lane picture the pose works out and the tackle and the catch draw.

#include "CoreMinimal.h"
#include "Avatar/ArcadeAvatar.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingRules.h"

DECLARE_LOG_CATEGORY_EXTERN(LogFishing, Log, All);

namespace MovecadeKart::Fishing::GameTuning
{
	/** The rules step on a fixed 10 ms clock (the fight's own substep), so every machine agrees. */
	constexpr double StepMs = 10.0;
	/** A long hitch is played out as at most this much time (s), never as a burst of catch-up steps. */
	constexpr float MaxFrameSeconds = 0.1f;
	/** The weigh-in shows this long (s) before the results are committed (the arcade overlay's results bar). */
	constexpr double ShowcaseSeconds = 7.0;
	/** A full turn of the local cast meter (empty, full, empty), s. */
	constexpr double MeterPeriodSeconds = 1.5;

	/** The house anglers' first cast (match ms), staggered down the dock. */
	constexpr double BotFirstCastMs = 400.0;
	constexpr double BotFirstCastStaggerMs = 500.0;

	/** Rod and line pieces per lane (the blank bends piece by piece; the line sags through its pieces). */
	constexpr int32 RodSegments = 6;
	constexpr int32 LineSegments = 22;

	/** The angler's head over its feet (cm), for the HUD's callouts and the catch card. */
	constexpr double HeadHeightCm = 205.0;
	/** Where a catch is held up, in the avatar's frame (cm forward, up). */
	constexpr double HoldForwardCm = 44.0;
	constexpr double HoldUpCm = 124.0;
}

namespace MovecadeKart::Fishing::GameMath
{
	/** Hermite smoothstep of X from A to B (A > B runs it backwards: 1 before, 0 after). */
	inline double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Cubic ease-out of a 0..1 progress. */
	inline double EaseOut(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return 1.0 - FMath::Pow(1.0 - T, 3.0);
	}

	inline FRotator Facing(double Yaw)
	{
		return FRotator(0.0, Yaw, 0.0);
	}
}

/**
 * One lane's frame of presentation: the pose works out where the line goes, how the rod pitches and bends and
 * where the hands are; the tackle draws the rod, line and bobber from it and the catch places the fish.
 */
struct AFishingGame::FLaneFrame
{
	FArcadePoseInput* Pose = nullptr;
	/** Where the angler stands, and how long (s) it has been in its current state. */
	FVector Spot = FVector::ZeroVector;
	double InState = 0.0;
	/** The fish on the line or just landed (null otherwise), and whether it is junk. */
	const MovecadeKart::Fishing::FSpecies* Caught = nullptr;
	bool bJunk = false;
	/** Turned round to the camera: showing a catch, or the weigh-in. */
	bool bShowing = false;
	bool bFinished = false;
	/** Where the line meets the water: the lure, or the fish in a fight. */
	FVector Out = FVector::ZeroVector;

	// The rod (degrees) and line.
	double Pitch = 0.0;
	double Bend = 0.0;
	double Sag = 0.0;
	bool bLine = true;
	bool bBobber = true;
	bool bRod = true;
	/** The line's far end and the bobber (zero: not placed this frame). */
	FVector End = FVector::ZeroVector;
	FVector Bob = FVector::ZeroVector;
	/** The floating bobber's rest height on the swell (cm). */
	double Rest = 0.0;
	/** Shoulder-relative hand targets in arm lengths: the right on the handle, the left on the reel. */
	FVector Right = FVector::ZeroVector;
	FVector Left = FVector::ZeroVector;
	/** The cast's draw (0..1): a held cast's meter, or a phone's wind-up. */
	double Charge = 0.0;
	/** The rod is leaning on the bucket (a fish held up in both hands). */
	bool bRodDown = false;
};
