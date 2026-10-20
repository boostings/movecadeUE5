#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
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
	bool bHasGrip = false;
	FVector LeftHand = FVector::ZeroVector;
	FVector RightHand = FVector::ZeroVector;
};

/**
 * The seated driver pose for an avatar4 body (Docs/Port/03 §6.1, drivingExpression.ts): legs forward
 * under the cowl (or astride the moto), the torso reclined and rolling with the load, the head looking
 * into the turn, and both arms solved by two-bone IK onto the steering wheel's grip points. It runs
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
	FKartDriverPoseInput Input;
	/** The damped pose state (radians), so the body lags the wheel like mass. */
	float TorsoRoll = 0.f;
	float TorsoRollVelocity = 0.f;
	float HeadYaw = 0.f;
	float DeltaTime = 0.f;
};

namespace MovecadeKart::DriverRig
{
	/**
	 * The avatar's own axes in its component space, from the reference pose: Forward (where the face
	 * looks), Right (the character's right) and Up. Returns false if the rig lacks the joints.
	 */
	MOVECADEKART_API bool ComputeBasis(const FReferenceSkeleton& Skeleton, FVector& OutForward, FVector& OutRight, FVector& OutUp);
}
