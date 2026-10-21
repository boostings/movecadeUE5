#pragma once

#include "CoreMinimal.h"

/**
 * Samurai Slice's blade (Docs/Port/08-samurai.md §3): where the katana is, when a sweep is meant, and what it
 * touched. On the web the cut comes from the rendered katana's sweep between render frames, gated by an intent
 * test; the port samples the blade on the fixed sim clock instead (M2), so a swing scores the same at any frame
 * rate. All positions are web world metres (x right, y up, z toward the camera; the swordsman faces -z).
 */
namespace MovecadeKart::Samurai
{
	/** bladeContact.ts: did the blade's sweep from (A base, B tip) to (C, D) pass within Radius + 2 cm of Centre? */
	MOVECADEKART_API bool SweptBladeHits(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Centre, double Radius);
	/** The closest point on triangle ABC to P (three.js Triangle.closestPointToPoint). */
	MOVECADEKART_API FVector ClosestPointOnTriangle(const FVector& P, const FVector& A, const FVector& B, const FVector& C);

	/** bladeIntent.ts: a purposeful sweep, not an adjustment or a sensor spike. Rates deg/s, acceleration m/s^2. */
	class MOVECADEKART_API FBladeIntent
	{
	public:
		bool Feed(double RateDegPerSec, double AccelMs2, double DtMs);
		void Reset();
		bool IsActive() const { return bActive; }

	private:
		double Duration = 0.0;
		double Travel = 0.0;
		double Impulse = 0.0;
		double Quiet = 0.0;
		bool bActive = false;
	};

	/** The swordsman's rig: the web's shared avatar at scale .64, the katana .095 to 1.85 along its +Y. */
	struct FBladeRig
	{
		static constexpr double Scale = 0.64;
		static constexpr double ShoulderHeight = 1.83 * Scale;
		static constexpr double ShoulderSide = 0.318 * Scale;
		static constexpr double StandZ = 1.15;
		/** Shoulder to palm at full reach. */
		static constexpr double Reach = 0.52;
		static constexpr double BladeBase = 0.095 * Scale;
		static constexpr double BladeTip = 1.85 * Scale;
		/** How much the hand follows the blade's direction rather than its rest (handTarget.ts elbowFollow). */
		static constexpr double ElbowFollow = 0.7;
	};

	struct FBlade
	{
		FVector Shoulder = FVector::ZeroVector;
		FVector Hand = FVector::ZeroVector;
		/** Unit direction of the blade, hilt to tip. */
		FVector Direction = FVector(0.0, 1.0, 0.0);
		FVector Base = FVector::ZeroVector;
		FVector Tip = FVector::ZeroVector;
	};

	/** The swordsman's right shoulder in lane LaneX. */
	MOVECADEKART_API FVector ShoulderFor(double LaneX);
	/** The blade held in a direction: the hand reaches along a blend of rest and the blade, the blade runs from it. */
	MOVECADEKART_API FBlade BladeFor(double LaneX, const FVector& Direction);
	/**
	 * A phone's blade direction: the calibrated attitude (q_cal^-1 q, in the calibration pose's device frame, which
	 * for a hilt held upright with the screen to the player is the world's frame) applied to the phone's top (+Y).
	 */
	MOVECADEKART_API FVector BladeDirectionFromQuaternion(const FQuat& Q);
	/** Without a quaternion: the calibrated W3C Euler angles (Z-X'-Y'', degrees). */
	MOVECADEKART_API FVector BladeDirectionFromEuler(double AlphaDeg, double BetaDeg, double GammaDeg);
	/** The blade aimed so its tip heads for a point (the mouse and pad aim the tip, M9). */
	MOVECADEKART_API FVector BladeDirectionToward(double LaneX, const FVector& TipGoal);
}
