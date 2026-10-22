#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "KartDriverAnimInstance.generated.h"

struct FReferenceSkeleton;

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
 * natively in the animation proxy (no AnimBP asset; FKartDriverAnimProxy, KartDriverAnimProxy.h), on the shared
 * 26-joint skeleton. AKartPawn fills Input every frame.
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

namespace MovecadeKart::DriverRig
{
	/**
	 * The avatar's own axes in its component space, from the reference pose: Forward (where the face
	 * looks), Right (the character's right) and Up. Returns false if the rig lacks the joints.
	 */
	MOVECADEKART_API bool ComputeBasis(const FReferenceSkeleton& Skeleton, FVector& OutForward, FVector& OutRight, FVector& OutUp);
}
