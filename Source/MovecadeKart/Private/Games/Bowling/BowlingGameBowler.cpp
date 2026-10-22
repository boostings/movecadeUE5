// ABowlingGame: the bowler up, as an avatar. Where they stand, the pendulum swing of the bowling arm, the approach and
// slide, and the reaction to the roll (avatar/motion reactions.ts).
#include "Games/Bowling/BowlingGame.h"

#include "Avatar/ArcadeAvatar.h"
#include "BowlingGameTuning.h"
#include "Components/StaticMeshComponent.h"
#include "Games/Bowling/BowlingAlley.h"

using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Bowling::Tuning;

namespace
{
	/** The approach walks the bowler this far down the lane (web z) through the backswing; the slide ends at SlideZ. */
	constexpr double ApproachZ = 1.36;
	constexpr double SlideZ = 0.95;
	/** The walk to ApproachZ takes this long into the grip; the slide to SlideZ this long after the release. */
	constexpr double ApproachWalkSeconds = 0.85;
	constexpr double SlideInSeconds = 0.55;
	/** The bowler's line follows the aim at this rate (FInterpTo). */
	constexpr double StanceFollowRate = 10.0;
	/** "Since release" for a bowler who isn't throwing: every release blend is long finished. */
	constexpr double LongAfterRelease = 10.0;

	// The arm, radians: where the backswing starts, how far each kind of bowler takes it, and the follow-through.
	constexpr double BackswingStart = -0.35;
	constexpr double LocalBackswing = 1.25;
	constexpr double BotBackswing = 1.2;
	constexpr double BotBackswingSeconds = 0.8;
	constexpr double PhoneBackswing = 1.2;
	/** A phone's backswing start eases in over the first of its swing energy. */
	constexpr double PhoneBackswingRamp = 0.15;
	/** Past this the arm counts as resting: both hands cradle the ball. */
	constexpr double RestingArm = 0.05;
	/** The swing's bottom (at the release) and its follow-through high in front. */
	constexpr double ReleaseArm = 0.25;
	constexpr double FollowThroughArm = 1.75;
	constexpr double FollowThroughSeconds = 0.45;
	/** A full backswing for the crouch and lean (radians). */
	constexpr double FullBackswing = 1.6;
	/** A phone swing past this energy moves the arm (and starts the backswing clock). */
	constexpr double PhoneSwingCharge = 0.02;

	/** The eyes sit this far above the avatar's origin (UE cm). */
	constexpr double EyeHeightCm = 160.0;
	constexpr float LookWeight = 0.75f;
	/** Before the roll the bowler looks at the head pin, this high (web units). */
	constexpr double PinsLookY = 0.4;

	// The arms, in the avatar's arm space (x forward, y across, z up from the shoulder; arm lengths).
	/** The bowling arm's reach from the shoulder, and the pendulum's plane just outside the hip. */
	constexpr double ArmReach = 0.97;
	constexpr double PendulumY = 0.12;
	/** At the address both hands cradle the ball at the chest. */
	const FVector CradleBowlingHand(0.5, -0.05, -0.38);
	const FVector CradleFreeHand(0.5, 0.24, -0.4);
	/** Once the swing starts the free arm balances it: out to the side, swinging against the bowling arm. */
	constexpr double BalanceSwing = 0.4;
	constexpr double BalanceY = -0.55;
	constexpr double BalanceZ = -0.7;
	constexpr float BalanceWeightThrown = 0.85f;
	constexpr float BalanceWeightSwinging = 0.4f;

	/** A body channel: its rest value plus how much the backswing's charge and the post-release settle add. */
	struct FBodyCurve
	{
		double Rest, PerCharge, PerSettle;
		double At(double Charge, double Settle) const { return Rest + PerCharge * Charge + PerSettle * Settle; }
	};
	constexpr FBodyCurve CrouchCurve{ 0.15, 0.3, 0.35 };
	constexpr FBodyCurve LeanCurve{ 0.12, 0.25, 0.4 };
	/** The slide's lunge: in over LungeInSeconds, held at Rest, deeper while settling. */
	constexpr double LungeInSeconds = 0.25;
	constexpr double LungeRest = 0.4;
	constexpr double LungePerSettle = 0.6;
	/** The settle (the bowler holding the finish) fades between these seconds after the release. */
	constexpr double SettleFadeFrom = 1.0;
	constexpr double SettleFadeTo = 2.6;
	constexpr double TwistPerCharge = -0.12;

	/** The feet: the approach's steps through the backswing, the slide's after the release. */
	struct FGait
	{
		/** Step size walking the approach, fading out between these grip seconds (the bowler reaches the line). */
		double ApproachStep, ApproachStopFrom, ApproachStopTo, ApproachCadence;
		/** Step size of the slide, fading out over SlideSeconds. */
		double SlideStep, SlideSeconds, SlideCadence;
	};
	constexpr FGait Gait{ 0.75, 0.7, 0.95, 9.0, 0.8, 0.45, 12.0 };

	/** The reaction to a roll (avatar/motion reactions.ts): a cheer, a bounce of LiftHeight at LiftRate, a slump. */
	struct FReaction
	{
		float Cheer = 0.f;
		double LiftHeight = 0.0;
		double LiftRate = 0.0;
		float Slump = 0.f;
	};
	constexpr FReaction StrikeReaction{ 1.f, 0.14, 7.5, 0.f };
	constexpr FReaction TurkeyReaction{ 1.f, 0.2, 7.5, 0.f };
	constexpr FReaction SpareReaction{ 0.75f, 0.06, 6.0, 0.f };
	constexpr FReaction MissReaction{ 0.f, 0.0, 0.0, 1.f };
	/** An open ball of this many pins or fewer slumps a little. */
	constexpr FReaction PoorReaction{ 0.f, 0.0, 0.0, 0.6f };
	constexpr int32 PoorBallPins = 3;
	/** A reaction eases in and out over these; a non-celebration holds ReactionHoldSeconds. */
	constexpr double ReactionInSeconds = 0.2;
	constexpr double ReactionOutSeconds = 0.3;
	constexpr double ReactionHoldSeconds = 1.8;
	/** Once the game is over the bowler on the approach keeps a little cheer, whatever the last ball did. */
	constexpr float FinishedCheer = 0.4f;

	FReaction ReactionTo(EBowlingBeat Beat, int32 Felled)
	{
		switch (Beat)
		{
		case EBowlingBeat::Strike: return StrikeReaction;
		case EBowlingBeat::Turkey: return TurkeyReaction;
		case EBowlingBeat::Spare:
		case EBowlingBeat::SplitConvert: return SpareReaction;
		case EBowlingBeat::Gutter:
		case EBowlingBeat::Split: return MissReaction;
		case EBowlingBeat::Open: return Felled <= PoorBallPins ? PoorReaction : FReaction();
		default: return FReaction();
		}
	}

	/** The arms for a swing of Arm radians: the bowling hand's pendulum, the free hand balancing (or cradling). */
	void PoseArms(FArcadePoseInput& Pose, double Arm, bool bAtAddress, bool bThrown)
	{
		Pose.Hand[1] = FVector(FMath::Sin(Arm) * ArmReach, PendulumY, -FMath::Cos(Arm) * ArmReach);
		Pose.HandWeight[1] = 1.f;
		if (FMath::Abs(Arm) < RestingArm && bAtAddress)
		{
			Pose.Hand[1] = CradleBowlingHand;
			Pose.Hand[0] = CradleFreeHand;
			Pose.HandWeight[0] = 1.f;
			return;
		}
		Pose.Hand[0] = FVector(-FMath::Sin(Arm) * BalanceSwing, BalanceY, BalanceZ);
		Pose.HandWeight[0] = bThrown ? BalanceWeightThrown : BalanceWeightSwinging;
	}

	/** The body through the throw: crouching into the backswing, lunging at the release, the approach's steps. */
	void PoseBody(FArcadePoseInput& Pose, double Arm, double Grip, bool bThrown, double SinceRelease)
	{
		const bool bApproach = Grip >= 0.0;
		const double Charge = Arm < 0.0 ? FMath::Clamp(-Arm / FullBackswing, 0.0, 1.0) : 0.0;
		const double Settle = bThrown ? 1.0 - Smooth(SettleFadeFrom, SettleFadeTo, SinceRelease) : 0.0;
		Pose.Crouch = static_cast<float>(CrouchCurve.At(Charge, Settle));
		Pose.Lean = static_cast<float>(LeanCurve.At(Charge, Settle));
		Pose.Lunge = static_cast<float>(bThrown ? Smooth(0.0, LungeInSeconds, SinceRelease) * (LungeRest + LungePerSettle * Settle) : 0.0);
		const double Walking = bApproach ? Gait.ApproachStep * (1.0 - Smooth(Gait.ApproachStopFrom, Gait.ApproachStopTo, Grip)) : 0.0;
		Pose.Step = static_cast<float>(bThrown ? Gait.SlideStep * (1.0 - Smooth(0.0, Gait.SlideSeconds, SinceRelease)) : Walking);
		Pose.StepPhase = static_cast<float>(bThrown ? SinceRelease * Gait.SlideCadence : FMath::Max(0.0, Grip) * Gait.ApproachCadence);
		Pose.Twist = static_cast<float>(TwistPerCharge * Charge);
	}
}

void ABowlingGame::UpdateBowler(float Dt)
{
	FArcadePoseInput& Pose = Avatar->Pose();
	const double Arm = ArmSwing();
	const double Age = GetPhaseAge();
	const double Grip = GripAge();
	const bool bAtAddress = Phase == EBowlingPhase::Aiming || Phase == EBowlingPhase::Intro;
	const bool bThrown = Phase == EBowlingPhase::Delivery || Phase == EBowlingPhase::Rolling || Phase == EBowlingPhase::Settling;
	const double SinceRelease = Phase == EBowlingPhase::Delivery ? Age : (Phase == EBowlingPhase::Rolling ? DeliverySeconds + Age : LongAfterRelease);
	UpdateStance(Dt, Grip, bThrown, SinceRelease);
	PoseArms(Pose, Arm, bAtAddress, bThrown);
	PoseBody(Pose, Arm, Grip, bThrown, SinceRelease);
	// Eyes on the pins, then on the ball. The avatar faces UE +X, so world directions are its own (X fwd, Y right).
	const FVector LookAt = Phase == EBowlingPhase::Rolling ? Ball->GetComponentLocation() : LaneToWorld(0.0, PinsLookY, ABowlingAlley::DeckZ);
	Pose.Look = (LookAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, EyeHeightCm)).GetSafeNormal();
	Pose.LookWeight = LookWeight;
	PoseReaction(Pose);
	Pose.Idle = 1.f;
}

void ABowlingGame::UpdateStance(float Dt, double Grip, bool bThrown, double SinceRelease)
{
	if (Phase == EBowlingPhase::Aiming || Phase == EBowlingPhase::Intro)
	{
		// On the line, the right hand over it.
		double Aim = 0.0, Curve = 0.0, Charge = 0.0;
		const double Line = StanceForAim(LiveAim(Aim, Curve, Charge) ? Aim : 0.0);
		StanceX = FMath::FInterpTo(StanceX, Line, Dt, StanceFollowRate);
	}
	if (bThrown)
	{
		StanceZ = FMath::Lerp(ReleaseZ, SlideZ, Smooth(0.0, SlideInSeconds, SinceRelease));
	}
	else if (Phase == EBowlingPhase::Reset)
	{
		// Back up to the address while the pinsetter works.
		StanceZ = FMath::Lerp(SlideZ, AddressZ, Smooth(0.0, 1.0, GetPhaseAge() / ResetSeconds));
	}
	else
	{
		StanceZ = Grip >= 0.0 ? FMath::Lerp(AddressZ, ApproachZ, Smooth(0.0, ApproachWalkSeconds, Grip)) : AddressZ;
	}
	Avatar->SetWorldLocationAndRotation(LaneToWorld(StanceX, 0.0, StanceZ), FRotator::ZeroRotator);
}

void ABowlingGame::PoseReaction(FArcadePoseInput& Pose) const
{
	// A strike pumps both fists and jumps, a spare cheers, a gutter slumps; each eases in and out over its hold.
	const double Since = Clock - LastBeatAt;
	const double Hold = IsCelebration(LastBeat) ? CelebrationSeconds : ReactionHoldSeconds;
	const double React = Since < Hold ? FMath::Clamp(Since / ReactionInSeconds, 0.0, 1.0) * FMath::Clamp((Hold - Since) / ReactionOutSeconds, 0.0, 1.0) : 0.0;
	const FReaction Reaction = ReactionTo(LastBeat, LastFelled);
	const float Lift = static_cast<float>(Reaction.LiftHeight * FMath::Abs(FMath::Sin(Since * Reaction.LiftRate)));
	Pose.Cheer = Reaction.Cheer * static_cast<float>(React) + (Phase == EBowlingPhase::Finished ? FinishedCheer : 0.f);
	Pose.Slump = Reaction.Slump * static_cast<float>(React);
	Pose.Lift = Lift * static_cast<float>(React);
}

double ABowlingGame::GripAge() const
{
	if (Phase != EBowlingPhase::Aiming)
	{
		return -1.0;
	}
	if (Local.bGripped)
	{
		return Clock - Local.GripAt;
	}
	if (BotGripAt >= 0.0)
	{
		return Clock - BotGripAt;
	}
	return PhoneGripAt >= 0.0 && PhoneCharge > PhoneSwingCharge ? Clock - PhoneGripAt : -1.0;
}

double ABowlingGame::ArmSwing() const
{
	const double Age = GetPhaseAge();
	switch (Phase)
	{
	case EBowlingPhase::Aiming:
		if (Local.bGripped)
		{
			// The pendulum follows the meter (or the mouse flick's build-up).
			return BackswingStart - LocalBackswing * Local.Power;
		}
		if (BotGripAt >= 0.0)
		{
			return BackswingStart - BotBackswing * Smooth(0.0, 1.0, (Clock - BotGripAt) / BotBackswingSeconds);
		}
		if (PhoneCharge > PhoneSwingCharge)
		{
			// A phone's swing: the backswing grows with its energy.
			return BackswingStart * Smooth(0.0, PhoneBackswingRamp, PhoneCharge) - PhoneBackswing * PhoneCharge;
		}
		return 0.0;
	case EBowlingPhase::Delivery:
		return FMath::Lerp(SwingAtRelease, ReleaseArm, Smooth(0.0, 1.0, Age / DeliverySeconds));
	case EBowlingPhase::Rolling:
		return FMath::Lerp(ReleaseArm, FollowThroughArm, Smooth(0.0, 1.0, Age / FollowThroughSeconds));
	case EBowlingPhase::Settling:
		return FollowThroughArm;
	default:
		return 0.0;
	}
}
