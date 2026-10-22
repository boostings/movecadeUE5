#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Avatar/AvatarDressing.h"
#include "Avatar/AvatarRig.h"
#include "Components/SceneComponent.h"
#include "KartDriverExpression.h"
#include "KartLook.h"
#include "ArcadeAvatar.generated.h"

class USkeletalMeshComponent;

/** A gesture the avatar plays with its arms on top of the pose (FArcadePoseInput::Emote). */
enum class EArcadeEmote : uint8
{
	None,
	/** One hand up, waving side to side (EmoteHand). */
	Wave,
	/** One arm straight out toward PointAt, finger extended. */
	Point,
	/** Both hands meeting in front of the chest, in rhythm. */
	Clap,
	/** One fist pumped up and down. */
	FistPump,
	/** Shoulders up, palms out: "what?". */
	Shrug,
};

/** How a celebration looks (FArcadePoseInput::CheerStyle). Auto picks a fresh one per celebration, per avatar. */
enum class EArcadeCheer : uint8
{
	Auto,
	/** Both arms up in a V, waving (the original). */
	Wave,
	/** Both fists pumped, knees bouncing. */
	FistPump,
	/** Hopping on the spot, arms flung up. */
	Jump,
	/** Clapping overhead. */
	Clap,
	/** One fist punched to the sky, the other on the hip. */
	Victory,
	Count,
};

/** How a disappointment looks (FArcadePoseInput::SlumpStyle). Auto picks per reaction, per avatar. */
enum class EArcadeSlump : uint8
{
	Auto,
	/** Head down, shoulders dropped, arms hanging (the original). */
	Droop,
	/** Hands on the head: "no way". */
	HandsOnHead,
	/** Facepalm with one hand. */
	Facepalm,
	/** Arms flung out low, shoulders up: "why?". */
	ArmsOut,
	Count,
};

/** Which frame FArcadePoseInput::Hand is given in. */
enum class EArcadeHandSpace : uint8
{
	/** The posed torso's frame (lean and twist carry the hands): the original behaviour. */
	Torso,
	/** The avatar's own unposed frame, from the rest shoulder: hands stay where the game put them as the body moves. */
	Avatar,
};

/**
 * What a game asks of its standing player avatar each frame (the party games' share of the web's avatar motion
 * system, Docs/Port/05 §3). Directions are in the avatar's own frame: X forward, Y to its right, Z up. Hand
 * targets are relative to that side's shoulder, in arm lengths, so the same numbers fit every body.
 *
 * Every field has a neutral default, so a game sets only what it needs and the avatar does the rest: it breathes,
 * shifts its weight and glances about when idle, its hair and hems swing as it moves, its face follows its mood,
 * and its reactions wind up and overshoot.
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
	/** A celebration, 0..1 (its look: CheerStyle). */
	float Cheer = 0.f;
	/** A miss or a loss, 0..1 (its look: SlumpStyle). */
	float Slump = 0.f;
	/** Breath, weight shifts, glances and sway, 0..1. */
	float Idle = 1.f;

	// ---- Added for the richer avatar (all neutral by default). ----

	/** A gesture over the arms, and how much of it (0..1; the arms blend from the pose into it). */
	EArcadeEmote Emote = EArcadeEmote::None;
	float EmoteWeight = 0.f;
	/** The hand a one-handed emote uses: 0 left, 1 right. */
	uint8 EmoteHand = 1;
	/** Where Point points (and Wave faces), a direction in the avatar frame. */
	FVector PointAt = FVector::ForwardVector;
	/** The celebration and disappointment variants; Auto varies them per reaction. */
	EArcadeCheer CheerStyle = EArcadeCheer::Auto;
	EArcadeSlump SlumpStyle = EArcadeSlump::Auto;
	/** Straining (a charge, a heave), 0..1: teeth set, brows down, a tremble in the arms. */
	float Effort = 0.f;
	/** Where each elbow should point, a direction in the avatar frame; zero lets the avatar choose. */
	FVector ElbowHint[2] = { FVector::ZeroVector, FVector::ZeroVector };
	/** How closed each hand is, 0 open .. 1 fist; negative lets the avatar choose (relaxed, gripping, fists). */
	float Grip[2] = { -1.f, -1.f };
	/** The frame Hand is in. */
	EArcadeHandSpace HandSpace = EArcadeHandSpace::Torso;
	/** Scales the avatar's own secondary motion (hair, hems, reactions' wind-up and overshoot), 0..1+. */
	float Secondary = 1.f;
};

/** What the component measures about the avatar's movement for the proxy's secondary motion (not game input). */
struct FArcadeAvatarMotion
{
	/** The avatar's world velocity in its own frame (cm/s), and its turn rate (rad/s, + to the left). */
	FVector Velocity = FVector::ZeroVector;
	float YawRate = 0.f;
	/** Picks this avatar's reaction variants and idle rhythm (from the component, so two players differ). */
	uint32 Seed = 0;
};

UCLASS(Transient, NotBlueprintable)
class MOVECADEKART_API UArcadeAvatarAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	FArcadePoseInput Input;
	FArcadeAvatarMotion Motion;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
};

/**
 * Poses the avatar4 rig standing: stance, steps, lunge, arm IK to the hand targets (soft reach, elbow hints),
 * finger curl, gaze, emotes, celebration and disappointment variants, idle life, and spring-driven hair and hems.
 */
struct FArcadeAvatarProxy : public FAnimInstanceProxy
{
	FArcadeAvatarProxy() = default;
	explicit FArcadeAvatarProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	/** The joints the pose drives, as compact-pose indices (INDEX_NONE if missing). */
	struct FBones
	{
		int32 Root = INDEX_NONE;
		int32 Torso = INDEX_NONE;
		int32 Neck = INDEX_NONE;
		int32 Head = INDEX_NONE;
		int32 Clavicle[2] = { INDEX_NONE, INDEX_NONE };
		int32 Arm[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
		int32 Fingers[2] = { INDEX_NONE, INDEX_NONE };
		int32 Thumb[2] = { INDEX_NONE, INDEX_NONE };
		int32 Leg[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
		TArray<int32, TInlineAllocator<8>> Hair;
		TArray<int32, TInlineAllocator<4>> Hem;
	};

private:
	const FBones& CacheAvatarBones(const FBoneContainer& Bones);

	FBones Cached;
	uint16 CachedSerial = 0;
	bool bCached = false;
	FArcadePoseInput Input;
	FArcadeAvatarMotion Motion;
	/** Reused every evaluation (no allocation). */
	MovecadeKart::Rig::FRigWork Rig;

	// Secondary motion, stepped on the game thread in PreUpdate.
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
	FVector Glance = FVector::ForwardVector;
	float GlanceUntil = 0.f, NextGlance = 1.5f;
	/** Hair and hem lag, in cm, in the avatar frame, on bouncy springs. */
	MovecadeKart::Rig::TSpring<FVector> HairLag, HemLag;
	/** Landing squash from Lift. */
	float LastLift = 0.f;
	MovecadeKart::Rig::TSpring<float> Land;
};

/** The weights of the face morphs for a standing avatar's pose and mood (pure, so it is unit-tested). */
namespace MovecadeKart::Avatar
{
	MOVECADEKART_API void FaceFor(const FArcadePoseInput& Pose, float (&OutWeights)[MovecadeKart::DriverFace::MorphCount]);

	/**
	 * Writes morph targets only when they change: the morph map lookups and the render-state dirtying cost real
	 * time with a dozen avatars, and most frames the face holds still.
	 */
	struct MOVECADEKART_API FMorphWriter
	{
		/** Sets Name to Weight if it moved by more than Epsilon since the last write. Returns whether it wrote. */
		bool Set(USkeletalMeshComponent* Mesh, int32 Slot, FName Name, float Weight, float Epsilon = 0.002f);
		/** Forget what was written (after a mesh swap, which clears the morphs). */
		void Reset() { Written.Reset(); }

	private:
		TArray<float, TInlineAllocator<16>> Written;
	};

	/** The slot FMorphWriter uses for the blink (after the expression morphs). */
	constexpr int32 BlinkSlot = MovecadeKart::DriverFace::MorphCount;
}

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
	void UpdateFace(float DeltaTime);

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Driver;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Outfit;

	MovecadeKart::Avatar::FDressing Dressing;
	MovecadeKart::Avatar::FMorphWriter Morphs;
	FArcadePoseInput Input;
	FKartLook CurrentLook;
	bool bHasLook = false;
	float NextBlink = 2.f;
	float BlinkAt = -10.f;
	float Clock = 0.f;
	float Face[MovecadeKart::DriverFace::MorphCount] = {};
	FTransform LastTransform = FTransform::Identity;
	bool bHasLastTransform = false;
	FVector SmoothedVelocity = FVector::ZeroVector;
	float SmoothedYawRate = 0.f;
	FVector LastLook = FVector::ForwardVector;
};
