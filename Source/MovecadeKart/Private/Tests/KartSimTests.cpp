#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Race/KartBotDriver.h"
#include "Sim/KartSim.h"
#include "Sim/KartTrack.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	constexpr EAutomationTestFlags KartTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	bool LoadHarbor(FAutomationTestBase& Test, FTrack& Out)
	{
		FString Error;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks/sunset-harbor.track.json"));
		if (!FTrack::LoadFromJsonFile(Path, Out, Error))
		{
			Test.AddError(FString::Printf(TEXT("Harbor track failed to load: %s"), *Error));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTrackLoadTest, "MovecadeKart.Sim.TrackLoads", KartTestFlags)
bool FKartTrackLoadTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	TestEqual(TEXT("870 centre samples"), Track.Centre.Num(), 870);
	TestEqual(TEXT("87 waypoints"), Track.Waypoints.Num(), 87);
	TestEqual(TEXT("9 checkpoints"), Track.Checkpoints.Num(), 9);
	TestEqual(TEXT("4 grid slots"), Track.Grid.Num(), 4);
	TestEqual(TEXT("3 boost pads"), Track.BoostPads.Num(), 3);
	TestEqual(TEXT("1740 wall segments"), Track.Walls.Segments().Num(), 1740);
	TestTrue(TEXT("analytic walls"), Track.bAnalyticWalls);
	TestEqual(TEXT("wall offset"), Track.WallOffset, 16.3);
	return true;
}

// Docs/Port/01 §2.2: throttle from rest follows 40 * (1 - e^(-1.8 t)); 90 % at ~1.279 s.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartPedalTest, "MovecadeKart.Sim.PedalEnvelope", KartTestFlags)
bool FKartPedalTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	FKartInput Input;
	Input.bForward = true;
	Sim.Step(Input);
	TestNearlyEqual(TEXT("first step"), Sim.State().Speed, 40.0 * (1.0 - FMath::Exp(-1.8 / 60.0)), 1e-9);
	for (int32 i = 1; i < 77; ++i)
	{
		Sim.Step(Input);
	}
	const double Expected = 40.0 * (1.0 - FMath::Exp(-1.8 * 77.0 / 60.0));
	TestNearlyEqual(TEXT("speed after 77 steps (1.283 s)"), Sim.State().Speed, Expected, 1e-6);
	TestTrue(TEXT("about 90% of top speed"), Sim.State().Speed > 0.89 * 40.0 && Sim.State().Speed < 0.91 * 40.0);

	// Brake/reverse: target -15 at lambda 6.
	Input = FKartInput();
	Input.bBackward = true;
	const double Before = Sim.State().Speed;
	Sim.Step(Input);
	TestNearlyEqual(TEXT("brake step"), Sim.State().Speed, Before + (-15.0 - Before) * (1.0 - FMath::Exp(-6.0 / 60.0)), 1e-9);
	return true;
}

// Docs/Port/01 §3.2: full lock at >= 40 SU turns 0.018 rad per step (1.08 rad/s); positive input turns left.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTurnTest, "MovecadeKart.Sim.TurnRate", KartTestFlags)
bool FKartTurnTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	Sim.MutableState().Speed = 40.0;
	FKartInput Input;
	Input.bForward = true;
	Input.SteeringX = 1.0;
	const double Yaw0 = Sim.State().Yaw;
	Sim.Step(Input);
	TestNearlyEqual(TEXT("one step of full lock"), Sim.State().Yaw - Yaw0, 0.018, 1e-9);

	// Half speed halves the turn.
	FKartSim Slow;
	Slow.Init(&Track, 0);
	Slow.MutableState().Speed = 20.0;
	Slow.Step(Input);
	TestTrue(TEXT("turn scales with min(speed, 40) / 40"), FMath::Abs((Slow.State().Yaw - Yaw0) - 0.018 * Slow.State().Speed / 40.0) < 1e-9);
	return true;
}

// Docs/Port/01 §4.3: drift release tiers.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartDriftTierTest, "MovecadeKart.Sim.DriftTiers", KartTestFlags)
bool FKartDriftTierTest::RunTest(const FString&)
{
	FDriftBoost B;
	TestFalse(TEXT("too short"), DriftExitBoost(0.5, 500.0, 0.0, 1.4, B));
	TestTrue(TEXT("mini"), DriftExitBoost(0.7, 700.0, 0.0, 1.4, B) && B.Multiplier == 1.18 && B.Time == 0.85);
	TestTrue(TEXT("super, wheel centred"), DriftExitBoost(1.2, 1200.0, 0.0, 1.4, B) && B.Multiplier == 1.35 && B.Time == 1.25);
	TestTrue(TEXT("clean super"), DriftExitBoost(1.2, 1200.0, 0.5, 1.4, B) && B.Multiplier == 1.42 && B.Time == 1.38);
	TestTrue(TEXT("clean ultra"), DriftExitBoost(1.9, 1900.0, 0.5, 1.4, B) && B.Multiplier == 1.62 && B.Time == 1.73);
	TestTrue(TEXT("sloppy ultra drops to super"), DriftExitBoost(2.0, 2000.0, -0.5, 1.4, B) && B.Multiplier == 1.35 && B.Label == TEXT("DRIFT_SLOPPY_SUPER"));
	TestTrue(TEXT("apex"), DriftExitBoost(2.3, 2300.0, 0.7, 1.4, B) && B.Multiplier == 1.7 && B.Time == 1.88);
	TestTrue(TEXT("apex needs the steering window"), DriftExitBoost(2.3, 2300.0, 1.0, 1.4, B) && B.Label == TEXT("DRIFT_CLEAN_ULTRA"));
	return true;
}

// A held drift through a corner releases a boost and raises top speed by 40 + 40 * (mult - 1).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartDriftReleaseTest, "MovecadeKart.Sim.DriftRelease", KartTestFlags)
bool FKartDriftReleaseTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	Sim.MutableState().Speed = 40.0;
	FKartInput Input;
	Input.bForward = true;
	Input.bJump = true;
	Input.bLeft = true; // picks the drift side on the hop
	Sim.Step(Input);
	// Hold for 0.8 s, counter-steering so the kart stays on the start straight.
	Input.bLeft = false;
	Input.SteeringX = -0.9;
	for (int32 i = 1; i < 48; ++i)
	{
		Sim.Step(Input);
	}
	TestEqual(TEXT("drifting left"), Sim.State().DriftDirection, 1.4);
	TestFalse(TEXT("stayed off the walls"), Sim.State().bHitWallThisStep);
	Input.bJump = false;
	Input.SteeringX = 0.0;
	Sim.Step(Input);
	TestTrue(TEXT("mini turbo released"), Sim.State().Turbo > 0.0 && Sim.State().ActiveBoost == TEXT("DRIFT_MINI"));
	TestEqual(TEXT("drift reset"), Sim.State().DriftDirection, 0.0);
	return true;
}

// The Harbor sweep keeps the kart centre inside 16.3 - 1.15 of the centreline, whatever the driver does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartWallTest, "MovecadeKart.Sim.WallsHold", KartTestFlags)
bool FKartWallTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	FKartInput Input;
	Input.bForward = true;
	double Worst = 0.0;
	int32 WallHits = 0;
	for (int32 i = 0; i < 60 * 40; ++i)
	{
		// Alternate hard left and right every 3 s, with drift-boost bursts, to slam both walls.
		const bool bLeftPhase = (i / 180) % 2 == 0;
		Input.bLeft = bLeftPhase;
		Input.bRight = !bLeftPhase;
		Input.bJump = (i % 120) < 90;
		Sim.Step(Input);
		FVector2D Nearest;
		double Offset = 0.0;
		Track.NearestCentre(Sim.State().Pos, Nearest, Offset);
		Worst = FMath::Max(Worst, FMath::Abs(Offset));
		WallHits += Sim.State().bHitWallThisStep ? 1 : 0;
	}
	TestTrue(TEXT("hit the walls"), WallHits > 0);
	TestTrue(FString::Printf(TEXT("never past the wall limit (worst offset %.3f m)"), Worst), Worst <= 16.3 - 1.15 + 0.05);
	return true;
}

// Visiting the checkpoints in order counts laps; the finish only counts after 1..8.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartLapTest, "MovecadeKart.Sim.LapCounting", KartTestFlags)
bool FKartLapTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	const FKartInput Idle;
	auto Visit = [&](int32 Index)
	{
		Sim.MutableState().Pos = Track.Checkpoints[Index].Position;
		Sim.Step(Idle);
	};
	Visit(0);
	TestEqual(TEXT("first finish crossing does not count"), Sim.State().LapsCompleted, 0);
	for (int32 Lap = 0; Lap < 3; ++Lap)
	{
		for (int32 Index = 1; Index < Track.Checkpoints.Num(); ++Index)
		{
			Visit(Index);
		}
		Visit(0);
	}
	TestEqual(TEXT("three laps"), Sim.State().LapsCompleted, 3);
	TestTrue(TEXT("finished"), Sim.State().bFinished);
	return true;
}

// The lobby bot (kartPilot.ts) laps Harbor on its own, drifting through the tight bends, with no respawns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartBotLapTest, "MovecadeKart.Race.BotLaps", KartTestFlags)
bool FKartBotLapTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	FKartBotDriver Bot;
	int32 Steps = 0;
	uint32 Teleports = 0;
	int32 OffTrack = 0;
	while (!Sim.State().bFinished && Steps < 60 * 180)
	{
		const FVector2D Before = Sim.State().Pos;
		Sim.Step(Bot.Update(Sim.State(), Track.Waypoints, true, Steps / 60.0));
		++Steps;
		if (Sim.State().TeleportCount != Teleports)
		{
			Teleports = Sim.State().TeleportCount;
			OffTrack += Sim.State().LastRespawnReason == TEXT("offtrack") ? 1 : 0;
			AddInfo(FString::Printf(TEXT("respawn '%s' at %.2f s from (%.1f, %.1f), next checkpoint %d, lap %d"),
				*Sim.State().LastRespawnReason, Steps / 60.0, Before.X, Before.Y, Sim.State().NextCheckpoint, Sim.State().LapsCompleted));
		}
	}
	const double Seconds = Steps / 60.0;
	TestTrue(FString::Printf(TEXT("finished 3 laps (%.1f s)"), Seconds), Sim.State().bFinished);
	TestTrue(TEXT("faster than 3 x 45 s"), Seconds < 135.0);
	// The legacy "blocked" branch can wedge a boosted kart nose-first into a wall (spec 01 bug 9); the
	// stuck watchdog recovers it, as on the web. Leaving the track is still a failure.
	TestEqual(TEXT("no off-track respawns"), OffTrack, 0);
	TestTrue(TEXT("at most one stuck recovery per lap"), Sim.State().TeleportCount <= 3u);
	TestTrue(TEXT("drifted for boosts"), Sim.State().DriftBoostCount > 0);
	AddInfo(FString::Printf(TEXT("bot: %.2f s for 3 laps, %u drift boosts"), Seconds, Sim.State().DriftBoostCount));
	return true;
}

// B1: a multiplier below 1 lowers the top speed (a bogged start is 0.82 for 0.75 s).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartSlowdownTest, "MovecadeKart.Sim.SlowdownApplies", KartTestFlags)
bool FKartSlowdownTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	Sim.MutableState().Speed = 40.0;
	Sim.ApplyBoost(0.5, 2.0, TEXT("TEST_SLOW"));
	FKartInput Input;
	Input.bForward = true;
	for (int32 i = 0; i < 60; ++i)
	{
		Sim.Step(Input);
	}
	TestTrue(FString::Printf(TEXT("slowed toward 20 SU (now %.2f)"), Sim.State().Speed), Sim.State().Speed < 30.0);
	FDriftBoost Start;
	TestTrue(TEXT("bogged start"), RocketStartBoost(2500.0, Start) && Start.Multiplier == 0.82);
	TestTrue(TEXT("perfect start"), RocketStartBoost(1000.0, Start) && Start.Multiplier == 1.6);
	TestFalse(TEXT("no boost under 500 ms"), RocketStartBoost(400.0, Start));
	return true;
}

#endif
