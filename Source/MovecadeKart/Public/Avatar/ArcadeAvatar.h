#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Avatar/AvatarDressing.h"
#include "Components/SceneComponent.h"
#include "KartLook.h"
#include "ArcadeAvatar.generated.h"

class USkeletalMeshComponent;

/**
 * What a game asks of its standing player avatar each frame (the party games' share of the web's avatar motion
 * system, Docs/Port/05 §3). Directions are in the avatar's own frame: X forward, Y to its right, Z up. Hand
 * targets are relative to that side's shoulder, in arm lengths, so the same numbers fit every body.
 */
struct FArcadePoseInput
{
	float Time = 0.f;
	/** Bend at the waist, radians (+ forward). */
	float Lean = 0.f;
	/** Turn of the shoulders about the spine, radians (+ to the avatar's left). */
	float Twist = 0.f;
	/** Knees bent, 0..1. */
	float Crouch = 0.f;
	/** A walk: the step cycle's phase (radians) and how much of a stride, 0..1. */
	float StepPhase = 0.f;
	float Step = 0.f;
	/** The bowler's slide, 0..1: left leg forward and bent, right leg trailing behind. */
	float Lunge = 0.f;
	/** Off the ground (a jump), metres. */
	float Lift = 0.f;
	/** 0 left, 1 right. Weight 0 lets the arm hang at rest. */
	FVector Hand[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float HandWeight[2] = { 0.f, 0.f };
	/** Where to look, as a direction in the avatar frame, and how much. */
	FVector Look = FVector::ForwardVector;
	float LookWeight = 0.f;
	/** Both arms up and waving, chin up: a celebration, 0..1. */
	float Cheer = 0.f;
	/** Head down, shoulders dropped: a miss, 0..1. */
	float Slump = 0.f;
	/** Breath and idle sway, 0..1. */
	float Idle = 1.f;
};

UCLASS(Transient, NotBlueprintable)
class MOVECADEKART_API UArcadeAvatarAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	FArcadePoseInput Input;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

/** Poses the avatar4 rig standing: stance, steps, lunge, arm IK to the hand targets, gaze, cheer and slump. */
struct FArcadeAvatarProxy : public FAnimInstanceProxy
{
	FArcadeAvatarProxy() = default;
	explicit FArcadeAvatarProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	struct FBones
	{
		int32 Root = INDEX_NONE;
		int32 Torso = INDEX_NONE;
		int32 Neck = INDEX_NONE;
		int32 Head = INDEX_NONE;
		int32 Arm[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
		int32 Leg[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
	};
	const FBones& CacheBones(const FBoneContainer& Bones);

	FBones Cached;
	uint16 CachedSerial = 0;
	bool bCached = false;
	FArcadePoseInput Input;
};

/**
 * A party game's player avatar: the avatar4 driver mesh (and paid outfit) dressed in an FKartLook, standing on the
 * component's origin and facing its +X, posed natively from an FArcadePoseInput the game fills every frame.
 */
UCLASS(ClassGroup = (Movecade))
class MOVECADEKART_API UArcadeAvatarComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UArcadeAvatarComponent();

	void SetLook(const FKartLook& Look);
	/** The pose to fill this frame; it goes to the anim instance on tick. */
	FArcadePoseInput& Pose() { return Input; }

	/** A bone's world position (rightHand, head, ...), or the component's location if missing. */
	FVector BoneWorld(FName Bone) const;
	USkeletalMeshComponent* GetMesh() const { return Driver; }

	/** Uniform scale of the avatar (the rig is about 2.1 m tall at 1). */
	UPROPERTY(EditAnywhere, Category = "Avatar")
	float AvatarScale = 0.86f;

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Driver;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Outfit;

	MovecadeKart::Avatar::FDressing Dressing;
	FArcadePoseInput Input;
	FKartLook CurrentLook;
	bool bHasLook = false;
	float NextBlink = 2.f;
	float BlinkAt = -10.f;
	float Clock = 0.f;
};
