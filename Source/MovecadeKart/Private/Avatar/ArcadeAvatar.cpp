#include "Avatar/ArcadeAvatar.h"

#include "Animation/AnimNodeBase.h"
#include "Avatar/AvatarRig.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

using MovecadeKart::Rig::FRigWork;
using MovecadeKart::Rig::SolveMiddle;

FAnimInstanceProxy* UArcadeAvatarAnimInstance::CreateAnimInstanceProxy()
{
	return new FArcadeAvatarProxy(this);
}

void UArcadeAvatarAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FArcadeAvatarProxy*>(InProxy);
}

void FArcadeAvatarProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UArcadeAvatarAnimInstance* Instance = Cast<UArcadeAvatarAnimInstance>(InAnimInstance))
	{
		Input = Instance->Input;
	}
}

const FArcadeAvatarProxy::FBones& FArcadeAvatarProxy::CacheBones(const FBoneContainer& Bones)
{
	if (bCached && CachedSerial == Bones.GetSerialNumber())
	{
		return Cached;
	}
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
	Cached.Root = 0;
	Cached.Torso = Find(TEXT("torso"));
	Cached.Neck = Find(TEXT("neck"));
	Cached.Head = Find(TEXT("head"));
	const TCHAR* ArmNames[2][3] = { { TEXT("leftUpperArm"), TEXT("leftForearm"), TEXT("leftHand") }, { TEXT("rightUpperArm"), TEXT("rightForearm"), TEXT("rightHand") } };
	const TCHAR* LegNames[2][3] = { { TEXT("leftUpperLeg"), TEXT("leftLowerLeg"), TEXT("leftFoot") }, { TEXT("rightUpperLeg"), TEXT("rightLowerLeg"), TEXT("rightFoot") } };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			Cached.Arm[Side][Joint] = Find(ArmNames[Side][Joint]);
			Cached.Leg[Side][Joint] = Find(LegNames[Side][Joint]);
		}
	}
	CachedSerial = Bones.GetSerialNumber();
	bCached = true;
	return Cached;
}

bool FArcadeAvatarProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	FCompactPose& Pose = Output.Pose;
	const FBones& B = CacheBones(Pose.GetBoneContainer());
	if (B.Torso == INDEX_NONE || B.Head == INDEX_NONE || B.Arm[0][0] == INDEX_NONE || B.Arm[1][0] == INDEX_NONE)
	{
		return true;
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

	// The avatar's own frame, from its rest skeleton.
	const FVector Up = (Rig.Pos(B.Head) - Rig.Pos(B.Torso)).GetSafeNormal();
	FVector Right = Rig.Pos(B.Arm[1][0]) - Rig.Pos(B.Arm[0][0]);
	Right = (Right - Up * FVector::DotProduct(Right, Up)).GetSafeNormal();
	const FVector Forward = FVector::CrossProduct(Right, Up);
	const FVector Down = -Up;
	const float T = Input.Time;

	// Legs: knees bend for a crouch, swing for a stride, and split into the bowler's slide.
	double LegLength = 0.0;
	if (B.Leg[0][0] != INDEX_NONE && B.Leg[0][1] != INDEX_NONE && B.Leg[0][2] != INDEX_NONE)
	{
		LegLength = (Rig.Pos(B.Leg[0][1]) - Rig.Pos(B.Leg[0][0])).Size() + (Rig.Pos(B.Leg[0][2]) - Rig.Pos(B.Leg[0][1])).Size();
	}
	const float Crouch = FMath::Clamp(Input.Crouch, 0.f, 1.f);
	const float Lunge = FMath::Clamp(Input.Lunge, 0.f, 1.f);
	const float CrouchPitch = Crouch * 0.62f;
	const double Drop = LegLength * ((1.0 - FMath::Cos(CrouchPitch)) * 1.6 + Lunge * 0.22) + LegLength * 0.02 * Input.Step * FMath::Abs(FMath::Sin(Input.StepPhase));
	Rig.Local[B.Root].AddToTranslation(Down * Drop + Up * Input.Lift * 100.0);
	Rig.Recompute();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Upper = B.Leg[Side][0], Lower = B.Leg[Side][1], Foot = B.Leg[Side][2];
		if (Upper == INDEX_NONE || Lower == INDEX_NONE || Foot == INDEX_NONE)
		{
			continue;
		}
		const float Swing = Input.Step * 0.42f * FMath::Sin(Input.StepPhase + Side * UE_PI);
		const float Lift = Input.Step * 0.7f * FMath::Max(0.f, FMath::Cos(Input.StepPhase + Side * UE_PI));
		// The slide: the left leg plants forward and bent, the right trails behind.
		const float SlideThigh = Side == 0 ? 0.95f * Lunge : -0.6f * Lunge;
		const float SlideKnee = Side == 0 ? 1.15f * Lunge : 0.25f * Lunge;
		const float Thigh = CrouchPitch + Swing + SlideThigh + Lift * 0.4f;
		const float Knee = CrouchPitch * 2.f + Lift + SlideKnee;
		const FVector Out = Side == 0 ? -Right : Right;
		Rig.Aim(Upper, Lower, (Down * FMath::Cos(Thigh) + Forward * FMath::Sin(Thigh) + Out * 0.04f).GetSafeNormal());
		const float Shin = Thigh - Knee;
		Rig.Aim(Lower, Foot, (Down * FMath::Cos(Shin) + Forward * FMath::Sin(Shin) + Out * 0.02f).GetSafeNormal());
	}

	// Torso: lean, twist, breath; a cheer leans back, a slump folds forward.
	const float Breath = FMath::Sin(T * 1.55f) * 0.016f * Input.Idle;
	const float Lean = Input.Lean + Breath - 0.12f * Input.Cheer + 0.28f * Input.Slump;
	const float Twist = Input.Twist + FMath::Sin(T * 0.43f) * 0.03f * Input.Idle;
	const FQuat TorsoDelta = FQuat(Up, Twist) * FQuat(Right, Lean);
	Rig.Rotate(B.Torso, TorsoDelta);

	// Head: the gaze, toward level, chin up in a cheer and down in a slump.
	const FVector Facing = TorsoDelta.RotateVector(Forward);
	FVector Gaze = Forward;
	if (Input.LookWeight > 0.f && !Input.Look.IsNearlyZero())
	{
		const FVector Want = (Forward * Input.Look.X + Right * Input.Look.Y + Up * Input.Look.Z).GetSafeNormal();
		Gaze = FMath::Lerp(Gaze, Want, FMath::Clamp(Input.LookWeight, 0.f, 1.f)).GetSafeNormal();
	}
	Gaze = (Gaze + Up * (0.35f * Input.Cheer) + Down * (0.6f * Input.Slump)).GetSafeNormal();
	const FQuat HeadDelta = FQuat::FindBetweenNormals(Facing, FMath::Lerp(Facing, Gaze, 0.8f).GetSafeNormal());
	if (B.Neck != INDEX_NONE)
	{
		Rig.Rotate(B.Neck, FQuat::Slerp(FQuat::Identity, HeadDelta, 0.4f));
	}
	Rig.Rotate(B.Head, FQuat::Slerp(FQuat::Identity, HeadDelta, 0.6f));

	// Arms: two-bone IK onto each hand's target, or hanging at rest; up and waving in a cheer.
	const FVector TorsoForward = TorsoDelta.RotateVector(Forward), TorsoRight = TorsoDelta.RotateVector(Right), TorsoUp = TorsoDelta.RotateVector(Up);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Upper = B.Arm[Side][0], Fore = B.Arm[Side][1], Hand = B.Arm[Side][2];
		if (Fore == INDEX_NONE || Hand == INDEX_NONE)
		{
			continue;
		}
		const FVector Shoulder = Rig.Pos(Upper);
		const double L1 = (Rig.Pos(Fore) - Shoulder).Size();
		const double L2 = (Rig.Pos(Hand) - Rig.Pos(Fore)).Size();
		const double Reach = L1 + L2;
		const FVector Out = Side == 0 ? -TorsoRight : TorsoRight;
		const FVector Rest = (TorsoForward * 0.08f + Out * (0.2f + 0.02f * FMath::Sin(T * 1.1f + Side)) + Down * 0.94f).GetSafeNormal() * Reach * 0.97;
		const FVector Given = (TorsoForward * Input.Hand[Side].X + TorsoRight * Input.Hand[Side].Y + TorsoUp * Input.Hand[Side].Z) * Reach;
		FVector Target = Shoulder + FMath::Lerp(Rest, Given, FMath::Clamp(Input.HandWeight[Side], 0.f, 1.f));
		if (Input.Cheer > 0.f)
		{
			const float Wave = FMath::Sin(T * 7.f + Side * UE_PI) * 0.16f;
			const FVector Raised = Shoulder + (TorsoUp * 0.9f + Out * (0.36f + Wave) + TorsoForward * 0.12f).GetSafeNormal() * Reach * 0.94;
			Target = FMath::Lerp(Target, Raised, Input.Cheer);
		}
		// Elbows out, down and back.
		const FVector Pole = Out * 0.55f + Down * 0.45f - TorsoForward * 0.45f;
		const FVector Elbow = SolveMiddle(Shoulder, Target, L1, L2, Pole);
		Rig.Aim(Upper, Fore, Elbow - Shoulder);
		Rig.Aim(Fore, Hand, Target - Rig.Pos(Fore));
	}

	for (int32 i = 0; i < Num; ++i)
	{
		Pose[FCompactPoseBoneIndex(i)] = Rig.Local[i];
	}
	return true;
}

UArcadeAvatarComponent::UArcadeAvatarComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UArcadeAvatarComponent::OnRegister()
{
	Super::OnRegister();
	if (!Driver)
	{
		Driver = NewObject<USkeletalMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), USkeletalMeshComponent::StaticClass(), TEXT("AvatarMesh")));
		Driver->SetupAttachment(this);
		Driver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Driver->RegisterComponent();
		Outfit = NewObject<USkeletalMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), USkeletalMeshComponent::StaticClass(), TEXT("AvatarOutfit")));
		Outfit->SetupAttachment(Driver);
		Outfit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Outfit->RegisterComponent();
	}
	if (bHasLook)
	{
		SetLook(CurrentLook);
	}
}

void UArcadeAvatarComponent::SetLook(const FKartLook& Look)
{
	CurrentLook = Look;
	bHasLook = true;
	if (!Driver)
	{
		return;
	}
	if (Dressing.Dress(Driver, Outfit, Look) == MovecadeKart::Avatar::EDressResult::NewMesh)
	{
		Driver->SetAnimInstanceClass(UArcadeAvatarAnimInstance::StaticClass());
		Driver->SetRelativeTransform(FTransform(MovecadeKart::Avatar::Facing(Driver->GetSkeletalMeshAsset(), FVector::XAxisVector, FVector::ZAxisVector),
			FVector::ZeroVector, FVector(AvatarScale)));
	}
	Dressing.Colour(Look);
}

FVector UArcadeAvatarComponent::BoneWorld(FName Bone) const
{
	if (Driver && Driver->GetBoneIndex(Bone) != INDEX_NONE)
	{
		return Driver->GetBoneLocation(Bone);
	}
	return GetComponentLocation();
}

void UArcadeAvatarComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Clock += DeltaTime;
	Input.Time = Clock;
	if (UArcadeAvatarAnimInstance* Anim = Driver ? Cast<UArcadeAvatarAnimInstance>(Driver->GetAnimInstance()) : nullptr)
	{
		Anim->Input = Input;
	}
	// Blinks every 2-5 s (blink.ts), 0.22 s each.
	if (Clock >= NextBlink)
	{
		BlinkAt = Clock;
		NextBlink = Clock + FMath::FRandRange(2.f, 5.f);
	}
	const float Age = Clock - BlinkAt;
	const float Blink = Age < 0.065f ? Age / 0.065f : (Age < 0.085f ? 1.f : FMath::Clamp(1.f - (Age - 0.085f) / 0.135f, 0.f, 1.f));
	if (Driver)
	{
		Driver->SetMorphTarget(TEXT("eyeBlink"), Blink);
	}
}
