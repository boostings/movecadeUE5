#pragma once

// ABowlingGame's shared tunables and helpers, for its own .cpp files only (BowlingGame*.cpp). Values one file uses
// live at the top of that file; the ones here are read by several (the phase timing, the ball's run, the aim limits).

#include "CoreMinimal.h"
#include "Games/Bowling/BowlingAlley.h"

namespace MovecadeKart::Bowling::Tuning
{
	// runtime.ts phase timing (Docs/Port/06 §3), seconds.
	constexpr double IntroSeconds = 2.4;
	constexpr double DeliverySeconds = 0.16;
	constexpr double SettleSeconds = 1.15;
	/** The rake clears the deadwood (a second ball follows) or sets a fresh rack. */
	constexpr double DeadwoodSeconds = 0.56;
	constexpr double RackResetSeconds = 0.82;
	/** After a turn ends, the reaction plays out before the next bowler steps up: longer for a celebration. */
	constexpr double ReactionSeconds = 1.2;
	constexpr double CelebrationSeconds = 2.0;

	/** A bot lines up, then winds up for 0.9 s before the ball goes (was 1.5 s: the wait read as dead air). */
	constexpr double BotThinkSeconds = 1.15;
	constexpr double BotWindUpSeconds = 0.9;
	/** The keys and pad meter swings 0 -> 1 -> 0 over this period while the ball is gripped. */
	constexpr double MeterPeriod = 1.6;

	/** runtime.ts releaseGestureFromPayload: the release line and the hook, metres either way (throw.ts AimRange, MaxHook). */
	constexpr double MaxAim = 0.45;
	constexpr double MaxCurve = 0.45;
	/** The softest ball a local release throws (throw.ts MinPower). */
	constexpr double MinPower = 0.04;
	/** The pocket (between the 1 and 3 pins, sim metres at the head pin) and its tolerance. */
	constexpr double PocketX = 0.07;
	constexpr double PocketWindow = 0.045;

	/** Ball centre above the deck while rolling (radius 0.19 on boards at 0.02), web units. */
	constexpr double BallRollY = 0.21;
	/** Where the ball touches down (web z) and how far it runs to the deck. */
	constexpr double BallStartZ = 0.7;
	constexpr double BallTravel = 15.35;
	/** The bowler's right hand is this far right of their body: the body stands left of the line. */
	constexpr double HandOffsetX = 0.3;
	/** Where the bowler addresses the lane (web z). */
	constexpr double AddressZ = 1.72;
	/** The camera jolt of an impact or a celebration decays to nothing over this long. */
	constexpr double ShakeSeconds = 0.55;
	/** The live guide's dots along the line being aimed. */
	constexpr int32 GuideDots = 36;

	/** smoothstep(A, B, X), as the web's presentation code uses it everywhere. */
	inline double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** A lane point in web units (x across, y up, z toward the bowler) to UE cm. */
	inline FVector LaneToWorld(double X, double Y, double Z)
	{
		return ABowlingAlley::ToWorld(FVector(X, Y, Z));
	}

	/** Where the bowler's body stands for a release line (web x). */
	inline double StanceForAim(double Aim)
	{
		return Aim * ABowlingAlley::LaneVisualScale - HandOffsetX;
	}
}
