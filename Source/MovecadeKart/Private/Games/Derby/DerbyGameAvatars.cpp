// ADerbyGame's two avatars, posed every frame through UArcadeAvatarComponent's pose input: the pitcher's set,
// windup, stride and follow-through, and the batter's stance, load, swing, and the call (a fist pump, a slump).
// Hand targets are relative to their shoulders in reach units (Avatar/ArcadeAvatar.h); lengths are metres.

#include "Games/Derby/DerbyGame.h"

#include "Avatar/ArcadeAvatar.h"
#include "Components/StaticMeshComponent.h"
#include "Games/Derby/DerbyStage.h"

using namespace MovecadeKart::Derby;
using namespace MovecadeKart::Derby::Stage;

namespace
{
	const FName LeftHand(TEXT("leftHand"));
	const FName RightHand(TEXT("rightHand"));

	/** The pitcher's keyframes: the glove hand and the throwing hand at each beat of the delivery. */
	namespace PitcherKeys
	{
		/** The set: hands together at the chest. */
		const FVector SetGlove(0.45, 0.32, -0.38);
		const FVector SetThrow(0.45, -0.3, -0.42);
		constexpr float SetCrouch = 0.12f;
		constexpr float SetLean = 0.12f;
		/** Rock and lift (the windup's first 0.05-0.4, gone by 0.45-0.6): hands up over the cap, the body turned away. */
		const FVector LiftGlove(0.3, 0.3, 0.55);
		const FVector LiftThrow(0.3, -0.25, 0.5);
		constexpr double LiftTwist = -0.55;
		constexpr double LiftCrouchFrom = 0.12;
		constexpr double LiftCrouchDrop = 0.08;
		/** Stride (0.45-0.8): the glove out front, the ball back and up. */
		const FVector StrideGlove(0.85, 0.05, 0.05);
		const FVector StrideThrow(-0.75, -0.35, 0.25);
		constexpr double StrideLunge = 0.75;
		constexpr float StrideTwist = -0.35f;
		/** Whip (0.82-1): over the top. */
		const FVector WhipThrow(0.35, -0.2, 0.85);
		const FVector WhipGlove(0.3, 0.45, -0.3);
		constexpr float WhipTwist = 0.35f;
		constexpr double WhipLeanFrom = 0.12;
		constexpr double WhipLean = 0.35;
		/** The follow-through (F 1 at the release, easing to 0 over 0.35-1.1 s): from the finish back to fielding. */
		const FVector FieldThrow(0.4, -0.3, -0.4);
		const FVector FinishThrow(0.85, 0.35, -0.75);
		const FVector FieldGlove(0.45, 0.32, -0.3);
		const FVector FinishGlove(0.2, 0.55, -0.35);
		constexpr double FieldLean = 0.15;
		constexpr double FinishLean = 0.5;
		constexpr double FinishTwist = 0.45;
		constexpr double FinishLunge = 0.75;
		constexpr double FieldCrouch = 0.2;
		constexpr double FinishCrouch = 0.2;
		/** Watching the hit go: arms loose; a home run slumps him. */
		constexpr float WatchHandWeight = 0.2f;
		constexpr float WatchCrouch = 0.1f;
		constexpr float HomeRunSlump = 0.7f;
		/** He follows through for this long into a hit before turning to watch. */
		constexpr double FollowIntoFlightSeconds = 0.6;
		/** His eyes, above his feet (cm), and where he looks at the plate (metres from the plate's front). */
		const FVector EyeCm(0.0, 0.0, 160.0);
		const FVector PlateLookM(0.0, -0.3, 0.3);
		constexpr float LookWeight = 0.8f;
		/** The glove sits on the left hand, turned square to the body. */
		const FRotator GloveTurn(0.0, -90.0, 0.0);
	}

	/**
	 * The batter's swing, as the barrel's angle round the body: A, degrees in the batter's frame (0 back toward the
	 * catcher, 90 out over the plate, 180 toward the pitcher), and its elevation E.
	 */
	namespace BatterKeys
	{
		/** The stance: the barrel up over the back shoulder, tipped toward the catcher. */
		constexpr double StanceA = -28.0;
		constexpr double StanceE = 58.0;
		/** The swing to contact (A eases in, squared) and from contact to the finish (eases out). */
		constexpr double StartA = -25.0;
		constexpr double StartE = 68.0;
		constexpr double ContactA = 90.0;
		constexpr double ContactE = -3.0;
		constexpr double FinishA = 245.0;
		constexpr double FinishE = 40.0;
		/** The finish is held, then the bat settles back into the stance between these seconds past the swing. */
		constexpr double SettleFrom = 0.9;
		constexpr double SettleTo = 1.6;
		/** The champion's bat flip: up over the shoulder. */
		constexpr double FlipA = 250.0;
		constexpr double FlipE = 45.0;
		/** A loaded stance pulls the barrel back and up; an idle one twitches (degrees, rad/s). */
		constexpr double LoadPullA = 16.0;
		constexpr double LoadLiftE = 8.0;
		constexpr double WaggleA = 4.0;
		constexpr double WaggleRate = 2.3;
		/** A phone's live swing energy reads as a load at twice its value; otherwise the bat loads through the windup's end. */
		constexpr double PhoneLoadGain = 2.0;
		constexpr double WindupLoad = 0.7;
		constexpr double WindupLoadFrom = 0.6;

		/** The hands circle a pivot at the chest: close in when the bat is up, out in front at contact. */
		const FVector HandsPivot(0.14, -0.04, 1.08);
		constexpr double HandsRadiusMin = 0.08;
		constexpr double HandsRadiusFlat = 0.34;
		constexpr double HandsRise = 0.34;
		/** Each hand's grip along the barrel from the knob. */
		constexpr double LowerGrip = 0.02;
		constexpr double UpperGrip = 0.11;
		const FVector LeftShoulder(0.0, -0.19, 1.46);
		const FVector RightShoulder(0.0, 0.19, 1.46);
		constexpr double ArmReach = 0.52;
		/** The torso turns with the barrel (A from -25 through 245 is the full turn), less when coiled. */
		constexpr double TurnFromA = 25.0;
		constexpr double TurnSpanA = 270.0;
		constexpr double BaseTwist = -0.22;
		constexpr double CoilTwist = 0.12;
		constexpr double BaseCrouch = 0.28;
		constexpr double CoilCrouch = 0.1;
		constexpr double BaseLean = 0.2;
		constexpr double CoilLean = 0.06;
		/** The stride: the front foot goes on the swing (in -0.1-0.05 s), comes back after the settle (0.9-1.8 s). */
		constexpr double StrideLunge = 0.35;
		/** His eyes above his feet (cm). */
		const FVector EyeCm(0.0, 0.0, 150.0);
		constexpr float LookWeight = 0.85f;
		/** The call: a home run's fist pump (it bounces at this rate) or a strikeout's slump, in over this long. */
		constexpr double CallIn = 0.25;
		constexpr double PumpLift = 0.12;
		constexpr double PumpRate = 7.0;
		constexpr double ChampionLift = 0.1;
		constexpr double ChampionRate = 6.0;
		/** Cheering hands drop the bat: it lies on the grass by the plate. */
		constexpr float DropBatCheer = 0.4f;
		const FVector DroppedBatM(0.6, -1.7, 0.04);
		const FRotator DroppedBatRotation(0.0, 25.0, 90.0);
		/** The bat's knob sits this far below the hands (cm). */
		constexpr double KnobBelowHandsCm = 9.0;
	}
}

void ADerbyGame::UpdatePitcher()
{
	using namespace PitcherKeys;
	FArcadePoseInput& Pose = Pitcher->Pose();
	Pose.Idle = 1.f;
	Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
	Pose.Lunge = 0.f;
	Pose.Lift = 0.f;
	Pose.Step = 0.f;
	Pose.Cheer = 0.f;
	Pose.Slump = 0.f;
	FVector Glv = SetGlove, Throw = SetThrow;
	float Crouch = SetCrouch, Lean = SetLean, Twist = 0.f, Lunge = 0.f;
	if (Phase == EDerbyPhase::Windup)
	{
		const double W = FMath::Clamp(GetPhaseAge() / (Pitch.WindupMs / 1000.0), 0.0, 1.0);
		const double Lift = FMath::SmoothStep(0.05, 0.4, W) * (1.0 - FMath::SmoothStep(0.45, 0.6, W));
		Glv = FMath::Lerp(Glv, LiftGlove, Lift);
		Throw = FMath::Lerp(Throw, LiftThrow, Lift);
		Twist = static_cast<float>(LiftTwist * Lift);
		Crouch = static_cast<float>(LiftCrouchFrom - LiftCrouchDrop * Lift);
		const double Stride = FMath::SmoothStep(0.45, 0.8, W);
		Glv = FMath::Lerp(Glv, StrideGlove, Stride);
		Throw = FMath::Lerp(Throw, StrideThrow, Stride);
		Lunge = static_cast<float>(StrideLunge * Stride);
		Twist = FMath::Lerp(Twist, StrideTwist, static_cast<float>(Stride));
		const double Whip = FMath::SmoothStep(0.82, 1.0, W);
		Throw = FMath::Lerp(Throw, WhipThrow, Whip);
		Glv = FMath::Lerp(Glv, WhipGlove, Whip);
		Twist = FMath::Lerp(Twist, WhipTwist, static_cast<float>(Whip));
		Lean = static_cast<float>(WhipLeanFrom + WhipLean * Whip);
	}
	else if (Phase == EDerbyPhase::Pitch || (Phase == EDerbyPhase::Flight && GetPhaseAge() < FollowIntoFlightSeconds))
	{
		// The follow-through, then square up to field.
		const double F = 1.0 - FMath::SmoothStep(0.35, 1.1, Clock - PitchReleasedAt);
		Throw = FMath::Lerp(FieldThrow, FinishThrow, F);
		Glv = FMath::Lerp(FieldGlove, FinishGlove, F);
		Lean = static_cast<float>(FieldLean + FinishLean * F);
		Twist = static_cast<float>(FinishTwist * F);
		Lunge = static_cast<float>(FinishLunge * F);
		Crouch = static_cast<float>(FieldCrouch + FinishCrouch * F);
	}
	else if (Phase == EDerbyPhase::Flight || (Phase == EDerbyPhase::Result && HasHitFlight()))
	{
		// Turns to watch it go.
		Pose.HandWeight[0] = Pose.HandWeight[1] = WatchHandWeight;
		Crouch = WatchCrouch;
		if (Phase == EDerbyPhase::Result && IsHomeRun(Outcome.Result))
		{
			Pose.Slump = HomeRunSlump;
		}
	}
	Pose.Hand[0] = Glv;
	Pose.Hand[1] = Throw;
	Pose.Crouch = Crouch;
	Pose.Lean = Lean;
	Pose.Twist = Twist;
	Pose.Lunge = Lunge;
	// Eyes on the plate, or on the ball once it is hit.
	const FVector Eye = Pitcher->GetComponentLocation() + EyeCm;
	const FVector Target = Phase == EDerbyPhase::Flight ? Ball->GetComponentLocation() : ToCm(PlateM + PlateLookM);
	Pose.Look = Pitcher->GetComponentTransform().InverseTransformVectorNoScale(Target - Eye).GetSafeNormal();
	Pose.LookWeight = LookWeight;
	Glove->SetWorldLocationAndRotation(Pitcher->BoneWorld(LeftHand), Pitcher->GetComponentRotation() + GloveTurn);
}

double ADerbyGame::WaitingLoad() const
{
	using namespace BatterKeys;
	// The load: a held button, a phone's waggle, or else the bat cocking with the pitcher's windup.
	const FDerbySeat* Up = GetBatter();
	if (Up && Up->Kind == EDerbyKind::Local && Local.bLoading)
	{
		return Local.Load;
	}
	if (Up && Up->Kind == EDerbyKind::Phone)
	{
		return FMath::Min(1.0, PhoneCharge * PhoneLoadGain);
	}
	if (Phase == EDerbyPhase::Windup)
	{
		return FMath::SmoothStep(WindupLoadFrom, 1.0, GetPhaseAge() / (Pitch.WindupMs / 1000.0)) * WindupLoad;
	}
	return Phase == EDerbyPhase::Pitch ? WindupLoad : 0.0;
}

void ADerbyGame::BatAngles(double& OutA, double& OutE, double& OutCoil) const
{
	using namespace BatterKeys;
	OutA = StanceA;
	OutE = StanceE;
	OutCoil = 0.0;
	const double U = Clock - SwingAnimAt;
	const double Contact = Tuning.SwingLeadSeconds;
	const double Swing = Tuning.SwingSeconds;
	if (Phase == EDerbyPhase::Finished)
	{
		OutA = FlipA;
		OutE = FlipE;
	}
	else if (U >= 0.0 && U < Swing + SettleTo)
	{
		if (U < Contact)
		{
			const double K = U / Contact;
			OutA = FMath::Lerp(StartA, ContactA, K * K);
			OutE = FMath::Lerp(StartE, ContactE, FMath::SmoothStep(0.0, 1.0, K));
		}
		else if (U < Swing)
		{
			const double K = (U - Contact) / (Swing - Contact);
			OutA = FMath::Lerp(ContactA, FinishA, 1.0 - (1.0 - K) * (1.0 - K));
			OutE = FMath::Lerp(ContactE, FinishE, FMath::SmoothStep(0.0, 1.0, K));
		}
		else
		{
			const double K = FMath::SmoothStep(SettleFrom, SettleTo, U - Swing);
			OutA = FMath::Lerp(FinishA, StanceA, K);
			OutE = FMath::Lerp(FinishE, StanceE, K);
		}
	}
	else
	{
		const double Load = WaitingLoad();
		OutCoil = Load;
		OutA = StanceA - LoadPullA * Load + WaggleA * FMath::Sin(Clock * WaggleRate) * (1.0 - Load);
		OutE = StanceE + LoadLiftE * Load;
	}
}

void ADerbyGame::UpdateBatter()
{
	using namespace BatterKeys;
	FArcadePoseInput& Pose = Batter->Pose();
	Pose.Idle = 1.f;
	Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
	Pose.Cheer = 0.f;
	Pose.Slump = 0.f;
	Pose.Lift = 0.f;
	double A = 0.0, E = 0.0, Coil = 0.0;
	BatAngles(A, E, Coil);
	const double Ar = FMath::DegreesToRadians(A), Er = FMath::DegreesToRadians(E);
	const double Radius = HandsRadiusMin + HandsRadiusFlat * FMath::Cos(Er);
	const FVector Hands = HandsPivot + FVector(FMath::Sin(Ar), FMath::Cos(Ar), 0.0) * Radius + FVector(0.0, 0.0, HandsRise * FMath::Sin(Er));
	const FVector BatDir = FVector(FMath::Sin(Ar) * FMath::Cos(Er), FMath::Cos(Ar) * FMath::Cos(Er), FMath::Sin(Er)).GetSafeNormal();
	const double Turn = FMath::Clamp((A + TurnFromA) / TurnSpanA, 0.0, 1.0);
	const float Twist = static_cast<float>(BaseTwist - CoilTwist * Coil + Turn);
	// Into each shoulder's frame (the torso turns with the twist), in reach.
	const FQuat Untwist(FVector::UpVector, -Twist);
	Pose.Hand[0] = Untwist.RotateVector(Hands + BatDir * LowerGrip - LeftShoulder) / ArmReach;
	Pose.Hand[1] = Untwist.RotateVector(Hands + BatDir * UpperGrip - RightShoulder) / ArmReach;
	Pose.Twist = Twist;
	Pose.Crouch = static_cast<float>(BaseCrouch + CoilCrouch * Coil);
	Pose.Lean = static_cast<float>(BaseLean + CoilLean * Coil);
	const double U = Clock - SwingAnimAt;
	Pose.Lunge = static_cast<float>(StrideLunge * FMath::SmoothStep(-0.1, 0.05, U) * (1.0 - FMath::SmoothStep(0.9, 1.8, U)));
	Pose.Step = 0.f;
	// Eyes on the pitcher, then on the ball.
	const FVector Eye = Batter->GetComponentLocation() + EyeCm;
	const FVector Target = Phase == EDerbyPhase::Pitch || Phase == EDerbyPhase::Flight ? Ball->GetComponentLocation() : ToCm(ReleaseM);
	Pose.Look = Batter->GetComponentTransform().InverseTransformVectorNoScale(Target - Eye).GetSafeNormal();
	Pose.LookWeight = LookWeight;
	PoseBatterCall(Pose);
	PlaceBat(BatDir, Pose.Cheer > DropBatCheer);
}

void ADerbyGame::PoseBatterCall(FArcadePoseInput& Pose) const
{
	using namespace BatterKeys;
	if (Phase == EDerbyPhase::Result && bHasOutcome)
	{
		const double Age = GetPhaseAge();
		const float In = static_cast<float>(FMath::SmoothStep(0.0, CallIn, Age));
		if (IsHomeRun(Outcome.Result))
		{
			Pose.Cheer = In;
			Pose.Lift = static_cast<float>(PumpLift * FMath::Abs(FMath::Sin(Age * PumpRate)) * In);
		}
		else if (Outcome.bStrike && ShownStrikeCount >= StrikesPerOut)
		{
			Pose.Slump = In;
		}
	}
	if (Phase == EDerbyPhase::Finished)
	{
		Pose.Cheer = 1.f;
		Pose.Lift = static_cast<float>(ChampionLift * FMath::Abs(FMath::Sin(GetPhaseAge() * ChampionRate)));
	}
}

void ADerbyGame::PlaceBat(const FVector& BatDirLocal, bool bFlipped)
{
	using namespace BatterKeys;
	if (bFlipped)
	{
		Bat->SetWorldLocationAndRotation(ToCm(DroppedBatM), DroppedBatRotation);
		return;
	}
	// In the hands, along the barrel.
	const FVector Grip = (Batter->BoneWorld(LeftHand) + Batter->BoneWorld(RightHand)) / 2.0;
	const FVector Barrel = Batter->GetComponentTransform().TransformVectorNoScale(BatDirLocal);
	Bat->SetWorldLocationAndRotation(Grip - Barrel * KnobBelowHandsCm, FQuat::FindBetweenNormals(FVector::UpVector, Barrel));
}
