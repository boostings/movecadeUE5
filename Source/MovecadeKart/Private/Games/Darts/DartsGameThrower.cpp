// ADartsGame: the thrower's avatar on the oche (stance, wind-up, the throw, reactions) and the dart in their hand.

#include "Games/Darts/DartsGame.h"

#include "Avatar/ArcadeAvatar.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/DartsPub.h"

using namespace MovecadeKart::Darts;
using MovecadeKart::Darts::Tuning::Smooth;
using MovecadeKart::Darts::Tuning::ToWorld;

namespace
{
	/** DartsAvatar.tsx: the thrower turns this far (degrees) past facing the board, side-on at the oche. */
	constexpr double StanceYawDegrees = 6.0;
	/** The feet sit on the oche mat. */
	constexpr double MatHeight = 0.024;

	// The throw: a snap forward from the release (the dart leaves at the cheek, the arm chases it), then the
	// follow-through held and eased back.
	constexpr double SnapSeconds = 0.09;
	constexpr double FollowThroughDecaySeconds = 0.2;
	constexpr double ThrowPoseSeconds = 0.7;
	/** The pose counts as active while the flick is above this (it fades exponentially). */
	constexpr double ActiveFlick = 0.02;
	/** A local hand draws back with the meter: this much at the grip, plus this much more at full power. */
	constexpr double GripWindup = 0.35;
	constexpr double GripWindupPower = 0.65;
	/** A bot's wind-up finishes this long before its release. */
	constexpr double BotWindupEndLead = 0.05;

	// The reaction after a landing: in over ReactInSeconds, held, out between ReactOutStart and ReactSeconds.
	constexpr double ReactSeconds = 1.5;
	constexpr double ReactOutStart = 1.2;
	constexpr double ReactInSeconds = 0.15;
	/** Points that earn a small cheer (a fat treble or better). */
	constexpr int32 GoodDartPoints = 40;
	/** How much a landing cheers or slumps: the bull or a bounty, a good dart, a miss. */
	constexpr float BigHitCheer = 1.f;
	constexpr float GoodDartCheer = 0.45f;
	constexpr float MissSlump = 0.7f;
	/** At the results everyone half-cheers; the winner cheers properly. */
	constexpr float ResultsCheer = 0.35f;
	constexpr float WinnerCheer = 0.8f;

	/** The held dart shows once the throwing arm has recovered from the last throw, until the next one flies. */
	constexpr double HeldDartAfterReleaseSeconds = 0.32;
	/** The held dart sits this far (cm) out of the hand toward the aim point, nose tipped this much down. */
	constexpr double HeldDartReachCm = 14.0;
	constexpr double HeldDartPitchDegrees = 10.0;
	/** The look target is taken from roughly the avatar's eyes, this far above its root (cm). */
	constexpr double EyeHeightCm = 150.0;

	/**
	 * The throwing pose (DartsAvatar.tsx's keyframes, avatar-local metres): the dart hand by the cheek, drawn back
	 * with the wind-up, punched out along the line by the flick, and the body's lean and twist into the throw.
	 */
	struct FThrowPoseRecipe
	{
		// The throwing hand: rest, then per unit of wind-up, flick and aim.
		double HandX = 0.42;
		double HandWindupBack = 0.24;
		double HandFlickOut = 0.6;
		double HandY = 0.16;
		double HandAimSide = 0.17;
		double HandZ = 0.42;
		double HandAimUp = 0.2;
		double HandWindupUp = 0.09;
		double HandFlickDown = 0.12;
		/** Aiming high drives the throw: a floor plus this much per unit of vertical aim, capped at 1. */
		double DriveBase = 0.28;
		double DriveAimUp = 0.38;
		// Lean and twist while active, and the lean at rest.
		double Lean = 0.06;
		double LeanDrive = 0.1;
		double LeanFlick = 0.14;
		double LeanWindup = 0.03;
		double RestLean = 0.02;
		double Twist = -0.18;
		double TwistWindup = 0.06;
		double TwistFlick = 0.08;
		float Crouch = 0.04f;
		/** The other hand out for balance, at this weight while active. */
		FVector3d OffHand = FVector3d(0.25, -0.35, -0.55);
		float OffHandWeight = 0.45f;
		/** The eyes go to half the aim (and a touch above), firmly while active. */
		double LookAimShare = 0.5;
		double LookAbove = 0.12;
		float LookWeight = 0.85f;
		float RestLookWeight = 0.5f;
	};
	const FThrowPoseRecipe ThrowPose;

	const FName NameRightHand(TEXT("rightHand"));

	/** How far through the throw's flick the arm is, 0..1, Since seconds after the release. */
	double FlickAt(double Since)
	{
		if (Since < 0.0 || Since > ThrowPoseSeconds)
		{
			return 0.0;
		}
		return Since < SnapSeconds ? Smooth(0.0, SnapSeconds, Since) : FMath::Exp(-(Since - SnapSeconds) / FollowThroughDecaySeconds);
	}

	/** The cheer or slump a landing is worth (before the reaction's envelope). */
	void ReactionTo(const FDartLanding& Landing, float& OutCheer, float& OutSlump)
	{
		OutCheer = 0.f;
		OutSlump = 0.f;
		if (Landing.Hit.Ring == ERing::Bull || Landing.bBounty)
		{
			OutCheer = BigHitCheer;
		}
		else if (Landing.ScoreDelta == 0)
		{
			OutSlump = MissSlump;
		}
		else if (Landing.ScoreDelta >= GoodDartPoints)
		{
			OutCheer = GoodDartCheer;
		}
	}
}

void ADartsGame::UpdateThrower()
{
	// DartsAvatar.tsx: on the oche left of the board's line, facing the board.
	const FVector Feet = ToWorld(FVector(ADartsPub::ThrowerX, ADartsPub::FloorY + MatHeight, ADartsPub::OcheZ));
	const FVector Board = ToWorld(FVector(0.0, ADartsPub::BoardY, 0.0));
	const FRotator Facing(0.0, (Board - Feet).Rotation().Yaw + StanceYawDegrees, 0.0);
	Avatar->SetWorldLocationAndRotation(Feet, Facing);
	FArcadePoseInput& Pose = Avatar->Pose();

	FVector2D Aim = FVector2D::ZeroVector;
	const bool bAiming = GetReticle(Aim);
	const FDartsSeat* Thrower = GetThrower();
	const bool bBotAiming = Thrower && Thrower->Kind == EThrowerKind::Bot && Stage == EDartsStage::Match && Match.Phase == EMatchPhase::Playing
		&& Match.TurnDarts < DartsPerRound;
	const double Since = Clock - LastReleaseAt;
	const double Flick = FlickAt(Since);
	// Bots draw back over the wind-up before they throw; a local hand draws back with the meter.
	const double Windup = Local.bGripped ? GripWindup + GripWindupPower * Local.Power
		: (bBotAiming ? Smooth(BotReadyAt - Tuning::BotWindupSeconds, BotReadyAt - BotWindupEndLead, Clock) : 0.0);
	const FThrowPoseRecipe& R = ThrowPose;
	// Aiming high leans into the throw.
	const double Drive = FMath::Min(1.0, R.DriveBase + FMath::Abs(Aim.Y) * R.DriveAimUp);
	const bool bActive = bAiming || bBotAiming || Flick > ActiveFlick;

	Pose.Hand[1] = FVector(R.HandX - R.HandWindupBack * Windup + R.HandFlickOut * Flick, R.HandY + Aim.X * R.HandAimSide,
		R.HandZ + Aim.Y * R.HandAimUp + R.HandWindupUp * Windup - R.HandFlickDown * Flick);
	Pose.HandWeight[1] = bActive ? 1.f : 0.f;
	Pose.Hand[0] = R.OffHand;
	Pose.HandWeight[0] = bActive ? R.OffHandWeight : 0.f;
	Pose.Lean = static_cast<float>(bActive ? R.Lean + R.LeanDrive * Drive + R.LeanFlick * Flick - R.LeanWindup * Windup : R.RestLean);
	Pose.Twist = static_cast<float>(bActive ? R.Twist - R.TwistWindup * Windup + R.TwistFlick * Flick : 0.0);
	Pose.Crouch = R.Crouch;
	const FVector LookAt = ToWorld(ADartsPub::BoardPoint(Aim.X * R.LookAimShare, Aim.Y * R.LookAimShare + R.LookAbove));
	Pose.Look = Facing.UnrotateVector((LookAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, EyeHeightCm)).GetSafeNormal());
	Pose.LookWeight = bActive ? R.LookWeight : R.RestLookWeight;

	// Reactions: the bull and the bounty cheer, a miss slumps, the end wins or loses.
	const double SinceLanding = Clock - LastLandingAt;
	const double React = SinceLanding < ReactSeconds ? Smooth(0.0, ReactInSeconds, SinceLanding) * Smooth(ReactSeconds, ReactOutStart, SinceLanding) : 0.0;
	float Cheer = 0.f, Slump = 0.f;
	ReactionTo(LastLanding, Cheer, Slump);
	const bool bResults = Stage == EDartsStage::Finished;
	if (bResults)
	{
		const bool bWon = Thrower ? Match.WinnerIds.Contains(Thrower->Id) : false;
		Cheer = FMath::Max(Cheer, bWon ? WinnerCheer : 0.f);
	}
	Pose.Cheer = Cheer * static_cast<float>(React) + (bResults ? ResultsCheer : 0.f);
	Pose.Slump = Slump * static_cast<float>(React);
	Pose.Idle = 1.f;

	// The held dart, in the throwing hand, pointing at the board, while the hand has one to throw.
	UpdateHeldDart(bActive && Since > HeldDartAfterReleaseSeconds && Match.Flights.Num() == 0, Aim);
}

void ADartsGame::UpdateHeldDart(bool bHeld, const FVector2D& Aim)
{
	HeldDart->SetVisibility(bHeld);
	if (!bHeld)
	{
		return;
	}
	FVector HandWorld = Avatar->GetComponentLocation();
	if (USkeletalMeshComponent* Mesh = Avatar->GetMesh())
	{
		if (HandBone == INDEX_NONE)
		{
			HandBone = Mesh->GetBoneIndex(NameRightHand);
		}
		HandWorld = HandBone != INDEX_NONE ? Mesh->GetBoneTransform(HandBone).GetLocation() : Avatar->BoneWorld(NameRightHand);
	}
	const FVector ToBoard = (ToWorld(ADartsPub::BoardPoint(Aim.X * AimReach, Aim.Y * AimReach)) - HandWorld).GetSafeNormal();
	HeldDart->SetWorldLocationAndRotation(HandWorld + ToBoard * HeldDartReachCm, ToBoard.Rotation() + FRotator(HeldDartPitchDegrees, 0.0, 0.0));
	SetFlightColor(HeldDart, ThrowerSeat);
}
