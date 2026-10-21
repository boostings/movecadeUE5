#pragma once

#include "CoreMinimal.h"
#include "Games/Bowling/BowlingSim.h"

/**
 * The visual pin solver (movecade-v2 bowling/visualPhysics.ts, Docs/Port/06-bowling.md §3): acts out a resolved
 * shot. It cannot change which pins fall; it only decides how they tumble, scatter and settle. Fixed 1/120 s steps.
 *
 * Rack-local web units: x across (the bowler's right +), y up from the deck, z back from the head pin (down-lane +).
 * A pose's (x, y, z) is the pin's centre; Tilt is its lean as a (x, z) vector whose length is the angle in radians.
 */
namespace MovecadeKart::Bowling
{
	constexpr double VisualUnitsPerMetre = 0.68 / 0.3048;
	constexpr double VisualPinHeight = 0.381 * VisualUnitsPerMetre;
	constexpr double VisualPinRadius = 0.0605 * VisualUnitsPerMetre;
	constexpr double VisualBallRadius = 0.19;
	constexpr double VisualDeckY = 0.02;

	struct FPinPose
	{
		int32 Id = 0;
		FVector Position = FVector::ZeroVector;
		/** Lean direction times angle, in the rack's (x, z). */
		FVector2D Tilt = FVector2D::ZeroVector;
		/** Spin about the pin's own axis, radians. */
		double Spin = 0.0;
		bool bVisible = true;
		bool bToppled = false;
		double OffSpot = 0.0;
	};

	struct FVisualBall
	{
		/** Rack-local web units. */
		double X = 0.0;
		double Z = 0.0;
		/** m/s. */
		double Speed = 8.0;
	};

	class MOVECADEKART_API FPinSolver
	{
	public:
		FPinSolver();

		/** A fresh rack: Standing pins visible and upright; Shot (if any) says which of them must fall and how hard. */
		void Reset(const FShotResult* Shot, const TArray<int32>& Standing);
		/** Advances by Dt seconds; Ball, when given, is swept since the last call for contacts. */
		void Step(double Dt, const FVisualBall* Ball = nullptr);

		const TArray<FPinPose>& Poses() const { return Poses_; }
		double Elapsed() const { return ElapsedSeconds; }

	private:
		struct FBody
		{
			double BaseX = 0.0, BaseZ = 0.0;
			double X = 0.0, Y = 0.0, Z = 0.0;
			double RotX = 0.0, RotY = 0.0, RotZ = 0.0;
			double Lean = 0.0, LeanX = 0.0, LeanZ = 1.0, LeanRate = 0.0;
			double VX = 0.0, VY = 0.0, VZ = 0.0, Spin = 0.0;
			double NudgeX = 0.0, NudgeZ = 0.0, Wobble = 0.0;
			double Age = 0.0, Jitter = 0.0, FallbackAt = TNumericLimits<double>::Max(), Energy = 0.0;
			int32 Id = 0;
			int32 ScatterSign = 1;
			bool bVisible = true, bFelled = false, bStruck = false, bBallHit = false;
		};

		void Strike(FBody& Body, double NX, double NZ, double Strength) const;
		void BallContact(double FromX, double FromZ, const FVisualBall& Ball);
		void Integrate(double Dt);
		void PinContact(double Dt);
		void EnforceAuthority();
		void FixedStep(double Dt);
		void Publish();

		TArray<FBody> Bodies;
		TArray<FPinPose> Poses_;
		double ElapsedSeconds = 0.0;
		double Accumulator = 0.0;
		double ArmedAt = 0.0;
		double PrevBallX = 0.0, PrevBallZ = 0.0;
		bool bBallDriven = false;
		bool bArmed = false;
		bool bHasPrevBall = false;
	};
}
