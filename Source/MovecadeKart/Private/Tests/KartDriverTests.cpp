#include "KartDriverExpression.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::DriverFace;

namespace
{
	float Weight(const float (&W)[MorphCount], EMorph Morph)
	{
		return W[static_cast<int32>(Morph)];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartDriverFaceTest, "MovecadeKart.Art.DriverFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartDriverFaceTest::RunTest(const FString& Parameters)
{
	float W[MorphCount];

	// At rest: the gentle default smile, nothing else.
	Mix(FMood(), W);
	TestTrue(TEXT("rest: a little smile"), Weight(W, EMorph::Smile) > 0.1f && Weight(W, EMorph::Smile) < 0.4f);
	TestTrue(TEXT("rest: no shock"), FMath::IsNearlyZero(Weight(W, EMorph::EyeWide)) && FMath::IsNearlyZero(Weight(W, EMorph::JawOpen)));

	// Every mix stays in 0..1 whatever the moods (they are lerps between in-range faces).
	FMood All;
	All.Drift = All.Boost = All.Rev = All.Shock = All.Cheer = All.Slump = 1.f;
	Mix(All, W);
	bool bInRange = true;
	for (const float V : W)
	{
		bInRange &= V >= 0.f && V <= 1.f;
	}
	TestTrue(TEXT("weights stay in 0..1"), bInRange);

	// Boost grins; a full shock overrides the grin (later moods win).
	FMood Boost;
	Boost.Boost = 1.f;
	Mix(Boost, W);
	TestTrue(TEXT("boost: grin"), Weight(W, EMorph::Smile) > 0.7f);
	Boost.Shock = 1.f;
	Mix(Boost, W);
	TestTrue(TEXT("shock wins: wide eyes"), Weight(W, EMorph::EyeWide) > 0.9f);
	TestTrue(TEXT("shock wins: no grin"), Weight(W, EMorph::Smile) < 0.1f);

	// The finish: the podium beams, the rest frown.
	FMood Podium;
	Podium.Cheer = 1.f;
	Mix(Podium, W);
	TestTrue(TEXT("podium: beaming"), Weight(W, EMorph::Smile) > 0.95f && Weight(W, EMorph::JawOpen) > 0.3f);
	FMood Off;
	Off.Slump = 1.f;
	Mix(Off, W);
	TestTrue(TEXT("off the podium: frown"), Weight(W, EMorph::MouthFrown) > 0.7f && Weight(W, EMorph::Smile) < 0.05f);

	// Every morph has a name on the avatar4 rig.
	for (int32 i = 0; i < MorphCount; ++i)
	{
		TestFalse(TEXT("morph named"), MorphName(static_cast<EMorph>(i)).IsNone());
	}
	return true;
}

#endif
