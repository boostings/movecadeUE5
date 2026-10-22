#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstanceProxy.h"
#include "Avatar/ArcadeAvatar.h"
#include "Avatar/AvatarFaceLife.h"
#include "Avatar/AvatarRigPose.h"

/**
 * Poses the avatar4 rig standing for UArcadeAvatarAnimInstance: stance, steps, lunge, arm IK to the hand targets
 * (soft reach, elbow hints), finger curl, gaze, emotes, celebration and disappointment variants, idle life, and
 * spring-driven hair and hems. PreUpdate (game thread) steps the springs; Evaluate (any thread) builds the pose from
 * them in layers: legs, torso, shoulders, head, arms, spring chains.
 */
struct FArcadeAvatarProxy : public FAnimInstanceProxy
{
	FArcadeAvatarProxy() = default;
	explicit FArcadeAvatarProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	using FBones = MovecadeKart::Rig::FAvatarBones;
	using FBodyFrame = MovecadeKart::Rig::FBodyFrame;

	/** The reactions' and idle's levels for this evaluation, shared by every layer. */
	struct FLayers
	{
		float Time = 0.f;
		float Secondary = 1.f;
		float Cheer = 0.f;
		float Slump = 0.f;
		float Emote = 0.f;
		float Effort = 0.f;
		/** Idle life, faded out as the game (or a reaction) takes over the body. */
		float Calm = 0.f;
		/** This avatar's own phase for its idle rhythms. */
		float Phase = 0.f;
		/** The idle weight shift, -1 (over the left foot) .. 1. */
		float Shift = 0.f;
		float Lunge = 0.f;
		float Breath = 0.f;
		/** Shoulders up: the shrug emote, or the arms-out disappointment. */
		float Shrug = 0.f;
	};

	/** The posed torso's axes. */
	struct FTorsoFrame
	{
		FQuat Delta = FQuat::Identity;
		FVector Forward;
		FVector Right;
		FVector Up;
	};

	/** Where the head ended up, for the hand-to-head reactions and the hair. */
	struct FHeadFrame
	{
		FQuat Turn = FQuat::Identity;
		FVector Position;
		FVector Up;
		FVector Forward;
	};

private:
	// PreUpdate: secondary motion, stepped on the game thread.
	void StepReactions(float Secondary);
	void StepGaze();
	void StepHairAndHems(float Secondary, float LiftVelocity);
	void StepLanding(float Secondary, float LiftVelocity);

	// Evaluate: the layers, in order.
	FLayers MakeLayers() const;
	void PoseLegs(const FBones& B, const FBodyFrame& Body, const FLayers& L);
	FTorsoFrame PoseTorso(const FBones& B, const FBodyFrame& Body, const FLayers& L);
	void PoseShoulders(const FBones& B, const FTorsoFrame& Torso, const FLayers& L);
	FHeadFrame PoseHead(const FBones& B, const FBodyFrame& Body, const FTorsoFrame& Torso, const FLayers& L);
	void PoseArm(int32 Side, const FBones& B, const FBodyFrame& Body, const FTorsoFrame& Torso, const FHeadFrame& Head, const FVector& ShoulderMid,
		const FLayers& L);
	void SwingSpringChains(const FBones& B, const FBodyFrame& Body, const FTorsoFrame& Torso, const FHeadFrame& Head, const FLayers& L);

	MovecadeKart::Rig::FAvatarBoneCache BoneCache;
	FArcadePoseInput Input;
	FArcadeAvatarMotion Motion;
	/** Reused every evaluation (no allocation). */
	MovecadeKart::Rig::FRigWork Rig;

	float Dt = 0.f;
	/** The reactions through springs: they overshoot, then settle. */
	MovecadeKart::Rig::TSpring<float> CheerSpring, SlumpSpring, EmoteSpring;
	/** Seconds since the current celebration / disappointment began (for the wind-up), and its variant. */
	float CheerAge = 10.f, SlumpAge = 10.f;
	EArcadeCheer CheerNow = EArcadeCheer::Wave;
	EArcadeSlump SlumpNow = EArcadeSlump::Droop;
	uint32 Reactions = 0;
	/** The gaze eases toward its target, the head lagging the eyes' intent. */
	MovecadeKart::Rig::TSpring<FVector> GazeSpring;
	bool bGazeInit = false;
	/** Idle glances: where, until when. */
	MovecadeKart::Avatar::FIdleGlance Glance;
	/** Hair and hem lag, in cm, in the avatar frame, on bouncy springs. */
	MovecadeKart::Rig::TSpring<FVector> HairLag, HemLag;
	/** Landing squash from Lift. */
	float LastLift = 0.f;
	MovecadeKart::Rig::TSpring<float> Land;
};
