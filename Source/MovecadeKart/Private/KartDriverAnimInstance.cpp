#include "KartDriverAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "KartDriverAnimProxy.h"

using MovecadeKart::Rig::SolveMiddle;

namespace
{
	// drivingExpression.ts: thighs near horizontal and shins dropping into the footwell; astride a
	// saddle the thighs drop toward the pegs with the knees out round the tank.
	constexpr float SeatedUpperLegPitch = 1.45f;
	constexpr float SeatedLowerLegPitch = -1.35f;
	constexpr float SeatedUpperLegSplay = 0.10f;
	constexpr float StraddleUpperLegPitch = 0.95f;
	constexpr float StraddleLowerLegPitch = -1.3f; // shins back toward the pegs
	constexpr float StraddleUpperLegSplay = 0.42f;
	/** A cockpit driver sits back against the seat; a rider leans into the bars. */
	constexpr float SeatedTorsoRecline = 0.14f;
	constexpr float StraddleTorsoLean = -0.36f;
	/**
	 * The hand mesh reaches past the wrist joint (the rig has no finger bones), so the wrist is pulled back along
	 * the arm by this fraction of the forearm's length, to put the middle of the hand, not the wrist, on the rim.
	 */
	constexpr double HandReach = 0.3;
	/** The grips sit at most this fraction of the arm's full length from the shoulder, so the elbows stay bent. */
	constexpr double GripComfort = 0.9;
	/** How far (radians) the torso may lean in to reach the wheel, in at most this many steps. */
	constexpr float MaxReachLean = 0.5f;
	constexpr int32 ReachLeanSteps = 4;
	/** Cartoon proportions over the avatar4 body: a bigger head (hair, eyes and face come with it) and chunkier hands. */
	constexpr double HeadScale = 1.14;
	constexpr double HandScale = 1.2;
	/** The longest frame the springs step in one go. */
	constexpr float MaxStepSeconds = 1.f / 30.f;
	/** drivingExpression.ts: the torso's roll into the turn, an underdamped spring (lags the wheel, overshoots ~12 %). */
	constexpr float RollPerSteer = 0.12f;
	constexpr float RollPerLateralG = 0.06f;
	constexpr float RollOmega = 11.f;
	constexpr float RollZeta = 0.55f;
	/** The head leads the body into the turn. */
	constexpr float HeadYawPerSteer = 0.42f;
	constexpr float HeadYawSpeed = 15.f;
	/** The hair's spring: lightly damped, so it bounces a couple of times before settling. */
	constexpr float HairOmega = 9.f;
	constexpr float HairZeta = 0.22f;
}

FAnimInstanceProxy* UKartDriverAnimInstance::CreateAnimInstanceProxy()
{
	return new FKartDriverAnimProxy(this);
}

void UKartDriverAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FKartDriverAnimProxy*>(InProxy);
}

// ------------------------------------------------------------------------------------------------- PreUpdate

void FKartDriverAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UKartDriverAnimInstance* Instance = Cast<UKartDriverAnimInstance>(InAnimInstance))
	{
		Input = Instance->Input;
	}
	const float Dt = FMath::Min(DeltaSeconds, MaxStepSeconds);
	StepTorsoAndHead(Dt);
	if (Dt > 0.f)
	{
		StepHair(Dt);
	}
}

void FKartDriverAnimProxy::StepTorsoAndHead(float Dt)
{
	// The body leans into the turn with some mass; the head leads it (drivingExpression.ts).
	const float TargetRoll = Input.Steer * RollPerSteer + Input.LateralG * RollPerLateralG;
	TorsoRollVelocity += (RollOmega * RollOmega * (TargetRoll - TorsoRoll) - 2.f * RollZeta * RollOmega * TorsoRollVelocity) * Dt;
	TorsoRoll += TorsoRollVelocity * Dt;
	HeadYaw = FMath::FInterpTo(HeadYaw, Input.Steer * HeadYawPerSteer, Dt, HeadYawSpeed);
}

void FKartDriverAnimProxy::StepHair(float Dt)
{
	// The airflow streams the hair back with speed and boosts, bumps toss it, corners and the head's turn swing it
	// sideways.
	const float HeadTurnRate = (HeadYaw - LastHeadYaw) / Dt;
	LastHeadYaw = HeadYaw;
	const FVector2f Target(
		0.10f * Input.Moving + 0.22f * Input.Boost - 0.45f * Input.Bounce + 0.5f * Input.JoltBack + 0.25f * Input.Cheer,
		-0.35f * Input.LateralG - 0.45f * Input.JoltSide - 0.2f * Input.DriftSide - 0.04f * HeadTurnRate);
	HairSwingVelocity += (HairOmega * HairOmega * (Target - HairSwing) - 2.f * HairZeta * HairOmega * HairSwingVelocity) * Dt;
	HairSwing += HairSwingVelocity * Dt;
	HairSwing.X = FMath::Clamp(HairSwing.X, -0.6f, 0.8f);
	HairSwing.Y = FMath::Clamp(HairSwing.Y, -0.6f, 0.6f);
}

// -------------------------------------------------------------------------------------------------- Evaluate

bool FKartDriverAnimProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	FCompactPose& Pose = Output.Pose;
	const FBones& B = BoneCache.Get(Pose.GetBoneContainer());
	if (!B.IsPoseable())
	{
		return true; // not an avatar4 rig: keep the reference pose
	}
	MovecadeKart::Rig::LoadPose(Pose, Rig);

	// The avatar's own frame, from where its joints are.
	const FBodyFrame Body(Rig, B);
	const float Bounce = FMath::Clamp(Input.Bounce, -1.f, 1.f);
	PoseLegs(B, Body, Bounce);
	FQuat TorsoDelta = PoseTorso(B, Body, Bounce);

	const float Cheer = FMath::Clamp(Input.Cheer, 0.f, 1.f);
	const float Spin = FMath::Clamp(Input.Spin, 0.f, 1.f);
	const float HandsOff = FMath::Max(Cheer, Spin);
	if (Input.bHasGrip && HandsOff < 0.99f)
	{
		LeanToReach(B, HandsOff, TorsoDelta);
	}
	const FQuat HeadDelta = PoseHead(B, Body, TorsoDelta, Bounce, Cheer, Spin);
	SwingSpringChains(B, Body, TorsoDelta, HeadDelta);
	if (Input.bHasGrip)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			PoseArm(Side, B, Body, Cheer, Spin, HandsOff);
		}
	}

	MovecadeKart::Rig::ScaleHeadAndHands(Rig, B, HeadScale, HandScale);
	MovecadeKart::Rig::StorePose(Rig, Pose);
	return true;
}

void FKartDriverAnimProxy::PoseLegs(const FBones& B, const FBodyFrame& Body, float Bounce)
{
	// Forward under the cowl, or astride the moto; a bump flexes the knees.
	const bool bStraddle = Input.bStraddle;
	const float ThighPitch = (bStraddle ? StraddleUpperLegPitch : SeatedUpperLegPitch) - Bounce * 0.06f;
	const float ShinPitch = ThighPitch + (bStraddle ? StraddleLowerLegPitch : SeatedLowerLegPitch) + Bounce * 0.05f;
	const float Splay = bStraddle ? StraddleUpperLegSplay : SeatedUpperLegSplay;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!B.HasLimb(B.Leg[Side]))
		{
			continue;
		}
		const int32 Upper = B.Leg[Side][0], Lower = B.Leg[Side][1], Foot = B.Leg[Side][2];
		const FVector Out = Side == 0 ? -Body.Right : Body.Right;
		FVector Thigh = Body.Down * FMath::Cos(ThighPitch) + Body.Forward * FMath::Sin(ThighPitch);
		Thigh = Thigh * FMath::Cos(Splay) + Out * FMath::Sin(Splay);
		Rig.Aim(Upper, Lower, Thigh);
		const FVector Shin = Body.Down * FMath::Cos(ShinPitch) + Body.Forward * FMath::Sin(ShinPitch) + Out * (bStraddle ? 0.12f : 0.03f);
		Rig.Aim(Lower, Foot, Shin);
	}
}

FQuat FKartDriverAnimProxy::PoseTorso(const FBones& B, const FBodyFrame& Body, float Bounce)
{
	// Reclined in the seat (or over the bars), rolling into the turn. On top: pressed back by a boost, hunched and
	// buzzing while revving, slumped after a bad finish, whiplash from impacts, and breathing.
	const float Time = Input.Time;
	const float Still = 1.f - FMath::Clamp(Input.Moving * 3.f, 0.f, 1.f);
	const float Breath = FMath::Sin(Time * 1.7f) * 0.018f * Still;
	const float Buzz = FMath::Sin(Time * 17.f) * 0.014f * Input.Rev;
	const float Recline = (Input.bStraddle ? StraddleTorsoLean : 0.f) + SeatedTorsoRecline + 0.05f * Input.Moving - Bounce * 0.05f
		+ 0.16f * Input.Boost - 0.12f * Input.Rev - 0.30f * Input.Slump + 0.35f * Input.JoltBack + Breath + Buzz;
	const float Roll = TorsoRoll + 0.16f * Input.DriftSide + 0.30f * Input.JoltSide;
	FVector TorsoUp = Body.Up * FMath::Cos(Recline) - Body.Forward * FMath::Sin(Recline);
	TorsoUp = TorsoUp * FMath::Cos(Roll) + Body.Right * FMath::Sin(Roll);
	const FQuat TorsoDelta = FQuat::FindBetweenNormals(Body.Up, TorsoUp.GetSafeNormal());
	Rig.Rotate(B.Torso, TorsoDelta);
	return TorsoDelta;
}

void FKartDriverAnimProxy::LeanToReach(const FBones& B, float HandsOff, FQuat& TorsoDelta)
{
	// Lean in until both grips are comfortably within reach, rather than locking the elbows straight toward a wheel
	// that sits past the arm's length (the mounts place the rim for the web's thinner arms).
	float Leaned = 0.f;
	for (int32 Iteration = 0; Iteration < ReachLeanSteps && Leaned < MaxReachLean; ++Iteration)
	{
		double Excess = 0.0;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Upper = B.Arm[Side][0], Fore = B.Arm[Side][1], Hand = B.Arm[Side][2];
			if (Fore == INDEX_NONE || Hand == INDEX_NONE)
			{
				continue;
			}
			const double Length = (Rig.Pos(Fore) - Rig.Pos(Upper)).Size() + (Rig.Pos(Hand) - Rig.Pos(Fore)).Size();
			const FVector Target = Side == 0 ? Input.LeftHand : Input.RightHand;
			Excess = FMath::Max(Excess, (Target - Rig.Pos(Upper)).Size() - Length * GripComfort);
		}
		const FVector Pivot = Rig.Pos(B.Torso);
		const FVector Shoulders = (Rig.Pos(B.Arm[0][0]) + Rig.Pos(B.Arm[1][0])) * 0.5 - Pivot;
		const FVector Grips = (Input.LeftHand + Input.RightHand) * 0.5 - Pivot;
		const FVector Axis = FVector::CrossProduct(Shoulders, Grips).GetSafeNormal();
		const double Lever = Shoulders.Size();
		if (Excess < 0.5 || Axis.IsNearlyZero() || Lever < 1.0)
		{
			break;
		}
		const float Step = FMath::Min(static_cast<float>(Excess / Lever), MaxReachLean - Leaned) * (1.f - HandsOff);
		const FQuat Lean(Axis, Step);
		Rig.Rotate(B.Torso, Lean);
		TorsoDelta = Lean * TorsoDelta;
		Leaned += Step;
	}
}

FQuat FKartDriverAnimProxy::PoseHead(const FBones& B, const FBodyFrame& Body, const FQuat& TorsoDelta, float Bounce, float Cheer, float Spin)
{
	// Eyes back toward level and looking into the turn.
	const FVector& Forward = Body.Forward;
	const FVector& Right = Body.Right;
	const FVector& Up = Body.Up;
	const FVector& Down = Body.Down;
	const FVector Facing = TorsoDelta.RotateVector(Forward);
	FVector Gaze = Forward * FMath::Cos(HeadYaw) + Right * FMath::Sin(HeadYaw);
	Gaze = (Gaze + Down * (0.06f - Bounce * 0.07f) + Up * (0.3f * Cheer + 0.12f * Input.Boost) + Down * 0.45f * Input.Slump).GetSafeNormal();
	if (Input.LookWeight > 0.f && !Input.LookAt.IsNearlyZero())
	{
		// A glance at a rival alongside, never fully over the shoulder: the target is pulled toward the front.
		const FVector ToTarget = (Input.LookAt - Rig.Pos(B.Head)).GetSafeNormal();
		const FVector Glance = (ToTarget + Forward * 0.45f).GetSafeNormal();
		Gaze = FMath::Lerp(Gaze, Glance, FMath::Clamp(Input.LookWeight, 0.f, 1.f)).GetSafeNormal();
	}
	if (Spin > 0.f)
	{
		// Dizzy: the head rolls round in a loose circle.
		const float Time = Input.Time;
		Gaze = (Gaze + (Right * FMath::Cos(Time * 9.f) + Up * FMath::Sin(Time * 9.f)) * 0.4f * Spin).GetSafeNormal();
	}
	const FQuat HeadDelta = FQuat::FindBetweenNormals(Facing, FMath::Lerp(Facing, Gaze, 0.85f).GetSafeNormal());
	if (B.Neck != INDEX_NONE)
	{
		Rig.Rotate(B.Neck, FQuat::Slerp(FQuat::Identity, HeadDelta, 0.4f));
	}
	Rig.Rotate(B.Head, HeadDelta);
	return HeadDelta;
}

void FKartDriverAnimProxy::SwingSpringChains(const FBones& B, const FBodyFrame& Body, const FQuat& TorsoDelta, const FQuat& HeadDelta)
{
	// Each joint takes a share of the swing about the head's (hair) or hips' (hems) own axes, so the tips curl furthest.
	const FQuat HeadFrame = HeadDelta * TorsoDelta;
	auto Swing = [this](const TArray<int32, TInlineAllocator<8>>& Chain, const FVector& Side, const FVector& Ahead, float Pitch, float RollAngle)
	{
		for (const int32 Bone : Chain)
		{
			Rig.Rotate(Bone, FQuat(Ahead, RollAngle) * FQuat(Side, -Pitch));
		}
	};
	const float PerJoint = B.Hair.Num() > 0 ? 1.f / FMath::Sqrt(static_cast<float>(B.Hair.Num())) : 0.f;
	Swing(B.Hair, HeadFrame.RotateVector(Body.Right), HeadFrame.RotateVector(Body.Forward), HairSwing.X * PerJoint, HairSwing.Y * PerJoint);
	Swing(B.Hem, TorsoDelta.RotateVector(Body.Right), TorsoDelta.RotateVector(Body.Forward), 0.15f * HairSwing.X, HairSwing.Y * 0.3f);
}

void FKartDriverAnimProxy::PoseArm(int32 Side, const FBones& B, const FBodyFrame& Body, float Cheer, float Spin, float HandsOff)
{
	// Two-bone IK onto the grip, elbow out and down; a cheer, an item or a spin takes the hand off the wheel.
	const int32 Upper = B.Arm[Side][0], Fore = B.Arm[Side][1], Hand = B.Arm[Side][2];
	if (Fore == INDEX_NONE || Hand == INDEX_NONE)
	{
		return;
	}
	const FVector& Forward = Body.Forward;
	const FVector& Right = Body.Right;
	const FVector& Up = Body.Up;
	const FVector& Down = Body.Down;
	const float Time = Input.Time;
	const FVector Shoulder = Rig.Pos(Upper);
	const double L1 = (Rig.Pos(Fore) - Shoulder).Size();
	const double L2 = (Rig.Pos(Hand) - Rig.Pos(Fore)).Size();
	FVector Target = Side == 0 ? Input.LeftHand : Input.RightHand;
	// Hold the rim with the hand, not the wrist: back off along the reach by the hand's length.
	Target -= (Target - Shoulder).GetSafeNormal() * L2 * HandReach;
	if (Side == 1 && Input.ItemRaise > 0.f)
	{
		// Item use takes the right hand off the wheel, up and out (drivingExpression.ts).
		const FVector Raised = Shoulder + (Up * 0.8f + Right * 0.35f + Forward * 0.25f).GetSafeNormal() * (L1 + L2) * 0.95;
		Target = FMath::Lerp(Target, Raised, FMath::Clamp(Input.ItemRaise, 0.f, 1.f));
	}
	const FVector Out = Side == 0 ? -Right : Right;
	if (Cheer > 0.f)
	{
		// Both arms up in a V, waving out of phase.
		const float Wave = FMath::Sin(Input.Time * 7.f + Side * UE_PI) * 0.18f;
		const FVector Raised = Shoulder + (Up * 0.85f + Out * (0.38f + Wave) + Forward * 0.12f).GetSafeNormal() * (L1 + L2) * 0.93;
		Target = FMath::Lerp(Target, Raised, Cheer);
	}
	if (Spin > 0.f)
	{
		// Flailing: each hand circles up and out, out of phase with the other.
		const float Phase = Time * 13.f + Side * 2.4f;
		const FVector Flail = Up * (0.55f + 0.25f * FMath::Sin(Phase)) + Out * (0.75f + 0.2f * FMath::Cos(Phase)) + Forward * (0.15f + 0.3f * FMath::Sin(Phase * 0.7f));
		Target = FMath::Lerp(Target, Shoulder + Flail.GetSafeNormal() * (L1 + L2) * 0.9, Spin);
	}
	const FVector Pole = Out * 0.6f + Down * 0.7f - Forward * 0.3f;
	const FVector Elbow = SolveMiddle(Shoulder, Target, L1, L2, Pole);
	Rig.Aim(Upper, Fore, Elbow - Shoulder);
	Rig.Aim(Fore, Hand, Target - Rig.Pos(Fore));
	// Fingers wrapped round the rim; open as the hand leaves it (a wave, an item, a flail).
	const float Off = FMath::Max(HandsOff, Side == 1 ? FMath::Clamp(Input.ItemRaise, 0.f, 1.f) : 0.f);
	MovecadeKart::Rig::CurlFingers(Rig, Hand, B.Fingers[Side], B.Thumb[Side], FMath::Lerp(0.9f, 0.15f, Off));
}

namespace MovecadeKart::DriverRig
{
	bool ComputeBasis(const FReferenceSkeleton& Skeleton, FVector& OutForward, FVector& OutRight, FVector& OutUp)
	{
		const TArray<FTransform>& Local = Skeleton.GetRefBonePose();
		TArray<FTransform> CS;
		CS.SetNum(Local.Num());
		for (int32 i = 0; i < Local.Num(); ++i)
		{
			const int32 Parent = Skeleton.GetParentIndex(i);
			CS[i] = Parent == INDEX_NONE ? Local[i] : Local[i] * CS[Parent];
		}
		const int32 Torso = Skeleton.FindBoneIndex(TEXT("torso"));
		const int32 Head = Skeleton.FindBoneIndex(TEXT("head"));
		const int32 Left = Skeleton.FindBoneIndex(TEXT("leftUpperArm"));
		const int32 Right = Skeleton.FindBoneIndex(TEXT("rightUpperArm"));
		if (Torso == INDEX_NONE || Head == INDEX_NONE || Left == INDEX_NONE || Right == INDEX_NONE)
		{
			return false;
		}
		OutUp = (CS[Head].GetLocation() - CS[Torso].GetLocation()).GetSafeNormal();
		const FVector Side = CS[Right].GetLocation() - CS[Left].GetLocation();
		OutRight = (Side - OutUp * FVector::DotProduct(Side, OutUp)).GetSafeNormal();
		OutForward = FVector::CrossProduct(OutRight, OutUp);
		return true;
	}
}
