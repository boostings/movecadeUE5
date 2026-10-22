#pragma once

#include "CoreMinimal.h"
#include "Avatar/ArcadeAvatarPose.h"
#include "KartDriverExpression.h"

class USkeletalMeshComponent;

/** The standing avatar's face (ArcadeAvatarFace.cpp): its mood's morph weights, and a writer that skips no-op writes. */
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
