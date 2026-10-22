#include "Avatar/ArcadeAvatar.h"

#include "Animation/AnimNodeBase.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

using MovecadeKart::Rig::Bump;
using MovecadeKart::Rig::FRigWork;
using MovecadeKart::Rig::Smooth01;
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

	/** A small deterministic hash to [0, 1). */
	float Hash01(uint32 A, uint32 B)
	{
		uint32 H = HashCombineFast(A * 2654435761u, B + 0x9e3779b9u);
		H ^= H >> 15;
		H *= 0x2c1b3c6du;
		H ^= H >> 12;
		return static_cast<float>(H & 0xffffff) / static_cast<float>(0x1000000);
	}

	FVector AvatarDir(const FVector& V, const FVector& Forward, const FVector& Right, const FVector& Up)
	{
		return Forward * V.X + Right * V.Y + Up * V.Z;
	}
}

FAnimInstanceProxy* UArcadeAvatarAnimInstance::CreateAnimInstanceProxy()
{
	return new FArcadeAvatarProxy(this);
}

void UArcadeAvatarAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FArcadeAvatarProxy*>(InProxy);
}

void FArcadeAvatarProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UArcadeAvatarAnimInstance* Instance = Cast<UArcadeAvatarAnimInstance>(InAnimInstance))
	{
		Input = Instance->Input;
		Motion = Instance->Motion;
	}
	Dt = FMath::Min(DeltaSeconds, 1.f / 20.f);
	const float Secondary = FMath::Clamp(Input.Secondary, 0.f, 2.f);
	const float T = Input.Time;

	// Reactions: each new one picks its variant (Auto: varied per avatar and per reaction), winds up from its
	// start, and the spring carries it past full and back (follow-through).
	const float CheerIn = FMath::Clamp(Input.Cheer, 0.f, 1.f);
	if (CheerIn > 0.05f && CheerSpring.Value < 0.05f && CheerAge > 0.4f)
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
	if (SlumpIn > 0.05f && SlumpSpring.Value < 0.05f && SlumpAge > 0.4f)
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
	const float Bouncy = FMath::Lerp(1.f, 0.42f, FMath::Min(Secondary, 1.f));
	CheerSpring.Step(CheerIn, 11.f, Bouncy, Dt);
	SlumpSpring.Step(SlumpIn, 7.f, FMath::Lerp(1.f, 0.6f, FMath::Min(Secondary, 1.f)), Dt);
	EmoteSpring.Step(Input.Emote == EArcadeEmote::None ? 0.f : FMath::Clamp(Input.EmoteWeight, 0.f, 1.f), 13.f, 0.6f, Dt);

	// Idle glances: now and then the eyes (and head) wander, more when nothing else holds them.
	if (T >= NextGlance)
	{
		const uint32 Beat = static_cast<uint32>(NextGlance * 7.f);
		const float Yaw = (Hash01(Motion.Seed, Beat) * 2.f - 1.f) * 0.75f;
		const float Pitch = (Hash01(Motion.Seed ^ 0xabcdu, Beat) * 2.f - 1.f) * 0.18f;
		Glance = FVector(FMath::Cos(Yaw), -FMath::Sin(Yaw), Pitch).GetSafeNormal();
		GlanceUntil = T + 0.5f + 0.9f * Hash01(Beat, Motion.Seed);
		NextGlance = T + 2.2f + 4.f * Hash01(Beat ^ 0x1234u, Motion.Seed);
	}
	if (NextGlance - T > 12.f)
	{
		NextGlance = T + 2.f; // the clock went back (a restart): keep glancing
	}

	// The gaze target, in the avatar frame, eased: heads turn with weight rather than snapping to a new target.
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
	GazeSpring.Step(Gaze, 16.f, 0.85f, Dt);

	// Hair and hems trail the body: moving streams them back, starts and stops swing them, turns fling them out,
	// a jump lifts them. The lag (cm, avatar frame) runs on lightly damped springs, so they bounce and settle.
	const float LiftVelocity = Dt > 0.f ? (Input.Lift - LastLift) * 100.f / Dt : 0.f;
	const FVector Drive = -(Motion.Velocity + FVector(0.0, Motion.YawRate * 35.0, LiftVelocity)) * 0.07;
	const FVector Sway(0.0, FMath::Sin(T * 1.3f) * 1.5f * Input.Idle, 0.0);
	HairLag.Step((Drive + Sway) * Secondary, 8.5f, 0.24f, Dt);
	HemLag.Step(Drive * 0.7 * Secondary, 11.f, 0.32f, Dt);
	HairLag.Value = HairLag.Value.BoundToCube(40.0);
	HemLag.Value = HemLag.Value.BoundToCube(30.0);

	// Landing: coming down from a hop sinks the knees, then springs back up.
	if (LastLift > 0.01f && Input.Lift <= 0.004f && LiftVelocity < 0.f)
	{
		Land.Velocity += FMath::Min(-LiftVelocity, 400.f) * 0.012f * Secondary;
	}
	LastLift = Input.Lift;
	Land.Step(0.f, 15.f, 0.45f, Dt);
	Land.Value = FMath::Clamp(Land.Value, -0.2f, 0.6f);
}

const FArcadeAvatarProxy::FBones& FArcadeAvatarProxy::CacheAvatarBones(const FBoneContainer& Bones)
{
	if (bCached && CachedSerial == Bones.GetSerialNumber())
	{
		return Cached;
	}
	const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
	auto Find = [&](const TCHAR* Name) -> int32
	{
		const int32 MeshIndex = Ref.FindBoneIndex(FName(Name));
		if (MeshIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}
		const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		return Index.IsValid() ? Index.GetInt() : INDEX_NONE;
	};
	Cached.Root = 0;
	Cached.Torso = Find(TEXT("torso"));
	Cached.Neck = Find(TEXT("neck"));
	Cached.Head = Find(TEXT("head"));
	const TCHAR* ArmNames[2][3] = { { TEXT("leftUpperArm"), TEXT("leftForearm"), TEXT("leftHand") }, { TEXT("rightUpperArm"), TEXT("rightForearm"), TEXT("rightHand") } };
	const TCHAR* LegNames[2][3] = { { TEXT("leftUpperLeg"), TEXT("leftLowerLeg"), TEXT("leftFoot") }, { TEXT("rightUpperLeg"), TEXT("rightLowerLeg"), TEXT("rightFoot") } };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		for (int32 Joint = 0; Joint < 3; ++Joint)
		{
			Cached.Arm[Side][Joint] = Find(ArmNames[Side][Joint]);
			Cached.Leg[Side][Joint] = Find(LegNames[Side][Joint]);
		}
	}
	Cached.Clavicle[0] = Find(TEXT("leftClavicle"));
	Cached.Clavicle[1] = Find(TEXT("rightClavicle"));
	Cached.Fingers[0] = Find(TEXT("leftFingers"));
	Cached.Fingers[1] = Find(TEXT("rightFingers"));
	Cached.Thumb[0] = Find(TEXT("leftThumb"));
	Cached.Thumb[1] = Find(TEXT("rightThumb"));
	Cached.Hair.Reset();
	Cached.Hem.Reset();
	for (int32 MeshIndex = 0; MeshIndex < Ref.GetNum(); ++MeshIndex)
	{
		const FString Name = Ref.GetBoneName(MeshIndex).ToString();
		const bool bHair = Name.StartsWith(TEXT("Spring_hair"), ESearchCase::IgnoreCase);
		if (!bHair && !Name.StartsWith(TEXT("Spring_hem"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (Index.IsValid())
		{
			if (bHair)
			{
				Cached.Hair.Add(Index.GetInt());
			}
			else
			{
				Cached.Hem.Add(Index.GetInt());
			}
		}
	}
	CachedSerial = Bones.GetSerialNumber();
	bCached = true;
	return Cached;
}

bool FArcadeAvatarProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	FCompactPose& Pose = Output.Pose;
	const FBones& B = CacheAvatarBones(Pose.GetBoneContainer());
	if (B.Torso == INDEX_NONE || B.Head == INDEX_NONE || B.Arm[0][0] == INDEX_NONE || B.Arm[1][0] == INDEX_NONE)
	{
		return true;
	}
	const int32 Num = Pose.GetNumBones();
	Rig.SetNum(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		const FCompactPoseBoneIndex Index(i);
		Rig.Local[i] = Pose[Index];
		Rig.Parent[i] = Pose.GetParentBoneIndex(Index).GetInt();
	}
	Rig.Recompute();

	// The avatar's own frame, from its rest skeleton.
	const FVector Up = (Rig.Pos(B.Head) - Rig.Pos(B.Torso)).GetSafeNormal();
	FVector Right = Rig.Pos(B.Arm[1][0]) - Rig.Pos(B.Arm[0][0]);
	Right = (Right - Up * FVector::DotProduct(Right, Up)).GetSafeNormal();
	const FVector Forward = FVector::CrossProduct(Right, Up);
	const FVector Down = -Up;
	const float T = Input.Time;
	const float Secondary = FMath::Clamp(Input.Secondary, 0.f, 2.f);

	// How much the game is driving the body: idle life fades as it takes over.
	const float Cheer = FMath::Clamp(CheerSpring.Value, 0.f, 1.15f);
	const float Slump = FMath::Clamp(SlumpSpring.Value, 0.f, 1.1f);
	const float Emote = FMath::Clamp(EmoteSpring.Value, 0.f, 1.f);
	const float Effort = FMath::Clamp(Input.Effort, 0.f, 1.f);
	const float Busy = FMath::Clamp(FMath::Max3(FMath::Max(Input.HandWeight[0], Input.HandWeight[1]), FMath::Max(Cheer, Slump),
		FMath::Max3(Effort, Input.Step, Input.Lunge)), 0.f, 1.f);
	const float Calm = FMath::Clamp(Input.Idle, 0.f, 1.f) * (1.f - Busy);
	const uint32 Seed = Motion.Seed;
	const float Phase = Hash01(Seed, 77u) * UE_TWO_PI;

	// The reactions' wind-up: a quick dip of the knees before the celebration springs up.
	const float CheerDip = Bump(CheerAge, 0.f, 0.09f, 0.24f) * FMath::Clamp(Input.Cheer, 0.f, 1.f) * FMath::Min(Secondary, 1.f);
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
	if (Slump > 0.f && SlumpNow == EArcadeSlump::Droop)
	{
		ExtraCrouch += 0.1f * Slump;
	}

	// Idle weight shift: the hips settle over one foot, then the other, every few seconds, the free knee easing.
	const float ShiftWave = FMath::Sin(T * 0.62f + Phase);
	const float Shift = FMath::Clamp(ShiftWave * 1.8f, -1.f, 1.f) * Calm;

	// Legs: knees bend for a crouch, swing for a stride, and split into the bowler's slide.
	double LegLength = 0.0;
	if (B.Leg[0][0] != INDEX_NONE && B.Leg[0][1] != INDEX_NONE && B.Leg[0][2] != INDEX_NONE)
	{
		LegLength = (Rig.Pos(B.Leg[0][1]) - Rig.Pos(B.Leg[0][0])).Size() + (Rig.Pos(B.Leg[0][2]) - Rig.Pos(B.Leg[0][1])).Size();
	}
	const float Crouch = FMath::Clamp(Input.Crouch + ExtraCrouch, 0.f, 1.f);
	const float Lunge = FMath::Clamp(Input.Lunge, 0.f, 1.f);
	const float CrouchPitch = Crouch * 0.62f;
	const double HipShift = LegLength * 0.035 * Shift;
	const double Drop = LegLength * ((1.0 - FMath::Cos(CrouchPitch)) * 1.6 + Lunge * 0.22) + LegLength * 0.02 * Input.Step * FMath::Abs(FMath::Sin(Input.StepPhase))
		+ LegLength * 0.006 * FMath::Abs(Shift);
	Rig.Local[B.Root].AddToTranslation(Down * Drop + Up * (Input.Lift + ExtraLift) * 100.0 + Right * HipShift);
	Rig.Recompute();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Upper = B.Leg[Side][0], Lower = B.Leg[Side][1], Foot = B.Leg[Side][2];
		if (Upper == INDEX_NONE || Lower == INDEX_NONE || Foot == INDEX_NONE)
		{
			continue;
		}
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

	// Torso: lean, twist, breath; each reaction bends it its own way; the hips' shift tilts it back over the feet.
	const float Breath = FMath::Sin(T * 1.55f + Phase);
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
	const float Tremble = Effort * 0.012f * FMath::Sin(T * 31.f) * FMath::Sin(T * 7.3f);
	const float Lean = Input.Lean + Breath * 0.016f * Input.Idle + ReactLean + Tremble;
	const float Twist = Input.Twist + FMath::Sin(T * 0.43f + Phase) * 0.03f * Input.Idle;
	const float Roll = ReactRoll - 0.05f * Shift;
	const FQuat TorsoDelta = FQuat(Up, Twist) * FQuat(Forward, Roll) * FQuat(Right, Lean);
	Rig.Rotate(B.Torso, TorsoDelta);
	const FVector TorsoForward = TorsoDelta.RotateVector(Forward), TorsoRight = TorsoDelta.RotateVector(Right), TorsoUp = TorsoDelta.RotateVector(Up);

	// Shoulders: rise with each breath, hunch in a shrug, lift in a cheer, drop in a slump.
	float Shrug = 0.f;
	if (Input.Emote == EArcadeEmote::Shrug)
	{
		Shrug += Emote * (0.6f + 0.4f * Bump(FMath::Fmod(T, 1.6f), 0.f, 0.25f, 1.2f));
	}
	if (SlumpNow == EArcadeSlump::ArmsOut)
	{
		Shrug += 0.7f * Slump;
	}
	const float Shoulders = 0.05f * Breath * Input.Idle + 0.25f * Shrug + 0.12f * Cheer - (SlumpNow == EArcadeSlump::Droop ? 0.22f * Slump : 0.f) + 0.12f * Effort;
	const float Hunch = SlumpNow == EArcadeSlump::Droop ? Slump : 0.f;
	if (FMath::Abs(Shoulders) > 1e-3f || Hunch > 1e-3f)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			// The clavicles are leaf joints in avatar4: the shoulder joint itself moves, and the clavicle turns after it.
			const int32 Clavicle = B.Clavicle[Side], Upper = B.Arm[Side][0];
			const FVector Before = Rig.Pos(Upper);
			Rig.Translate(Upper, (TorsoUp * Shoulders * 16.f - TorsoForward * Hunch * 3.5f) * (Rig.Pos(B.Head) - Rig.Pos(B.Torso)).Size() / 66.0);
			if (Clavicle != INDEX_NONE)
			{
				Rig.TurnToward(Clavicle, Before, Rig.Pos(Upper));
			}
		}
	}

	// Head: the eased gaze, chin up in a cheer and down in a slump; a facepalm bows into the hand.
	const FVector Facing = TorsoForward;
	FVector Gaze = AvatarDir(GazeSpring.Value, Forward, Right, Up).GetSafeNormal();
	float SlumpHead = 0.6f;
	switch (SlumpNow)
	{
	case EArcadeSlump::HandsOnHead: SlumpHead = -0.25f; break;
	case EArcadeSlump::ArmsOut: SlumpHead = -0.35f; break;
	case EArcadeSlump::Facepalm: SlumpHead = 0.75f; break;
	default: break;
	}
	Gaze = (Gaze + Up * (0.35f * Cheer) + Down * (SlumpHead * Slump)).GetSafeNormal();
	FQuat HeadDelta = FQuat::FindBetweenNormals(Facing, FMath::Lerp(Facing, Gaze, 0.8f).GetSafeNormal());
	// A head tilt: a shrug cocks it, idle lets it lean a little with the hips.
	const float Tilt = 0.18f * Shrug + 0.04f * Shift + (SlumpNow == EArcadeSlump::ArmsOut ? 0.12f * Slump : 0.f);
	HeadDelta = FQuat(HeadDelta.RotateVector(Facing), Tilt) * HeadDelta;
	if (B.Neck != INDEX_NONE)
	{
		Rig.Rotate(B.Neck, FQuat::Slerp(FQuat::Identity, HeadDelta, NeckShare));
	}
	const FQuat HeadTurn = FQuat::Slerp(FQuat::Identity, HeadDelta, HeadShare);
	Rig.Rotate(B.Head, HeadTurn);
	const FVector HeadPos = Rig.Pos(B.Head);
	const FVector HeadUp = (HeadTurn * TorsoDelta).RotateVector(Up);
	const FVector HeadForward = (HeadTurn * TorsoDelta).RotateVector(Forward);

	// Arms: two-bone IK onto each hand's target, or hanging at rest, with the reaction and emote layered on top.
	const FVector ShoulderMid = (Rig.Pos(B.Arm[0][0]) + Rig.Pos(B.Arm[1][0])) * 0.5;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 Upper = B.Arm[Side][0], Fore = B.Arm[Side][1], Hand = B.Arm[Side][2];
		if (Fore == INDEX_NONE || Hand == INDEX_NONE)
		{
			continue;
		}
		const FVector Shoulder = Rig.Pos(Upper);
		const double L1 = (Rig.Pos(Fore) - Shoulder).Size();
		const double L2 = (Rig.Pos(Hand) - Rig.Pos(Fore)).Size();
		const double Reach = L1 + L2;
		const FVector Out = Side == 0 ? -TorsoRight : TorsoRight;
		const FVector Rest = (TorsoForward * 0.08f + Out * (0.2f + 0.02f * FMath::Sin(T * 1.1f + Side)) + Down * 0.94f).GetSafeNormal() * Reach * 0.97;
		const FVector& H = Input.Hand[Side];
		const FVector Given = (Input.HandSpace == EArcadeHandSpace::Avatar ? AvatarDir(H, Forward, Right, Up)
			: TorsoForward * H.X + TorsoRight * H.Y + TorsoUp * H.Z) * Reach;
		const float GivenWeight = FMath::Clamp(Input.HandWeight[Side], 0.f, 1.f);
		FVector Target = Shoulder + FMath::Lerp(Rest, Given, GivenWeight);
		FVector Pole = Out * 0.55f + Down * 0.45f - TorsoForward * 0.45f;
		float Grip = 0.3f + 0.4f * GivenWeight; // relaxed, or holding something
		auto Blend = [&](const FVector& To, float Weight, const FVector& ToPole, float ToGrip)
		{
			const float W = FMath::Clamp(Weight, 0.f, 1.f);
			Target = FMath::Lerp(Target, To, W);
			Pole = FMath::Lerp(Pole, ToPole, W);
			Grip = FMath::Lerp(Grip, ToGrip, W);
		};

		// The celebration.
		if (Cheer > 0.f)
		{
			const float CheerW = FMath::Min(Cheer, 1.f);
			switch (CheerNow)
			{
			case EArcadeCheer::FistPump:
			{
				const float Pump = FMath::Abs(FMath::Sin(T * 6.f + Side * 0.6f));
				Blend(Shoulder + (TorsoUp * 0.75f + Out * 0.3f + TorsoForward * 0.3f).GetSafeNormal() * Reach * (0.55 + 0.32 * Pump), CheerW, Out * 0.8f + Down * 0.5f, 1.f);
				break;
			}
			case EArcadeCheer::Jump:
			{
				const float Fling = FMath::Abs(FMath::Sin(T * 5.2f));
				Blend(Shoulder + (TorsoUp * (0.6f + 0.35f * Fling) + Out * (0.75f - 0.25f * Fling) + TorsoForward * 0.15f).GetSafeNormal() * Reach * 0.95, CheerW, Out * 0.5f + Down * 0.6f, 0.1f);
				break;
			}
			case EArcadeCheer::Clap:
			{
				const float Open = 0.5f + 0.5f * FMath::Sin(T * 9.f);
				const FVector Above = ShoulderMid + TorsoUp * Reach * 0.82 + TorsoForward * Reach * 0.18 + Out * Reach * (0.03 + 0.13 * Open);
				Blend(Above, CheerW, Out * 0.7f + TorsoForward * 0.3f, 0.f);
				break;
			}
			case EArcadeCheer::Victory:
				if (Side == 1)
				{
					const float Punch = 0.92f + 0.06f * FMath::Sin(T * 3.f);
					Blend(Shoulder + (TorsoUp * 0.95f + Out * 0.12f + TorsoForward * 0.1f).GetSafeNormal() * Reach * Punch, CheerW, Out + Down * 0.3f, 1.f);
				}
				else
				{
					// The other hand on the hip, elbow out.
					Blend(Shoulder + (Down * 0.62f + Out * 0.32f - TorsoForward * 0.06f).GetSafeNormal() * Reach * 0.66, CheerW, Out * 0.9f - TorsoForward * 0.3f, 0.7f);
				}
				break;
			default:
			{
				const float Wave = FMath::Sin(T * 7.f + Side * UE_PI) * 0.16f;
				Blend(Shoulder + (TorsoUp * 0.9f + Out * (0.36f + Wave) + TorsoForward * 0.12f).GetSafeNormal() * Reach * 0.94, CheerW, Out * 0.55f + Down * 0.45f - TorsoForward * 0.45f, 0.2f);
				break;
			}
			}
		}

		// The disappointment.
		if (Slump > 0.f)
		{
			const float SlumpW = FMath::Min(Slump, 1.f);
			switch (SlumpNow)
			{
			case EArcadeSlump::HandsOnHead:
				// Wrists at the crown, palms flat on the head (the head bone sits at the neck; the crown is ~0.6 arm above).
				Blend(HeadPos + HeadUp * Reach * 0.58 + Out * Reach * 0.17 - HeadForward * Reach * 0.04, SlumpW, Out + TorsoUp * 0.4f, 0.1f);
				break;
			case EArcadeSlump::Facepalm:
				if (Side == 1)
				{
					// The wrist under the brow, the palm over the eyes.
					Blend(HeadPos + HeadForward * Reach * 0.3 + HeadUp * Reach * 0.24 + Out * Reach * 0.03, SlumpW, Out * 0.4f + Down, 0.15f);
				}
				break;
			case EArcadeSlump::ArmsOut:
			{
				const float Beat = 0.04f * FMath::Sin(T * 3.f);
				Blend(Shoulder + (Down * (0.55f + Beat) + Out * 0.6f + TorsoForward * 0.4f).GetSafeNormal() * Reach * 0.93, SlumpW, Down + Out * 0.3f - TorsoForward * 0.2f, 0.f);
				break;
			}
			default:
				// Arms hang heavy and a little forward.
				Blend(Shoulder + (Down + TorsoForward * 0.16f + Out * 0.08f).GetSafeNormal() * Reach * 0.98, SlumpW * 0.6f, Pole, 0.15f);
				break;
			}
		}

		// The emote.
		if (Emote > 0.f && Input.Emote != EArcadeEmote::None)
		{
			const bool bMine = Side == FMath::Min<int32>(Input.EmoteHand, 1);
			const FVector PointDir = AvatarDir(Input.PointAt.IsNearlyZero() ? FVector::ForwardVector : Input.PointAt.GetSafeNormal(), Forward, Right, Up);
			switch (Input.Emote)
			{
			case EArcadeEmote::Wave:
				if (bMine)
				{
					const float Swing = FMath::Sin(T * 8.f);
					const FVector To = Shoulder + (TorsoUp * 0.72f + Out * (0.42f + 0.2f * Swing) + PointDir * 0.25f).GetSafeNormal() * Reach * 0.82;
					Blend(To, Emote, Out * 0.6f + Down * 0.8f, 0.f);
				}
				break;
			case EArcadeEmote::Point:
				if (bMine)
				{
					Blend(SoftReach(Shoulder, Shoulder + (PointDir + TorsoUp * 0.08f).GetSafeNormal() * Reach, Reach, 0.06), Emote, Out * 0.4f + Down, 0.25f);
				}
				break;
			case EArcadeEmote::Clap:
			{
				const float Open = 0.5f + 0.5f * FMath::Sin(T * 10.f);
				const FVector Front = ShoulderMid + TorsoForward * Reach * 0.52 + Down * Reach * 0.22 + Out * Reach * (0.025 + 0.14 * Open);
				Blend(Front, Emote, Out + Down * 0.6f, 0.f);
				break;
			}
			case EArcadeEmote::FistPump:
				if (bMine)
				{
					const float Pump = FMath::Abs(FMath::Sin(T * 6.5f));
					Blend(Shoulder + (TorsoUp * 0.65f + Out * 0.22f + TorsoForward * 0.35f).GetSafeNormal() * Reach * (0.55 + 0.3 * Pump), Emote, Out * 0.8f + Down * 0.5f, 1.f);
				}
				break;
			case EArcadeEmote::Shrug:
				Blend(Shoulder + (Down * 0.5f + Out * 0.45f + TorsoForward * 0.6f).GetSafeNormal() * Reach * 0.74, Emote, Down + Out * 0.2f - TorsoForward * 0.3f, 0.f);
				break;
			default:
				break;
			}
		}

		// Straining arms tremble.
		if (Effort > 0.f)
		{
			Target += (TorsoUp * FMath::Sin(T * 37.f + Side) + Out * FMath::Sin(T * 29.f + 2.f * Side)) * Reach * 0.006 * Effort;
		}
		// The game's elbow hint, and its hand closure, win over the automatic ones.
		if (!Input.ElbowHint[Side].IsNearlyZero())
		{
			Pole = AvatarDir(Input.ElbowHint[Side].GetSafeNormal(), Forward, Right, Up);
		}
		if (Input.Grip[Side] >= 0.f)
		{
			Grip = Input.Grip[Side];
		}
		// Soft reach: arms ease into full extension instead of snapping straight.
		Target = SoftReach(Shoulder, Target, Reach);
		const FVector Elbow = SolveMiddle(Shoulder, Target, L1, L2, Pole);
		Rig.Aim(Upper, Fore, Elbow - Shoulder);
		Rig.Aim(Fore, Hand, Target - Rig.Pos(Fore));

		// Fingers: curl toward the palm about the knuckles' axis (across the hand, toward the thumb).
		CurlFingers(Rig, Hand, B.Fingers[Side], B.Thumb[Side], Grip);
	}

	// Spring chains: hair and hems swing with the lag (pitch back when it trails behind, roll when it swings
	// sideways), each joint taking a share so the tips curl furthest.
	const FQuat HeadFrame = HeadTurn * TorsoDelta;
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
	const FVector& HL = HairLag.Value;
	const FVector& ML = HemLag.Value;
	Swing(B.Hair, HeadFrame.RotateVector(Right), HeadFrame.RotateVector(Forward),
		FMath::Clamp(static_cast<float>(-HL.X * 0.018 - HL.Z * 0.01) + 0.05f * Cheer, -0.6f, 0.8f), FMath::Clamp(static_cast<float>(HL.Y * 0.018), -0.6f, 0.6f));
	const float StrideFlap = 0.12f * Input.Step * FMath::Sin(Input.StepPhase * 2.f) + 0.1f * Lunge;
	Swing(B.Hem, TorsoDelta.RotateVector(Right), TorsoDelta.RotateVector(Forward),
		FMath::Clamp(static_cast<float>(-ML.X * 0.012 - ML.Z * 0.008) + StrideFlap, -0.5f, 0.5f), FMath::Clamp(static_cast<float>(ML.Y * 0.01), -0.4f, 0.4f));

	Rig.Local[B.Head].SetScale3D(Rig.Local[B.Head].GetScale3D() * HeadScale);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (B.Arm[Side][2] != INDEX_NONE)
		{
			Rig.Local[B.Arm[Side][2]].SetScale3D(Rig.Local[B.Arm[Side][2]].GetScale3D() * HandScale);
		}
	}
	for (int32 i = 0; i < Num; ++i)
	{
		Pose[FCompactPoseBoneIndex(i)] = Rig.Local[i];
	}
	return true;
}

namespace MovecadeKart::Avatar
{
	void FaceFor(const FArcadePoseInput& Pose, float (&OutWeights)[DriverFace::MorphCount])
	{
		using namespace MovecadeKart::DriverFace;
		FMood Mood;
		const float Emote = Pose.Emote == EArcadeEmote::None ? 0.f : FMath::Clamp(Pose.EmoteWeight, 0.f, 1.f);
		switch (Pose.Emote)
		{
		case EArcadeEmote::Wave:
		case EArcadeEmote::Clap:
		case EArcadeEmote::FistPump:
			Mood.Boost = 0.8f * Emote;
			break;
		case EArcadeEmote::Point:
			Mood.Rev = 0.7f * Emote;
			break;
		case EArcadeEmote::Shrug:
			Mood.Meh = Emote;
			break;
		default:
			break;
		}
		Mood.Effort = FMath::Clamp(Pose.Effort, 0.f, 1.f);
		Mood.Cheer = FMath::Clamp(Pose.Cheer, 0.f, 1.f);
		Mood.Slump = FMath::Clamp(Pose.Slump, 0.f, 1.f);
		Mix(Mood, OutWeights);
		// A cheer yells: the jaw works open and shut ("woo!").
		const int32 Jaw = static_cast<int32>(EMorph::JawOpen);
		OutWeights[Jaw] = FMath::Clamp(OutWeights[Jaw] + 0.3f * Mood.Cheer * FMath::Abs(FMath::Sin(Pose.Time * 7.5f)), 0.f, 1.f);
	}

	bool FMorphWriter::Set(USkeletalMeshComponent* Mesh, int32 Slot, FName Name, float Weight, float Epsilon)
	{
		if (!Mesh || Slot < 0)
		{
			return false;
		}
		while (Written.Num() <= Slot)
		{
			Written.Add(-1.f); // never written
		}
		float& Last = Written[Slot];
		const bool bSettled = (Weight == 0.f || Weight == 1.f) && Last != Weight;
		if (Last >= 0.f && FMath::Abs(Weight - Last) <= Epsilon && !bSettled)
		{
			return false;
		}
		Mesh->SetMorphTarget(Name, Weight);
		Last = Weight;
		return true;
	}
}

UArcadeAvatarComponent::UArcadeAvatarComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UArcadeAvatarComponent::OnRegister()
{
	Super::OnRegister();
	if (!Driver)
	{
		Driver = NewObject<USkeletalMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), USkeletalMeshComponent::StaticClass(), TEXT("AvatarMesh")));
		Driver->SetupAttachment(this);
		Driver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Driver->RegisterComponent();
		Outfit = NewObject<USkeletalMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), USkeletalMeshComponent::StaticClass(), TEXT("AvatarOutfit")));
		Outfit->SetupAttachment(Driver);
		Outfit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Outfit->RegisterComponent();
	}
	if (bHasLook)
	{
		SetLook(CurrentLook);
	}
}

void UArcadeAvatarComponent::SetLook(const FKartLook& Look)
{
	CurrentLook = Look;
	bHasLook = true;
	if (!Driver)
	{
		return;
	}
	if (Dressing.Dress(Driver, Outfit, Look) == MovecadeKart::Avatar::EDressResult::NewMesh)
	{
		Driver->SetAnimInstanceClass(UArcadeAvatarAnimInstance::StaticClass());
		Driver->SetRelativeTransform(FTransform(MovecadeKart::Avatar::Facing(Driver->GetSkeletalMeshAsset(), FVector::XAxisVector, FVector::ZAxisVector),
			FVector::ZeroVector, FVector(AvatarScale)));
		Morphs.Reset();
	}
	Dressing.Colour(Look);
}

FVector UArcadeAvatarComponent::BoneWorld(FName Bone) const
{
	if (Driver && Driver->GetBoneIndex(Bone) != INDEX_NONE)
	{
		return Driver->GetBoneLocation(Bone);
	}
	return GetComponentLocation();
}

void UArcadeAvatarComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Clock += DeltaTime;
	Input.Time = Clock;

	// How the avatar moves through the world, in its own frame, for the hair and hems.
	const FTransform& Now = GetComponentTransform();
	if (bHasLastTransform && DeltaTime > 1e-4f)
	{
		const FVector Velocity = Now.InverseTransformVectorNoScale((Now.GetLocation() - LastTransform.GetLocation()) / DeltaTime);
		const FQuat Turn = Now.GetRotation() * LastTransform.GetRotation().Inverse();
		const float Yaw = FMath::FindDeltaAngleRadians(0.f, static_cast<float>(Turn.Rotator().Yaw * UE_PI / 180.0));
		// Teleports (a new seat, a reset) aren't motion.
		const bool bJump = Velocity.SizeSquared() > 2000.0 * 2000.0;
		SmoothedVelocity = bJump ? FVector::ZeroVector : FMath::VInterpTo(SmoothedVelocity, Velocity, DeltaTime, 20.f);
		SmoothedYawRate = FMath::FInterpTo(SmoothedYawRate, bJump ? 0.f : -Yaw / DeltaTime, DeltaTime, 20.f);
	}
	LastTransform = Now;
	bHasLastTransform = true;

	if (UArcadeAvatarAnimInstance* Anim = Driver ? Cast<UArcadeAvatarAnimInstance>(Driver->GetAnimInstance()) : nullptr)
	{
		Anim->Input = Input;
		Anim->Motion.Velocity = SmoothedVelocity;
		Anim->Motion.YawRate = SmoothedYawRate;
		Anim->Motion.Seed = GetTypeHash(GetFName()) ^ (GetUniqueID() * 2654435761u);
	}
	UpdateFace(DeltaTime);
}

void UArcadeAvatarComponent::UpdateFace(float DeltaTime)
{
	using namespace MovecadeKart::DriverFace;
	if (!Driver || !Driver->GetSkeletalMeshAsset())
	{
		return;
	}
	// Blinks every 2-5 s (blink.ts), 0.22 s each; sometimes a double blink, and a blink with a big turn of the gaze.
	const FVector Look = Input.LookWeight > 0.3f ? Input.Look.GetSafeNormal() : FVector::ForwardVector;
	const bool bGazeJump = FVector::DotProduct(Look, LastLook) < 0.8f && Clock - BlinkAt > 0.6f;
	LastLook = Look;
	if (Clock >= NextBlink || bGazeJump)
	{
		BlinkAt = Clock;
		NextBlink = Clock + (FMath::FRand() < 0.18f ? 0.3f : FMath::FRandRange(2.f, 5.f));
	}
	const float Age = Clock - BlinkAt;
	float Blink = Age < 0.065f ? Age / 0.065f : (Age < 0.085f ? 1.f : FMath::Clamp(1.f - (Age - 0.085f) / 0.135f, 0.f, 1.f));
	// A slump's heavy lids; a cheer's happy squint is in the expression.
	Blink = FMath::Max(Blink, 0.25f * FMath::Clamp(Input.Slump, 0.f, 1.f));
	static const FName EyeBlink(TEXT("eyeBlink"));
	Morphs.Set(Driver, MovecadeKart::Avatar::BlinkSlot, EyeBlink, Blink);

	float Target[MorphCount];
	MovecadeKart::Avatar::FaceFor(Input, Target);
	for (int32 i = 0; i < MorphCount; ++i)
	{
		Face[i] = FMath::FInterpTo(Face[i], Target[i], DeltaTime, 12.f);
		Morphs.Set(Driver, i, MorphName(static_cast<EMorph>(i)), Face[i]);
	}
}
