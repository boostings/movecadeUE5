#pragma once

#include "CoreMinimal.h"

/**
 * Pose maths shared by the native avatar anim proxies (the kart driver, the party games' players): a working copy of
 * a compact pose in local and component space, rotations about a joint, aiming a bone at its child, two-bone IK
 * (with a softened reach so arms never snap straight), springs for secondary motion, and the easing curves the
 * reactions use for anticipation and follow-through.
 */
namespace MovecadeKart::Rig
{
	/** Room for every joint of the avatar4 rig (27 with the proxy root) and then some, without touching the heap. */
	constexpr int32 InlineBones = 64;

	struct FRigWork
	{
		TArray<FTransform, TInlineAllocator<InlineBones>> Local;
		TArray<FTransform, TInlineAllocator<InlineBones>> CS;
		TArray<int32, TInlineAllocator<InlineBones>> Parent;
		/** Scratch for RecomputeFrom: which bones sit under the one that moved. */
		TArray<uint8, TInlineAllocator<InlineBones>> Moved;

		/** Sizes the arrays for Num bones (keeping their memory: a proxy reuses one FRigWork every evaluation). */
		void SetNum(int32 Num)
		{
			Local.SetNum(Num, EAllowShrinking::No);
			CS.SetNum(Num, EAllowShrinking::No);
			Parent.SetNum(Num, EAllowShrinking::No);
			Moved.SetNum(Num, EAllowShrinking::No);
		}

		void Recompute()
		{
			for (int32 i = 0; i < Local.Num(); ++i)
			{
				CS[i] = Parent[i] == INDEX_NONE ? Local[i] : Local[i] * CS[Parent[i]];
			}
		}

		/**
		 * Recomputes the component-space transforms of bone Root and everything under it, and nothing else. A
		 * compact pose lists parents before children, so the subtree is found in one forward pass; the results
		 * are bit-identical to Recompute() (the same products of the same inputs) whenever the rest was current.
		 */
		void RecomputeFrom(int32 Root)
		{
			const int32 Num = Local.Num();
			CS[Root] = Parent[Root] == INDEX_NONE ? Local[Root] : Local[Root] * CS[Parent[Root]];
			Moved[Root] = 1;
			for (int32 i = Root + 1; i < Num; ++i)
			{
				const int32 P = Parent[i];
				const bool bUnder = P >= Root && Moved[P] != 0;
				Moved[i] = bUnder ? 1 : 0;
				if (bUnder)
				{
					CS[i] = Local[i] * CS[P];
				}
			}
		}

		FVector Pos(int32 i) const { return CS[i].GetLocation(); }

		void SetRotation(int32 i, const FQuat& Rotation)
		{
			CS[i].SetRotation(Rotation.GetNormalized());
			Local[i] = Parent[i] == INDEX_NONE ? CS[i] : CS[i].GetRelativeTransform(CS[Parent[i]]);
			RecomputeFrom(i);
		}

		void Rotate(int32 i, const FQuat& Delta)
		{
			SetRotation(i, Delta * CS[i].GetRotation());
		}

		/** Moves bone i (and everything under it) by Delta in component space. */
		void Translate(int32 i, const FVector& Delta)
		{
			CS[i].AddToTranslation(Delta);
			Local[i] = Parent[i] == INDEX_NONE ? CS[i] : CS[i].GetRelativeTransform(CS[Parent[i]]);
			RecomputeFrom(i);
		}

		/** Turns bone i about its joint so the direction toward From now points toward To (for leaf bones like clavicles). */
		void TurnToward(int32 i, const FVector& From, const FVector& To)
		{
			const FVector A = (From - Pos(i)).GetSafeNormal();
			const FVector B = (To - Pos(i)).GetSafeNormal();
			if (!A.IsNearlyZero() && !B.IsNearlyZero())
			{
				Rotate(i, FQuat::FindBetweenNormals(A, B));
			}
		}

		/** Turns bone i (about its own joint) so the segment to Child points along Direction. */
		void Aim(int32 i, int32 Child, const FVector& Direction)
		{
			const FVector Current = (Pos(Child) - Pos(i)).GetSafeNormal();
			const FVector Desired = Direction.GetSafeNormal();
			if (Current.IsNearlyZero() || Desired.IsNearlyZero())
			{
				return;
			}
			Rotate(i, FQuat::FindBetweenNormals(Current, Desired));
		}
	};

	/**
	 * Closes a hand: the fingers joint (a leaf in avatar4) turns toward the palm, which faces the thumb's side of the
	 * hand, by Grip (0 open .. 1 fist); the thumb folds in a little after it. Sign-free, so it fits both mirrored hands.
	 */
	inline void CurlFingers(FRigWork& Rig, int32 Hand, int32 Fingers, int32 Thumb, float Grip)
	{
		if (Hand == INDEX_NONE || Fingers == INDEX_NONE)
		{
			return;
		}
		const float G = FMath::Clamp(Grip, 0.f, 1.f);
		const FVector Along = (Rig.Pos(Fingers) - Rig.Pos(Hand)).GetSafeNormal();
		FVector Palm = Thumb != INDEX_NONE ? Rig.Pos(Thumb) - Rig.Pos(Hand) : FVector::ZeroVector;
		Palm = (Palm - Along * FVector::DotProduct(Palm, Along)).GetSafeNormal();
		if (Along.IsNearlyZero() || Palm.IsNearlyZero() || G < 1e-3f)
		{
			return;
		}
		const float Angle = G * 1.4f;
		Rig.Rotate(Fingers, FQuat::FindBetweenNormals(Along, Along * FMath::Cos(Angle) + Palm * FMath::Sin(Angle)));
		if (Thumb != INDEX_NONE)
		{
			const FVector ThumbDir = (Rig.Pos(Thumb) - Rig.Pos(Hand)).GetSafeNormal();
			Rig.Rotate(Thumb, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(ThumbDir, Along), 0.3f * G));
		}
	}

	/** Two-bone IK: where the elbow (or knee) goes for a root at Root reaching Target, bending toward Pole. */
	inline FVector SolveMiddle(const FVector& Root, const FVector& Target, double L1, double L2, const FVector& Pole)
	{
		const FVector ToTarget = Target - Root;
		const double Distance = ToTarget.Size();
		if (Distance < KINDA_SMALL_NUMBER)
		{
			return Root + Pole.GetSafeNormal() * L1;
		}
		const FVector Dir = ToTarget / Distance;
		const double Reach = FMath::Clamp(Distance, FMath::Abs(L1 - L2) + 0.01, (L1 + L2) * 0.999);
		const double Along = (L1 * L1 - L2 * L2 + Reach * Reach) / (2.0 * Reach);
		const double Out = FMath::Sqrt(FMath::Max(0.0, L1 * L1 - Along * Along));
		FVector Bend = Pole - Dir * FVector::DotProduct(Pole, Dir);
		if (!Bend.Normalize())
		{
			Bend = FVector::UpVector;
		}
		return Root + Dir * Along + Bend * Out;
	}

	/**
	 * Soft IK: pulls a target that is nearly out of reach in along its ray, so the limb eases into full extension
	 * instead of popping straight (the last few percent of reach are approached exponentially). Soft is the share
	 * of the full length over which the softening happens.
	 */
	inline FVector SoftReach(const FVector& Root, const FVector& Target, double Length, double Soft = 0.08)
	{
		const FVector To = Target - Root;
		const double Distance = To.Size();
		const double Hard = Length * (1.0 - Soft);
		if (Distance <= Hard || Distance < KINDA_SMALL_NUMBER || Soft <= 0.0)
		{
			return Target;
		}
		const double Band = Length * Soft;
		const double Eased = Hard + Band * (1.0 - FMath::Exp(-(Distance - Hard) / Band));
		return Root + To * (Eased / Distance);
	}

	/**
	 * A damped spring toward a target (semi-implicit Euler, stable for Omega * Dt < ~1.5): Omega is the natural
	 * frequency (rad/s) and Zeta the damping ratio, under 1 overshoots (follow-through), 1 settles without.
	 */
	template <typename T>
	struct TSpring
	{
		T Value = T(0);
		T Velocity = T(0);

		const T& Step(const T& Target, float Omega, float Zeta, float Dt)
		{
			if (Dt > 0.f)
			{
				// Substeps keep stiff springs stable on a long frame.
				const int32 Steps = FMath::Clamp(FMath::CeilToInt(Omega * Dt / 0.5f), 1, 8);
				const float H = Dt / Steps;
				for (int32 i = 0; i < Steps; ++i)
				{
					Velocity += (Omega * Omega * (Target - Value) - 2.f * Zeta * Omega * Velocity) * H;
					Value += Velocity * H;
				}
			}
			return Value;
		}

		void Snap(const T& To)
		{
			Value = To;
			Velocity = T(0);
		}
	};

	/** Easing for poses: 0 at 0, 1 at 1, smooth at both ends. */
	inline float Smooth01(float X)
	{
		const float T = FMath::Clamp(X, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** A bump that rises over [A, B] and falls over [B, C]: 0 outside, 1 at B. */
	inline float Bump(float X, float A, float B, float C)
	{
		if (X <= A || X >= C)
		{
			return 0.f;
		}
		return X < B ? Smooth01((X - A) / FMath::Max(B - A, 1e-4f)) : 1.f - Smooth01((X - B) / FMath::Max(C - B, 1e-4f));
	}

	/**
	 * Anticipation, action and follow-through for a move that plays over Phase 0..1: the value first dips the
	 * other way by Windup (the wind-up, over the first Lead share of the move), swings to 1, overshoots by
	 * Overshoot and settles back to 1. Games use it for throws and swings; the avatar uses it for its reactions.
	 */
	inline float Anticipate(float Phase, float Windup = 0.15f, float Overshoot = 0.12f, float Lead = 0.25f)
	{
		const float P = FMath::Clamp(Phase, 0.f, 1.f);
		const float L = FMath::Clamp(Lead, 0.05f, 0.6f);
		if (P < L)
		{
			// Back: ease out of rest into the wind-up.
			return -Windup * FMath::Sin(UE_HALF_PI * P / L);
		}
		const float Action = (P - L) / (1.f - L);
		// Forward past the target, then back onto it (a damped half wave on top of an ease).
		const float Base = Smooth01(Action * 1.6f);
		const float Over = Overshoot * FMath::Sin(UE_PI * FMath::Clamp((Action - 0.3f) / 0.7f, 0.f, 1.f)) * (1.f - Action * 0.5f);
		return FMath::Lerp(-Windup, 1.f, Base) + Over;
	}
}
