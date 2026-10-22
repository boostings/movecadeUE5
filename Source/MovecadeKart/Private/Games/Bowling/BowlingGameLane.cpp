// ABowlingGame: the lane's moving parts. The ball (in the hand, down the lane, into the pit), the played rack as the
// pin solver poses it, the pinsetter's sweep, and the live guide along the line being aimed.
#include "Games/Bowling/BowlingGame.h"

#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "BowlingGameTuning.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Games/Bowling/BowlingAlley.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Bowling::Tuning;

namespace
{
	const FName ColorParam(TEXT("Color"));
	const FName RightHand(TEXT("rightHand"));
	const FName LeftHand(TEXT("leftHand"));

	// The ball in the hand (UE cm from the hand bones): hanging under the swinging hand, or cradled between both.
	const FVector HangingOffset(6.0, 0.0, -14.0);
	const FVector CradledOffset(12.0, 0.0, -4.0);
	/** The arm counts as swinging (the ball hangs from it) past this backswing, radians. */
	constexpr double SwingingArm = -0.05;

	/** The release's arc over the foul line: this high (web units), plus more with loft. */
	constexpr double DeliveryArc = 0.06;
	constexpr double DeliveryArcPerLoft = 0.26;
	/** A lofted ball flies this share of the lane (plus more with loft) before it lands, peaking this high. */
	constexpr double LoftWindow = 0.05;
	constexpr double LoftWindowPerLoft = 0.13;
	constexpr double LoftHop = 0.1;
	constexpr double LoftHopPerLoft = 0.45;
	constexpr double MinLoft = 0.02;
	/** W1: over the deck the ball's line eases from the lane's scale to the rack's, between these roll fractions. */
	constexpr double DeckBlendFrom = 0.8;
	constexpr double DeckBlendTo = 0.9;
	/** A gutter ball drops this far into the channel, which sits this far outside the lane's edge (web units). */
	constexpr double GutterDrop = 0.32;
	constexpr double GutterBeyondEdge = 0.18;

	// presentation.ts:196-291, the pinsetter (web units): the rake drops, sweeps the deck and lifts; the setter table
	// comes down and goes back up.
	constexpr double RakeUpY = 1.42;
	constexpr double RakeDownY = 0.34;
	constexpr double RakeHomeZ = ABowlingAlley::DeckZ + 0.62;
	constexpr double RakeSweptZ = ABowlingAlley::DeckZ - 2.7;
	constexpr double TableUpY = 2.2;
	constexpr double TableDownY = 1.16;
	constexpr double TableZ = ABowlingAlley::DeckZ - 0.88;
	/** Deadwood stays this far ahead of the rake, and drops into the pit past this line. */
	constexpr double RakeClearance = 0.15;
	constexpr double PitLineZ = ABowlingAlley::DeckZ - 1.95;

	// BowlingScene.tsx:1908-1951, the live guide: dots along the previewed line (the port draws more, closer).
	constexpr double GuideStartZ = 0.15;
	constexpr double GuideLength = 13.3;
	/** Dots bunch toward the bowler (an exponent under 1 spaces them out down the lane). */
	constexpr double GuideSpacing = 0.78;
	constexpr double GuideDotY = 0.024;
	constexpr double GuideDotSize = 0.11;
	constexpr double GuideGlow = 4.0;
	constexpr double GuideGlowPerCharge = 5.0;
	/** The target ring in front of the head pin: bigger on a good line, pulsing. */
	constexpr double TargetGoodRadius = 0.34;
	constexpr double TargetRadius = 0.26;
	constexpr double TargetPulse = 0.12;
	constexpr double TargetPulseRate = 7.0;
	constexpr double TargetY = 0.026;
	constexpr double TargetAheadZ = 0.55;
	constexpr double TargetGlow = 6.0;
	constexpr double TargetGlowPerCharge = 4.0;
	/** Flat discs: the engine cylinder squashed to this height. */
	constexpr double DiscHeight = 0.002;

	FRotator BallRotation(double Spin)
	{
		return FRotator(-FMath::RadiansToDegrees(Spin), 0.0, 0.0);
	}

	/** The rake's way down the deck at reset progress P (web z). */
	double RakeSweepZ(double P)
	{
		return FMath::Lerp(RakeHomeZ, RakeSweptZ, Smooth(0.10, 0.36, P));
	}

	/** A pin's lean (the solver's tilt vector: direction times angle, rack x and z) as a UE rotation, then its spin. */
	FQuat PinRotation(const FPinPose& Pose)
	{
		const double Angle = Pose.Tilt.Size();
		FQuat Rotation = FQuat::Identity;
		if (Angle > 1e-6)
		{
			const double S = FMath::Sin(Angle), C = FMath::Cos(Angle);
			// The tilt's (x, z) in the rack, with z running down the lane: in UE, down the lane is +X.
			const FVector Up(Pose.Tilt.Y / Angle * S, Pose.Tilt.X / Angle * S, C);
			Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Up.GetSafeNormal());
		}
		return Rotation * FQuat(FVector::UpVector, Pose.Spin);
	}
}

// ---- The ball ---------------------------------------------------------------------------------------------------

void ABowlingGame::UpdateBall(float Dt)
{
	const double Age = GetPhaseAge();
	FVector Web = FVector::ZeroVector;
	double Y = BallRollY;
	switch (Phase)
	{
	case EBowlingPhase::Intro:
	case EBowlingPhase::Aiming:
	case EBowlingPhase::Finished:
		HoldBall();
		return;
	case EBowlingPhase::Delivery:
	{
		// From the hand to the lane, in a little arc over the foul line.
		const double P = FMath::Clamp(Age / DeliverySeconds, 0.0, 1.0);
		const FVector Start = BallWorld(0.0, Y);
		const FVector From = FMath::Lerp(ReleaseFrom, LaneToWorld(Start.X, Start.Y, Start.Z), P);
		const double Arc = FMath::Sin(UE_DOUBLE_PI * P) * (DeliveryArc + DeliveryArcPerLoft * Shot.Shot.Loft) * 100.0;
		Ball->SetWorldLocationAndRotation(From + FVector(0.0, 0.0, Arc), BallRotation(BallSpin));
		return;
	}
	case EBowlingPhase::Rolling:
	{
		const double T = FMath::Clamp(Age / RollSeconds, 0.0, 1.0);
		Web = BallWorld(T, Y);
		BallSpin += Dt * Shot.Rps * 2.0 * UE_DOUBLE_PI;
		break;
	}
	case EBowlingPhase::Settling:
	case EBowlingPhase::Reset:
	{
		const double P = FMath::Clamp(Age / SettleSeconds, 0.0, 1.0);
		if (Phase == EBowlingPhase::Reset)
		{
			// Gone to the ball return, under the deck.
			Web = FVector(0.0, -2.0, ABowlingAlley::DeckZ - 3.0);
		}
		else if (Shot.bGutter)
		{
			// Down the channel and off the end of it.
			Web = FVector(Shot.GutterSide * 1.785, BallRollY - GutterDrop - 0.4 * P, ABowlingAlley::DeckZ + 0.1 - 1.6 * P);
		}
		else
		{
			// Through the rack and down into the pit.
			Web = FVector(Shot.EndX * ABowlingAlley::VisualPinScale, BallRollY - 0.6 * P, ABowlingAlley::DeckZ - 2.1 - 0.65 * P);
		}
		break;
	}
	}
	Ball->SetWorldLocationAndRotation(LaneToWorld(Web.X, Web.Y, Web.Z), BallRotation(BallSpin));
}

void ABowlingGame::HoldBall()
{
	const FVector Right = Avatar->BoneWorld(RightHand);
	const bool bSwinging = ArmSwing() < SwingingArm;
	const FVector At = bSwinging ? Right + HangingOffset : (Right + Avatar->BoneWorld(LeftHand)) / 2.0 + CradledOffset;
	Ball->SetWorldLocationAndRotation(At, BallRotation(BallSpin));
}

FVector ABowlingGame::BallWorld(double T, double& OutY) const
{
	// The ball's line on the lane (sim metres -> lane units), easing to the rack's scale over the deck (W1), so
	// the ball meets the pins where the sim says it does.
	const double SimX = BallXAt(Shot.Shot, T * LaneLength);
	const double Scale = FMath::Lerp(ABowlingAlley::LaneVisualScale, ABowlingAlley::VisualPinScale, Smooth(DeckBlendFrom, DeckBlendTo, T));
	double X = SimX * Scale;
	OutY = BallRollY;
	const double Loft = Shot.Shot.Loft;
	const double Window = LoftWindow + LoftWindowPerLoft * Loft;
	if (T < Window && Loft > MinLoft)
	{
		const double A = T / Window;
		OutY += (1.0 - A * A) * (LoftHop + LoftHopPerLoft * Loft);
	}
	if (Shot.bGutter)
	{
		// Into the channel where the sim says it drops off the lane.
		const double Capture = FMath::Clamp(Shot.GutterAt / LaneLength, 0.08, 0.94);
		const double In = Smooth(Capture, Capture + 0.03, T);
		X = FMath::Lerp(X, Shot.GutterSide * (ABowlingAlley::LaneVisualWidth / 2.0 + GutterBeyondEdge), In);
		OutY -= GutterDrop * In;
	}
	return FVector(X, OutY, BallStartZ - T * BallTravel);
}

// ---- The rack ---------------------------------------------------------------------------------------------------

void ABowlingGame::UpdatePins(float Dt)
{
	if (Phase == EBowlingPhase::Rolling || Phase == EBowlingPhase::Settling)
	{
		// The ball in the rack's frame, while it is on the lane.
		double Y = 0.0;
		const double T = Phase == EBowlingPhase::Rolling ? FMath::Clamp(GetPhaseAge() / RollSeconds, 0.0, 1.0) : 1.0;
		const FVector Web = BallWorld(Phase == EBowlingPhase::Rolling ? T : 1.0, Y);
		if (Phase == EBowlingPhase::Rolling && !Shot.bGutter)
		{
			FVisualBall VisualBall;
			VisualBall.X = Web.X;
			VisualBall.Z = ABowlingAlley::DeckZ - Web.Z;
			VisualBall.Speed = Shot.Speed;
			Solver.Step(Dt, &VisualBall);
		}
		else
		{
			Solver.Step(Dt);
		}
	}
	else if (Phase != EBowlingPhase::Delivery)
	{
		// Not while the ball is still leaving the hand: a step with no ball arms the solver's fallback, and the
		// rack would start to fall before the ball is even on the lane.
		Solver.Step(Dt);
	}
	PlaceRack();
}

void ABowlingGame::PlaceRack()
{
	// Deadwood is raked off the deck during the reset.
	const bool bRaking = Phase == EBowlingPhase::Reset;
	const double RakeZ = RakeSweepZ(bRaking ? FMath::Clamp(GetPhaseAge() / ResetSeconds, 0.0, 1.0) : 0.0);
	for (const FPinPose& Pose : Solver.Poses())
	{
		FTransform& Out = PinTransforms[Pose.Id - 1];
		FVector Web(Pose.Position.X, Pose.Position.Y, ABowlingAlley::DeckZ - Pose.Position.Z);
		bool bShow = Pose.bVisible;
		if (bRaking && bShow && !Standing.Contains(Pose.Id))
		{
			Web.Z = FMath::Min(Web.Z, RakeZ - RakeClearance);
			bShow = Web.Z >= PitLineZ;
		}
		if (!bShow)
		{
			// A hidden pin is an empty instance under the deck: no pose to work out.
			Out = FTransform(FQuat::Identity, LaneToWorld(0.0, -3.0, ABowlingAlley::DeckZ - 2.0), FVector::ZeroVector);
			continue;
		}
		Out = FTransform(PinRotation(Pose), LaneToWorld(Web.X, Web.Y, Web.Z));
	}
	PinRack->BatchUpdateInstancesTransforms(0, PinTransforms, true, true, true);
}

void ABowlingGame::UpdatePinsetter()
{
	// Between resets the machinery waits up behind the masking units, out of the shot (and is not moved).
	const bool bResetting = Phase == EBowlingPhase::Reset;
	Rake->SetVisibility(bResetting);
	Setter->SetVisibility(bResetting);
	if (!bResetting)
	{
		return;
	}
	const double P = FMath::Clamp(GetPhaseAge() / ResetSeconds, 0.0, 1.0);
	const double RakeY = FMath::Lerp(RakeUpY, RakeDownY, Smooth(0.0, 0.13, P)) + (RakeUpY - RakeDownY) * Smooth(0.36, 0.52, P);
	const double Back = Smooth(0.52, 1.0, P);
	Rake->SetWorldLocation(LaneToWorld(0.0, RakeY, FMath::Lerp(RakeSweepZ(P), RakeHomeZ, Back)));
	const double TableY = FMath::Lerp(TableUpY, TableDownY, Smooth(0.28, 0.62, P)) + (TableUpY - TableDownY) * Smooth(0.70, 1.0, P);
	Setter->SetWorldLocation(LaneToWorld(0.0, TableY, TableZ));
}

// ---- The live guide ---------------------------------------------------------------------------------------------

void ABowlingGame::UpdateGuide()
{
	const FBowlerSeat* Bowler = GetBowler();
	double Aim = 0.0, Curve = 0.0, Charge = 0.0;
	const bool bShow = LiveAim(Aim, Curve, Charge);
	Guide->SetVisibility(bShow);
	Target->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	FShot Preview;
	Preview.Aim = Aim;
	Preview.Curve = Curve;
	const double EndX = BallXAt(Preview, LaneLength);
	const EBowlingLine Line = ReadLine(EndX, FirstGutter(Preview).IsSet(), Standing);
	// The line turns mint in the pocket and red when it would drop into the gutter, so the read is instant.
	const bool bGood = Line == EBowlingLine::Pocket || Line == EBowlingLine::OnTarget;
	const FLinearColor LineColor = bGood ? MovecadeKart::Art::Hex(TEXT("#3dffb0"))
		: (Line == EBowlingLine::Gutter ? MovecadeKart::Art::Hex(TEXT("#ff3b4a")) : Bowler->Color);
	// Brighter as the arm charges.
	GuideMaterial->SetVectorParameterValue(ColorParam, LineColor * static_cast<float>(GuideGlow + GuideGlowPerCharge * Charge));
	for (int32 i = 0; i < GuideDots; ++i)
	{
		const double T = (i + 0.5) / GuideDots;
		const double Z = GuideStartZ - FMath::Pow(T, GuideSpacing) * GuideLength;
		const double Progress = (BallStartZ - Z) / BallTravel;
		const double X = BallXAt(Preview, FMath::Clamp(Progress, 0.0, 1.0) * LaneLength) * ABowlingAlley::LaneVisualScale;
		// Dots grow down the lane and as the arm charges.
		const double Size = (0.65 + 0.3 * Charge) * (0.75 + 0.45 * T) * GuideDotSize;
		Guide->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, LaneToWorld(X, GuideDotY, Z), FVector(Size, Size, DiscHeight)), true, i == GuideDots - 1, true);
	}
	const double Pulse = 1.0 + TargetPulse * FMath::Sin(Clock * TargetPulseRate);
	const double Radius = (bGood ? TargetGoodRadius : TargetRadius) * Pulse;
	Target->SetWorldLocationAndRotation(LaneToWorld(EndX * ABowlingAlley::VisualPinScale, TargetY, ABowlingAlley::DeckZ + TargetAheadZ), FRotator::ZeroRotator);
	Target->SetWorldScale3D(FVector(Radius, Radius, DiscHeight));
	TargetMaterial->SetVectorParameterValue(ColorParam, LineColor * static_cast<float>(TargetGlow + TargetGlowPerCharge * Charge));
}
