// AFishingGame's angler poses: for each moment of a cast, a wait, a fight and a catch, where the rod points and
// bends, where the hands go, and how the body leans. The keyframes are the tables below; the functions apply them.
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Games/Fishing/FishingGame.h"
#include "Games/Fishing/FishingGameInternal.h"
#include "Games/Fishing/FishingLake.h"

using namespace MovecadeKart::Fishing;
using namespace MovecadeKart::Fishing::GameMath;

namespace
{
	/** A hand target, shoulder-relative in arm lengths (X forward, Y to the side, Z up). */
	struct FHandTarget
	{
		double X;
		double Y;
		double Z;

		FVector Get() const { return FVector(X, Y, Z); }
	};

	/** A hand that moves with a 0..1 drive (a cast's draw): Rest + PerDrive x drive on each axis. */
	struct FHandRamp
	{
		FHandTarget Rest;
		FHandTarget PerDrive;

		FVector At(double Drive) const { return FVector(Rest.X + PerDrive.X * Drive, Rest.Y + PerDrive.Y * Drive, Rest.Z + PerDrive.Z * Drive); }
	};

	/** A hand that travels from one target to another as a 0..1 drive goes (a whip). */
	struct FHandLerp
	{
		FHandTarget From;
		FHandTarget To;

		FVector At(double T) const { return FVector(FMath::Lerp(From.X, To.X, T), FMath::Lerp(From.Y, To.Y, T), FMath::Lerp(From.Z, To.Z, T)); }
	};

	/** Turning round to show a catch (1/s), the yaw it shows at, fanned a little across the lanes. */
	constexpr double TurnRate = 7.0;
	constexpr double ShowYaw = 162.0;
	constexpr double ShowYawFan = 6.0;
	/** A landed fish is shown from this long after landing until this long before the hold ends (s). */
	constexpr double ShowMarginSeconds = 0.35;
	/** The avatar's eyes over its feet (cm), for the look direction. */
	constexpr double EyeHeightCm = 165.0;
	/** A record's cheer: from 1.2 s to 2.5 s after it (the card's moment). */
	constexpr double RecordCheerSeconds = 2.5;

	/** The default every state starts from: the rod up and swaying a little, the bobber riding the swell. */
	namespace RestPose
	{
		constexpr double Pitch = 38.0;
		constexpr double SwayDeg = 2.0;
		constexpr double SwayRate = 1.3;
		constexpr double Bend = 6.0;
		constexpr FHandTarget Right{ 0.55, -0.08, -0.18 };
		constexpr FHandTarget Left{ 0.48, 0.18, -0.32 };
		/** The bobber's rest height on the swell (cm), its rate and its phase step per lane. */
		constexpr double SwellCm = 1.2;
		constexpr double SwellRate = 1.3;
		constexpr double SwellLanePhase = 1.7;
	}

	/** Idle (and lost): drawing a cast by its Charge 0..1, the rod back over the shoulder, the body twisting back. */
	namespace DrawPose
	{
		constexpr double Pitch = 40.0;
		constexpr double PitchPerCharge = 92.0;
		constexpr double BendPerCharge = -8.0;
		constexpr FHandRamp Right{ { 0.5, -0.05, -0.15 }, { -0.35, 0.0, 0.65 } };
		constexpr FHandRamp Left{ { 0.45, 0.2, -0.3 }, { -0.1, 0.0, 0.35 } };
		constexpr double LeanPerCharge = -0.12;
		constexpr double TwistPerCharge = 0.3;
		/** The bobber hangs under the tip (cm), swinging a little, the float under the line's end. */
		constexpr double HangCm = 55.0;
		constexpr double SwingCm = 3.0;
		constexpr double SwingRate = 1.7;
		constexpr double BobberUnderCm = 12.0;
		/** A phone's backward flick draws the rod back for this long (s), to this much of a full draw. */
		constexpr double WindupShowSeconds = 0.4;
		constexpr double WindupShowDraw = 0.8;
	}

	/** Lost: the shoulders drop; a snapped line whips back and a fresh float is tied on after a second. */
	namespace LostPose
	{
		constexpr float Slump = 0.85f;
		constexpr double SnapBend = -18.0;
		constexpr double RetieSeconds = 1.0;
	}

	/** Casting: the whip forward over a quarter second, the rod springing through it, then following the line out. */
	namespace WhipPose
	{
		constexpr double PitchFrom = 130.0;
		constexpr double PitchTo = 14.0;
		constexpr double SpringBend = -26.0;
		constexpr FHandLerp Right{ { 0.15, -0.02, 0.5 }, { 0.78, -0.02, 0.05 } };
		constexpr FHandTarget Left{ 0.5, 0.15, -0.25 };
		constexpr double LeanPerWhip = 0.22;
		constexpr double WhipSeconds = 0.22;
		constexpr double SpringSeconds = 0.3;
		/** The lure's arc: 140 cm plus up to 360 cm for a full-power cast; the line's sag in flight (cm). */
		constexpr double ArcCm = 140.0;
		constexpr double ArcPowerCm = 360.0;
		constexpr double LineSag = 10.0;
	}

	/** Waiting (and the bite): the rod low toward the float; on a bite the tip jitters and the angler crouches to strike. */
	struct FWaitPose
	{
		double Pitch;
		float Lean;
		float Crouch;
	};
	constexpr FWaitPose WaitingPose{ 24.0, 0.05f, 0.08f };
	constexpr FWaitPose BitePose{ 17.0, 0.16f, 0.22f };
	namespace WaitPose
	{
		constexpr double SwayDeg = 2.0;
		constexpr double SwayRate = 0.9;
		constexpr FHandTarget Right{ 0.6, -0.05, -0.12 };
		constexpr FHandTarget Left{ 0.45, 0.18, -0.3 };
		constexpr double Bend = 7.0;
		/** The bite: the tip jitters about this bend, and the bobber is pulled under (cm). */
		constexpr double BiteBend = 14.0;
		constexpr double BiteJitterDeg = 4.0;
		constexpr double BiteJitterRate = 30.0;
		constexpr double BiteUnderCm = -15.0;
		constexpr double BiteBobCm = 3.0;
		constexpr double BiteBobRate = 22.0;
		/** The line's sag: a base plus a share of its length (cm). */
		constexpr double Sag = 18.0;
		constexpr double SagPerCm = 0.06;
	}

	/** Fighting, by the line's tension (0..1.3) and the strike's jerk just after the hook. */
	namespace FightPose
	{
		constexpr double MaxTension = 1.3;
		constexpr double Pitch = 34.0;
		constexpr double PitchPerTension = 14.0;
		constexpr double PitchPerJerk = 30.0;
		constexpr double Bend = 10.0;
		constexpr double BendPerTension = 48.0;
		constexpr double BendTensionCap = 1.1;
		/** The right hand rises with the load; the left turns the reel handle round this centre. */
		constexpr FHandTarget Right{ 0.48, -0.04, 0.12 };
		constexpr double RightRisePerTension = 0.22;
		constexpr double RightRisePerJerk = 0.35;
		constexpr FHandTarget ReelCentre{ 0.4, 0.2, -0.05 };
		constexpr double ReelRadius = 0.11;
		/** The reel handle turns 1.6 times a second at a full-speed reel. */
		constexpr double ReelTurnsPerReel = 1.6;
		constexpr double Lean = -0.08;
		constexpr double LeanPerTension = -0.16;
		constexpr double Crouch = 0.22;
		constexpr double CrouchPerTension = 0.25;
		constexpr double TwistPerLateral = 0.25;
		/** The line comes up out of the water as the fish nears (cm), and sags until the tension takes it up. */
		constexpr double LineDepthCm = -12.0;
		constexpr double LineRiseCm = 10.0;
		constexpr double SagSlackTension = 0.75;
		constexpr double SagPerCm = 0.14;
		/** The bobber dragged behind the fish (cm) and bobbing hard on the swell. */
		constexpr double BobBehindCm = -10.0;
		constexpr double BobSwayCm = 6.0;
		constexpr double BobSwayRate = 9.0;
		constexpr double FloatCm = -6.0;
		constexpr double FloatBobCm = 2.0;
		constexpr double FloatBobRate = 11.0;
		/** The fish thrashes at the surface inside this distance (m), this many splashes a second. */
		constexpr double ThrashInsideM = 3.5;
		constexpr double ThrashPerSecond = 3.0;
	}

	/** Landed: the catch comes up to the hands; junk dangles at arm's length while the shoulders drop. */
	namespace LandedPose
	{
		constexpr double Pitch = 70.0;
		constexpr double Bend = 4.0;
		constexpr double LineShowsSeconds = 0.5;
		constexpr FHandTarget JunkRight{ 0.85, 0.05, 0.15 };
		constexpr double JunkSlump = 0.55;
		constexpr FHandTarget FishRight{ 0.62, 0.3, -0.08 };
		constexpr FHandTarget FishLeft{ 0.62, -0.3, -0.08 };
		constexpr float FishLean = -0.06f;
	}

	/** The weigh-in: the winner cheers, an empty bucket slumps, everyone else holds up their best fish. */
	namespace FinishedPose
	{
		constexpr float WinnerCheer = 0.85f;
		constexpr float EmptySlump = 0.35f;
		constexpr FHandTarget ShowRight{ 0.55, 0.28, 0.3 };
		constexpr FHandTarget ShowLeft{ 0.55, -0.28, 0.3 };
	}
}

void AFishingGame::UpdateAnglers(float Dt)
{
	const int32 N = LaneCount();
	for (int32 i = 0; i < N; ++i)
	{
		FLane& Lane = Lanes[i];
		const FAngler& A = Match.Anglers[i];
		UArcadeAvatarComponent* Avatar = Avatars[i];
		FLaneFrame Frame;
		Frame.Spot = AFishingLake::AnglerSpot(i, N);
		Frame.InState = Clock - Lane.Show.StateAt;
		Frame.Caught = A.State == EAnglerState::Fighting || A.State == EAnglerState::Landed ? &Species()[A.Fight.Species] : nullptr;
		Frame.bJunk = Frame.Caught && Frame.Caught->Kind == EKind::Junk;
		Frame.bFinished = Stage == EFishingStage::Finished;
		// Landed: turn round to show the camera, then back. At the end everyone turns round.
		const double HoldSeconds = LandedHoldMs / 1000.0;
		Frame.bShowing = (A.State == EAnglerState::Landed && Frame.InState > ShowMarginSeconds && Frame.InState < HoldSeconds - ShowMarginSeconds && !Frame.bJunk) || Frame.bFinished;
		const double WantYaw = Frame.bShowing ? (ShowYaw + (i - (N - 1) / 2.0) * ShowYawFan) : 0.0;
		Lane.Show.TurnYaw += (WantYaw - Lane.Show.TurnYaw) * (1.0 - FMath::Exp(-TurnRate * Dt));
		Avatar->SetWorldLocationAndRotation(Frame.Spot, Facing(Lane.Show.TurnYaw));

		FArcadePoseInput& Pose = Avatar->Pose();
		Pose = FArcadePoseInput();
		Pose.Idle = 1.f;
		Pose.LookWeight = 0.7f;
		Frame.Pose = &Pose;
		PoseForState(i, Frame, Dt);
		if (Frame.bFinished)
		{
			PoseFinished(i, Frame);
		}
		ApplyPose(i, Frame);
		UpdateTackle(i, Frame, Dt);
		UpdateCatch(i, Frame, Dt);
	}
}

void AFishingGame::PoseForState(int32 i, FLaneFrame& Frame, float Dt)
{
	const FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const bool bFighting = A.State == EAnglerState::Fighting;
	Frame.Pitch = RestPose::Pitch + RestPose::SwayDeg * FMath::Sin(Clock * RestPose::SwayRate + i);
	Frame.Bend = RestPose::Bend;
	Frame.Out = AFishingLake::WaterPoint(i, LaneCount(), bFighting ? A.Fight.DistanceM : A.LureM, bFighting ? A.Fight.Lateral : 0.0);
	Frame.Right = RestPose::Right.Get();
	Frame.Left = RestPose::Left.Get();
	const double SinceWindup = Clock - Lane.Phone.WindupAt;
	Frame.Charge = Lane.bHolding ? Lane.Charge
		: (SinceWindup < DrawPose::WindupShowSeconds ? Smooth(0.0, 0.15, SinceWindup) * Smooth(DrawPose::WindupShowSeconds, 0.25, SinceWindup) * DrawPose::WindupShowDraw : 0.0);
	Frame.Rest = RestPose::SwellCm * FMath::Sin(Clock * RestPose::SwellRate + i * RestPose::SwellLanePhase);
	switch (A.State)
	{
	case EAnglerState::Idle:
	case EAnglerState::Lost:
		PoseIdle(i, Frame);
		break;
	case EAnglerState::Casting:
		PoseCasting(i, Frame);
		break;
	case EAnglerState::Waiting:
	case EAnglerState::Bite:
		PoseWaiting(i, Frame);
		break;
	case EAnglerState::Fighting:
		PoseFighting(i, Frame, Dt);
		break;
	case EAnglerState::Landed:
		PoseLanded(i, Frame);
		break;
	}
}

void AFishingGame::PoseIdle(int32 i, FLaneFrame& Frame)
{
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	FArcadePoseInput& Pose = *Frame.Pose;
	const double Charge = Frame.Charge;
	Frame.Pitch = DrawPose::Pitch + DrawPose::PitchPerCharge * Charge;
	Frame.Bend = DrawPose::BendPerCharge * Charge;
	Frame.Right = DrawPose::Right.At(Charge);
	Frame.Left = DrawPose::Left.At(Charge);
	Pose.Lean = static_cast<float>(DrawPose::LeanPerCharge * Charge);
	Pose.Twist = static_cast<float>(DrawPose::TwistPerCharge * Charge);
	Frame.End = Lane.Show.Tip - FVector(0.0, 0.0, DrawPose::HangCm) + FVector(DrawPose::SwingCm * FMath::Sin(Clock * DrawPose::SwingRate + i), 0.0, 0.0);
	Frame.Bob = Frame.End - FVector(0.0, 0.0, DrawPose::BobberUnderCm);
	Lane.Show.BobZ = 0.0;
	Lane.Show.BobV = 0.0;
	if (A.State != EAnglerState::Lost)
	{
		return;
	}
	const double LostSeconds = LostHoldMs / 1000.0;
	Pose.Slump = static_cast<float>(Smooth(0.0, 0.2, Frame.InState) * Smooth(LostSeconds, LostSeconds - 0.4, Frame.InState)) * LostPose::Slump;
	if (A.LastLoss == ELoss::Snap)
	{
		Frame.bLine = Frame.InState > LostPose::RetieSeconds;
		Frame.bBobber = Frame.InState > LostPose::RetieSeconds;
		Frame.Bend = LostPose::SnapBend * Smooth(0.25, 0.0, Frame.InState);
	}
}

void AFishingGame::PoseCasting(int32 i, FLaneFrame& Frame)
{
	const FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const double Flight = FMath::Clamp(A.StateMs / FMath::Max(1.0, A.FlightMs), 0.0, 1.0);
	const double Whip = EaseOut(Frame.InState / WhipPose::WhipSeconds);
	Frame.Pitch = FMath::Lerp(WhipPose::PitchFrom, WhipPose::PitchTo, Whip);
	Frame.Bend = WhipPose::SpringBend * FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(Frame.InState / WhipPose::SpringSeconds, 0.0, 1.0));
	Frame.Right = WhipPose::Right.At(Whip);
	Frame.Left = WhipPose::Left.Get();
	Frame.Pose->Lean = static_cast<float>(WhipPose::LeanPerWhip * Whip);
	// The lure flies an arc from the tip to where the cast lands.
	const FVector Land = AFishingLake::WaterPoint(i, LaneCount(), A.CastM, 0.0);
	const FVector From = Lane.Show.CastFrom.IsNearlyZero() ? Lane.Show.Tip : Lane.Show.CastFrom;
	Frame.Bob = FMath::Lerp(From, Land, Flight) + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * Flight) * (WhipPose::ArcCm + WhipPose::ArcPowerCm * A.CastPower));
	Frame.End = Frame.Bob;
	Frame.Sag = WhipPose::LineSag;
}

void AFishingGame::PoseWaiting(int32 i, FLaneFrame& Frame)
{
	const FLane& Lane = Lanes[i];
	const bool bBite = Match.Anglers[i].State == EAnglerState::Bite;
	const FWaitPose& Wait = bBite ? BitePose : WaitingPose;
	FArcadePoseInput& Pose = *Frame.Pose;
	Frame.Pitch = Wait.Pitch + WaitPose::SwayDeg * FMath::Sin(Clock * WaitPose::SwayRate + i);
	Frame.Bend = bBite ? WaitPose::BiteBend + WaitPose::BiteJitterDeg * FMath::Sin(Clock * WaitPose::BiteJitterRate) : WaitPose::Bend;
	Frame.Right = WaitPose::Right.Get();
	Frame.Left = WaitPose::Left.Get();
	Pose.Lean = Wait.Lean;
	Pose.Crouch = Wait.Crouch;
	if (bBite)
	{
		Frame.Rest = WaitPose::BiteUnderCm + WaitPose::BiteBobCm * FMath::Sin(Clock * WaitPose::BiteBobRate);
	}
	Frame.Bob = Frame.Out;
	Frame.End = Frame.Out;
	Frame.Sag = WaitPose::Sag + FVector::Dist(Lane.Show.Tip, Frame.Out) * WaitPose::SagPerCm;
}

void AFishingGame::PoseFighting(int32 i, FLaneFrame& Frame, float Dt)
{
	using namespace FightPose;
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const FFight& F = A.Fight;
	FArcadePoseInput& Pose = *Frame.Pose;
	const double Tension = FMath::Clamp(F.Tension, 0.0, MaxTension);
	// The strike: a jerk of the rod just after the hook is set.
	const double SinceHook = Clock - Lane.Show.HookedAt;
	const double Jerk = Smooth(0.0, 0.08, SinceHook) * Smooth(0.35, 0.15, SinceHook);
	Frame.Pitch = FightPose::Pitch + PitchPerTension * Tension + PitchPerJerk * Jerk;
	Frame.Bend = FightPose::Bend + BendPerTension * FMath::Min(BendTensionCap, Tension);
	Lane.Show.ReelPhase += A.Reel * ReelTurnsPerReel * 2.0 * UE_DOUBLE_PI * Dt;
	Frame.Right = FVector(Right.X, Right.Y, Right.Z + RightRisePerTension * Tension + RightRisePerJerk * Jerk);
	Frame.Left = ReelCentre.Get() + FVector(0.0, FMath::Cos(Lane.Show.ReelPhase), FMath::Sin(Lane.Show.ReelPhase)) * ReelRadius;
	Pose.Lean = static_cast<float>(FightPose::Lean + LeanPerTension * Tension);
	Pose.Crouch = static_cast<float>(FightPose::Crouch + CrouchPerTension * Tension);
	Pose.Twist = static_cast<float>(-F.Lateral * TwistPerLateral);
	const double Near = Smooth(4.0, 1.2, F.DistanceM);
	Frame.End = Frame.Out + FVector(0.0, 0.0, LineDepthCm + LineRiseCm * Near);
	Frame.Bob = Frame.Out + FVector(BobBehindCm, BobSwayCm * FMath::Sin(Clock * BobSwayRate), 0.0);
	Frame.Rest = FloatCm + FloatBobCm * FMath::Sin(Clock * FloatBobRate);
	Frame.Sag = FMath::Max(0.0, 1.0 - Tension / SagSlackTension) * FVector::Dist(Lane.Show.Tip, Frame.End) * SagPerCm;
	if (A.Reel > 0.1)
	{
		const float Reel = static_cast<float>(FMath::Min(1.0, A.Reel));
		Cues.Play(this, TEXT("fishing.reel"), 0.35f + 0.25f * Reel, 0.9f + 0.5f * Reel);
	}
	// Thrashing at the surface once it's close.
	if (F.DistanceM < ThrashInsideM)
	{
		Lane.Show.ThrashCarry += Dt * ThrashPerSecond;
		if (Lane.Show.ThrashCarry >= 1.0)
		{
			Lane.Show.ThrashCarry -= 1.0;
			Splash(Frame.Out, 0.25 + 0.3 * F.Stamina, false);
		}
	}
}

void AFishingGame::PoseLanded(int32 i, FLaneFrame& Frame)
{
	Frame.bLine = Frame.InState < LandedPose::LineShowsSeconds;
	Frame.bBobber = false;
	Frame.bRod = true;
	Frame.Pitch = LandedPose::Pitch;
	Frame.Bend = LandedPose::Bend;
	Frame.End = HoldPoint(i);
	const double HoldSeconds = LandedHoldMs / 1000.0;
	const double Hold = Smooth(0.25, 0.6, Frame.InState) * Smooth(HoldSeconds, HoldSeconds - 0.3, Frame.InState);
	if (Frame.bJunk)
	{
		Frame.Right = LandedPose::JunkRight.Get();
		Frame.Pose->Slump = static_cast<float>(LandedPose::JunkSlump * Hold);
	}
	else
	{
		Frame.Right = LandedPose::FishRight.Get();
		Frame.Left = LandedPose::FishLeft.Get();
		Frame.Pose->Lean = LandedPose::FishLean;
	}
}

void AFishingGame::PoseFinished(int32 i, FLaneFrame& Frame)
{
	const FAngler& A = Match.Anglers[i];
	FArcadePoseInput& Pose = *Frame.Pose;
	const bool bWin = Match.WinnerIds.Contains(A.PlayerId);
	Frame.bLine = false;
	Frame.bBobber = false;
	if (bWin || A.BiggestSpecies < 0)
	{
		// The winner cheers (holding the best catch high in one hand); an empty bucket slumps.
		Pose.Cheer = bWin ? FinishedPose::WinnerCheer : 0.f;
		Pose.Slump = bWin ? 0.f : FinishedPose::EmptySlump;
	}
	else
	{
		Frame.Right = FinishedPose::ShowRight.Get();
		Frame.Left = FinishedPose::ShowLeft.Get();
	}
}

void AFishingGame::ApplyPose(int32 i, const FLaneFrame& Frame)
{
	const FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	UArcadeAvatarComponent* Avatar = Avatars[i];
	FArcadePoseInput& Pose = *Frame.Pose;
	Pose.Hand[1] = Frame.Right;
	Pose.Hand[0] = Frame.Left;
	const bool bHandsFree = Pose.Cheer > 0.3f;
	Pose.HandWeight[0] = Pose.HandWeight[1] = bHandsFree ? 0.f : 1.f;
	// Look at the float (or the camera when showing off).
	const FVector LookAt = Frame.bShowing ? (Camera ? Camera->GetActorLocation() : Frame.Spot - FVector(500.0, 0.0, -150.0))
		: (A.IsLineOut() ? Frame.Out : Frame.Spot + FVector(300.0, 0.0, 40.0));
	Pose.Look = Avatar->GetComponentTransform().InverseTransformVectorNoScale(LookAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, EyeHeightCm)).GetSafeNormal();
	const double ReactAge = Clock - Lane.Show.ReactionAt;
	if (Lane.Show.Reaction == EEffectKind::Record && ReactAge < RecordCheerSeconds)
	{
		Pose.Cheer = FMath::Max(Pose.Cheer, static_cast<float>(0.6 * Smooth(1.2, 1.6, ReactAge) * Smooth(2.5, 2.2, ReactAge)));
	}
}
