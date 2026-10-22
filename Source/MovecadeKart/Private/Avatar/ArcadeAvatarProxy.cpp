#include "Avatar/ArcadeAvatarProxy.h"

#include "Animation/AnimNodeBase.h"

using MovecadeKart::Rig::Bump;
using MovecadeKart::Rig::SoftReach;
using MovecadeKart::Rig::SolveMiddle;

namespace
{
	/** Cartoon proportions over the avatar4 body, as on the karts' drivers: a bigger head and chunkier hands. */
	constexpr double HeadScale = 1.12;
	constexpr double HandScale = 1.18;
	/** How much of the head's turn the neck and the head take (both hang off the torso in avatar4). */
	constexpr float NeckShare = 0.45f;
	constexpr float HeadShare = 0.85f;
	/** The longest frame the springs step in one go (a hitch must not fling the hair). */
	constexpr float MaxStepSeconds = 1.f / 20.f;
	/** A reaction under this level counts as over, and a new one needs this many seconds since the last began. */
	constexpr float ReactionStartLevel = 0.05f;
	constexpr float ReactionRetriggerSeconds = 0.4f;

	// The secondary motion's springs: natural frequency (rad/s) and damping ratio (under 1 overshoots). The reactions'
	// ratios are at full Secondary; at Secondary 0 they settle without overshoot (ratio 1).
	constexpr float CheerOmega = 11.f;
	constexpr float CheerZeta = 0.42f;
	constexpr float SlumpOmega = 7.f;
	constexpr float SlumpZeta = 0.6f;
	constexpr float EmoteOmega = 13.f;
	constexpr float EmoteZeta = 0.6f;
	constexpr float GazeOmega = 16.f;
	constexpr float GazeZeta = 0.85f;
	constexpr float HairOmega = 8.5f;
	constexpr float HairZeta = 0.24f;
	constexpr float HemOmega = 11.f;
	constexpr float HemZeta = 0.32f;
	constexpr float LandOmega = 15.f;
	constexpr float LandZeta = 0.45f;
	/** How far the hair and hems may trail (cm, per axis). */
	constexpr double HairMaxLagCm = 40.0;
	constexpr double HemMaxLagCm = 30.0;

	/** Idle glances: how far the eyes wander (radians), how long a glance holds and the gap to the next (seconds). */
	constexpr float GlanceMaxYaw = 0.75f;
	constexpr float GlanceMaxPitch = 0.18f;
	constexpr float GlanceHoldMin = 0.5f;
	constexpr float GlanceHoldRange = 0.9f;
	constexpr float GlanceGapMin = 2.2f;
	constexpr float GlanceGapRange = 4.f;

	// The pose layers below key their shapes inline (radians of bend, shares of an arm's reach): each number is read
	// with its neighbours as one hand-tuned pose, so they stay where the pose is built.

	/** A small deterministic hash to [0, 1). */
	float Hash01(uint32 A, uint32 B)
	{
		uint32 H = HashCombineFast(A * 2654435761u, B + 0x9e3779b9u);
		H ^= H >> 15;
		H *= 0x2c1b3c6du;
		H ^= H >> 12;
		return static_cast<float>(H & 0xffffff) / static_cast<float>(0x1000000);
	}

	/** One arm's IK goal while the layers (rest, the game's hand, reactions, emotes) blend over each other. */
	struct FArmGoal
	{
		FVector Target;
		FVector Pole;
		float Grip = 0.f;

		void Blend(const FVector& To, float Weight, const FVector& ToPole, float ToGrip)
		{
			const float W = FMath::Clamp(Weight, 0.f, 1.f);
			Target = FMath::Lerp(Target, To, W);
			Pole = FMath::Lerp(Pole, ToPole, W);
			Grip = FMath::Lerp(Grip, ToGrip, W);
		}
	};

	/** What every arm layer needs to know about the arm it moves. */
	struct FArmFrame
	{
		int32 Side = 0;
		FVector Shoulder;
		double Reach = 0.0;
		/** Away from the body on this arm's side (in the torso's frame). */
		FVector Out;
		FVector ShoulderMid;
		float Time = 0.f;
	};
}

FAnimInstanceProxy* UArcadeAvatarAnimInstance::CreateAnimInstanceProxy()
{
	return new FArcadeAvatarProxy(this);
}

void UArcadeAvatarAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FArcadeAvatarProxy*>(InProxy);
}

// ------------------------------------------------------------------------------------------------- PreUpdate

void FArcadeAvatarProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UArcadeAvatarAnimInstance* Instance = Cast<UArcadeAvatarAnimInstance>(InAnimInstance))
	{
		Input = Instance->Input;
		Motion = Instance->Motion;
	}
	Dt = FMath::Min(DeltaSeconds, MaxStepSeconds);
	const float Secondary = FMath::Clamp(Input.Secondary, 0.f, 2.f);
	StepReactions(Secondary);
	StepGaze();
	const float LiftVelocity = Dt > 0.f ? (Input.Lift - LastLift) * 100.f / Dt : 0.f;
	StepHairAndHems(Secondary, LiftVelocity);
	StepLanding(Secondary, LiftVelocity);
}

void FArcadeAvatarProxy::StepReactions(float Secondary)
{
	// Each new reaction picks its variant (Auto: varied per avatar and per reaction), winds up from its start, and
	// the spring carries it past full and back (follow-through).
	const float CheerIn = FMath::Clamp(Input.Cheer, 0.f, 1.f);
	if (CheerIn > ReactionStartLevel && CheerSpring.Value < ReactionStartLevel && CheerAge > ReactionRetriggerSeconds)
	{
		CheerAge = 0.f;
		const int32 Styles = static_cast<int32>(EArcadeCheer::Count) - 1;
		CheerNow = Input.CheerStyle != EArcadeCheer::Auto ? Input.CheerStyle
			: static_cast<EArcadeCheer>(1 + FMath::Min(Styles - 1, static_cast<int32>(Hash01(Motion.Seed, ++Reactions) * Styles)));
	}
	else if (Input.CheerStyle != EArcadeCheer::Auto)
	{
		CheerNow = Input.CheerStyle;
	}
	const float SlumpIn = FMath::Clamp(Input.Slump, 0.f, 1.f);
	if (SlumpIn > ReactionStartLevel && SlumpSpring.Value < ReactionStartLevel && SlumpAge > ReactionRetriggerSeconds)
	{
		SlumpAge = 0.f;
		const int32 Styles = static_cast<int32>(EArcadeSlump::Count) - 1;
		SlumpNow = Input.SlumpStyle != EArcadeSlump::Auto ? Input.SlumpStyle
			: static_cast<EArcadeSlump>(1 + FMath::Min(Styles - 1, static_cast<int32>(Hash01(Motion.Seed ^ 0x5bd1e995u, ++Reactions) * Styles)));
	}
	else if (Input.SlumpStyle != EArcadeSlump::Auto)
	{
		SlumpNow = Input.SlumpStyle;
	}
	CheerAge += Dt;
	SlumpAge += Dt;
	// Secondary 0 settles the reactions without overshoot; 1 lets them bounce.
	const float Bouncy = FMath::Lerp(1.f, CheerZeta, FMath::Min(Secondary, 1.f));
	CheerSpring.Step(CheerIn, CheerOmega, Bouncy, Dt);
	SlumpSpring.Step(SlumpIn, SlumpOmega, FMath::Lerp(1.f, SlumpZeta, FMath::Min(Secondary, 1.f)), Dt);
	EmoteSpring.Step(Input.Emote == EArcadeEmote::None ? 0.f : FMath::Clamp(Input.EmoteWeight, 0.f, 1.f), EmoteOmega, EmoteZeta, Dt);
}

void FArcadeAvatarProxy::StepGaze()
{
	const float T = Input.Time;
	// Idle glances: now and then the eyes (and head) wander, more when nothing else holds them.
	if (T >= NextGlance)
	{
		const uint32 Beat = static_cast<uint32>(NextGlance * 7.f);
		const float Yaw = (Hash01(Motion.Seed, Beat) * 2.f - 1.f) * GlanceMaxYaw;
		const float Pitch = (Hash01(Motion.Seed ^ 0xabcdu, Beat) * 2.f - 1.f) * GlanceMaxPitch;
		Glance = FVector(FMath::Cos(Yaw), -FMath::Sin(Yaw), Pitch).GetSafeNormal();
		GlanceUntil = T + GlanceHoldMin + GlanceHoldRange * Hash01(Beat, Motion.Seed);
		NextGlance = T + GlanceGapMin + GlanceGapRange * Hash01(Beat ^ 0x1234u, Motion.Seed);
	}
	if (NextGlance - T > 12.f)
	{
		NextGlance = T + 2.f; // the clock went back (a restart): keep glancing
	}

	// The gaze target, in the avatar frame, eased: heads turn with weight rather than snapping to a new target.
	const float CheerIn = FMath::Clamp(Input.Cheer, 0.f, 1.f);
	const float SlumpIn = FMath::Clamp(Input.Slump, 0.f, 1.f);
	const float Busy = FMath::Clamp(FMath::Max3(Input.HandWeight[0], Input.HandWeight[1], FMath::Max3(CheerIn, SlumpIn, Input.Effort)), 0.f, 1.f);
	FVector Gaze = FVector::ForwardVector;
	if (T < GlanceUntil)
	{
		Gaze = FMath::Lerp(Gaze, Glance, FMath::Clamp(Input.Idle, 0.f, 1.f) * (1.f - Busy) * (1.f - FMath::Clamp(Input.LookWeight, 0.f, 1.f) * 0.7f)).GetSafeNormal();
	}
	if (Input.LookWeight > 0.f && !Input.Look.IsNearlyZero())
	{
		Gaze = FMath::Lerp(Gaze, Input.Look.GetSafeNormal(), FMath::Clamp(Input.LookWeight, 0.f, 1.f)).GetSafeNormal();
	}
	if (Input.Emote == EArcadeEmote::Point && !Input.PointAt.IsNearlyZero())
	{
		Gaze = FMath::Lerp(Gaze, Input.PointAt.GetSafeNormal(), 0.7f * EmoteSpring.Value).GetSafeNormal();
	}
	if (!bGazeInit)
	{
		GazeSpring.Snap(Gaze);
		bGazeInit = true;
	}
	GazeSpring.Step(Gaze, GazeOmega, GazeZeta, Dt);
}

void FArcadeAvatarProxy::StepHairAndHems(float Secondary, float LiftVelocity)
{
	// Hair and hems trail the body: moving streams them back, starts and stops swing them, turns fling them out,
	// a jump lifts them. The lag (cm, avatar frame) runs on lightly damped springs, so they bounce and settle.
	const FVector Drive = -(Motion.Velocity + FVector(0.0, Motion.YawRate * 35.0, LiftVelocity)) * 0.07;
	const FVector Sway(0.0, FMath::Sin(Input.Time * 1.3f) * 1.5f * Input.Idle, 0.0);
	HairLag.Step((Drive + Sway) * Secondary, HairOmega, HairZeta, Dt);
	HemLag.Step(Drive * 0.7 * Secondary, HemOmega, HemZeta, Dt);
	HairLag.Value = HairLag.Value.BoundToCube(HairMaxLagCm);
	HemLag.Value = HemLag.Value.BoundToCube(HemMaxLagCm);
}

void FArcadeAvatarProxy::StepLanding(float Secondary, float LiftVelocity)
{
	// Coming down from a hop sinks the knees, then springs back up.
	if (LastLift > 0.01f && Input.Lift <= 0.004f && LiftVelocity < 0.f)
	{
		Land.Velocity += FMath::Min(-LiftVelocity, 400.f) * 0.012f * Secondary;
	}
	LastLift = Input.Lift;
	Land.Step(0.f, LandOmega, LandZeta, Dt);
	Land.Value = FMath::Clamp(Land.Value, -0.2f, 0.6f);
}

// -------------------------------------------------------------------------------------------------- Evaluate

bool FArcadeAvatarProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	FCompactPose& Pose = Output.Pose;
	const FBones& B = BoneCache.Get(Pose.GetBoneContainer());
	if (!B.IsPoseable())
	{
		return true; // not an avatar4 rig: keep the reference pose
	}
	MovecadeKart::Rig::LoadPose(Pose, Rig);

	// The avatar's own frame, from its rest skeleton.
	const FBodyFrame Body(Rig, B);
	const FLayers L = MakeLayers();
	PoseLegs(B, Body, L);
	const FTorsoFrame Torso = PoseTorso(B, Body, L);
	PoseShoulders(B, Torso, L);
	const FHeadFrame Head = PoseHead(B, Body, Torso, L);
	const FVector ShoulderMid = (Rig.Pos(B.Arm[0][0]) + Rig.Pos(B.Arm[1][0])) * 0.5;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		PoseArm(Side, B, Body, Torso, Head, ShoulderMid, L);
	}
	SwingSpringChains(B, Body, Torso, Head, L);

	MovecadeKart::Rig::ScaleHeadAndHands(Rig, B, HeadScale, HandScale);
	MovecadeKart::Rig::StorePose(Rig, Pose);
	return true;
}

FArcadeAvatarProxy::FLayers FArcadeAvatarProxy::MakeLayers() const
{
	FLayers L;
	L.Time = Input.Time;
	L.Secondary = FMath::Clamp(Input.Secondary, 0.f, 2.f);
	L.Cheer = FMath::Clamp(CheerSpring.Value, 0.f, 1.15f);
	L.Slump = FMath::Clamp(SlumpSpring.Value, 0.f, 1.1f);
	L.Emote = FMath::Clamp(EmoteSpring.Value, 0.f, 1.f);
	L.Effort = FMath::Clamp(Input.Effort, 0.f, 1.f);
	// How much the game is driving the body: idle life fades as it takes over.
	const float Busy = FMath::Clamp(FMath::Max3(FMath::Max(Input.HandWeight[0], Input.HandWeight[1]), FMath::Max(L.Cheer, L.Slump),
		FMath::Max3(L.Effort, Input.Step, Input.Lunge)), 0.f, 1.f);
	L.Calm = FMath::Clamp(Input.Idle, 0.f, 1.f) * (1.f - Busy);
	L.Phase = Hash01(Motion.Seed, 77u) * UE_TWO_PI;
	// Idle weight shift: the hips settle over one foot, then the other, every few seconds, the free knee easing.
	const float ShiftWave = FMath::Sin(L.Time * 0.62f + L.Phase);
	L.Shift = FMath::Clamp(ShiftWave * 1.8f, -1.f, 1.f) * L.Calm;
	L.Lunge = FMath::Clamp(Input.Lunge, 0.f, 1.f);
	L.Breath = FMath::Sin(L.Time * 1.55f + L.Phase);
	if (Input.Emote == EArcadeEmote::Shrug)
	{
		L.Shrug += L.Emote * (0.6f + 0.4f * Bump(FMath::Fmod(L.Time, 1.6f), 0.f, 0.25f, 1.2f));
	}
	if (SlumpNow == EArcadeSlump::ArmsOut)
	{
		L.Shrug += 0.7f * L.Slump;
	}
	return L;
}

void FArcadeAvatarProxy::PoseLegs(const FBones& B, const FBodyFrame& Body, const FLayers& L)
{
	const float T = L.Time;
	const float Cheer = L.Cheer;
	const FVector& Forward = Body.Forward;
	const FVector& Right = Body.Right;
	const FVector& Up = Body.Up;
	const FVector& Down = Body.Down;

	// The reactions' wind-up: a quick dip of the knees before the celebration springs up.
	const float CheerDip = Bump(CheerAge, 0.f, 0.09f, 0.24f) * FMath::Clamp(Input.Cheer, 0.f, 1.f) * FMath::Min(L.Secondary, 1.f);
	float ExtraCrouch = 0.38f * CheerDip + Land.Value;
	float ExtraLift = 0.f;
	if (Cheer > 0.f)
	{
		switch (CheerNow)
		{
		case EArcadeCheer::FistPump: ExtraCrouch += 0.16f * Cheer * FMath::Abs(FMath::Sin(T * 6.f)); break;
		case EArcadeCheer::Jump: ExtraLift += 0.16f * Cheer * FMath::Abs(FMath::Sin(T * 5.2f)); ExtraCrouch += 0.12f * Cheer * (1.f - FMath::Abs(FMath::Sin(T * 5.2f))); break;
		case EArcadeCheer::Clap: ExtraCrouch += 0.06f * Cheer * FMath::Abs(FMath::Sin(T * 4.5f)); break;
		default: break;
		}
	}
	if (L.Slump > 0.f && SlumpNow == EArcadeSlump::Droop)
	{
		ExtraCrouch += 0.1f * L.Slump;
	}

	// Knees bend for a crouch, swing for a stride, and split into the bowler's slide; the hips drop to keep the feet down.
	double LegLength = 0.0;
	if (B.HasLimb(B.Leg[0]))
	{
		LegLength = (Rig.Pos(B.Leg[0][1]) - Rig.Pos(B.Leg[0][0])).Size() + (Rig.Pos(B.Leg[0][2]) - Rig.Pos(B.Leg[0][1])).Size();
	}
	const float Shift = L.Shift;
	const float Lunge = L.Lunge;
	const float Crouch = FMath::Clamp(Input.Crouch + ExtraCrouch, 0.f, 1.f);
	const float CrouchPitch = Crouch * 0.62f;
	const double HipShift = LegLength * 0.035 * Shift;
	const double Drop = LegLength * ((1.0 - FMath::Cos(CrouchPitch)) * 1.6 + Lunge * 0.22) + LegLength * 0.02 * Input.Step * FMath::Abs(FMath::Sin(Input.StepPhase))
		+ LegLength * 0.006 * FMath::Abs(Shift);
	Rig.Local[B.Root].AddToTranslation(Down * Drop + Up * (Input.Lift + ExtraLift) * 100.0 + Right * HipShift);
	Rig.Recompute();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!B.HasLimb(B.Leg[Side]))
		{
			continue;
		}
		const int32 Upper = B.Leg[Side][0], Lower = B.Leg[Side][1], Foot = B.Leg[Side][2];
		const float Swing = Input.Step * 0.42f * FMath::Sin(Input.StepPhase + Side * UE_PI);
		const float Lift = Input.Step * 0.7f * FMath::Max(0.f, FMath::Cos(Input.StepPhase + Side * UE_PI));
		// The slide: the left leg plants forward and bent, the right trails behind.
		const float SlideThigh = Side == 0 ? 0.95f * Lunge : -0.6f * Lunge;
		const float SlideKnee = Side == 0 ? 1.15f * Lunge : 0.25f * Lunge;
		// The leg that isn't carrying the weight relaxes: knee forward, heel a touch off.
		const float Relax = FMath::Max(0.f, Side == 0 ? Shift : -Shift);
		const float Thigh = CrouchPitch + Swing + SlideThigh + Lift * 0.4f + 0.07f * Relax;
		const float Knee = CrouchPitch * 2.f + Lift + SlideKnee + 0.16f * Relax;
		const FVector Out = Side == 0 ? -Right : Right;
		// Lean the legs back under the shifted hips, so the feet stay planted.
		const FVector Plant = -Right * (LegLength > 1.0 ? HipShift / LegLength : 0.0);
		Rig.Aim(Upper, Lower, (Down * FMath::Cos(Thigh) + Forward * FMath::Sin(Thigh) + Out * 0.04f + Plant).GetSafeNormal());
		const float Shin = Thigh - Knee;
		Rig.Aim(Lower, Foot, (Down * FMath::Cos(Shin) + Forward * FMath::Sin(Shin) + Out * 0.02f + Plant * 0.5).GetSafeNormal());
	}
}

FArcadeAvatarProxy::FTorsoFrame FArcadeAvatarProxy::PoseTorso(const FBones& B, const FBodyFrame& Body, const FLayers& L)
{
	// Lean, twist, breath; each reaction bends it its own way; the hips' shift tilts it back over the feet.
	const float T = L.Time;
	const float Cheer = L.Cheer;
	const float Slump = L.Slump;
	float ReactLean = 0.f, ReactRoll = 0.f;
	switch (CheerNow)
	{
	case EArcadeCheer::Jump: ReactLean = -0.16f * Cheer; break;
	case EArcadeCheer::FistPump: ReactLean = 0.06f * Cheer; break;
	case EArcadeCheer::Victory: ReactLean = -0.1f * Cheer; ReactRoll = -0.08f * Cheer; break;
	default: ReactLean = -0.12f * Cheer; break;
	}
	switch (SlumpNow)
	{
	case EArcadeSlump::HandsOnHead: ReactLean += -0.06f * Slump; break;
	case EArcadeSlump::Facepalm: ReactLean += 0.18f * Slump; break;
	case EArcadeSlump::ArmsOut: ReactLean += -0.1f * Slump; break;
	default: ReactLean += 0.28f * Slump; break;
	}
	const float Tremble = L.Effort * 0.012f * FMath::Sin(T * 31.f) * FMath::Sin(T * 7.3f);
	const float Lean = Input.Lean + L.Breath * 0.016f * Input.Idle + ReactLean + Tremble;
	const float Twist = Input.Twist + FMath::Sin(T * 0.43f + L.Phase) * 0.03f * Input.Idle;
	const float Roll = ReactRoll - 0.05f * L.Shift;

	FTorsoFrame Torso;
	Torso.Delta = FQuat(Body.Up, Twist) * FQuat(Body.Forward, Roll) * FQuat(Body.Right, Lean);
	Rig.Rotate(B.Torso, Torso.Delta);
	Torso.Forward = Torso.Delta.RotateVector(Body.Forward);
	Torso.Right = Torso.Delta.RotateVector(Body.Right);
	Torso.Up = Torso.Delta.RotateVector(Body.Up);
	return Torso;
}

void FArcadeAvatarProxy::PoseShoulders(const FBones& B, const FTorsoFrame& Torso, const FLayers& L)
{
	// Shoulders rise with each breath, hunch in a shrug, lift in a cheer, drop in a slump.
	const bool bDroop = SlumpNow == EArcadeSlump::Droop;
	const float Shoulders = 0.05f * L.Breath * Input.Idle + 0.25f * L.Shrug + 0.12f * L.Cheer - (bDroop ? 0.22f * L.Slump : 0.f) + 0.12f * L.Effort;
	const float Hunch = bDroop ? L.Slump : 0.f;
	if (FMath::Abs(Shoulders) <= 1e-3f && Hunch <= 1e-3f)
	{
		return;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		// The clavicles are leaf joints in avatar4: the shoulder joint itself moves, and the clavicle turns after it.
		const int32 Clavicle = B.Clavicle[Side], Upper = B.Arm[Side][0];
		const FVector Before = Rig.Pos(Upper);
		// Sized to the torso (66 cm from torso to head on the reference avatar).
		Rig.Translate(Upper, (Torso.Up * Shoulders * 16.f - Torso.Forward * Hunch * 3.5f) * (Rig.Pos(B.Head) - Rig.Pos(B.Torso)).Size() / 66.0);
		if (Clavicle != INDEX_NONE)
		{
			Rig.TurnToward(Clavicle, Before, Rig.Pos(Upper));
		}
	}
}

FArcadeAvatarProxy::FHeadFrame FArcadeAvatarProxy::PoseHead(const FBones& B, const FBodyFrame& Body, const FTorsoFrame& Torso, const FLayers& L)
{
	// The eased gaze, chin up in a cheer and down in a slump; a facepalm bows into the hand.
	const FVector Facing = Torso.Forward;
	FVector Gaze = Body.ToComponent(GazeSpring.Value).GetSafeNormal();
	float SlumpHead = 0.6f;
	switch (SlumpNow)
	{
	case EArcadeSlump::HandsOnHead: SlumpHead = -0.25f; break;
	case EArcadeSlump::ArmsOut: SlumpHead = -0.35f; break;
	case EArcadeSlump::Facepalm: SlumpHead = 0.75f; break;
	default: break;
	}
	Gaze = (Gaze + Body.Up * (0.35f * L.Cheer) + Body.Down * (SlumpHead * L.Slump)).GetSafeNormal();
	FQuat HeadDelta = FQuat::FindBetweenNormals(Facing, FMath::Lerp(Facing, Gaze, 0.8f).GetSafeNormal());
	// A head tilt: a shrug cocks it, idle lets it lean a little with the hips.
	const float Tilt = 0.18f * L.Shrug + 0.04f * L.Shift + (SlumpNow == EArcadeSlump::ArmsOut ? 0.12f * L.Slump : 0.f);
	HeadDelta = FQuat(HeadDelta.RotateVector(Facing), Tilt) * HeadDelta;
	if (B.Neck != INDEX_NONE)
	{
		Rig.Rotate(B.Neck, FQuat::Slerp(FQuat::Identity, HeadDelta, NeckShare));
	}
	FHeadFrame Head;
	Head.Turn = FQuat::Slerp(FQuat::Identity, HeadDelta, HeadShare);
	Rig.Rotate(B.Head, Head.Turn);
	Head.Position = Rig.Pos(B.Head);
	Head.Up = (Head.Turn * Torso.Delta).RotateVector(Body.Up);
	Head.Forward = (Head.Turn * Torso.Delta).RotateVector(Body.Forward);
	return Head;
}

namespace
{
	/** The celebration's arms (CheerW 0..1). */
	void BlendCheer(FArmGoal& Goal, const FArmFrame& Arm, const FArcadeAvatarProxy::FTorsoFrame& Torso, const FVector& Down, EArcadeCheer Style, float CheerW)
	{
		const float T = Arm.Time;
		const FVector& Shoulder = Arm.Shoulder;
		const FVector& Out = Arm.Out;
		const double Reach = Arm.Reach;
		switch (Style)
		{
		case EArcadeCheer::FistPump:
		{
			const float Pump = FMath::Abs(FMath::Sin(T * 6.f + Arm.Side * 0.6f));
			Goal.Blend(Shoulder + (Torso.Up * 0.75f + Out * 0.3f + Torso.Forward * 0.3f).GetSafeNormal() * Reach * (0.55 + 0.32 * Pump), CheerW, Out * 0.8f + Down * 0.5f, 1.f);
			break;
		}
		case EArcadeCheer::Jump:
		{
			const float Fling = FMath::Abs(FMath::Sin(T * 5.2f));
			Goal.Blend(Shoulder + (Torso.Up * (0.6f + 0.35f * Fling) + Out * (0.75f - 0.25f * Fling) + Torso.Forward * 0.15f).GetSafeNormal() * Reach * 0.95, CheerW, Out * 0.5f + Down * 0.6f, 0.1f);
			break;
		}
		case EArcadeCheer::Clap:
		{
			const float Open = 0.5f + 0.5f * FMath::Sin(T * 9.f);
			const FVector Above = Arm.ShoulderMid + Torso.Up * Reach * 0.82 + Torso.Forward * Reach * 0.18 + Out * Reach * (0.03 + 0.13 * Open);
			Goal.Blend(Above, CheerW, Out * 0.7f + Torso.Forward * 0.3f, 0.f);
			break;
		}
		case EArcadeCheer::Victory:
			if (Arm.Side == 1)
			{
				const float Punch = 0.92f + 0.06f * FMath::Sin(T * 3.f);
				Goal.Blend(Shoulder + (Torso.Up * 0.95f + Out * 0.12f + Torso.Forward * 0.1f).GetSafeNormal() * Reach * Punch, CheerW, Out + Down * 0.3f, 1.f);
			}
			else
			{
				// The other hand on the hip, elbow out.
				Goal.Blend(Shoulder + (Down * 0.62f + Out * 0.32f - Torso.Forward * 0.06f).GetSafeNormal() * Reach * 0.66, CheerW, Out * 0.9f - Torso.Forward * 0.3f, 0.7f);
			}
			break;
		default:
		{
			const float Wave = FMath::Sin(T * 7.f + Arm.Side * UE_PI) * 0.16f;
			Goal.Blend(Shoulder + (Torso.Up * 0.9f + Out * (0.36f + Wave) + Torso.Forward * 0.12f).GetSafeNormal() * Reach * 0.94, CheerW, Out * 0.55f + Down * 0.45f - Torso.Forward * 0.45f, 0.2f);
			break;
		}
		}
	}

	/** The disappointment's arms (SlumpW 0..1). */
	void BlendSlump(FArmGoal& Goal, const FArmFrame& Arm, const FArcadeAvatarProxy::FTorsoFrame& Torso, const FArcadeAvatarProxy::FHeadFrame& Head, const FVector& Down,
		EArcadeSlump Style, float SlumpW)
	{
		const FVector& Shoulder = Arm.Shoulder;
		const FVector& Out = Arm.Out;
		const double Reach = Arm.Reach;
		switch (Style)
		{
		case EArcadeSlump::HandsOnHead:
			// Wrists at the crown, palms flat on the head (the head bone sits at the neck; the crown is ~0.6 arm above).
			Goal.Blend(Head.Position + Head.Up * Reach * 0.58 + Out * Reach * 0.17 - Head.Forward * Reach * 0.04, SlumpW, Out + Torso.Up * 0.4f, 0.1f);
			break;
		case EArcadeSlump::Facepalm:
			if (Arm.Side == 1)
			{
				// The wrist under the brow, the palm over the eyes.
				Goal.Blend(Head.Position + Head.Forward * Reach * 0.3 + Head.Up * Reach * 0.24 + Out * Reach * 0.03, SlumpW, Out * 0.4f + Down, 0.15f);
			}
			break;
		case EArcadeSlump::ArmsOut:
		{
			const float Beat = 0.04f * FMath::Sin(Arm.Time * 3.f);
			Goal.Blend(Shoulder + (Down * (0.55f + Beat) + Out * 0.6f + Torso.Forward * 0.4f).GetSafeNormal() * Reach * 0.93, SlumpW, Down + Out * 0.3f - Torso.Forward * 0.2f, 0.f);
			break;
		}
		default:
			// Arms hang heavy and a little forward.
			Goal.Blend(Shoulder + (Down + Torso.Forward * 0.16f + Out * 0.08f).GetSafeNormal() * Reach * 0.98, SlumpW * 0.6f, Goal.Pole, 0.15f);
			break;
		}
	}

	/** A gesture's arms (Weight 0..1). One-handed emotes move only the EmoteHand side. */
	void BlendEmote(FArmGoal& Goal, const FArmFrame& Arm, const FArcadeAvatarProxy::FTorsoFrame& Torso, const FVector& Down, const FArcadePoseInput& Input,
		const FVector& PointDir, float Weight)
	{
		const float T = Arm.Time;
		const FVector& Shoulder = Arm.Shoulder;
		const FVector& Out = Arm.Out;
		const double Reach = Arm.Reach;
		const bool bMine = Arm.Side == FMath::Min<int32>(Input.EmoteHand, 1);
		switch (Input.Emote)
		{
		case EArcadeEmote::Wave:
			if (bMine)
			{
				const float Swing = FMath::Sin(T * 8.f);
				const FVector To = Shoulder + (Torso.Up * 0.72f + Out * (0.42f + 0.2f * Swing) + PointDir * 0.25f).GetSafeNormal() * Reach * 0.82;
				Goal.Blend(To, Weight, Out * 0.6f + Down * 0.8f, 0.f);
			}
			break;
		case EArcadeEmote::Point:
			if (bMine)
			{
				Goal.Blend(SoftReach(Shoulder, Shoulder + (PointDir + Torso.Up * 0.08f).GetSafeNormal() * Reach, Reach, 0.06), Weight, Out * 0.4f + Down, 0.25f);
			}
			break;
		case EArcadeEmote::Clap:
		{
			const float Open = 0.5f + 0.5f * FMath::Sin(T * 10.f);
			const FVector Front = Arm.ShoulderMid + Torso.Forward * Reach * 0.52 + Down * Reach * 0.22 + Out * Reach * (0.025 + 0.14 * Open);
			Goal.Blend(Front, Weight, Out + Down * 0.6f, 0.f);
			break;
		}
		case EArcadeEmote::FistPump:
			if (bMine)
			{
				const float Pump = FMath::Abs(FMath::Sin(T * 6.5f));
				Goal.Blend(Shoulder + (Torso.Up * 0.65f + Out * 0.22f + Torso.Forward * 0.35f).GetSafeNormal() * Reach * (0.55 + 0.3 * Pump), Weight, Out * 0.8f + Down * 0.5f, 1.f);
			}
			break;
		case EArcadeEmote::Shrug:
			Goal.Blend(Shoulder + (Down * 0.5f + Out * 0.45f + Torso.Forward * 0.6f).GetSafeNormal() * Reach * 0.74, Weight, Down + Out * 0.2f - Torso.Forward * 0.3f, 0.f);
			break;
		default:
			break;
		}
	}
}

void FArcadeAvatarProxy::PoseArm(int32 Side, const FBones& B, const FBodyFrame& Body, const FTorsoFrame& Torso, const FHeadFrame& Head, const FVector& ShoulderMid,
	const FLayers& L)
{
	// Two-bone IK onto the hand's target, or hanging at rest, with the reaction and emote layered on top.
	const int32 Upper = B.Arm[Side][0], Fore = B.Arm[Side][1], Hand = B.Arm[Side][2];
	if (Fore == INDEX_NONE || Hand == INDEX_NONE)
	{
		return;
	}
	const float T = L.Time;
	const FVector& Down = Body.Down;
	FArmFrame Arm;
	Arm.Side = Side;
	Arm.Shoulder = Rig.Pos(Upper);
	const double L1 = (Rig.Pos(Fore) - Arm.Shoulder).Size();
	const double L2 = (Rig.Pos(Hand) - Rig.Pos(Fore)).Size();
	Arm.Reach = L1 + L2;
	Arm.Out = Side == 0 ? -Torso.Right : Torso.Right;
	Arm.ShoulderMid = ShoulderMid;
	Arm.Time = T;
	const FVector& Shoulder = Arm.Shoulder;
	const FVector& Out = Arm.Out;
	const double Reach = Arm.Reach;

	// The base: hanging at rest, or the game's hand target.
	const FVector Rest = (Torso.Forward * 0.08f + Out * (0.2f + 0.02f * FMath::Sin(T * 1.1f + Side)) + Down * 0.94f).GetSafeNormal() * Reach * 0.97;
	const FVector& H = Input.Hand[Side];
	const FVector Given = (Input.HandSpace == EArcadeHandSpace::Avatar ? Body.ToComponent(H)
		: Torso.Forward * H.X + Torso.Right * H.Y + Torso.Up * H.Z) * Reach;
	const float GivenWeight = FMath::Clamp(Input.HandWeight[Side], 0.f, 1.f);
	FArmGoal Goal;
	Goal.Target = Shoulder + FMath::Lerp(Rest, Given, GivenWeight);
	Goal.Pole = Out * 0.55f + Down * 0.45f - Torso.Forward * 0.45f;
	Goal.Grip = 0.3f + 0.4f * GivenWeight; // relaxed, or holding something

	if (L.Cheer > 0.f)
	{
		BlendCheer(Goal, Arm, Torso, Down, CheerNow, FMath::Min(L.Cheer, 1.f));
	}
	if (L.Slump > 0.f)
	{
		BlendSlump(Goal, Arm, Torso, Head, Down, SlumpNow, FMath::Min(L.Slump, 1.f));
	}
	if (L.Emote > 0.f && Input.Emote != EArcadeEmote::None)
	{
		const FVector PointDir = Body.ToComponent(Input.PointAt.IsNearlyZero() ? FVector::ForwardVector : Input.PointAt.GetSafeNormal());
		BlendEmote(Goal, Arm, Torso, Down, Input, PointDir, L.Emote);
	}

	// Straining arms tremble.
	if (L.Effort > 0.f)
	{
		Goal.Target += (Torso.Up * FMath::Sin(T * 37.f + Side) + Out * FMath::Sin(T * 29.f + 2.f * Side)) * Reach * 0.006 * L.Effort;
	}
	// The game's elbow hint, and its hand closure, win over the automatic ones.
	if (!Input.ElbowHint[Side].IsNearlyZero())
	{
		Goal.Pole = Body.ToComponent(Input.ElbowHint[Side].GetSafeNormal());
	}
	if (Input.Grip[Side] >= 0.f)
	{
		Goal.Grip = Input.Grip[Side];
	}
	// Soft reach: arms ease into full extension instead of snapping straight.
	const FVector Target = SoftReach(Shoulder, Goal.Target, Reach);
	const FVector Elbow = SolveMiddle(Shoulder, Target, L1, L2, Goal.Pole);
	Rig.Aim(Upper, Fore, Elbow - Shoulder);
	Rig.Aim(Fore, Hand, Target - Rig.Pos(Fore));
	// Fingers: curl toward the palm about the knuckles' axis (across the hand, toward the thumb).
	MovecadeKart::Rig::CurlFingers(Rig, Hand, B.Fingers[Side], B.Thumb[Side], Goal.Grip);
}

void FArcadeAvatarProxy::SwingSpringChains(const FBones& B, const FBodyFrame& Body, const FTorsoFrame& Torso, const FHeadFrame& Head, const FLayers& L)
{
	// Hair and hems swing with the lag (pitch back when it trails behind, roll when it swings sideways), each joint
	// taking a share so the tips curl furthest.
	auto Swing = [this](const auto& Chain, const FVector& Side, const FVector& Ahead, float Pitch, float RollAngle)
	{
		if (FMath::Abs(Pitch) + FMath::Abs(RollAngle) < 1e-4f)
		{
			return;
		}
		const float PerJoint = 1.f / FMath::Sqrt(static_cast<float>(FMath::Max(1, Chain.Num())));
		for (const int32 Bone : Chain)
		{
			Rig.Rotate(Bone, FQuat(Ahead, RollAngle * PerJoint) * FQuat(Side, -Pitch * PerJoint));
		}
	};
	const FQuat HeadFrame = Head.Turn * Torso.Delta;
	const FVector& HairCm = HairLag.Value;
	const FVector& HemCm = HemLag.Value;
	Swing(B.Hair, HeadFrame.RotateVector(Body.Right), HeadFrame.RotateVector(Body.Forward),
		FMath::Clamp(static_cast<float>(-HairCm.X * 0.018 - HairCm.Z * 0.01) + 0.05f * L.Cheer, -0.6f, 0.8f), FMath::Clamp(static_cast<float>(HairCm.Y * 0.018), -0.6f, 0.6f));
	const float StrideFlap = 0.12f * Input.Step * FMath::Sin(Input.StepPhase * 2.f) + 0.1f * L.Lunge;
	Swing(B.Hem, Torso.Delta.RotateVector(Body.Right), Torso.Delta.RotateVector(Body.Forward),
		FMath::Clamp(static_cast<float>(-HemCm.X * 0.012 - HemCm.Z * 0.008) + StrideFlap, -0.5f, 0.5f), FMath::Clamp(static_cast<float>(HemCm.Y * 0.01), -0.4f, 0.4f));
}
