#pragma once

#include "CoreMinimal.h"

/**
 * Bowling's ball and pin resolution (movecade-v2 bowling/sim.ts, Docs/Port/06-bowling.md §3): which pins a shot
 * fells, decided at launch. A deterministic energy model, not a physics engine: the ball's path is an analytic hook
 * over the oil, each pin takes direct energy from how squarely the ball meets it, and toppling pins pass energy to
 * their neighbours in waves. The visual pin solver only acts it out.
 *
 * Metres on the lane: x across (negative = the bowler's left), z down the lane from the foul line (sim.ts), so the
 * head pin is at z = 0 of the rack frame and the deck at z = LaneLength.
 */
namespace MovecadeKart::Bowling
{
	constexpr double PinSpacing = 0.3048;
	constexpr double LaneWidth = 1.0668;
	constexpr double LaneHalfWidth = LaneWidth / 2.0;
	constexpr double LaneLength = 18.288;
	constexpr double OilLength = 12.0;
	constexpr double BallRadius = 0.108;
	constexpr double PinRadius = 0.06;
	constexpr double GutterCaptureEdge = LaneHalfWidth + BallRadius * 0.55;
	constexpr double PinToppleEnergy = 0.24;
	constexpr double PinTransferReach = PinSpacing * 1.85;
	constexpr double PinTransferCoefficient = 0.66;
	constexpr double BallContactReach = 0.168;
	constexpr double MaxPinEnergy = 1.25;
	constexpr double SplitGap = 2.35 * PinSpacing;
	constexpr double PathSampleStep = 0.36;

	/** What the bowler's release decided (bowling:release, clamped). Loft is presentation only. */
	struct FShot
	{
		/** Lateral release line, metres, ±0.45. */
		double Aim = 0.0;
		/** 0..1. */
		double Power = 0.0;
		/** Hook at the deck, metres, ±0.45 (the sim does not clamp it). */
		double Curve = 0.0;
		double Loft = 0.0;
	};

	/** Pin 1..10 in the rack frame (x across, z back from the head pin). */
	MOVECADEKART_API FVector2D PinPosition(int32 Id);

	MOVECADEKART_API double SpeedForPower(double Power);
	MOVECADEKART_API double RotationRps(const FShot& Shot);
	/** The ball's lateral position at distance Z down the lane. At the deck it is exactly Aim + Curve. */
	MOVECADEKART_API double BallXAt(const FShot& Shot, double Z);

	/** How far down the lane the ball drops into a gutter, if it does (0 = off the lane at release). */
	MOVECADEKART_API TOptional<double> FirstGutter(const FShot& Shot);

	struct FPinImpact
	{
		int32 Id = 0;
		double Energy = 0.0;
		double DirectEnergy = 0.0;
		/** How hard it slides once down, 0..1. */
		double Slide = 0.0;
		/** Which way it scatters: away from the ball's line. */
		int32 ScatterSign = 1;
	};

	struct FShotResult
	{
		FShot Shot;
		TArray<int32> Felled;
		TArray<FPinImpact> Impacts;
		bool bGutter = false;
		double GutterAt = 0.0;
		/** -1 left, +1 right. */
		int32 GutterSide = 0;
		/** (x, z) every PathSampleStep metres, ending at the gutter capture or the deck. */
		TArray<FVector2D> Path;
		double EndX = 0.0;
		double Speed = 0.0;
		double Rps = 0.0;
	};

	/** Resolves a shot against the standing pins (ids 1..10). */
	MOVECADEKART_API FShotResult ResolveShot(const FShot& Shot, const TArray<int32>& Standing);

	/** A split left on a full rack: the head pin down, two or more standing with a wide gap between some pair. */
	MOVECADEKART_API bool IsSplit(int32 StandingBefore, const TArray<int32>& Remaining);

	/** How long the ball takes down the lane at this speed (runtime.ts rolling phase), seconds. */
	MOVECADEKART_API double RollSeconds(double Speed);
}
