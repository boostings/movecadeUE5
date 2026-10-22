#include "Avatar/ArcadeAvatar.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	/** Faster than this (cm/s) between two frames is a teleport (a new seat, a reset), not motion. */
	constexpr double TeleportSpeed = 2000.0;
	/** How quickly the measured velocity and turn rate follow the component (interp speed, 1/s). */
	constexpr float MotionSmoothing = 20.f;
	/** blink.ts: a blink every 2-5 s, sometimes a quick double; closing, held shut, then opening (seconds). */
	constexpr float BlinkMinGap = 2.f;
	constexpr float BlinkMaxGap = 5.f;
	constexpr float DoubleBlinkChance = 0.18f;
	constexpr float DoubleBlinkGap = 0.3f;
	constexpr float BlinkShutAt = 0.065f;
	constexpr float BlinkOpensAt = 0.085f;
	constexpr float BlinkOpening = 0.135f;
	/** A turn of the gaze bigger than this (dot product) blinks too, if the last blink is this old. */
	constexpr float GazeJumpDot = 0.8f;
	constexpr float GazeJumpMinAge = 0.6f;
	/** How quickly the face eases toward its mood (interp speed, 1/s). */
	constexpr float FaceEase = 12.f;
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
		Morphs.Reset();
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
		Anim->Motion.Seed = GetTypeHash(GetFName()) ^ (GetUniqueID() * 2654435761u);
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

void UArcadeAvatarComponent::UpdateFace(float DeltaTime)
{
	using namespace MovecadeKart::DriverFace;
	if (!Driver || !Driver->GetSkeletalMeshAsset())
	{
		return;
	}
	// Blinks now and then, and with a big turn of the gaze.
	const FVector Look = Input.LookWeight > 0.3f ? Input.Look.GetSafeNormal() : FVector::ForwardVector;
	const bool bGazeJump = FVector::DotProduct(Look, LastLook) < GazeJumpDot && Clock - BlinkAt > GazeJumpMinAge;
	LastLook = Look;
	if (Clock >= NextBlink || bGazeJump)
	{
		BlinkAt = Clock;
		NextBlink = Clock + (FMath::FRand() < DoubleBlinkChance ? DoubleBlinkGap : FMath::FRandRange(BlinkMinGap, BlinkMaxGap));
	}
	const float Age = Clock - BlinkAt;
	float Blink = Age < BlinkShutAt ? Age / BlinkShutAt : (Age < BlinkOpensAt ? 1.f : FMath::Clamp(1.f - (Age - BlinkOpensAt) / BlinkOpening, 0.f, 1.f));
	// A slump's heavy lids; a cheer's happy squint is in the expression.
	Blink = FMath::Max(Blink, 0.25f * FMath::Clamp(Input.Slump, 0.f, 1.f));
	static const FName EyeBlink(TEXT("eyeBlink"));
	Morphs.Set(Driver, MovecadeKart::Avatar::BlinkSlot, EyeBlink, Blink);

	float Target[MorphCount];
	MovecadeKart::Avatar::FaceFor(Input, Target);
	for (int32 i = 0; i < MorphCount; ++i)
	{
		Face[i] = FMath::FInterpTo(Face[i], Target[i], DeltaTime, FaceEase);
		Morphs.Set(Driver, i, MorphName(static_cast<EMorph>(i)), Face[i]);
	}
}
