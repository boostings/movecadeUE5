#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Avatar/AvatarRig.h"
#include "KartDriverAnimInstance.generated.h"

/** What the kart tells its driver each frame. Positions are in the driver mesh's component space. */
struct FKartDriverPoseInput
{
	/** -1..1, positive = steering right (UE convention). */
	float Steer = 0.f;
	/** 0..1 of top speed, for how much weight the pose carries. */
	float Moving = 0.f;
	/** Signed lateral load, -1..1, positive pushes the body to the right. */
	float LateralG = 0.f;
	/** -1..1 of a bump or hop, positive = the kart is rising. */
	float Bounce = 0.f;
	float Time = 0.f;
	/** Astride a saddle (moto) instead of legs forward in a cockpit. */
	bool bStraddle = false;
	/** Raises the right hand off the wheel (item throw), 0..1. */
	float ItemRaise = 0.f;
	/** Both arms up and waving, head back: the finish-line celebration, 0..1. */
	float Cheer = 0.f;
	/** Off the podium: slumped forward, head down, 0..1. */
	float Slump = 0.f;
	/** Drifting, -1..1, positive = drifting right (UE convention), scaled by the drift's charge. The body leans in. */
	float DriftSide = 0.f;
	/** Boosting, 0..1: pressed back into the seat, chin up. */
	float Boost = 0.f;
	/** Revving on the grid, 0..1: hunched forward, buzzing with the engine. */
	float Rev = 0.f;
	/** Spun out or tumbling, 0..1: arms flailing, head reeling. */
	float Spin = 0.f;
	/** An impact's whiplash from a spring: positive throws the torso back, and to the right. */
	float JoltBack = 0.f;
	float JoltSide = 0.f;
	/** Where the driver is looking (component space) and how much, 0 = the road ahead. */
	FVector LookAt = FVector::ZeroVector;
	float LookWeight = 0.f;
	bool bHasGrip = false;
	FVector LeftHand = FVector::ZeroVector;
	FVector RightHand = FVector::ZeroVector;
};

/**
 * The seated driver pose for an avatar4 body (Docs/Port/03 §6.1, drivingExpression.ts): legs forward
 * under the cowl (or astride the moto), the torso reclined and rolling with the load, the head looking
 * into the turn, and both arms solved by two-bone IK onto the steering wheel's grip points. The rig's spring
 * chains (hair, garment hems) swing on a bouncy spring after bumps, boosts, impacts and corners. It runs
 * natively in the animation proxy (no AnimBP asset), on the shared 26-joint skeleton.
 */
UCLASS(Transient, NotBlueprintable)
class MOVECADEKART_API UKartDriverAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	FKartDriverPoseInput Input;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

struct FKartDriverAnimProxy : public FAnimInstanceProxy
{
	FKartDriverAnimProxy() = default;
	explicit FKartDriverAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** Compact-pose indices of the joints the pose drives (INDEX_NONE if the rig lacks one). */
	struct FDriverBones
	{
		int32 Torso = INDEX_NONE;
		int32 Head = INDEX_NONE;
		int32 Neck = INDEX_NONE;
		int32 Arm[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
		int32 Leg[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
		int32 Fingers[2] = { INDEX_NONE, INDEX_NONE };
		int32 Thumb[2] = { INDEX_NONE, INDEX_NONE };
		/** The rig's spring chains (avatar4 "Spring_hair*", "Spring_hem*"), parents first. */
		TArray<int32, TInlineAllocator<8>> Hair;
		TArray<int32, TInlineAllocator<8>> Hem;
	};
	/** Looks the joints up by name only when the bone container changes (LOD, mesh swap), not every evaluation. */
	const FDriverBones& CacheDriverBones(const FBoneContainer& Bones);

	FDriverBones CachedBones;
	uint16 CachedSerial = 0;
	bool bBonesCached = false;
	FKartDriverPoseInput Input;
	/** The working pose, reused every evaluation (no allocation). */
	MovecadeKart::Rig::FRigWork Rig;
	/** The damped pose state (radians), so the body lags the wheel like mass. */
	float TorsoRoll = 0.f;
	float TorsoRollVelocity = 0.f;
	float HeadYaw = 0.f;
	float DeltaTime = 0.f;
	/** The hair's swing (pitch back, roll right; radians) on a bouncy spring, so it lags and overshoots the head. */
	FVector2f HairSwing = FVector2f::ZeroVector;
	FVector2f HairSwingVelocity = FVector2f::ZeroVector;
	float LastHeadYaw = 0.f;
};

namespace MovecadeKart::DriverRig
{
	/**
	 * The avatar's own axes in its component space, from the reference pose: Forward (where the face
	 * looks), Right (the character's right) and Up. Returns false if the rig lacks the joints.
	 */
	MOVECADEKART_API bool ComputeBasis(const FReferenceSkeleton& Skeleton, FVector& OutForward, FVector& OutRight, FVector& OutUp);
}
