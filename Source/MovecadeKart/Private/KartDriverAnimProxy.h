#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstanceProxy.h"
#include "Avatar/AvatarRigPose.h"
#include "KartDriverAnimInstance.h"

/**
 * Poses the seated kart driver for UKartDriverAnimInstance. PreUpdate (game thread) steps the damped torso roll, the
 * head's turn and the hair's swing; Evaluate (any thread) builds the pose in layers: legs, torso, the lean to reach
 * the wheel, head, spring chains, then the arms' IK onto the grips.
 */
struct FKartDriverAnimProxy : public FAnimInstanceProxy
{
	FKartDriverAnimProxy() = default;
	explicit FKartDriverAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	using FBones = MovecadeKart::Rig::FAvatarBones;
	using FBodyFrame = MovecadeKart::Rig::FBodyFrame;

	void StepTorsoAndHead(float Dt);
	void StepHair(float Dt);

	void PoseLegs(const FBones& B, const FBodyFrame& Body, float Bounce);
	/** Reclines and rolls the torso; returns its rotation. */
	FQuat PoseTorso(const FBones& B, const FBodyFrame& Body, float Bounce);
	/** Leans the torso in until both grips are in reach, adding each step to TorsoDelta. */
	void LeanToReach(const FBones& B, float HandsOff, FQuat& TorsoDelta);
	/** Turns the head toward the gaze; returns the head's rotation. */
	FQuat PoseHead(const FBones& B, const FBodyFrame& Body, const FQuat& TorsoDelta, float Bounce, float Cheer, float Spin);
	void SwingSpringChains(const FBones& B, const FBodyFrame& Body, const FQuat& TorsoDelta, const FQuat& HeadDelta);
	void PoseArm(int32 Side, const FBones& B, const FBodyFrame& Body, float Cheer, float Spin, float HandsOff);

	MovecadeKart::Rig::FAvatarBoneCache BoneCache;
	FKartDriverPoseInput Input;
	/** The working pose, reused every evaluation (no allocation). */
	MovecadeKart::Rig::FRigWork Rig;
	/** The damped pose state (radians), so the body lags the wheel like mass. */
	float TorsoRoll = 0.f;
	float TorsoRollVelocity = 0.f;
	float HeadYaw = 0.f;
	/** The hair's swing (pitch back, roll right; radians) on a bouncy spring, so it lags and overshoots the head. */
	FVector2f HairSwing = FVector2f::ZeroVector;
	FVector2f HairSwingVelocity = FVector2f::ZeroVector;
	float LastHeadYaw = 0.f;
};
