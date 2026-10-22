#pragma once

#include "CoreMinimal.h"
#include "Avatar/AvatarRig.h"
#include "BonePose.h"

/**
 * What the two native avatar anim proxies (the kart driver's, the party games' standing player's) share around the
 * pose maths of AvatarRig.h: finding the avatar4 joints in a compact pose (cached per bone container), moving a
 * compact pose in and out of an FRigWork, and the avatar's own body frame. Private to the proxies.
 */
namespace MovecadeKart::Rig
{
	/** The joints the proxies drive, as compact-pose indices (INDEX_NONE where the rig or this LOD lacks one). */
	struct FAvatarBones
	{
		/** The proxy root: compact index 0 in every avatar4 pose. */
		int32 Root = 0;
		int32 Torso = INDEX_NONE;
		int32 Neck = INDEX_NONE;
		int32 Head = INDEX_NONE;
		int32 Clavicle[2] = { INDEX_NONE, INDEX_NONE };
		/** [side 0 left, 1 right][upper, middle, end]: shoulder, forearm, hand / thigh, shin, foot. */
		int32 Arm[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
		int32 Leg[2][3] = { { INDEX_NONE, INDEX_NONE, INDEX_NONE }, { INDEX_NONE, INDEX_NONE, INDEX_NONE } };
		int32 Fingers[2] = { INDEX_NONE, INDEX_NONE };
		int32 Thumb[2] = { INDEX_NONE, INDEX_NONE };
		/** The spring chains (avatar4 "Spring_hair*", "Spring_hem*"), parents first. */
		TArray<int32, TInlineAllocator<8>> Hair;
		TArray<int32, TInlineAllocator<8>> Hem;

		/** The rig has what every pose needs (a torso, a head and both shoulders): otherwise keep the reference pose. */
		bool IsPoseable() const { return Torso != INDEX_NONE && Head != INDEX_NONE && Arm[0][0] != INDEX_NONE && Arm[1][0] != INDEX_NONE; }
		bool HasLimb(const int32 (&Limb)[3]) const { return Limb[0] != INDEX_NONE && Limb[1] != INDEX_NONE && Limb[2] != INDEX_NONE; }
	};

	/** Looks the joints up by name only when the bone container changes (LOD, mesh swap), not every evaluation. */
	class FAvatarBoneCache
	{
	public:
		const FAvatarBones& Get(const FBoneContainer& Bones)
		{
			if (!bValid || Serial != Bones.GetSerialNumber())
			{
				Rebuild(Bones);
				Serial = Bones.GetSerialNumber();
				bValid = true;
			}
			return Cached;
		}

	private:
		static int32 Find(const FBoneContainer& Bones, const TCHAR* Name)
		{
			const int32 MeshIndex = Bones.GetReferenceSkeleton().FindBoneIndex(FName(Name));
			if (MeshIndex == INDEX_NONE)
			{
				return INDEX_NONE;
			}
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			return Index.IsValid() ? Index.GetInt() : INDEX_NONE;
		}

		void Rebuild(const FBoneContainer& Bones)
		{
			static const TCHAR* const ArmNames[2][3] = { { TEXT("leftUpperArm"), TEXT("leftForearm"), TEXT("leftHand") }, { TEXT("rightUpperArm"), TEXT("rightForearm"), TEXT("rightHand") } };
			static const TCHAR* const LegNames[2][3] = { { TEXT("leftUpperLeg"), TEXT("leftLowerLeg"), TEXT("leftFoot") }, { TEXT("rightUpperLeg"), TEXT("rightLowerLeg"), TEXT("rightFoot") } };
			static const TCHAR* const ClavicleNames[2] = { TEXT("leftClavicle"), TEXT("rightClavicle") };
			static const TCHAR* const FingerNames[2] = { TEXT("leftFingers"), TEXT("rightFingers") };
			static const TCHAR* const ThumbNames[2] = { TEXT("leftThumb"), TEXT("rightThumb") };
			FAvatarBones& B = Cached;
			B.Torso = Find(Bones, TEXT("torso"));
			B.Neck = Find(Bones, TEXT("neck"));
			B.Head = Find(Bones, TEXT("head"));
			for (int32 Side = 0; Side < 2; ++Side)
			{
				for (int32 Joint = 0; Joint < 3; ++Joint)
				{
					B.Arm[Side][Joint] = Find(Bones, ArmNames[Side][Joint]);
					B.Leg[Side][Joint] = Find(Bones, LegNames[Side][Joint]);
				}
				B.Clavicle[Side] = Find(Bones, ClavicleNames[Side]);
				B.Fingers[Side] = Find(Bones, FingerNames[Side]);
				B.Thumb[Side] = Find(Bones, ThumbNames[Side]);
			}
			B.Hair.Reset();
			B.Hem.Reset();
			const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
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
					(bHair ? B.Hair : B.Hem).Add(Index.GetInt()); // reference order puts parents first
				}
			}
		}

		FAvatarBones Cached;
		uint16 Serial = 0;
		bool bValid = false;
	};

	/** Copies a compact pose (local transforms and parents) into Rig and computes its component space. */
	inline void LoadPose(const FCompactPose& Pose, FRigWork& Rig)
	{
		const int32 Num = Pose.GetNumBones();
		Rig.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const FCompactPoseBoneIndex Index(i);
			Rig.Local[i] = Pose[Index];
			Rig.Parent[i] = Pose.GetParentBoneIndex(Index).GetInt();
		}
		Rig.Recompute();
	}

	/** Writes Rig's local transforms back into the pose. */
	inline void StorePose(const FRigWork& Rig, FCompactPose& Pose)
	{
		for (int32 i = 0; i < Rig.Local.Num(); ++i)
		{
			Pose[FCompactPoseBoneIndex(i)] = Rig.Local[i];
		}
	}

	/** The cartoon proportions over the avatar4 body: the head (hair, eyes and face come with it) and the hands scaled up. */
	inline void ScaleHeadAndHands(FRigWork& Rig, const FAvatarBones& B, double HeadScale, double HandScale)
	{
		Rig.Local[B.Head].SetScale3D(Rig.Local[B.Head].GetScale3D() * HeadScale);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const int32 Hand = B.Arm[Side][2];
			if (Hand != INDEX_NONE)
			{
				Rig.Local[Hand].SetScale3D(Rig.Local[Hand].GetScale3D() * HandScale);
			}
		}
	}

	/** The avatar's own axes in component space, from where its joints are: Forward (where the face looks), Right, Up. */
	struct FBodyFrame
	{
		FVector Forward;
		FVector Right;
		FVector Up;
		FVector Down;

		FBodyFrame(const FRigWork& Rig, const FAvatarBones& B)
		{
			Up = (Rig.Pos(B.Head) - Rig.Pos(B.Torso)).GetSafeNormal();
			const FVector Across = Rig.Pos(B.Arm[1][0]) - Rig.Pos(B.Arm[0][0]);
			Right = (Across - Up * FVector::DotProduct(Across, Up)).GetSafeNormal();
			Forward = FVector::CrossProduct(Right, Up);
			Down = -Up;
		}

		/** A direction given in the avatar frame (X forward, Y right, Z up) in component space. */
		FVector ToComponent(const FVector& V) const { return Forward * V.X + Right * V.Y + Up * V.Z; }
	};
}
