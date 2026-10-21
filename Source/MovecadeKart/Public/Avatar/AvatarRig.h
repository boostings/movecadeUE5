#pragma once

#include "CoreMinimal.h"

/**
 * Pose maths shared by the native avatar anim proxies (the kart driver, the party games' players): a working copy of
 * a compact pose in local and component space, rotations about a joint, aiming a bone at its child, and the
 * two-bone IK middle joint.
 */
namespace MovecadeKart::Rig
{
	struct FRigWork
	{
		TArray<FTransform, TInlineAllocator<40>> Local;
		TArray<FTransform, TInlineAllocator<40>> CS;
		TArray<int32, TInlineAllocator<40>> Parent;

		void Recompute()
		{
			for (int32 i = 0; i < Local.Num(); ++i)
			{
				CS[i] = Parent[i] == INDEX_NONE ? Local[i] : Local[i] * CS[Parent[i]];
			}
		}

		FVector Pos(int32 i) const { return CS[i].GetLocation(); }

		void SetRotation(int32 i, const FQuat& Rotation)
		{
			CS[i].SetRotation(Rotation.GetNormalized());
			Local[i] = Parent[i] == INDEX_NONE ? CS[i] : CS[i].GetRelativeTransform(CS[Parent[i]]);
			Recompute();
		}

		void Rotate(int32 i, const FQuat& Delta)
		{
			SetRotation(i, Delta * CS[i].GetRotation());
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
}
