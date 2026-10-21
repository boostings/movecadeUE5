#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Track/TrackLifeMeshes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrackLifeMeshesTest, "MovecadeKart.Tracks.LifeMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrackLifeMeshesTest::RunTest(const FString& Parameters)
{
	using namespace MovecadeKart::Life;
	UObject* Outer = GetTransientPackage();
	auto Check = [this](const TCHAR* What, UStaticMesh* Mesh, double MinTop, double MaxTop, int32 Slots)
	{
		if (!TestNotNull(What, Mesh))
		{
			return;
		}
		TestEqual(FString(What) + TEXT(" slots"), Mesh->GetStaticMaterials().Num(), Slots);
		const FBox Box = Mesh->GetBoundingBox();
		TestTrue(FString::Printf(TEXT("%s height %.1f in [%.0f, %.0f]"), What, Box.Max.Z, MinTop, MaxTop), Box.Max.Z >= MinTop && Box.Max.Z <= MaxTop);
	};
	Check(TEXT("grass tuft"), BuildGrassTuftMesh(Outer, 1, 9, 20.0, 30.0), 14.0, 40.0, 1);
	Check(TEXT("flower"), BuildFlowerMesh(Outer, 6), 18.0, 26.0, 1);
	Check(TEXT("spectator"), BuildSpectatorMesh(Outer), 165.0, 176.0, 1);
	Check(TEXT("butterfly"), BuildButterflyMesh(Outer), 0.3, 1.0, 1);
	// Prefab axes (x, z, y): the roof is the stand's top, near 9.5 m.
	Check(TEXT("grandstand"), BuildGrandstandMesh(Outer), 900.0, 1000.0, 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrackLifeSpectatorsTest, "MovecadeKart.Tracks.LifeSpectators",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTrackLifeSpectatorsTest::RunTest(const FString& Parameters)
{
	using namespace MovecadeKart::Life;
	const FGrandstandSpec Spec;
	const TArray<FSpectatorSpot> Spots = GrandstandSpectators(Spec);
	// The web's generator skips about 22% of the ~40 places on each of the six tiers.
	TestTrue(FString::Printf(TEXT("%d spectators"), Spots.Num()), Spots.Num() > 150 && Spots.Num() < 220);
	TestTrue(TEXT("deterministic"), GrandstandSpectators(Spec).Num() == Spots.Num() && GrandstandSpectators(Spec)[7].Position.Equals(Spots[7].Position));
	for (const FSpectatorSpot& Spot : Spots)
	{
		// On their tier's top (three.js y = 0.65 + 0.9 t, prefab axes z), inside the stand's width.
		const double Floor = (0.65 + Spec.TierRise * Spot.Tier) * 100.0;
		if (!TestTrue(TEXT("stands on its tier"), FMath::IsNearlyEqual(Spot.Position.Z, Floor, 1.0))
			|| !TestTrue(TEXT("inside the stand"), FMath::Abs(Spot.Position.X) < Spec.Width * 50.0))
		{
			break;
		}
	}
	return true;
}

#endif
