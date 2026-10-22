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

	/** The bowling arm's reach from the shoulder, in the avatar's arm units. */
	constexpr double ArmReach = 0.97;
	/** The eyes sit this far above the avatar's origin (UE cm). */
	constexpr double EyeHeightCm = 160.0;
	constexpr float LookWeight = 0.75f;
	/** A non-celebration reaction holds this long (a celebration holds for CelebrationSeconds). */
	constexpr double ReactionHoldSeconds = 1.8;

	/** The arms for a swing of Arm radians: the bowling hand's pendulum, the free hand balancing (or cradling). */
	void PoseArms(FArcadePoseInput& Pose, double Arm, bool bAtAddress, bool bThrown)
	{
		Pose.Hand[1] = FVector(FMath::Sin(Arm) * ArmReach, 0.12, -FMath::Cos(Arm) * ArmReach);
		Pose.HandWeight[1] = 1.f;
		if (FMath::Abs(Arm) < RestingArm && bAtAddress)
		{
			// The other hand cradles the ball until the swing starts.
			Pose.Hand[1] = FVector(0.5, -0.05, -0.38);
			Pose.Hand[0] = FVector(0.5, 0.24, -0.4);
			Pose.HandWeight[0] = 1.f;
			return;
		}
		// Balance: the free arm swings out the other way.
		Pose.Hand[0] = FVector(-FMath::Sin(Arm) * 0.4, -0.55, -0.7);
		Pose.HandWeight[0] = bThrown ? 0.85f : 0.4f;
	}

	/** The body through the throw: crouching into the backswing, lunging at the release, the approach's steps. */
	void PoseBody(FArcadePoseInput& Pose, double Arm, double Grip, bool bThrown, double SinceRelease)
	{
		const bool bApproach = Grip >= 0.0;
		const double Charge = Arm < 0.0 ? FMath::Clamp(-Arm / FullBackswing, 0.0, 1.0) : 0.0;
		const double Settle = bThrown ? 1.0 - Smooth(1.0, 2.6, SinceRelease) : 0.0;
		Pose.Crouch = static_cast<float>(0.15 + 0.3 * Charge + 0.35 * Settle);
		Pose.Lean = static_cast<float>(0.12 + 0.25 * Charge + 0.4 * Settle);
		Pose.Lunge = static_cast<float>(bThrown ? Smooth(0.0, 0.25, SinceRelease) * (0.4 + 0.6 * Settle) : 0.0);
		// The approach steps through the backswing (they stop once the bowler reaches the line), then the slide.
		const double Walking = bApproach ? 0.75 * (1.0 - Smooth(0.7, 0.95, Grip)) : 0.0;
		Pose.Step = static_cast<float>(bThrown ? 0.8 * (1.0 - Smooth(0.0, 0.45, SinceRelease)) : Walking);
		Pose.StepPhase = static_cast<float>(bThrown ? SinceRelease * 12.0 : FMath::Max(0.0, Grip) * 9.0);
		Pose.Twist = static_cast<float>(-0.12 * Charge);
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
	const FVector LookAt = Phase == EBowlingPhase::Rolling ? Ball->GetComponentLocation() : LaneToWorld(0.0, 0.4, ABowlingAlley::DeckZ);
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
		StanceZ = FMath::Lerp(ReleaseZ, SlideZ, Smooth(0.0, 0.55, SinceRelease));
	}
	else if (Phase == EBowlingPhase::Reset)
	{
		// Back up to the address while the pinsetter works.
		StanceZ = FMath::Lerp(SlideZ, AddressZ, Smooth(0.0, 1.0, GetPhaseAge() / ResetSeconds));
	}
	else
	{
		StanceZ = Grip >= 0.0 ? FMath::Lerp(AddressZ, ApproachZ, Smooth(0.0, 0.85, Grip)) : AddressZ;
	}
	Avatar->SetWorldLocationAndRotation(LaneToWorld(StanceX, 0.0, StanceZ), FRotator::ZeroRotator);
}

void ABowlingGame::PoseReaction(FArcadePoseInput& Pose) const
{
	// A strike pumps both fists and jumps, a spare cheers, a gutter slumps; each eases in and out over its hold.
	const double Since = Clock - LastBeatAt;
	const double Hold = IsCelebration(LastBeat) ? CelebrationSeconds : ReactionHoldSeconds;
	const double React = Since < Hold ? FMath::Clamp(Since / 0.2, 0.0, 1.0) * FMath::Clamp((Hold - Since) / 0.3, 0.0, 1.0) : 0.0;
	float Cheer = 0.f, Slump = 0.f, Lift = 0.f;
	switch (LastBeat)
	{
	case EBowlingBeat::Strike:
	case EBowlingBeat::Turkey:
		Cheer = 1.f;
		Lift = static_cast<float>((LastBeat == EBowlingBeat::Turkey ? 0.2 : 0.14) * FMath::Abs(FMath::Sin(Since * 7.5)));
		break;
	case EBowlingBeat::Spare:
	case EBowlingBeat::SplitConvert:
		Cheer = 0.75f;
		Lift = static_cast<float>(0.06 * FMath::Abs(FMath::Sin(Since * 6.0)));
		break;
	case EBowlingBeat::Gutter:
	case EBowlingBeat::Split:
		Slump = 1.f;
		break;
	case EBowlingBeat::Open:
		// Only a poor ball slumps.
		Slump = LastFelled <= 3 ? 0.6f : 0.f;
		break;
	default:
		break;
	}
	// Once the game is over the bowler on the approach keeps a little cheer, whatever the last ball did.
	Pose.Cheer = Cheer * static_cast<float>(React) + (Phase == EBowlingPhase::Finished ? 0.4f : 0.f);
	Pose.Slump = Slump * static_cast<float>(React);
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
			return BackswingStart * Smooth(0.0, 0.15, PhoneCharge) - PhoneBackswing * PhoneCharge;
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
