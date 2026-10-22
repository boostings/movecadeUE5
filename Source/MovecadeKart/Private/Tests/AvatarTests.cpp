#include "Avatar/ArcadeAvatar.h"
#include "Avatar/AvatarProps.h"
#include "Avatar/AvatarRig.h"
#include "KartDriverExpression.h"
#include "KartLook.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	/** The rig work as it was before the subtree recompute: every change recomputes the whole skeleton. */
	struct FFullRig
	{
		TArray<FTransform> Local, CS;
		TArray<int32> Parent;

		void Recompute()
		{
			for (int32 i = 0; i < Local.Num(); ++i)
			{
				CS[i] = Parent[i] == INDEX_NONE ? Local[i] : Local[i] * CS[Parent[i]];
			}
		}
		void SetRotation(int32 i, const FQuat& Rotation)
		{
			CS[i].SetRotation(Rotation.GetNormalized());
			Local[i] = Parent[i] == INDEX_NONE ? CS[i] : CS[i].GetRelativeTransform(CS[Parent[i]]);
			Recompute();
		}
		void Translate(int32 i, const FVector& Delta)
		{
			CS[i].AddToTranslation(Delta);
			Local[i] = Parent[i] == INDEX_NONE ? CS[i] : CS[i].GetRelativeTransform(CS[Parent[i]]);
			Recompute();
		}
	};

	bool BitEqual(const FTransform& A, const FTransform& B)
	{
		const FQuat QA = A.GetRotation(), QB = B.GetRotation();
		const FVector TA = A.GetTranslation(), TB = B.GetTranslation(), SA = A.GetScale3D(), SB = B.GetScale3D();
		return FMemory::Memcmp(&QA, &QB, sizeof(FQuat)) == 0 && FMemory::Memcmp(&TA, &TB, sizeof(FVector)) == 0
			&& FMemory::Memcmp(&SA, &SB, sizeof(FVector)) == 0;
	}

	float Weight(const float (&W)[DriverFace::MorphCount], DriverFace::EMorph Morph)
	{
		return W[static_cast<int32>(Morph)];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarRigSubtreeTest, "MovecadeKart.Avatar.RigSubtreeBitExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarRigSubtreeTest::RunTest(const FString& Parameters)
{
	// Random parents-first skeletons (like a compact pose), random rotations and moves: the subtree recompute must
	// give exactly (bit for bit) what the whole-skeleton recompute gave.
	FRandomStream Random(1234);
	for (int32 Trial = 0; Trial < 20; ++Trial)
	{
		const int32 Num = Random.RandRange(5, 60);
		Rig::FRigWork Fast;
		FFullRig Full;
		Fast.SetNum(Num);
		Full.Local.SetNum(Num);
		Full.CS.SetNum(Num);
		Full.Parent.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 Parent = i == 0 ? INDEX_NONE : Random.RandRange(0, i - 1);
			const FTransform Local(FQuat(Random.GetUnitVector(), Random.FRandRange(-1.0, 1.0)), Random.GetUnitVector() * Random.FRandRange(1.0, 40.0),
				FVector(Random.FRandRange(0.9, 1.1)));
			Fast.Parent[i] = Full.Parent[i] = Parent;
			Fast.Local[i] = Full.Local[i] = Local;
		}
		Fast.Recompute();
		Full.Recompute();
		for (int32 Step = 0; Step < 40; ++Step)
		{
			const int32 Bone = Random.RandRange(0, Num - 1);
			if (Random.FRand() < 0.75f)
			{
				const FQuat Rotation = FQuat(Random.GetUnitVector(), Random.FRandRange(-0.8, 0.8)) * Fast.CS[Bone].GetRotation();
				Fast.SetRotation(Bone, Rotation);
				Full.SetRotation(Bone, Rotation);
			}
			else
			{
				const FVector Delta = Random.GetUnitVector() * Random.FRandRange(0.0, 5.0);
				Fast.Translate(Bone, Delta);
				Full.Translate(Bone, Delta);
			}
		}
		bool bSame = true;
		for (int32 i = 0; i < Num; ++i)
		{
			bSame &= BitEqual(Fast.CS[i], Full.CS[i]) && BitEqual(Fast.Local[i], Full.Local[i]);
		}
		TestTrue(FString::Printf(TEXT("trial %d bit-identical"), Trial), bSame);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarRigHelpersTest, "MovecadeKart.Avatar.RigHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarRigHelpersTest::RunTest(const FString& Parameters)
{
	// Soft reach: untouched within reach, never past the limb's length, and monotonic beyond.
	const FVector Root = FVector::ZeroVector;
	TestTrue(TEXT("soft reach leaves near targets"), Rig::SoftReach(Root, FVector(50, 0, 0), 100.0).Equals(FVector(50, 0, 0)));
	const double Far = Rig::SoftReach(Root, FVector(150, 0, 0), 100.0).X;
	const double Farther = Rig::SoftReach(Root, FVector(300, 0, 0), 100.0).X;
	TestTrue(TEXT("soft reach stays within the limb"), Far < 100.0 && Farther < 100.0 && Farther > Far && Far > 92.0);

	// The spring settles on its target; an underdamped one overshoots on the way.
	Rig::TSpring<float> Spring;
	float Peak = 0.f;
	for (int32 i = 0; i < 240; ++i)
	{
		Peak = FMath::Max(Peak, Spring.Step(1.f, 11.f, 0.42f, 1.f / 60.f));
	}
	TestTrue(TEXT("spring overshoots"), Peak > 1.05f);
	TestTrue(TEXT("spring settles"), FMath::IsNearlyEqual(Spring.Value, 1.f, 0.01f));
	Rig::TSpring<float> Long;
	Long.Step(1.f, 40.f, 0.5f, 0.25f); // a long hitch: substeps keep it stable
	TestTrue(TEXT("spring stable on a long frame"), FMath::Abs(Long.Value) < 3.f);

	// Anticipation: winds up the other way, ends on 1, and overshoots in between.
	TestTrue(TEXT("starts at rest"), FMath::IsNearlyZero(Rig::Anticipate(0.f)));
	TestTrue(TEXT("winds up"), Rig::Anticipate(0.2f) < -0.05f);
	TestTrue(TEXT("ends on target"), FMath::IsNearlyEqual(Rig::Anticipate(1.f), 1.f, 1e-4f));
	float Max = 0.f;
	for (float P = 0.f; P <= 1.f; P += 0.01f)
	{
		Max = FMath::Max(Max, Rig::Anticipate(P));
	}
	TestTrue(TEXT("follows through"), Max > 1.02f);
	TestTrue(TEXT("bump"), FMath::IsNearlyEqual(Rig::Bump(0.5f, 0.f, 0.5f, 1.f), 1.f) && Rig::Bump(1.5f, 0.f, 0.5f, 1.f) == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarPoseInputTest, "MovecadeKart.Avatar.PoseInputNeutral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarPoseInputTest::RunTest(const FString& Parameters)
{
	// Games that never touch the new fields get the old behaviour's inputs.
	const FArcadePoseInput P;
	TestTrue(TEXT("no emote"), P.Emote == EArcadeEmote::None && P.EmoteWeight == 0.f);
	TestTrue(TEXT("auto variants"), P.CheerStyle == EArcadeCheer::Auto && P.SlumpStyle == EArcadeSlump::Auto);
	TestTrue(TEXT("no effort"), P.Effort == 0.f);
	TestTrue(TEXT("automatic elbows and hands"), P.ElbowHint[0].IsZero() && P.ElbowHint[1].IsZero() && P.Grip[0] < 0.f && P.Grip[1] < 0.f);
	TestTrue(TEXT("hands in the torso frame"), P.HandSpace == EArcadeHandSpace::Torso);
	TestTrue(TEXT("full secondary motion"), P.Secondary == 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarFaceTest, "MovecadeKart.Avatar.Face",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarFaceTest::RunTest(const FString& Parameters)
{
	using namespace DriverFace;
	float Rest[MorphCount], W[MorphCount];
	Mix(FMood(), Rest);

	// A neutral pose wears the resting face.
	Avatar::FaceFor(FArcadePoseInput(), W);
	bool bSame = true;
	for (int32 i = 0; i < MorphCount; ++i)
	{
		bSame &= FMath::IsNearlyEqual(W[i], Rest[i]);
	}
	TestTrue(TEXT("neutral pose: resting face"), bSame);

	FArcadePoseInput Strain;
	Strain.Effort = 1.f;
	Avatar::FaceFor(Strain, W);
	TestTrue(TEXT("effort: brows down, teeth set"), Weight(W, EMorph::BrowDown) > 0.7f && Weight(W, EMorph::MouthPress) > 0.7f && Weight(W, EMorph::Smile) < 0.1f);

	FArcadePoseInput Shrug;
	Shrug.Emote = EArcadeEmote::Shrug;
	Shrug.EmoteWeight = 1.f;
	Avatar::FaceFor(Shrug, W);
	TestTrue(TEXT("shrug: brows up"), Weight(W, EMorph::BrowInnerUp) > 0.5f);

	FArcadePoseInput Wave;
	Wave.Emote = EArcadeEmote::Wave;
	Wave.EmoteWeight = 1.f;
	Avatar::FaceFor(Wave, W);
	TestTrue(TEXT("wave: smiling"), Weight(W, EMorph::Smile) > 0.6f);

	// A cheer yells: the jaw opens and closes over time.
	FArcadePoseInput Cheer;
	Cheer.Cheer = 1.f;
	float JawMin = 1.f, JawMax = 0.f;
	for (float T = 0.f; T < 1.f; T += 0.02f)
	{
		Cheer.Time = T;
		Avatar::FaceFor(Cheer, W);
		JawMin = FMath::Min(JawMin, Weight(W, EMorph::JawOpen));
		JawMax = FMath::Max(JawMax, Weight(W, EMorph::JawOpen));
	}
	TestTrue(TEXT("cheer: the mouth works"), JawMax - JawMin > 0.2f && JawMax <= 1.f);

	// Meh and effort are new moods: with them off, the race's faces are unchanged.
	FMood Old;
	Old.Boost = 0.5f;
	Old.Shock = 0.3f;
	float A[MorphCount], B[MorphCount];
	Mix(Old, A);
	Old.Effort = 0.f;
	Old.Meh = 0.f;
	Mix(Old, B);
	TestTrue(TEXT("race faces unchanged"), FMemory::Memcmp(A, B, sizeof(A)) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAvatarLooksTest, "MovecadeKart.Avatar.Variety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAvatarLooksTest::RunTest(const FString& Parameters)
{
	// The seats cover every outfit, both bodies for every face, and the code-built props.
	TSet<EKartOutfit> Outfits;
	TSet<EKartAccessory> Accessories;
	TSet<FString> FaceBodies;
	for (int32 Seat = 0; Seat < 12; ++Seat)
	{
		const FKartLook L = FKartLook::MakeVaried(Seat);
		Outfits.Add(L.Outfit);
		Accessories.Add(L.Accessory);
		FaceBodies.Add(FString(FKartLook::FaceId(L.Face)) + FKartLook::BodyId(L.Body));
	}
	TestEqual(TEXT("every outfit worn"), Outfits.Num(), static_cast<int32>(EKartOutfit::GlowHoodie) + 1);
	TestTrue(TEXT("props worn"), Accessories.Contains(EKartAccessory::Crown) && Accessories.Contains(EKartAccessory::Beanie)
		&& Accessories.Contains(EKartAccessory::PartyHat) && Accessories.Contains(EKartAccessory::Headphones));
	TestTrue(TEXT("twelve seats, twelve drivers"), FaceBodies.Num() == 12);

	TestTrue(TEXT("beanie hides the hair"), Avatar::HidesHair(EKartAccessory::Beanie) && Avatar::HidesHair(EKartAccessory::Cap));
	TestFalse(TEXT("crown keeps the hair"), Avatar::HidesHair(EKartAccessory::Crown));
	TestTrue(TEXT("mesh accessories aren't props"), !Avatar::IsBuiltProp(EKartAccessory::Glasses) && !Avatar::IsBuiltProp(EKartAccessory::None));
	for (const EKartAccessory A : { EKartAccessory::Crown, EKartAccessory::PartyHat, EKartAccessory::Headphones, EKartAccessory::Beanie })
	{
		UStaticMesh* Mesh = Avatar::PropMesh(A);
		TestTrue(TEXT("prop builds"), Mesh != nullptr && Mesh->GetStaticMaterials().Num() == static_cast<int32>(Avatar::EPropSlot::Count));
		TestTrue(TEXT("prop is shared"), Avatar::PropMesh(A) == Mesh);
		if (Mesh)
		{
			// Sits on the head: centred over the skull, above the brow line, within a head's reach.
			const FBox Box = Mesh->GetBoundingBox();
			TestTrue(TEXT("prop over the head"), Box.GetCenter().Z > 20.0 && Box.Max.Z < 90.0 && FMath::Abs(Box.GetCenter().X) < 8.0);
		}
	}
	return true;
}

#endif
