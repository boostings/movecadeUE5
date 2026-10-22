#pragma once

#include "CoreMinimal.h"
#include "Avatar/ArcadeAvatarPose.h"
#include "KartDriverExpression.h"
#include "Math/RandomStream.h"

/**
 * The life in a standing avatar's face (AvatarFaceLife.cpp), on top of its mood (FaceFor): where its eyes look
 * (saccades that lead the head, micro-saccades, smooth pursuit, eye-only glances), when and how it blinks, and the
 * secondary motion of its expression (springy morphs, the cheeks riding a smile, lids following the eyes, brow
 * flashes, an effort's clenched jaw, a cheer's uneven yell). Pure and seeded, so it is unit-tested
 * (AvatarFaceLifeTests); UArcadeAvatarComponent owns one per avatar and writes its output to the morphs and the
 * eye materials.
 */
namespace MovecadeKart::Avatar
{
	/**
	 * The idle glances: now and then the avatar looks somewhere else for a moment. A pure function of the pose
	 * clock and the avatar's seed, so the anim proxy (whose head follows it) and the component (whose eyes lead
	 * the head there) run identical copies.
	 */
	struct MOVECADEKART_API FIdleGlance
	{
		/** Where the glance looks, in the avatar frame, and until when (pose time). */
		FVector Direction = FVector::ForwardVector;
		float Until = 0.f;
		float Next = 1.5f;

		/** Advances to pose time Time for the avatar with this seed. */
		void Step(float Time, uint32 Seed);
		bool IsHeld(float Time) const { return Time < Until; }
	};

	/**
	 * Where the avatar wants to look, in its own frame: an idle glance while nothing else holds its attention, the
	 * game's look, and the point emote's target (PointWeight, 0..1, eases it in).
	 */
	MOVECADEKART_API FVector GazeTarget(const FArcadePoseInput& Pose, const FIdleGlance& Glance, float PointWeight);

	/** How a blink plays: close, hold shut, open (seconds) and how far the lids come down (1 = shut). */
	struct FBlinkShape
	{
		float Close = 0.07f;
		float Hold = 0.03f;
		float Open = 0.16f;
		float Depth = 1.f;

		float Duration() const { return Close + Hold + Open; }
		/** The lids' closure Age seconds into the blink (0 outside it). */
		float At(float Age) const;
	};

	/** The blink variants, so no two blinks in a row look machine-made. */
	enum class EBlinkKind : uint8
	{
		/** The everyday blink. */
		Full,
		/** A quick flutter (the second of a double, or a startle). */
		Quick,
		/** Heavy and slow: a disappointed or drowsy face. */
		Slow,
		/** The lids dip half way and lift again. */
		Half,
		Count
	};

	MOVECADEKART_API FBlinkShape BlinkShape(EBlinkKind Kind);

	/**
	 * Decides when the avatar blinks and how: every few seconds at random (less often while it concentrates, more
	 * when it is excited), sometimes twice, slower and heavier when it is down, and with most big eye jumps.
	 */
	class MOVECADEKART_API FBlinkScheduler
	{
	public:
		explicit FBlinkScheduler(int32 Seed = 0);

		/**
		 * Advances by Dt and returns the lids' closure, 0..1. Focus (0..1) stretches the gaps, Excite (0..1) shortens
		 * them, Drowsy (0..1) picks slow, heavy blinks; bSaccade asks for a blink with a big eye jump.
		 */
		float Step(float Dt, float Focus, float Excite, float Drowsy, bool bSaccade);

		/** The blink playing now (or the last one). */
		EBlinkKind Current() const { return Kind; }
		/** Seconds since the current blink began. */
		float Age() const { return Clock - StartedAt; }

	private:
		void Begin(EBlinkKind NewKind);
		float NextGap(float Focus, float Excite);

		FRandomStream Rng;
		float Clock = 0.f;
		float StartedAt = -10.f;
		float NextAt = 0.f;
		/** A double blink's second half, due at this time (negative: none). */
		float FollowUpAt = -1.f;
		EBlinkKind Kind = EBlinkKind::Full;
	};

	/**
	 * Where the eyes point, in the avatar frame (X forward, Y right, Z up). They hold a fixation and jump to a new
	 * target in a fast saccade whose duration grows with its size (the main sequence), follow a slowly moving
	 * target smoothly, flick in tiny micro-saccades while fixating, and now and then glance aside on their own
	 * when the avatar is idle. The head, which follows the same target on a softer spring, lags behind them.
	 */
	class MOVECADEKART_API FSaccadeScheduler
	{
	public:
		explicit FSaccadeScheduler(int32 Seed = 0);

		/**
		 * Advances by Dt toward Target (a direction in the avatar frame). Idle (0..1) lets the eyes wander on
		 * their own. Returns the eyes' direction; bOutSaccade is set on a frame that starts a big jump.
		 */
		FVector Step(float Dt, const FVector& Target, float Idle, bool& bOutSaccade);

		/** The eyes' direction now, in the avatar frame. */
		FVector Direction() const { return Eyes; }
		/** Whether a saccade is under way. */
		bool InSaccade() const { return SaccadeAge < SaccadeTime; }

	private:
		FVector WithOffsets(const FVector& Fixation) const;

		FRandomStream Rng;
		bool bStarted = false;
		/** The fixation the eyes hold (or are jumping to), and where the jump began. */
		FVector Fixation = FVector::ForwardVector;
		FVector JumpFrom = FVector::ForwardVector;
		float SaccadeAge = 1.f;
		float SaccadeTime = 0.f;
		/** The micro-saccade's offset (yaw, pitch radians) and when the next comes. */
		FVector2f Micro = FVector2f::ZeroVector;
		float NextMicro = 0.f;
		/** An eye-only glance's offset (yaw, pitch radians), until when it holds, and when the next comes. */
		FVector2f Glance = FVector2f::ZeroVector;
		float GlanceUntil = 0.f;
		float NextGlance = 0.f;
		float Clock = 0.f;
		FVector Eyes = FVector::ForwardVector;
	};

	/** What the face's secondary motion reads each frame, besides the mood's own morph targets. */
	struct FFaceLifeInput
	{
		/** The eyes' pitch relative to the head, radians (+ up): the lids follow it. */
		float EyePitch = 0.f;
		/** A big eye jump started this frame: the brows flick with it. */
		bool bSaccade = false;
		/** The pose's moods, 0..1. */
		float Effort = 0.f;
		float Cheer = 0.f;
		float Slump = 0.f;
		/** A greeting emote (a wave) started: an eyebrow flash. */
		bool bGreeting = false;
		float Time = 0.f;
	};

	/**
	 * The expression's secondary motion: each morph rides its own spring toward the mood's target (brows and lids
	 * quick, the mouth slower, a little overshoot), the cheeks lift and the eyes narrow with a broad smile, the
	 * lids track the eyes' pitch, the brows flash on a greeting and lift a touch with big eye jumps, an effort
	 * clenches and grunts, and a cheer yells unevenly.
	 */
	class MOVECADEKART_API FFaceLife
	{
	public:
		explicit FFaceLife(int32 Seed = 0);

		/** Steps every morph toward Target and returns the face to write, plus the lids' extra closure (0..1). */
		void Step(float Dt, const float (&Target)[DriverFace::MorphCount], const FFaceLifeInput& In, float (&OutWeights)[DriverFace::MorphCount],
			float& OutLidClosure);

		/** Starts from Target at rest (a new mesh, the first frame). */
		void Snap(const float (&Target)[DriverFace::MorphCount]);

	private:
		float Value[DriverFace::MorphCount] = {};
		float Velocity[DriverFace::MorphCount] = {};
		bool bStarted = false;
		/** The brow flash's level, decaying. */
		float BrowFlash = 0.f;
		/** This avatar's own phase for its yell and grunt rhythms. */
		float Phase = 0.f;
	};

	/** The eyes' direction relative to the head: yaw (+ to the avatar's right) and pitch (+ up), radians. */
	MOVECADEKART_API FVector2f EyeAngles(const FVector& EyesInHead);

	/** Limits a direction in the head's frame to the eyes' range of motion (an ellipse of yaw and pitch). */
	MOVECADEKART_API FVector ClampEyes(const FVector& EyesInHead, float MaxYaw, float MaxPitch);
}
