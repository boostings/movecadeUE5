#include "KartDriverAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "BonePose.h"

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

	struct FRigWork
	{
		TArray<FTransform, TInlineAllocator<40>> Local;
		TArray<FTransform, TInlineAllocator<40>> CS;
		TArray<int32, TInlineAllocator<40>> Parent;

		void Recompute()
		{
			for (int32 i = 0; i < Local.Num(); ++i)
			{
				CS[i] = Parent[i] == INDEX_NONE ? Local[i] : Local[i] * CS[Parent[i]];
			}
		}

		FVector Pos(int32 i) const { return CS[i].GetLocation(); }

		void SetRotation(int32 i, const FQuat& Rotation)
		{
			CS[i].SetRotation(Rotation.GetNormalized());
			Local[i] = Parent[i] == INDEX_NONE ? CS[i] : CS[i].GetRelativeTransform(CS[Parent[i]]);
			Recompute();
		}

		void Rotate(int32 i, const FQuat& Delta)
		{
			SetRotation(i, Delta * CS[i].GetRotation());
		}

		/** Turns bone i (about its own joint) so the segment to Child points along Direction. */
		void Aim(int32 i, int32 Child, const FVector& Direction)
		{
			const FVector Current = (Pos(Child) - Pos(i)).GetSafeNormal();
			const FVector Desired = Direction.GetSafeNormal();
			if (Current.IsNearlyZero() || Desired.IsNearlyZero())
			{
				return;
			}
			Rotate(i, FQuat::FindBetweenNormals(Current, Desired));
		}
	};

	/** Two-bone IK: where the elbow (or knee) goes for a root at Root reaching Target, bending toward Pole. */
	FVector SolveMiddle(const FVector& Root, const FVector& Target, double L1, double L2, const FVector& Pole)
	{
		const FVector ToTarget = Target - Root;
		const double Distance = ToTarget.Size();
		if (Distance < KINDA_SMALL_NUMBER)
		{
			return Root + Pole.GetSafeNormal() * L1;
		}
		const FVector Dir = ToTarget / Distance;
		const double Reach = FMath::Clamp(Distance, FMath::Abs(L1 - L2) + 0.01, (L1 + L2) * 0.999);
		const double Along = (L1 * L1 - L2 * L2 + Reach * Reach) / (2.0 * Reach);
		const double Out = FMath::Sqrt(FMath::Max(0.0, L1 * L1 - Along * Along));
		FVector Bend = Pole - Dir * FVector::DotProduct(Pole, Dir);
		if (!Bend.Normalize())
		{
			Bend = FVector::UpVector;
		}
		return Root + Dir * Along + Bend * Out;
	}
}

FAnimInstanceProxy* UKartDriverAnimInstance::CreateAnimInstanceProxy()
{
	return new FKartDriverAnimProxy(this);
}

void UKartDriverAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FKartDriverAnimProxy*>(InProxy);
}

void FKartDriverAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UKartDriverAnimInstance* Instance = Cast<UKartDriverAnimInstance>(InAnimInstance))
	{
		Input = Instance->Input;
	}
	// The body leans into the turn with some mass; the head leads it (drivingExpression.ts).
	const float Dt = FMath::Min(DeltaSeconds, 1.f / 30.f);
	const float TargetRoll = Input.Steer * 0.12f + Input.LateralG * 0.06f;
	// Underdamped spring (damping ratio 0.55): lags the wheel, overshoots ~12 %, settles.
	const float Omega = 11.f;
	const float Zeta = 0.55f;
	TorsoRollVelocity += (Omega * Omega * (TargetRoll - TorsoRoll) - 2.f * Zeta * Omega * TorsoRollVelocity) * Dt;
	TorsoRoll += TorsoRollVelocity * Dt;
	HeadYaw = FMath::FInterpTo(HeadYaw, Input.Steer * 0.42f, Dt, 15.f);
}

bool FKartDriverAnimProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
	auto Find = [&](const TCHAR* Name) -> int32
	{
		const int32 MeshIndex = Ref.FindBoneIndex(FName(Name));
		if (MeshIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		return Index.IsValid() ? Index.GetInt() : INDEX_NONE;
	};
	const int32 Torso = Find(TEXT("torso"));
	const int32 Head = Find(TEXT("head"));
	const int32 Neck = Find(TEXT("neck"));
	const int32 Arm[2][3] = {
		{ Find(TEXT("leftUpperArm")), Find(TEXT("leftForearm")), Find(TEXT("leftHand")) },
		{ Find(TEXT("rightUpperArm")), Find(TEXT("rightForearm")), Find(TEXT("rightHand")) },
	};
	const int32 Leg[2][3] = {
		{ Find(TEXT("leftUpperLeg")), Find(TEXT("leftLowerLeg")), Find(TEXT("leftFoot")) },
		{ Find(TEXT("rightUpperLeg")), Find(TEXT("rightLowerLeg")), Find(TEXT("rightFoot")) },
	};
	if (Torso == INDEX_NONE || Head == INDEX_NONE || Arm[0][0] == INDEX_NONE || Arm[1][0] == INDEX_NONE)
	{
		return true; // not an avatar4 rig: keep the reference pose
	}

	FRigWork Rig;
	const int32 Num = Pose.GetNumBones();
	Rig.Local.SetNum(Num);
	Rig.CS.SetNum(Num);
	Rig.Parent.SetNum(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		const FCompactPoseBoneIndex Index(i);
		Rig.Local[i] = Pose[Index];
		Rig.Parent[i] = Pose.GetParentBoneIndex(Index).GetInt();
	}
	Rig.Recompute();

	// The avatar's own frame, from where its joints are.
	const FVector Up = (Rig.Pos(Head) - Rig.Pos(Torso)).GetSafeNormal();
	FVector Right = Rig.Pos(Arm[1][0]) - Rig.Pos(Arm[0][0]);
	Right = (Right - Up * FVector::DotProduct(Right, Up)).GetSafeNormal();
	const FVector Forward = FVector::CrossProduct(Right, Up);
	const FVector Down = -Up;

	const bool bStraddle = Input.bStraddle;
	const float Bounce = FMath::Clamp(Input.Bounce, -1.f, 1.f);

	// Legs.
	const float ThighPitch = (bStraddle ? StraddleUpperLegPitch : SeatedUpperLegPitch) - Bounce * 0.06f;
	const float ShinPitch = ThighPitch + (bStraddle ? StraddleLowerLegPitch : SeatedLowerLegPitch) + Bounce * 0.05f;
	const float Splay = bStraddle ? StraddleUpperLegSplay : SeatedUpperLegSplay;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Upper = Leg[Side][0], Lower = Leg[Side][1], Foot = Leg[Side][2];
		if (Upper == INDEX_NONE || Lower == INDEX_NONE || Foot == INDEX_NONE)
		{
			continue;
		}
		const FVector Out = Side == 0 ? -Right : Right;
		FVector Thigh = Down * FMath::Cos(ThighPitch) + Forward * FMath::Sin(ThighPitch);
		Thigh = Thigh * FMath::Cos(Splay) + Out * FMath::Sin(Splay);
		Rig.Aim(Upper, Lower, Thigh);
		const FVector Shin = Down * FMath::Cos(ShinPitch) + Forward * FMath::Sin(ShinPitch) + Out * (bStraddle ? 0.12f : 0.03f);
		Rig.Aim(Lower, Foot, Shin);
	}

	// Torso: reclined in the seat (or over the bars), rolling into the turn.
	const float Recline = (bStraddle ? StraddleTorsoLean : 0.f) + SeatedTorsoRecline + 0.05f * Input.Moving - Bounce * 0.05f;
	FVector TorsoUp = Up * FMath::Cos(Recline) - Forward * FMath::Sin(Recline);
	TorsoUp = TorsoUp * FMath::Cos(TorsoRoll) + Right * FMath::Sin(TorsoRoll);
	const FQuat TorsoDelta = FQuat::FindBetweenNormals(Up, TorsoUp.GetSafeNormal());
	Rig.Rotate(Torso, TorsoDelta);

	// Head: eyes back toward level and looking into the turn.
	const FVector Facing = TorsoDelta.RotateVector(Forward);
	FVector Gaze = Forward * FMath::Cos(HeadYaw) + Right * FMath::Sin(HeadYaw);
	Gaze = (Gaze + Down * (0.06f - Bounce * 0.07f)).GetSafeNormal();
	const FQuat HeadDelta = FQuat::FindBetweenNormals(Facing, FMath::Lerp(Facing, Gaze, 0.85f).GetSafeNormal());
	if (Neck != INDEX_NONE)
	{
		Rig.Rotate(Neck, FQuat::Slerp(FQuat::Identity, HeadDelta, 0.4f));
	}
	Rig.Rotate(Head, HeadDelta);

	// Arms: two-bone IK onto the grips, elbows out and down.
	if (Input.bHasGrip)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Upper = Arm[Side][0], Fore = Arm[Side][1], Hand = Arm[Side][2];
			if (Fore == INDEX_NONE || Hand == INDEX_NONE)
			{
				continue;
			}
			const FVector Shoulder = Rig.Pos(Upper);
			const double L1 = (Rig.Pos(Fore) - Shoulder).Size();
			const double L2 = (Rig.Pos(Hand) - Rig.Pos(Fore)).Size();
			FVector Target = Side == 0 ? Input.LeftHand : Input.RightHand;
			if (Side == 1 && Input.ItemRaise > 0.f)
			{
				// Item use takes the right hand off the wheel, up and out (drivingExpression.ts).
				const FVector Raised = Shoulder + (Up * 0.8f + Right * 0.35f + Forward * 0.25f).GetSafeNormal() * (L1 + L2) * 0.95;
				Target = FMath::Lerp(Target, Raised, FMath::Clamp(Input.ItemRaise, 0.f, 1.f));
			}
			const FVector Out = Side == 0 ? -Right : Right;
			const FVector Pole = Out * 0.6f + Down * 0.7f - Forward * 0.3f;
			const FVector Elbow = SolveMiddle(Shoulder, Target, L1, L2, Pole);
			Rig.Aim(Upper, Fore, Elbow - Shoulder);
			Rig.Aim(Fore, Hand, Target - Rig.Pos(Fore));
		}
	}

	for (int32 i = 0; i < Num; ++i)
	{
		Pose[FCompactPoseBoneIndex(i)] = Rig.Local[i];
	}
	return true;
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
