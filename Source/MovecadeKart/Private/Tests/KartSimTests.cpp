#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
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

	bool LoadViolet(FAutomationTestBase& Test, FTrack& Out)
	{
		FString Error;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks/violet-loop.track.json"));
		if (!FTrack::LoadFromJsonFile(Path, Out, Error))
		{
			Test.AddError(FString::Printf(TEXT("Violet track failed to load: %s"), *Error));
			return false;
		}
		return true;
	}

	/** The -KartAutoDrive autopilot (KartPlayerController): aim at waypoint nearest + 3, full throttle. */
	FKartInput AutoDrive(const FTrack& Track, const FKartState& S)
	{
		const FVector2D Target = Track.Waypoints[(Track.NearestWaypoint(S.Pos) + 3) % Track.Waypoints.Num()];
		const FVector2D D = Target - S.Pos;
		FKartInput Input;
		Input.SteeringX = FMath::Clamp(FMath::FindDeltaAngleRadians(S.Yaw, FMath::Atan2(-D.X, -D.Y)) * 2.5, -1.0, 1.0);
		Input.bForward = true;
		return Input;
	}

	/** host/bots/kartPilot.ts, the lobby bots: 24 m look-ahead along the racing line, gain 5, drift past full lock. */
	struct FLobbyPilot
	{
		int32 Drift = 0;
		FKartInput Input;

		FKartInput Tick(const FTrack& Track, const FKartState& S, uint64 Step)
		{
			if (Step % 6 != 0)
			{
				return Input; // KART_TICK_MS = 100
			}
			const TArray<FVector2D>& Path = Track.Waypoints;
			const int32 N = Path.Num();
			const FVector2D Facing(-FMath::Sin(S.Yaw), -FMath::Cos(S.Yaw));
			double Best = TNumericLimits<double>::Max(), Along = 0.0;
			int32 Segment = 0;
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2D A = Path[i], D = Path[(i + 1) % N] - A;
				const double Len = FMath::Max(D.Size(), 1e-9);
				const double T = FMath::Clamp(FVector2D::DotProduct(S.Pos - A, D) / (Len * Len), 0.0, 1.0);
				const double Cost = FVector2D::Distance(A + D * T, S.Pos) + 20.0 * (1.0 - FVector2D::DotProduct(Facing, D) / Len);
				if (Cost < Best) { Best = Cost; Segment = i; Along = T; }
			}
			FVector2D P = Path[Segment] + (Path[(Segment + 1) % N] - Path[Segment]) * Along;
			double Remaining = 24.0;
			for (int32 k = 0; k <= N; ++k)
			{
				const FVector2D Next = Path[(Segment + 1 + k) % N];
				const double Gap = FVector2D::Distance(Next, P);
				if (Gap >= Remaining) { P += (Next - P) * (Remaining / Gap); break; }
				Remaining -= Gap;
				P = Next;
			}
			const double Error = FMath::FindDeltaAngleRadians(S.Yaw, FMath::Atan2(-(P.X - S.Pos.X), -(P.Y - S.Pos.Y)));
			const double Demand = FMath::Clamp(Error * 5.0, -2.4, 2.4);
			double Steer = 0.0;
			if (Drift == 0 && FMath::Abs(Demand) > 1.4)
			{
				Drift = Demand > 0.0 ? 1 : -1;
				Steer = Drift;
			}
			else
			{
				Drift = (Drift != 0 && Demand * Drift >= 0.9) ? Drift : 0;
				Steer = FMath::Clamp(Demand - 1.4 * Drift, -1.0, 1.0);
			}
			Input = FKartInput();
			Input.SteeringX = Steer;
			Input.bJump = Drift != 0;
			Input.bForward = true;
			return Input;
		}
	};

	struct FLapRun
	{
		TArray<double> Laps;
		uint32 Teleports = 0;
		int32 WallSteps = 0;
		int32 DirtSteps = 0;
		TArray<FString> Respawns;
	};

	/** Drives up to Seconds with the autopilot (bLobbyPilot false) or the lobby bot; returns the lap times. */
	FLapRun DriveLaps(const FTrack& Track, int32 Laps, double Seconds, bool bLobbyPilot)
	{
		FKartSim Sim;
		Sim.Init(&Track, 0);
		FLobbyPilot Pilot;
		FLapRun Run;
		double Last = 0.0;
		uint32 Seen = 0;
		for (int32 i = 0; i < Seconds * 60.0 && Sim.State().LapsCompleted < Laps; ++i)
		{
			const FVector2D Before = Sim.State().Pos;
			Sim.Step(bLobbyPilot ? Pilot.Tick(Track, Sim.State(), i) : AutoDrive(Track, Sim.State()));
			if (Sim.State().TeleportCount != Seen)
			{
				Seen = Sim.State().TeleportCount;
				Run.Respawns.Add(FString::Printf(TEXT("%s at %.2f s from (%.1f, %.1f)"), *Sim.State().LastRespawnReason, Sim.State().Time, Before.X, Before.Y));
			}
			Run.WallSteps += Sim.State().bHitWallThisStep ? 1 : 0;
			Run.DirtSteps += Sim.State().bOnDirt ? 1 : 0;
			if (Sim.State().LapsCompleted > Run.Laps.Num())
			{
				Run.Laps.Add(Sim.State().Time - Last);
				Last = Sim.State().Time;
			}
		}
		Run.Teleports = Sim.State().TeleportCount;
		return Run;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartVioletLoadTest, "MovecadeKart.Sim.VioletLoads", KartTestFlags)
bool FKartVioletLoadTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadViolet(*this, Track)) return false;
	TestEqual(TEXT("490 centre samples"), Track.Centre.Num(), 490);
	TestEqual(TEXT("55 waypoints"), Track.Waypoints.Num(), 55);
	TestFalse(TEXT("mesh walls, not analytic"), Track.bAnalyticWalls);
	TestTrue(TEXT("radial (meshBoundary) push"), Track.bRadialWallPush);
	TestTrue(TEXT("sweep walls from the GLB"), Track.Walls.Segments().Num() > 15000);
	TestTrue(TEXT("probe walls from the GLB"), Track.ProbeWalls.Segments().Num() > 15000);
	TestTrue(TEXT("probes use their own set"), &Track.RayWalls() == &Track.ProbeWalls);
	TestTrue(TEXT("dirt grid"), Track.DirtGrid.IsValid());
	FKartSim Sim;
	Sim.Init(&Track, 0);
	int32 Dirt = 0;
	for (const FVector2D& P : Track.Centre)
	{
		Dirt += Sim.IsOnDirt(P) ? 1 : 0;
	}
	TestEqual(TEXT("the centreline is asphalt"), Dirt, 0);
	// Start straight (sample 0 at (3,-7), heading -Z): the sand verge lies past the road edge on both sides.
	const FVector2D Right(-Track.CentreTangent[0].Y, Track.CentreTangent[0].X);
	TestTrue(TEXT("the start straight's right verge is dirt"), Sim.IsOnDirt(Track.Centre[0] + Right * 20.0) || Sim.IsOnDirt(Track.Centre[0] + Right * 16.0));

	// Harbor keeps one set and its segment-normal push.
	FTrack Harbor;
	if (!LoadHarbor(*this, Harbor)) return false;
	TestFalse(TEXT("harbor: segment-normal push"), Harbor.bRadialWallPush);
	TestTrue(TEXT("harbor: probes use the sweep walls"), &Harbor.RayWalls() == &Harbor.Walls);
	TestFalse(TEXT("harbor: no dirt grid"), Harbor.DirtGrid.IsValid());
	return true;
}

// meshBoundary.ts: a disc driven straight at a mesh wall never tunnels through it, whatever it slides along.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartVioletWallTest, "MovecadeKart.Sim.VioletWallsHold", KartTestFlags)
bool FKartVioletWallTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadViolet(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	int32 Tested = 0, Crossed = 0;
	double WorstOverlap = 0.0;
	for (int32 i = 0; i < Track.Centre.Num(); i += 7)
	{
		for (const double Side : { 1.0, -1.0 })
		{
			// From the centre, sweep 80 m sideways toward the wall in one go.
			const FVector2D Start = Track.Centre[i];
			const FVector2D Dir = FVector2D(-Track.CentreTangent[i].Y, Track.CentreTangent[i].X) * Side;
			double Hit = 0.0;
			FVector2D Normal;
			if (!Track.Walls.Raycast(Start, Dir, 80.0, Hit, Normal) || Hit < 3.0)
			{
				continue; // open run-off on this side
			}
			// Push toward the wall until 20 m past it. The disc may slide round
			// the tyre stacks, but its centre must never cross a wall line (no tunnelling) and it must touch one.
			++Tested;
			FVector2D P = Start;
			bool bCollided = false, bTunnelled = false;
			// Steps of 0.1 m, so the straight line between two results is the path the disc took.
			for (double Pushed = 0.0; Pushed < Hit + 20.0; Pushed += 0.1)
			{
				FVector2D Desired = P + Dir * 0.1;
				bCollided |= Sim.ConstrainMovement(P, Desired);
				const FVector2D Move = Desired - P;
				double Cross = 0.0;
				if (Move.Size() > 1e-6 && Track.Walls.Raycast(P, Move.GetSafeNormal(), Move.Size(), Cross, Normal))
				{
					bTunnelled = true;
				}
				P = Desired;
			}
			double Clearance = TNumericLimits<double>::Max();
			for (const FWallSegment& W : Track.Walls.Segments())
			{
				const double U = FMath::Clamp(FVector2D::DotProduct(P - W.A, W.D) / W.LengthSq, 0.0, 1.0);
				Clearance = FMath::Min(Clearance, FVector2D::Distance(P, W.A + W.D * U));
			}
			WorstOverlap = FMath::Max(WorstOverlap, Track.KartRadius - Clearance);
			if (!bCollided || bTunnelled)
			{
				++Crossed;
				AddInfo(FString::Printf(TEXT("sample %d side %.0f: wall at %.2f, collided %d, tunnelled %d"), i, Side, Hit, bCollided, bTunnelled));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("tested %d approaches; worst end overlap %.2f m"), Tested, WorstOverlap));
	TestTrue(FString::Printf(TEXT("tested walls on both sides (%d)"), Tested), Tested > 80);
	TestEqual(TEXT("no approach tunnels through a wall"), Crossed, 0);
	TestTrue(TEXT("the disc ends at most half a radius into a wall"), WorstOverlap < Track.KartRadius * 0.5);
	return true;
}

// Three laps of each track, two drivers: the in-game -KartAutoDrive autopilot (the Bullet Bill rule: waypoint
// nearest + 3) and the lobby bots' pilot (24 m along the racing line, drifting the tight bends). The bot must run
// Violet clean; the autopilot is logged there (its ~80 m look-ahead cuts Violet's wide corners across the sand
// verge and wide of two checkpoints), and pinned to its phase 2 reference on Harbor.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAutopilotLapsTest, "MovecadeKart.Sim.AutopilotLaps", KartTestFlags)
bool FKartAutopilotLapsTest::RunTest(const FString&)
{
	for (const bool bViolet : { false, true })
	{
		FTrack Track;
		if (!(bViolet ? LoadViolet(*this, Track) : LoadHarbor(*this, Track))) return false;
		for (const bool bBot : { false, true })
		{
			const FLapRun Run = DriveLaps(Track, 3, 200.0, bBot);
			const FString Name = FString::Printf(TEXT("%s %s"), bViolet ? TEXT("violet") : TEXT("harbor"), bBot ? TEXT("lobby bot") : TEXT("autopilot"));
			AddInfo(FString::Printf(TEXT("%s laps: %s; wall steps %d, dirt steps %d, respawns %u%s%s"), *Name,
				*FString::JoinBy(Run.Laps, TEXT(" / "), [](double T) { return FString::Printf(TEXT("%.2f"), T); }),
				Run.WallSteps, Run.DirtSteps, Run.Teleports, Run.Respawns.Num() ? TEXT(": ") : TEXT(""), *FString::Join(Run.Respawns, TEXT("; "))));
			TestEqual(*(Name + TEXT(": three laps")), Run.Laps.Num(), 3);
			if (bBot && bViolet)
			{
				// Violet's walls and dirt let a driver that follows the line run clean: no wall, no sand, no respawn.
				TestEqual(*(Name + TEXT(": no respawns")), Run.Teleports, 0u);
				TestEqual(*(Name + TEXT(": no wall contact")), Run.WallSteps, 0);
				for (const double T : Run.Laps)
				{
					TestTrue(*FString::Printf(TEXT("%s: lap %.2f s within 30-40 s"), *Name, T), T > 30.0 && T < 40.0);
				}
			}
			if (!bBot && !bViolet && Run.Laps.Num() == 3)
			{
				// Harbor regression: the phase 2 autopilot reference (each lap ends on the stuck watchdog at (-12.4, 20.9)).
				TestNearlyEqual(TEXT("harbor autopilot lap 1"), Run.Laps[0], 39.62, 0.02);
				TestNearlyEqual(TEXT("harbor autopilot lap 2"), Run.Laps[1], 40.0, 0.02);
				TestNearlyEqual(TEXT("harbor autopilot lap 3"), Run.Laps[2], 40.0, 0.02);
			}
		}
	}
	return true;
}

#endif
