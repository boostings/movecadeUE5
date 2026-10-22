#include "Avatar/AvatarFaceLife.h"

#include "Avatar/AvatarRig.h"

namespace
{
	using MovecadeKart::DriverFace::EMorph;
	using MovecadeKart::DriverFace::MorphCount;

	// ---- Idle glances: how far (radians), how long a glance holds and the gap to the next (seconds). ----

	constexpr float IdleGlanceMaxYaw = 0.75f;
	constexpr float IdleGlanceMaxPitch = 0.18f;
	constexpr float IdleGlanceHoldMin = 0.5f;
	constexpr float IdleGlanceHoldRange = 0.9f;
	constexpr float IdleGlanceGapMin = 2.2f;
	constexpr float IdleGlanceGapRange = 4.f;
	/** A next glance further off than this means the clock went back (a restart): glance again soon. */
	constexpr float IdleGlanceClockJump = 12.f;
	constexpr float IdleGlanceAfterJump = 2.f;
	/** A game's look keeps this share of an idle glance from pulling the gaze; a point turns it this far. */
	constexpr float LookOverGlance = 0.7f;
	constexpr float PointShare = 0.7f;

	// ---- Blinks (blink.ts gave 65 ms closing, 20 ms shut, 135 ms opening; the variants scale around it). ----

	/** The gap between blinks (seconds), before focus and excitement scale it, and the odd long stare. */
	constexpr float BlinkGapMin = 2.f;
	constexpr float BlinkGapMax = 5.5f;
	constexpr float StareChance = 0.1f;
	constexpr float StareExtra = 2.5f;
	/** Concentration stretches the gaps by up to this share; excitement shortens them by up to this share. */
	constexpr float FocusStretch = 0.8f;
	constexpr float ExciteShorten = 0.4f;
	/** The first blink comes this soon (seconds), spread so a line-up never blinks in step. */
	constexpr float FirstBlinkMin = 0.4f;
	constexpr float FirstBlinkMax = 3.f;
	/** A double blink: how likely (more when excited), and the pause before its quick second half (seconds). */
	constexpr float DoubleChance = 0.14f;
	constexpr float DoubleChanceExcited = 0.14f;
	constexpr float DoubleGapMin = 0.05f;
	constexpr float DoubleGapMax = 0.14f;
	/** The variants' odds (the rest are full blinks), and how much a drowsy face prefers the slow one. */
	constexpr float HalfChance = 0.12f;
	constexpr float QuickChance = 0.08f;
	constexpr float SlowChanceDrowsy = 0.75f;
	/** A big eye jump brings a blink this often, if the last one is at least this old (seconds). */
	constexpr float SaccadeBlinkChance = 0.6f;
	constexpr float SaccadeBlinkMinAge = 0.6f;

	// ---- Eyes (angles in radians). ----

	/** A target further than this from the fixation is reached by a saccade; nearer, the eyes pursue it smoothly. */
	constexpr float SaccadeThreshold = 0.06f;
	/** A saccade at least this big counts as a big jump (blinks and brows go with it). */
	constexpr float BigSaccade = 0.35f;
	/** The main sequence: a saccade takes this long plus this much per radian, capped (seconds). */
	constexpr float SaccadeBase = 0.022f;
	constexpr float SaccadePerRadian = 0.14f;
	constexpr float SaccadeMax = 0.12f;
	/** Smooth pursuit of a slowly moving target (interp speed, 1/s). */
	constexpr float PursuitRate = 9.f;
	/** Micro-saccades while fixating: how far (radians) and how often (seconds). */
	constexpr float MicroMin = 0.008f;
	constexpr float MicroMax = 0.022f;
	constexpr float MicroGapMin = 0.5f;
	constexpr float MicroGapMax = 1.7f;
	/** Each micro-saccade pulls back toward the centre by this share of the last one, so they never drift off. */
	constexpr float MicroRecentre = 0.5f;
	/** Eye-only glances while idle: yaw and pitch range, hold and gap (seconds), and the idle level they need. */
	constexpr float GlanceYawMin = 0.12f;
	constexpr float GlanceYawMax = 0.32f;
	constexpr float GlancePitchMax = 0.1f;
	constexpr float GlanceHoldMin = 0.35f;
	constexpr float GlanceHoldMax = 1.1f;
	constexpr float GlanceGapMin = 2.5f;
	constexpr float GlanceGapMax = 6.5f;
	constexpr float GlanceMinIdle = 0.5f;

	// ---- Expression springs: natural frequency (rad/s) and damping ratio per morph, in EMorph order. ----

	struct FMorphSpring
	{
		float Omega;
		float Zeta;
	};
	//                                         Smile         Cheek          BrowDown       BrowInnerUp    BrowOuterUp
	constexpr FMorphSpring MorphSprings[] = { { 14.f, 0.75f }, { 9.f, 0.9f }, { 22.f, 0.65f }, { 22.f, 0.62f }, { 22.f, 0.62f },
		//                                     JawOpen        MouthPress     MouthFrown     EyeWide        EyeSquint
		{ 20.f, 0.7f }, { 18.f, 0.8f }, { 12.f, 0.8f }, { 24.f, 0.7f }, { 18.f, 0.8f } };
	static_assert(UE_ARRAY_COUNT(MorphSprings) == MorphCount, "one spring per expression morph");
	/** The longest step the springs take at once (a hitch must not fling the face). */
	constexpr float MaxFaceStep = 1.f / 20.f;

	/** The cheeks ride a smile (share of the smile), and a broad smile narrows the eyes past this level. */
	constexpr float CheekWithSmile = 0.65f;
	constexpr float DuchenneFrom = 0.55f;
	constexpr float DuchenneSquint = 0.3f;
	/** The lids follow the eyes: looking down by this much (radians) lowers the upper lids by LidDrop. */
	constexpr float LidPitchRange = 0.4f;
	constexpr float LidDrop = 0.4f;
	/** Looking up by this much widens the eyes and lifts the brows this far. */
	constexpr float LookUpRange = 0.35f;
	constexpr float LookUpWide = 0.3f;
	constexpr float LookUpBrow = 0.2f;
	/** The brow flash: its size on a greeting and with a big eye jump, and how fast it fades (1/s). */
	constexpr float FlashGreeting = 1.f;
	constexpr float FlashSaccade = 0.3f;
	constexpr float FlashDecay = 4.5f;
	constexpr float FlashInnerUp = 0.3f;
	constexpr float FlashOuterUp = 0.42f;
	/** Effort: a clenched jaw that grunts in pulses, the press trembling, a slight frown. */
	constexpr float EffortJaw = 0.04f;
	constexpr float EffortGrunt = 0.09f;
	constexpr float EffortTremble = 0.06f;
	constexpr float EffortFrown = 0.1f;
	/** A cheer's yell gets an uneven second rhythm over FaceFor's steady one. */
	constexpr float CheerUneven = 0.12f;
	/** Idle breath parts the lips a touch. */
	constexpr float BreathJaw = 0.015f;
	constexpr float BreathRate = 1.55f;

	int32 Index(EMorph Morph)
	{
		return static_cast<int32>(Morph);
	}

	FVector2f ToAngles(const FVector& Direction)
	{
		return MovecadeKart::Avatar::EyeAngles(Direction);
	}

	FVector FromAngles(const FVector2f& Angles)
	{
		const float CosPitch = FMath::Cos(Angles.Y);
		return FVector(CosPitch * FMath::Cos(Angles.X), CosPitch * FMath::Sin(Angles.X), FMath::Sin(Angles.Y));
	}

	/** Easing for a saccade: it leaves fast and lands soft. */
	float SaccadeEase(float T)
	{
		const float X = 1.f - FMath::Clamp(T, 0.f, 1.f);
		return 1.f - X * X * X;
	}
}

namespace MovecadeKart::Avatar
{
	// ------------------------------------------------------------------------------------------------- Gaze

	void FIdleGlance::Step(float Time, uint32 Seed)
	{
		if (Time >= Next)
		{
			const uint32 Beat = static_cast<uint32>(Next * 7.f);
			const float Yaw = (Rig::Hash01(Seed, Beat) * 2.f - 1.f) * IdleGlanceMaxYaw;
			const float Pitch = (Rig::Hash01(Seed ^ 0xabcdu, Beat) * 2.f - 1.f) * IdleGlanceMaxPitch;
			Direction = FVector(FMath::Cos(Yaw), -FMath::Sin(Yaw), Pitch).GetSafeNormal();
			Until = Time + IdleGlanceHoldMin + IdleGlanceHoldRange * Rig::Hash01(Beat, Seed);
			Next = Time + IdleGlanceGapMin + IdleGlanceGapRange * Rig::Hash01(Beat ^ 0x1234u, Seed);
		}
		if (Next - Time > IdleGlanceClockJump)
		{
			Next = Time + IdleGlanceAfterJump;
		}
	}

	FVector GazeTarget(const FArcadePoseInput& Pose, const FIdleGlance& Glance, float PointWeight)
	{
		const float Cheer = FMath::Clamp(Pose.Cheer, 0.f, 1.f);
		const float Slump = FMath::Clamp(Pose.Slump, 0.f, 1.f);
		const float Busy = FMath::Clamp(FMath::Max3(Pose.HandWeight[0], Pose.HandWeight[1], FMath::Max3(Cheer, Slump, Pose.Effort)), 0.f, 1.f);
		const float Look = FMath::Clamp(Pose.LookWeight, 0.f, 1.f);
		FVector Gaze = FVector::ForwardVector;
		if (Glance.IsHeld(Pose.Time))
		{
			Gaze = FMath::Lerp(Gaze, Glance.Direction, FMath::Clamp(Pose.Idle, 0.f, 1.f) * (1.f - Busy) * (1.f - Look * LookOverGlance)).GetSafeNormal();
		}
		if (Pose.LookWeight > 0.f && !Pose.Look.IsNearlyZero())
		{
			Gaze = FMath::Lerp(Gaze, Pose.Look.GetSafeNormal(), Look).GetSafeNormal();
		}
		if (Pose.Emote == EArcadeEmote::Point && !Pose.PointAt.IsNearlyZero())
		{
			Gaze = FMath::Lerp(Gaze, Pose.PointAt.GetSafeNormal(), PointShare * PointWeight).GetSafeNormal();
		}
		return Gaze;
	}

	// ------------------------------------------------------------------------------------------------- Blinks

	float FBlinkShape::At(float Age) const
	{
		if (Age < 0.f || Age >= Duration())
		{
			return 0.f;
		}
		if (Age < Close)
		{
			return Depth * Rig::Smooth01(Age / FMath::Max(Close, 1e-4f));
		}
		if (Age < Close + Hold)
		{
			return Depth;
		}
		// Opening eases out: the lids lift quickly at first, then settle.
		const float Open01 = (Age - Close - Hold) / FMath::Max(Open, 1e-4f);
		return Depth * FMath::Square(1.f - Open01);
	}

	FBlinkShape BlinkShape(EBlinkKind Kind)
	{
		switch (Kind)
		{
		case EBlinkKind::Quick: return { 0.05f, 0.f, 0.1f, 1.f };
		case EBlinkKind::Slow: return { 0.13f, 0.1f, 0.28f, 1.f };
		case EBlinkKind::Half: return { 0.06f, 0.02f, 0.12f, 0.55f };
		default: return { 0.065f, 0.025f, 0.15f, 1.f };
		}
	}

	FBlinkScheduler::FBlinkScheduler(int32 Seed)
		: Rng(Seed)
	{
		NextAt = Rng.FRandRange(FirstBlinkMin, FirstBlinkMax);
	}

	float FBlinkScheduler::NextGap(float Focus, float Excite)
	{
		float Gap = Rng.FRandRange(BlinkGapMin, BlinkGapMax);
		if (Rng.FRand() < StareChance)
		{
			Gap += StareExtra;
		}
		return Gap * (1.f + FocusStretch * FMath::Clamp(Focus, 0.f, 1.f)) * (1.f - ExciteShorten * FMath::Clamp(Excite, 0.f, 1.f));
	}

	void FBlinkScheduler::Begin(EBlinkKind NewKind)
	{
		Kind = NewKind;
		StartedAt = Clock;
	}

	float FBlinkScheduler::Step(float Dt, float Focus, float Excite, float Drowsy, bool bSaccade)
	{
		Clock += FMath::Max(Dt, 0.f);
		const bool bBlinking = Age() < BlinkShape(Kind).Duration();
		if (FollowUpAt >= 0.f && Clock >= FollowUpAt && !bBlinking)
		{
			FollowUpAt = -1.f;
			Begin(EBlinkKind::Quick);
		}
		else if (!bBlinking && FollowUpAt < 0.f)
		{
			const bool bWithSaccade = bSaccade && Age() > SaccadeBlinkMinAge && Rng.FRand() < SaccadeBlinkChance;
			if (Clock >= NextAt || bWithSaccade)
			{
				const float Roll = Rng.FRand();
				EBlinkKind NewKind = EBlinkKind::Full;
				if (Rng.FRand() < SlowChanceDrowsy * FMath::Clamp(Drowsy, 0.f, 1.f))
				{
					NewKind = EBlinkKind::Slow;
				}
				else if (Roll < HalfChance)
				{
					NewKind = EBlinkKind::Half;
				}
				else if (Roll < HalfChance + QuickChance)
				{
					NewKind = EBlinkKind::Quick;
				}
				Begin(NewKind);
				if (NewKind != EBlinkKind::Slow && Rng.FRand() < DoubleChance + DoubleChanceExcited * FMath::Clamp(Excite, 0.f, 1.f))
				{
					FollowUpAt = Clock + BlinkShape(NewKind).Duration() + Rng.FRandRange(DoubleGapMin, DoubleGapMax);
				}
				NextAt = Clock + NextGap(Focus, Excite);
			}
		}
		return BlinkShape(Kind).At(Age());
	}

	// --------------------------------------------------------------------------------------------------- Eyes

	FVector2f EyeAngles(const FVector& Direction)
	{
		const double Flat = FMath::Sqrt(Direction.X * Direction.X + Direction.Y * Direction.Y);
		return FVector2f(static_cast<float>(FMath::Atan2(Direction.Y, Direction.X)), static_cast<float>(FMath::Atan2(Direction.Z, Flat)));
	}

	FVector ClampEyes(const FVector& EyesInHead, float MaxYaw, float MaxPitch)
	{
		FVector2f Angles = EyeAngles(EyesInHead.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
		const float Reach = FMath::Square(Angles.X / FMath::Max(MaxYaw, 1e-3f)) + FMath::Square(Angles.Y / FMath::Max(MaxPitch, 1e-3f));
		if (Reach > 1.f)
		{
			Angles /= FMath::Sqrt(Reach);
		}
		return FromAngles(Angles);
	}

	FSaccadeScheduler::FSaccadeScheduler(int32 Seed)
		: Rng(Seed)
	{
		NextMicro = Rng.FRandRange(MicroGapMin, MicroGapMax);
		NextGlance = Rng.FRandRange(GlanceGapMin, GlanceGapMax);
	}

	FVector FSaccadeScheduler::WithOffsets(const FVector& Direction) const
	{
		return FromAngles(ToAngles(Direction) + Micro);
	}

	FVector FSaccadeScheduler::Step(float Dt, const FVector& Target, float Idle, bool& bOutSaccade)
	{
		bOutSaccade = false;
		const float Step = FMath::Max(Dt, 0.f);
		Clock += Step;
		const FVector Goal = Target.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		if (!bStarted)
		{
			bStarted = true;
			Fixation = JumpFrom = Eyes = Goal;
		}

		// Eye-only glances: when idle, the eyes now and then dart aside and come back, the head staying put.
		if (Clock >= NextGlance)
		{
			NextGlance = Clock + Rng.FRandRange(GlanceGapMin, GlanceGapMax);
			if (Idle >= GlanceMinIdle)
			{
				const float Yaw = Rng.FRandRange(GlanceYawMin, GlanceYawMax) * (Rng.FRand() < 0.5f ? -1.f : 1.f);
				Glance = FVector2f(Yaw, Rng.FRandRange(-GlancePitchMax, GlancePitchMax));
				GlanceUntil = Clock + Rng.FRandRange(GlanceHoldMin, GlanceHoldMax);
			}
		}
		const bool bGlancing = Clock < GlanceUntil && Idle >= GlanceMinIdle;
		const FVector Wanted = bGlancing ? FromAngles(ToAngles(Goal) + Glance) : Goal;

		// A far target is reached in one saccade; a near or slowly moving one is pursued smoothly.
		SaccadeAge += Step;
		if (!InSaccade())
		{
			const float Off = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Wanted, Fixation)), -1.f, 1.f));
			if (Off > SaccadeThreshold)
			{
				JumpFrom = Eyes;
				Fixation = Wanted;
				SaccadeAge = 0.f;
				SaccadeTime = FMath::Min(SaccadeBase + SaccadePerRadian * Off, SaccadeMax);
				bOutSaccade = Off >= BigSaccade;
				Micro = FVector2f::ZeroVector;
			}
			else
			{
				Fixation = FMath::Lerp(Fixation, Wanted, FMath::Clamp(Step * PursuitRate, 0.f, 1.f)).GetSafeNormal(UE_SMALL_NUMBER, Wanted);
			}
		}

		// Micro-saccades: tiny flicks while holding a fixation, each partly undoing the last.
		if (!InSaccade() && Clock >= NextMicro)
		{
			NextMicro = Clock + Rng.FRandRange(MicroGapMin, MicroGapMax);
			const float Angle = Rng.FRandRange(0.f, UE_TWO_PI);
			const float Size = Rng.FRandRange(MicroMin, MicroMax);
			Micro = Micro * -MicroRecentre + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Size;
		}

		const FVector Base = InSaccade() ? FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(JumpFrom, Fixation), SaccadeEase(SaccadeAge / SaccadeTime)).RotateVector(JumpFrom)
			: Fixation;
		Eyes = WithOffsets(Base);
		return Eyes;
	}

	// ---------------------------------------------------------------------------------------------- Expression

	FFaceLife::FFaceLife(int32 Seed)
	{
		FRandomStream Rng(Seed);
		Phase = Rng.FRandRange(0.f, UE_TWO_PI);
	}

	void FFaceLife::Snap(const float (&Target)[MorphCount])
	{
		for (int32 i = 0; i < MorphCount; ++i)
		{
			Value[i] = Target[i];
			Velocity[i] = 0.f;
		}
		bStarted = true;
	}

	void FFaceLife::Step(float Dt, const float (&Target)[MorphCount], const FFaceLifeInput& In, float (&OutWeights)[MorphCount], float& OutLidClosure)
	{
		if (!bStarted)
		{
			Snap(Target);
		}
		const float Step = FMath::Clamp(Dt, 0.f, MaxFaceStep);
		const float T = In.Time;
		const float Effort = FMath::Clamp(In.Effort, 0.f, 1.f);
		const float Cheer = FMath::Clamp(In.Cheer, 0.f, 1.f);
		float Goal[MorphCount];
		FMemory::Memcpy(Goal, Target, sizeof(Goal));

		// The cheeks ride the smile as it is now (so they lag it), and a broad smile narrows the eyes.
		const float Smile = Value[Index(EMorph::Smile)];
		Goal[Index(EMorph::CheekSquint)] = FMath::Max(Goal[Index(EMorph::CheekSquint)], CheekWithSmile * Smile);
		Goal[Index(EMorph::EyeSquint)] += DuchenneSquint * FMath::Max(0.f, Smile - DuchenneFrom);

		// Looking up widens the eyes and lifts the brows; looking down lowers the lids (returned as extra closure).
		const float Up = FMath::Clamp(In.EyePitch / LookUpRange, 0.f, 1.f);
		const float Down = FMath::Clamp(-In.EyePitch / LidPitchRange, 0.f, 1.f);
		Goal[Index(EMorph::EyeWide)] += LookUpWide * Up;
		Goal[Index(EMorph::BrowOuterUp)] += LookUpBrow * Up;
		OutLidClosure = LidDrop * Down;

		// The brow flash: up on a greeting, a little with big eye jumps, fading.
		BrowFlash *= FMath::Exp(-FlashDecay * FMath::Max(Dt, 0.f));
		if (In.bGreeting)
		{
			BrowFlash = FMath::Max(BrowFlash, FlashGreeting);
		}
		if (In.bSaccade)
		{
			BrowFlash = FMath::Max(BrowFlash, FlashSaccade);
		}
		Goal[Index(EMorph::BrowInnerUp)] += FlashInnerUp * BrowFlash;
		Goal[Index(EMorph::BrowOuterUp)] += FlashOuterUp * BrowFlash;

		// Effort: the jaw clenched but grunting in pulses, the pressed lips trembling, a slight frown.
		const float Grunt = FMath::Pow(FMath::Max(0.f, FMath::Sin(T * 2.3f + Phase)), 4.f);
		Goal[Index(EMorph::JawOpen)] += Effort * (EffortJaw + EffortGrunt * Grunt);
		Goal[Index(EMorph::MouthPress)] += Effort * EffortTremble * FMath::Sin(T * 23.f + Phase);
		Goal[Index(EMorph::MouthFrown)] += Effort * EffortFrown;

		// A cheer's yell gets a second, uneven rhythm; idle breath parts the lips a touch.
		Goal[Index(EMorph::JawOpen)] += Cheer * CheerUneven * FMath::Sin(T * 3.1f + Phase) * FMath::Sin(T * 1.7f);
		Goal[Index(EMorph::JawOpen)] += BreathJaw * (0.5f + 0.5f * FMath::Sin(T * BreathRate + Phase)) * (1.f - Cheer) * (1.f - Effort);

		for (int32 i = 0; i < MorphCount; ++i)
		{
			Rig::TSpring<float> Spring{ Value[i], Velocity[i] };
			Spring.Step(FMath::Clamp(Goal[i], 0.f, 1.f), MorphSprings[i].Omega, MorphSprings[i].Zeta, Step);
			Value[i] = Spring.Value;
			Velocity[i] = Spring.Velocity;
			OutWeights[i] = FMath::Clamp(Value[i], 0.f, 1.f);
		}
	}
}
