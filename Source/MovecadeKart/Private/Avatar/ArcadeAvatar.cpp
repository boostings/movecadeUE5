#include "Avatar/ArcadeAvatar.h"

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

DECLARE_CYCLE_STAT(TEXT("Avatar face"), STAT_ArcadeAvatarFace, STATGROUP_Anim);

namespace
{
	/** Faster than this (cm/s) between two frames is a teleport (a new seat, a reset), not motion. */
	constexpr double TeleportSpeed = 2000.0;
	/** How quickly the measured velocity and turn rate follow the component (interp speed, 1/s). */
	constexpr float MotionSmoothing = 20.f;
	/** The eyes' range of motion in the head (radians): yaw either way, pitch up and down. */
	constexpr float EyeMaxYaw = 0.6f;
	constexpr float EyeMaxPitch = 0.35f;
	/** The eyes follow the chin up in a cheer and drop in a slump (shares of straight up / down added to the gaze). */
	constexpr float CheerEyesUp = 0.3f;
	constexpr float SlumpEyesDown = 0.45f;
	/** A slump's heavy lids (closure). */
	constexpr float SlumpLids = 0.25f;
	/** An emote this strong has taken the arms (a greeting starts as a wave crosses it). */
	constexpr float EmoteOnWeight = 0.3f;
	/** How much the game's look counts as concentration for the blinks. */
	constexpr float LookFocus = 0.5f;
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
		// Seeded once, so the face's rhythms (blinks, glances) and the proxy's reactions are this avatar's own.
		Seed = GetTypeHash(GetFName()) ^ (GetUniqueID() * 2654435761u);
		const int32 FaceSeed = static_cast<int32>(Seed & 0x7fffffff);
		Saccades = MovecadeKart::Avatar::FSaccadeScheduler(FaceSeed);
		Blinks = MovecadeKart::Avatar::FBlinkScheduler(FaceSeed ^ 0x2545f491);
		FaceLife = MovecadeKart::Avatar::FFaceLife(FaceSeed ^ 0x6c8e9cf5);
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
		Morphs.Reset();
		CacheHead();
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
	UpdateMotion(DeltaTime);
	if (UArcadeAvatarAnimInstance* Anim = Driver ? Cast<UArcadeAvatarAnimInstance>(Driver->GetAnimInstance()) : nullptr)
	{
		Anim->Input = Input;
		Anim->Motion.Velocity = SmoothedVelocity;
		Anim->Motion.YawRate = SmoothedYawRate;
		Anim->Motion.Seed = Seed;
	}
	UpdateFace(DeltaTime);
}

void UArcadeAvatarComponent::UpdateMotion(float DeltaTime)
{
	const FTransform& Now = GetComponentTransform();
	if (bHasLastTransform && DeltaTime > 1e-4f)
	{
		const FVector Velocity = Now.InverseTransformVectorNoScale((Now.GetLocation() - LastTransform.GetLocation()) / DeltaTime);
		const FQuat Turn = Now.GetRotation() * LastTransform.GetRotation().Inverse();
		const float Yaw = FMath::FindDeltaAngleRadians(0.f, static_cast<float>(Turn.Rotator().Yaw * UE_PI / 180.0));
		const bool bTeleport = Velocity.SizeSquared() > TeleportSpeed * TeleportSpeed;
		SmoothedVelocity = bTeleport ? FVector::ZeroVector : FMath::VInterpTo(SmoothedVelocity, Velocity, DeltaTime, MotionSmoothing);
		SmoothedYawRate = FMath::FInterpTo(SmoothedYawRate, bTeleport ? 0.f : -Yaw / DeltaTime, DeltaTime, MotionSmoothing);
	}
	LastTransform = Now;
	bHasLastTransform = true;
}

void UArcadeAvatarComponent::CacheHead()
{
	static const FName HeadName(TEXT("head"));
	const USkeletalMesh* Mesh = Driver ? Driver->GetSkeletalMeshAsset() : nullptr;
	HeadBone = Mesh ? Mesh->GetRefSkeleton().FindBoneIndex(HeadName) : INDEX_NONE;
	HeadRestInverse = HeadBone != INDEX_NONE ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Mesh->GetRefSkeleton(), HeadBone).GetRotation().Inverse()
		: FQuat::Identity;
}

float UArcadeAvatarComponent::UpdateEyes(float DeltaTime, bool& bOutSaccade)
{
	using namespace MovecadeKart::Avatar;
	// Where the avatar wants to look (the same glances its head follows), the eyes jumping there first.
	Glance.Step(Input.Time, Seed);
	const float PointWeight = Input.Emote == EArcadeEmote::Point ? FMath::Clamp(Input.EmoteWeight, 0.f, 1.f) : 0.f;
	const FVector Target = (GazeTarget(Input, Glance, PointWeight) + FVector::UpVector * (CheerEyesUp * FMath::Clamp(Input.Cheer, 0.f, 1.f))
		- FVector::UpVector * (SlumpEyesDown * FMath::Clamp(Input.Slump, 0.f, 1.f))).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const float Busy = FMath::Clamp(FMath::Max3(Input.HandWeight[0], Input.HandWeight[1], FMath::Max3(Input.Cheer, Input.Slump, Input.Effort)), 0.f, 1.f);
	const FVector Eyes = Saccades.Step(DeltaTime, Target, FMath::Clamp(Input.Idle, 0.f, 1.f) * (1.f - Busy), bOutSaccade);

	// Into the head's frame as it is posed now (last frame's pose: the eyes lead the head anyway). The mesh's rest
	// space is the eye material's space; the avatar's frame is that space turned by the mesh's Facing.
	const FQuat Facing = Driver->GetRelativeRotation().Quaternion();
	const FQuat HeadTurn = HeadBone != INDEX_NONE ? Driver->GetBoneTransform(HeadBone, FTransform::Identity).GetRotation() * HeadRestInverse : FQuat::Identity;
	const FVector InHead = ClampEyes(Facing.RotateVector(HeadTurn.UnrotateVector(Facing.UnrotateVector(Eyes))), EyeMaxYaw, EyeMaxPitch);
	Dressing.AimEyes(Facing.UnrotateVector(InHead));
	return EyeAngles(InHead).Y;
}

void UArcadeAvatarComponent::UpdateFace(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_ArcadeAvatarFace);
	using namespace MovecadeKart::DriverFace;
	using namespace MovecadeKart::Avatar;
	if (!Driver || !Driver->GetSkeletalMeshAsset())
	{
		return;
	}
	bool bSaccade = false;
	const float EyePitch = UpdateEyes(DeltaTime, bSaccade);

	// A greeting starts as a wave takes the arms.
	const EArcadeEmote Emote = Input.EmoteWeight > EmoteOnWeight ? Input.Emote : EArcadeEmote::None;
	const bool bGreeting = Emote == EArcadeEmote::Wave && LastEmote != EArcadeEmote::Wave;
	LastEmote = Emote;

	const float Effort = FMath::Clamp(Input.Effort, 0.f, 1.f);
	const float Cheer = FMath::Clamp(Input.Cheer, 0.f, 1.f);
	const float Slump = FMath::Clamp(Input.Slump, 0.f, 1.f);
	const float Excite = FMath::Max(Cheer, Emote != EArcadeEmote::None ? FMath::Clamp(Input.EmoteWeight, 0.f, 1.f) : 0.f);
	const float Focus = FMath::Max(Effort, LookFocus * FMath::Clamp(Input.LookWeight, 0.f, 1.f));
	const float Blink = Blinks.Step(DeltaTime, Focus, Excite, Slump, bSaccade);

	float Target[MorphCount];
	FaceFor(Input, Target);
	FFaceLifeInput Life;
	Life.EyePitch = EyePitch;
	Life.bSaccade = bSaccade;
	Life.Effort = Effort;
	Life.Cheer = Cheer;
	Life.Slump = Slump;
	Life.bGreeting = bGreeting;
	Life.Time = Clock;
	float Weights[MorphCount];
	float Lids = 0.f;
	FaceLife.Step(DeltaTime, Target, Life, Weights, Lids);

	// The lids close for a blink, for eyes looking down, or under a slump's weight, whichever closes them most.
	static const FName EyeBlink(TEXT("eyeBlink"));
	Morphs.Set(Driver, BlinkSlot, EyeBlink, FMath::Max3(Blink, Lids, SlumpLids * Slump));
	for (int32 i = 0; i < MorphCount; ++i)
	{
		Morphs.Set(Driver, i, MorphName(static_cast<EMorph>(i)), Weights[i]);
	}
}

