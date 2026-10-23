// Neon City, the original third track (Tools/track-export/src/neonCity.ts): it loads like Harbor, its walls
// never fold over the road (B14), and the lobby bot pilot laps it cleanly.
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Race/KartBotDriver.h"
#include "Sim/KartSim.h"
#include "Sim/KartTrack.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	constexpr EAutomationTestFlags NeonTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool LoadNeonCity(FAutomationTestBase& Test, FTrack& Out)
	{
		FString Error;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks/neon-city.track.json"));
		if (!FTrack::LoadFromJsonFile(Path, Out, Error))
		{
			Test.AddError(FString::Printf(TEXT("Neon City failed to load: %s"), *Error));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartNeonCityLoadTest, "MovecadeKart.Sim.NeonCityLoads", NeonTestFlags)
bool FKartNeonCityLoadTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadNeonCity(*this, Track)) return false;
	TestEqual(TEXT("id"), Track.Id, FString(TEXT("neon-city")));
	TestTrue(TEXT("analytic walls at 16.3 m"), Track.bAnalyticWalls && FMath::IsNearlyEqual(Track.WallOffset, 16.3));
	TestEqual(TEXT("9 checkpoints"), Track.Checkpoints.Num(), 9);
	TestTrue(TEXT("finish first, radius 24"), Track.Checkpoints[0].bFinish && Track.Checkpoints.Num() == 9 && Track.Checkpoints[1].Radius == 24.0);
	TestEqual(TEXT("start checkpoint index 1"), Track.StartCheckpoint, 1);
	TestEqual(TEXT("4-slot grid"), Track.Grid.Num(), 4);
	TestEqual(TEXT("12 item boxes"), Track.ItemBoxes.Num(), 12);
	TestEqual(TEXT("4 boost pads"), Track.BoostPads.Num(), 4);
	TestEqual(TEXT("9 static cameras"), Track.StaticCameras.Num(), 9);

	// The grid sits behind the finish, on the road, facing it.
	for (const FSpawnSlot& Slot : Track.Grid)
	{
		FVector2D Nearest;
		double Offset = 0.0;
		Track.NearestCentre(Slot.Position, Nearest, Offset);
		TestTrue(TEXT("grid slot on the road"), FMath::Abs(Offset) < 14.0 - Track.KartRadius);
		TestTrue(TEXT("grid slot behind the finish"), Slot.Position.Y > Track.Checkpoints[0].Position.Y);
	}

	// B14: every wall segment is a full offset from the centreline, so none was dropped as folded and none
	// crosses the road. 2 x 784 rows of analytic wall.
	TestEqual(TEXT("no folded wall segments"), Track.Walls.Segments().Num(), 2 * Track.Centre.Num());
	double Closest = TNumericLimits<double>::Max();
	for (const FWallSegment& S : Track.Walls.Segments())
	{
		FVector2D Nearest;
		double Offset = 0.0;
		Closest = FMath::Min(Closest, Track.NearestCentre(S.Start, Nearest, Offset));
	}
	TestTrue(FString::Printf(TEXT("walls stay %.2f m from the centreline"), Closest), Closest > Track.WallOffset - 0.05);

	// Checkpoints lie on the centreline, in order of travel.
	for (int32 i = 1; i < Track.Checkpoints.Num(); ++i)
	{
		FVector2D Nearest;
		double Offset = 0.0;
		TestTrue(FString::Printf(TEXT("checkpoint %d on the line"), i), Track.NearestCentre(Track.Checkpoints[i].Position, Nearest, Offset) < 0.5);
	}
	return true;
}

// The lobby bot (FKartBotDriver, kartPilot.ts) runs three clean laps: no respawn, no wall contact.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartNeonCityBotLapsTest, "MovecadeKart.Race.NeonCityBotLaps", NeonTestFlags)
bool FKartNeonCityBotLapsTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadNeonCity(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	FKartBotDriver Bot;
	int32 Steps = 0, WallSteps = 0, DirtSteps = 0;
	uint32 Teleports = 0;
	TArray<double> Laps;
	double LastLap = 0.0;
	while (!Sim.State().bFinished && Steps < 60 * 240)
	{
		const FVector2D Before = Sim.State().Pos;
		Sim.Step(Bot.Update(Sim.State(), Track.Waypoints, true, Steps / 60.0));
		++Steps;
		WallSteps += Sim.State().bHitWallThisStep ? 1 : 0;
		DirtSteps += Sim.State().bOnDirt ? 1 : 0;
		if (Sim.State().TeleportCount != Teleports)
		{
			Teleports = Sim.State().TeleportCount;
			AddInfo(FString::Printf(TEXT("respawn '%s' at %.2f s from (%.1f, %.1f)"), *Sim.State().LastRespawnReason, Steps / 60.0, Before.X, Before.Y));
		}
		if (Sim.State().LapsCompleted > Laps.Num())
		{
			Laps.Add(Steps / 60.0 - LastLap);
			LastLap = Steps / 60.0;
		}
	}
	AddInfo(FString::Printf(TEXT("neon city lobby bot: laps %s (total %.2f s), wall steps %d, dirt steps %d, %u drift boosts"),
		*FString::JoinBy(Laps, TEXT(" / "), [](double T) { return FString::Printf(TEXT("%.2f"), T); }), Steps / 60.0, WallSteps, DirtSteps, Sim.State().DriftBoostCount));
	TestTrue(TEXT("finished 3 laps"), Sim.State().bFinished && Laps.Num() == 3);
	TestEqual(TEXT("no respawns"), Teleports, 0u);
	TestEqual(TEXT("no wall contact"), WallSteps, 0);
	for (const double T : Laps)
	{
		TestTrue(FString::Printf(TEXT("lap %.2f s is a real lap (25-60 s)"), T), T > 25.0 && T < 60.0);
	}
	return true;
}

#endif
