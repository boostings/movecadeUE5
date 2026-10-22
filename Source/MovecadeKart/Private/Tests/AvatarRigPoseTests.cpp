#include "Avatar/AvatarRigPose.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Rig;

namespace
{
	/**
	 * A five-joint stand-in for the avatar4 upper body in component space: root, torso 100 cm up, head 60 cm above it,
	 * shoulders 20 cm to each side, and a hand on the right, posed facing UE +X (right = +Y, up = +Z).
	 */
	struct FTestBody
	{
		FRigWork Rig;
		FAvatarBones Bones;

		FTestBody()
		{
			const FVector Offsets[] = { FVector::ZeroVector, FVector(0, 0, 100), FVector(0, 0, 60), FVector(0, -20, 50), FVector(0, 20, 50), FVector(0, 30, 0) };
			const int32 Parents[] = { INDEX_NONE, 0, 1, 1, 1, 4 };
			Rig.SetNum(UE_ARRAY_COUNT(Offsets));
			for (int32 i = 0; i < UE_ARRAY_COUNT(Offsets); ++i)
			{
				Rig.Local[i] = FTransform(Offsets[i]);
				Rig.Parent[i] = Parents[i];
			}
			Rig.Recompute();
			Bones.Torso = 1;
			Bones.Head = 2;
			Bones.Arm[0][0] = 3;
			Bones.Arm[1][0] = 4;
			Bones.Arm[1][2] = 5;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarRigPoseTest, "MovecadeKart.Avatar.RigPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarRigPoseTest::RunTest(const FString& Parameters)
{
	FTestBody Test;
	TestTrue(TEXT("torso, head and shoulders: poseable"), Test.Bones.IsPoseable());
	TestFalse(TEXT("no left hand: not a whole arm"), Test.Bones.HasLimb(Test.Bones.Arm[0]));
	FAvatarBones Headless = Test.Bones;
	Headless.Head = INDEX_NONE;
	TestFalse(TEXT("no head: keep the reference pose"), Headless.IsPoseable());

	// The body frame comes from the joints: up torso->head, right across the shoulders, forward from both.
	const FBodyFrame Body(Test.Rig, Test.Bones);
	TestTrue(TEXT("up"), Body.Up.Equals(FVector::UpVector));
	TestTrue(TEXT("right"), Body.Right.Equals(FVector::RightVector));
	TestTrue(TEXT("forward"), Body.Forward.Equals(FVector::ForwardVector));
	TestTrue(TEXT("down"), Body.Down.Equals(-FVector::UpVector));
	TestTrue(TEXT("avatar-frame directions"), Body.ToComponent(FVector(1, 2, 3)).Equals(FVector(1, 2, 3)));

	// A slouched shoulder line still gives an orthonormal frame.
	Test.Rig.Translate(4, FVector(0, 0, 10));
	const FBodyFrame Tilted(Test.Rig, Test.Bones);
	TestTrue(TEXT("right stays square to up"), FMath::IsNearlyZero(FVector::DotProduct(Tilted.Right, Tilted.Up), 1e-6));
	TestTrue(TEXT("unit axes"), Tilted.Right.IsNormalized() && Tilted.Forward.IsNormalized());

	// Cartoon proportions scale the head and the hands present, nothing else.
	ScaleHeadAndHands(Test.Rig, Test.Bones, 1.5, 2.0);
	TestTrue(TEXT("head scaled"), Test.Rig.Local[2].GetScale3D().Equals(FVector(1.5)));
	TestTrue(TEXT("hand scaled"), Test.Rig.Local[5].GetScale3D().Equals(FVector(2.0)));
	TestTrue(TEXT("torso untouched"), Test.Rig.Local[1].GetScale3D().Equals(FVector::OneVector));
	return true;
}

#endif
