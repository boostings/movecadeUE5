#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Avatar/ArcadeAvatarPose.h"
#include "Avatar/AvatarDressing.h"
#include "Avatar/AvatarRig.h"
#include "Components/SceneComponent.h"
#include "KartDriverExpression.h"
#include "KartLook.h"
#include "ArcadeAvatar.generated.h"

class USkeletalMeshComponent;

/** What the component measures about the avatar's movement for the proxy's secondary motion (not game input). */
struct FArcadeAvatarMotion
{
	/** The avatar's world velocity in its own frame (cm/s), and its turn rate (rad/s, + to the left). */
	FVector Velocity = FVector::ZeroVector;
	float YawRate = 0.f;
	/** Picks this avatar's reaction variants and idle rhythm (from the component, so two players differ). */
	uint32 Seed = 0;
};

/**
 * The standing avatar's anim instance: no AnimBP asset, it hands the component's pose input to its native proxy
 * (FArcadeAvatarProxy, ArcadeAvatarProxy.h), which poses the avatar4 rig on the animation thread.
 */
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

/** The standing avatar's face (ArcadeAvatarFace.cpp). */
namespace MovecadeKart::Avatar
{
	/** The weights of the face morphs for a standing avatar's pose and mood (pure, so it is unit-tested). */
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
 * component's origin and facing its +X, posed natively from an FArcadePoseInput the game fills every frame. It also
 * blinks and eases its face toward its mood. Games add one per seat and write Pose() each tick.
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
	/** Measures how the avatar moves through the world (its own frame), for the hair and hems. */
	void UpdateMotion(float DeltaTime);
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
