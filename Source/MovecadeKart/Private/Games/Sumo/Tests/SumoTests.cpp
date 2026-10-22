// Golden tests for Sumo Bumpers' rules (Games/Sumo/SumoRules.h): the puck physics, contacts, ring-outs, the
// shrinking ring, power-ups, rounds and the match, and determinism (the same inputs give the same state).
#include "Games/Sumo/SumoRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Sumo;

namespace
{
	FMatch Make(int32 Players = 2, uint32 Seed = 7)
	{
		TArray<FSeed> Seeds;
		for (int32 i = 0; i < Players; ++i)
		{
			Seeds.Add({ FString::Printf(TEXT("p%d"), i + 1), FString::Printf(TEXT("P%d"), i + 1) });
		}
		return CreateMatch(Seeds, Seed);
	}

	/** Past the countdown, into the fight. */
	FMatch Fighting(int32 Players = 2, uint32 Seed = 7)
	{
		FMatch M = Make(Players, Seed);
		while (M.Phase == EPhase::Countdown)
		{
			StepMatch(M);
		}
		return M;
	}

	/** Parks every puck far apart near the middle, still, so a test can stage one contact. */
	void Park(FMatch& M)
	{
		for (int32 i = 0; i < M.Pucks.Num(); ++i)
		{
			M.Pucks[i].Pos = FVector2D(-3.0 + 2.0 * i, 6.0 - 12.0 * (i % 2)) * 0.5;
			M.Pucks[i].Vel = FVector2D::ZeroVector;
		}
	}

	/** Off the ring, each puck on its own bearing (so none lands on another). */
	void Eject(FPuck& P)
	{
		const double A = P.Slot * UE_DOUBLE_HALF_PI + 0.3;
		P.Pos = FVector2D(FMath::Cos(A), FMath::Sin(A)) * (ArenaRadius + 0.5);
	}

	int32 CountEvents(const FMatch& M, EEventKind Kind)
	{
		int32 N = 0;
		for (const FRingEvent& E : M.Events)
		{
			N += E.Kind == Kind ? 1 : 0;
		}
		return N;
	}

	/** An all-bot match to the end; returns the steps it took (capped). */
	int64 RunBots(FMatch& M, TArray<FBotBrain>& Brains, int64 Cap = 60 * 60 * 8)
	{
		while (M.Phase != EPhase::MatchOver && M.Steps < Cap)
		{
			for (int32 i = 0; i < M.Pucks.Num(); ++i)
			{
				SetInput(M, i, ThinkBot(M, i, Brains[i]));
			}
			StepMatch(M);
		}
		return M.Steps;
	}

	TArray<FBotBrain> Brains(int32 N, uint32 Seed)
	{
		TArray<FBotBrain> Out;
		for (int32 i = 0; i < N; ++i)
		{
			Out.Add(MakeBot(0.35 + 0.2 * i, Seed * 31u + i));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoPhysicsTest, "MovecadeKart.Sumo.Physics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoPhysicsTest::RunTest(const FString& Parameters)
{
	{
		// The countdown: three seconds of 60 Hz steps, pucks frozen at their spawns, then the fight.
		FMatch M = Make(2);
		TestTrue(TEXT("spawn left"), M.Pucks[0].Pos.Equals(FVector2D(0.0, -SpawnRadius), 1e-9));
		TestTrue(TEXT("spawn right"), M.Pucks[1].Pos.Equals(FVector2D(0.0, SpawnRadius), 1e-9));
		TestTrue(TEXT("facing the middle"), M.Pucks[0].Facing.Equals(FVector2D(0.0, 1.0), 1e-9));
		SetInput(M, 0, { FVector2D(1.0, 0.0), true });
		for (int32 i = 0; i < 179; ++i) StepMatch(M);
		TestTrue(TEXT("still counting at step 179"), M.Phase == EPhase::Countdown);
		TestTrue(TEXT("nobody moved"), M.Pucks[0].Pos.Equals(FVector2D(0.0, -SpawnRadius), 1e-9) && M.Pucks[0].DashCooldownMs == 0.0);
		StepMatch(M);
		TestTrue(TEXT("fight on step 180"), M.Phase == EPhase::Fight);
	}
	{
		// One step at full tilt: v = a dt, decayed by drag, then integrated.
		FMatch M = Fighting(2);
		SetInput(M, 0, { FVector2D(1.0, 0.0), false });
		const FVector2D P0 = M.Pucks[0].Pos;
		StepMatch(M);
		const double V = Accel * StepSeconds * FMath::Exp(-Drag * StepSeconds);
		TestTrue(TEXT("one step's speed"), FMath::IsNearlyEqual(M.Pucks[0].Vel.X, V, 1e-12) && M.Pucks[0].Vel.Y == 0.0);
		TestTrue(TEXT("one step's travel"), FMath::IsNearlyEqual(M.Pucks[0].Pos.X - P0.X, V * StepSeconds, 1e-12));
		// Held for ten seconds the speed settles at the terminal speed a (e^-k dt) dt / (1 - e^-k dt).
		for (int32 i = 0; i < 600; ++i)
		{
			M.Pucks[0].Pos = FVector2D(-3.0, 0.0);
			M.PowerUps.Reset();
			SetInput(M, 0, { FVector2D(1.0, 0.0), false });
			StepMatch(M);
		}
		const double K = FMath::Exp(-Drag * StepSeconds);
		TestTrue(TEXT("terminal speed"), FMath::IsNearlyEqual(M.Pucks[0].Vel.X, Accel * StepSeconds * K / (1.0 - K), 1e-6));
		// A tilt longer than 1 is clamped to the disc.
		SetInput(M, 1, { FVector2D(3.0, 4.0), false });
		TestTrue(TEXT("tilt clamped"), M.Pucks[1].Input.Tilt.Equals(FVector2D(0.6, 0.8), 1e-12));
	}
	{
		// The dash: +DashSpeed along the tilt, a 320 ms burst, a 1.5 s cooldown; a press on cooldown is buffered.
		FMatch M = Fighting(2);
		SetInput(M, 0, { FVector2D(0.0, 1.0), true });
		StepMatch(M);
		const FPuck& P = M.Pucks[0];
		const double Expect = (Accel * StepSeconds + DashSpeed) * FMath::Exp(-Drag * StepSeconds);
		TestTrue(TEXT("dash speed"), FMath::IsNearlyEqual(P.Vel.Y, Expect, 1e-9));
		TestEqual(TEXT("dash cooldown"), P.DashCooldownMs, DashCooldownMs);
		TestEqual(TEXT("a dash event"), CountEvents(M, EEventKind::Dash), 1);
		SetInput(M, 0, { FVector2D::ZeroVector, false });
		for (int32 i = 0; i < 80; ++i) StepMatch(M);
		SetInput(M, 0, { FVector2D(0.0, -1.0), true });
		StepMatch(M);
		TestEqual(TEXT("no second dash on cooldown"), CountEvents(M, EEventKind::Dash), 1);
		const int32 ToReady = FMath::CeilToInt32(M.Pucks[0].DashCooldownMs / StepMs);
		TestTrue(TEXT("buffered press is still there"), M.Pucks[0].DashQueuedMs > 0.0 || ToReady > 10);
		FMatch B = Fighting(2);
		SetInput(B, 0, { FVector2D(0.0, 1.0), true });
		StepMatch(B);
		SetInput(B, 0, { FVector2D::ZeroVector, false });
		for (int32 i = 0; i < 85; ++i) StepMatch(B);
		// 86 steps = 1433 ms in: ready in 4 steps, inside the 160 ms buffer.
		SetInput(B, 0, { FVector2D(0.0, -1.0), true });
		for (int32 i = 0; i < 6; ++i) StepMatch(B);
		TestEqual(TEXT("a buffered press fires when ready"), CountEvents(B, EEventKind::Dash), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoCollisionTest, "MovecadeKart.Sumo.Collisions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoCollisionTest::RunTest(const FString& Parameters)
{
	{
		// Head on, equal mass: the closing speed comes back scaled by the restitution, momentum kept.
		FMatch M = Fighting(2);
		Park(M);
		FPuck& A = M.Pucks[0];
		FPuck& B = M.Pucks[1];
		A.Pos = FVector2D(0.0, -(PuckRadius - 0.05));
		B.Pos = FVector2D(0.0, PuckRadius - 0.05);
		A.Vel = FVector2D(0.0, 4.0);
		B.Vel = FVector2D(0.0, -2.0);
		ResolveContact(M, 0, 1);
		TestTrue(TEXT("separated"), FMath::IsNearlyEqual(B.Pos.Y - A.Pos.Y, 2.0 * PuckRadius, 1e-12));
		TestTrue(TEXT("momentum kept"), FMath::IsNearlyEqual(A.Vel.Y + B.Vel.Y, 2.0, 1e-12));
		TestTrue(TEXT("restitution"), FMath::IsNearlyEqual(B.Vel.Y - A.Vel.Y, 6.0 * Restitution, 1e-12));
		TestTrue(TEXT("A bounced back"), FMath::IsNearlyEqual(A.Vel.Y, 1.0 - 3.0 * Restitution, 1e-12));
		TestEqual(TEXT("a bump event"), CountEvents(M, EEventKind::Bump), 1);
		TestTrue(TEXT("both remember the hit"), A.LastHitBy == 1 && B.LastHitBy == 0);
	}
	{
		// Heavy (2.4) into a still puck: the heavy one barely slows, the light one flies.
		FMatch M = Fighting(2);
		Park(M);
		FPuck& A = M.Pucks[0];
		FPuck& B = M.Pucks[1];
		A.Power = EPower::Heavy;
		A.PowerMs = HeavyMs;
		A.Pos = FVector2D(0.0, -(PuckRadius - 0.05));
		B.Pos = FVector2D(0.0, PuckRadius - 0.05);
		A.Vel = FVector2D(0.0, 5.0);
		ResolveContact(M, 0, 1);
		const double J = (1.0 + Restitution) * 5.0 / (1.0 / HeavyMass + 1.0);
		TestTrue(TEXT("heavy keeps going"), FMath::IsNearlyEqual(A.Vel.Y, 5.0 - J / HeavyMass, 1e-12) && A.Vel.Y > 0.0);
		TestTrue(TEXT("light flies"), FMath::IsNearlyEqual(B.Vel.Y, J, 1e-12) && B.Vel.Y > 5.0);
		TestTrue(TEXT("the light one is pushed out of the overlap more"), FMath::IsNearlyEqual(B.Pos.Y - (PuckRadius - 0.05), 0.1 * 1.0 / (1.0 / HeavyMass + 1.0), 1e-12));
	}
	{
		// A dash hit: the impulse plus the shove.
		FMatch M = Fighting(2);
		Park(M);
		FPuck& A = M.Pucks[0];
		FPuck& B = M.Pucks[1];
		A.Pos = FVector2D(0.0, -(PuckRadius - 0.01));
		B.Pos = FVector2D(0.0, PuckRadius - 0.01);
		A.Vel = FVector2D(0.0, 10.0);
		A.DashMs = 200.0;
		ResolveContact(M, 0, 1);
		const double J = (1.0 + Restitution) * 10.0 / 2.0;
		TestTrue(TEXT("victim shoved"), FMath::IsNearlyEqual(B.Vel.Y, J + DashShove, 1e-12));
		TestTrue(TEXT("attacker checked"), FMath::IsNearlyEqual(A.Vel.Y, 10.0 - J - DashShove * 0.25, 1e-12));
		TestEqual(TEXT("a dash hit event"), CountEvents(M, EEventKind::DashHit), 1);
	}
	{
		// A shield: its holder hardly moves, the attacker bounces off hard, and the shield pops.
		FMatch M = Fighting(2);
		Park(M);
		FPuck& A = M.Pucks[0];
		FPuck& B = M.Pucks[1];
		B.Power = EPower::Shield;
		B.PowerMs = ShieldMs;
		A.Pos = FVector2D(0.0, -(PuckRadius - 0.01));
		B.Pos = FVector2D(0.0, PuckRadius - 0.01);
		A.Vel = FVector2D(0.0, 10.0);
		A.DashMs = 200.0;
		ResolveContact(M, 0, 1);
		const double J = (1.0 + ShieldRestitution) * 10.0 / 1.25;
		TestTrue(TEXT("attacker rebounds"), FMath::IsNearlyEqual(A.Vel.Y, 10.0 - J, 1e-12) && A.Vel.Y < -8.0);
		TestTrue(TEXT("holder barely moves"), FMath::IsNearlyEqual(B.Vel.Y, J * 0.25, 1e-12) && B.Vel.Y < 5.0);
		TestTrue(TEXT("shield popped"), B.Power == EPower::None && A.DashMs == 0.0);
		TestEqual(TEXT("a block event"), CountEvents(M, EEventKind::ShieldBlock), 1);
	}
	{
		// Separating pucks that overlap are only pushed apart, with no impulse or event.
		FMatch M = Fighting(2);
		Park(M);
		M.Pucks[0].Pos = FVector2D(1.0, 0.0);
		M.Pucks[1].Pos = FVector2D(1.0, 1.0);
		M.Pucks[0].Vel = FVector2D(0.0, -1.0);
		M.Pucks[1].Vel = FVector2D(0.0, 1.0);
		const int32 Before = M.Events.Num();
		ResolveContact(M, 0, 1);
		TestTrue(TEXT("separation only"), M.Pucks[0].Vel.Equals(FVector2D(0.0, -1.0)) && M.Events.Num() == Before
			&& FMath::IsNearlyEqual(FVector2D::Distance(M.Pucks[0].Pos, M.Pucks[1].Pos), 2.0 * PuckRadius, 1e-12));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoRingTest, "MovecadeKart.Sumo.Ring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoRingTest::RunTest(const FString& Parameters)
{
	{
		// A shove over the edge: the victim falls, the shover gets the knockout, the round goes to the shover.
		FMatch M = Fighting(2);
		FPuck& A = M.Pucks[0];
		FPuck& B = M.Pucks[1];
		A.Pos = FVector2D(0.0, 6.0);
		B.Pos = FVector2D(0.0, 7.65);
		A.Vel = FVector2D(0.0, 9.0);
		A.DashMs = 200.0;
		int32 Steps = 0;
		while (B.bStanding && Steps < 120)
		{
			StepMatch(M);
			++Steps;
		}
		TestTrue(TEXT("over the edge"), !B.bStanding && B.bFalling);
		TestTrue(TEXT("knockout credited"), A.Knockouts == 1 && A.Score == KnockoutPoints + RoundWinPoints);
		const FRingEvent* Out = M.Events.FindByPredicate([](const FRingEvent& E) { return E.Kind == EEventKind::RingOut; });
		TestTrue(TEXT("ring-out event names the killer"), Out && Out->A == 1 && Out->B == 0);
		TestTrue(TEXT("round over, A wins"), M.Phase == EPhase::RoundOver && M.RoundWinner == 0 && A.RoundWins == 1);
		// The fall finishes in the sea.
		for (int32 i = 0; i < FMath::CeilToInt32(FallMs / StepMs) + 1; ++i) StepMatch(M);
		TestTrue(TEXT("splash"), B.bOut && CountEvents(M, EEventKind::Splash) == 1);
	}
	{
		// Rolling off alone (no hit for 3 s): no knockout for anyone.
		FMatch M = Fighting(3);
		FPuck& C = M.Pucks[2];
		C.LastHitBy = 0;
		C.LastHitAtMs = M.RoundMs - KoCreditMs - 100.0;
		C.Pos = FVector2D(ArenaRadius - 0.01, 0.0);
		C.Vel = FVector2D(3.0, 0.0);
		StepMatch(M);
		TestTrue(TEXT("self ring-out"), C.bFalling && M.Pucks[0].Knockouts == 0);
		TestTrue(TEXT("two still standing: the round goes on"), M.Phase == EPhase::Fight && M.StandingCount() == 2);
	}
	{
		// The shrink schedule: a warning 3 s before each band goes, 1 m a band down to the core, then the core.
		TestEqual(TEXT("first shrink"), ShrinkAtMs(0), FirstShrinkMs);
		TestEqual(TEXT("third shrink"), ShrinkAtMs(2), FirstShrinkMs + 2.0 * ShrinkEveryMs);
		TestEqual(TEXT("the collapse"), ShrinkAtMs(RingBands), CollapseMs);
		TestEqual(TEXT("core"), RadiusAfter(RingBands), CoreRadius);
		TestEqual(TEXT("nothing after"), RadiusAfter(RingBands + 1), 0.0);
		FMatch M = Fighting(2);
		// Keep both pucks still in the middle so the round lasts.
		auto Hold = [&M]()
		{
			M.Pucks[0].Pos = FVector2D(0.0, -1.0);
			M.Pucks[1].Pos = FVector2D(0.0, 1.0);
			M.Pucks[0].Vel = M.Pucks[1].Vel = FVector2D::ZeroVector;
		};
		while (!M.IsShrinkPending() && M.Phase == EPhase::Fight) { Hold(); StepMatch(M); }
		TestTrue(TEXT("warned 3 s ahead"), FMath::Abs(M.RoundMs - (FirstShrinkMs - ShrinkWarningMs)) <= StepMs && M.PendingRadius == ArenaRadius - BandWidth
			&& M.Radius == ArenaRadius && CountEvents(M, EEventKind::ShrinkWarning) == 1);
		while (M.ShrinksDone == 0 && M.Phase == EPhase::Fight) { Hold(); StepMatch(M); }
		TestTrue(TEXT("shrunk on time"), FMath::Abs(M.RoundMs - FirstShrinkMs) <= StepMs && M.Radius == ArenaRadius - BandWidth && !M.IsShrinkPending());
		while (M.Phase == EPhase::Fight && M.RoundMs < CollapseMs + 100.0) { Hold(); StepMatch(M); }
		TestTrue(TEXT("the collapse drops everyone: a drawn round"), M.Phase == EPhase::RoundOver && M.RoundWinner == INDEX_NONE
			&& M.RoundWinners.Num() == 1 && M.RoundWinners[0] == INDEX_NONE && CountEvents(M, EEventKind::RoundDraw) == 1);
		TestEqual(TEXT("bands fell"), M.ShrinksDone, RingBands + 1);
	}
	{
		// Power-ups: the first at 6 s, inside the ring, from the seeded RNG; picked up on touch.
		FMatch A = Fighting(2, 99), B = Fighting(2, 99);
		while (A.PowerUps.Num() == 0 && A.RoundMs < PowerFirstMs + 100.0)
		{
			A.Pucks[0].Pos = B.Pucks[0].Pos = FVector2D(-7.5, 0.0);
			A.Pucks[1].Pos = B.Pucks[1].Pos = FVector2D(7.5, 0.0);
			StepMatch(A);
			StepMatch(B);
		}
		TestTrue(TEXT("spawned on time"), A.PowerUps.Num() == 1 && FMath::IsNearlyEqual(A.RoundMs, PowerFirstMs, StepMs));
		TestTrue(TEXT("seeded: the same spot and kind"), B.PowerUps.Num() == 1 && A.PowerUps[0].Pos == B.PowerUps[0].Pos && A.PowerUps[0].Kind == B.PowerUps[0].Kind);
		TestTrue(TEXT("inside the ring"), A.PowerUps[0].Pos.Size() < ArenaRadius - 1.6);
		const EPower Kind = A.PowerUps[0].Kind;
		A.Pucks[0].Pos = A.PowerUps[0].Pos + FVector2D(0.5, 0.0);
		StepMatch(A);
		TestTrue(TEXT("picked up"), A.PowerUps.Num() == 0 && A.Pucks[0].Power == Kind && A.Pucks[0].PowerMs > 0.0);
		FMatch C = Fighting(2, 100);
		while (C.PowerUps.Num() == 0 && C.RoundMs < PowerFirstMs + 100.0)
		{
			C.Pucks[0].Pos = FVector2D(-7.5, 0.0);
			C.Pucks[1].Pos = FVector2D(7.5, 0.0);
			StepMatch(C);
		}
		TestTrue(TEXT("another seed, another spot"), C.PowerUps.Num() == 1 && C.PowerUps[0].Pos != A.Events.FindByPredicate([](const FRingEvent& E) { return E.Kind == EEventKind::PowerSpawn; })->Pos);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoMatchTest, "MovecadeKart.Sumo.Match",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoMatchTest::RunTest(const FString& Parameters)
{
	{
		// Two straight round wins take the match: round 2 resets the ring and the spawns, the scores carry.
		FMatch M = Fighting(2);
		for (int32 Round = 1; Round <= 2; ++Round)
		{
			M.Pucks[1].Pos = FVector2D(0.0, ArenaRadius + 0.1);
			StepMatch(M);
			TestTrue(*FString::Printf(TEXT("round %d to P1"), Round), M.Phase == EPhase::RoundOver && M.RoundWinner == 0);
			while (M.Phase == EPhase::RoundOver) StepMatch(M);
			if (Round == 1)
			{
				TestTrue(TEXT("round 2 counts down from the spawns"), M.Phase == EPhase::Countdown && M.Round == 2 && M.Radius == ArenaRadius
					&& M.Pucks[1].bStanding && M.Pucks[1].Pos.Equals(FVector2D(0.0, SpawnRadius), 1e-9));
				while (M.Phase == EPhase::Countdown) StepMatch(M);
			}
		}
		TestTrue(TEXT("match over after two"), M.Phase == EPhase::MatchOver && M.MatchWinner == 0 && M.RoundsPlayed == 2);
		TestTrue(TEXT("scores"), M.Pucks[0].Score == 2 * RoundWinPoints && M.Pucks[1].Score == 0 && M.Pucks[1].Falls == 2);
		TestEqual(TEXT("standings"), Standings(M)[0], 0);
	}
	{
		// Three players, three different winners: play on to a fourth round; a draw doesn't count.
		FMatch M = Fighting(3);
		auto WinRound = [&M](int32 Winner)
		{
			for (FPuck& P : M.Pucks)
			{
				if (P.Slot != Winner) Eject(P);
			}
			StepMatch(M);
			while (M.Phase == EPhase::RoundOver) StepMatch(M);
			while (M.Phase == EPhase::Countdown) StepMatch(M);
		};
		WinRound(0);
		WinRound(1);
		WinRound(2);
		TestTrue(TEXT("a fourth round"), M.Phase == EPhase::Fight && M.Round == 4);
		for (FPuck& P : M.Pucks) Eject(P);
		StepMatch(M);
		TestTrue(TEXT("all out at once: a draw"), M.RoundWinner == INDEX_NONE);
		while (M.Phase == EPhase::RoundOver) StepMatch(M);
		while (M.Phase == EPhase::Countdown) StepMatch(M);
		WinRound(1);
		TestTrue(TEXT("P2 takes it 2-1-1"), M.Phase == EPhase::MatchOver && M.MatchWinner == 1 && M.Pucks[1].RoundWins == 2 && M.RoundsPlayed == 5);
	}
	{
		// The cap: five rounds and the match ends even without a second win.
		FMatch M = Fighting(4);
		for (int32 R = 0; R < MaxRounds && M.Phase != EPhase::MatchOver; ++R)
		{
			for (FPuck& P : M.Pucks)
			{
				if (P.Slot != R % 4) Eject(P);
			}
			StepMatch(M);
			while (M.Phase == EPhase::RoundOver) StepMatch(M);
			while (M.Phase == EPhase::Countdown) StepMatch(M);
		}
		// Round 5 went to P1 again: two wins.
		TestTrue(TEXT("over"), M.Phase == EPhase::MatchOver && M.MatchWinner == 0 && M.RoundsPlayed == 5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoDeterminismTest, "MovecadeKart.Sumo.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoDeterminismTest::RunTest(const FString& Parameters)
{
	{
		// The same inputs from the same seed: the same state, step for step.
		FMatch A = Make(4, 1234), B = Make(4, 1234);
		uint32 InputRng = 42;
		bool bSame = true;
		for (int32 Step = 0; Step < 60 * 40; ++Step)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				const double Ang = Random01(InputRng) * 2.0 * UE_DOUBLE_PI;
				const FInput In{ FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Random01(InputRng), Random01(InputRng) < 0.02 };
				SetInput(A, i, In);
				SetInput(B, i, In);
			}
			StepMatch(A);
			StepMatch(B);
			bSame &= Fingerprint(A) == Fingerprint(B);
		}
		TestTrue(TEXT("identical every step"), bSame);
		// Idle pucks: only the power-up RNG differs between the seeds.
		FMatch C = Make(4, 1235), D = Make(4, 1234);
		for (int32 Step = 0; Step < 60 * 12; ++Step)
		{
			StepMatch(C);
			StepMatch(D);
		}
		TestNotEqual(TEXT("the seed matters"), Fingerprint(C), Fingerprint(D));
	}
	{
		// House bots play a whole match to a winner, and replay it exactly.
		for (const int32 N : { 2, 3, 4 })
		{
			FMatch A = Make(N, 77 + N), B = Make(N, 77 + N);
			TArray<FBotBrain> BA = Brains(N, 5), BB = Brains(N, 5);
			const int64 StepsA = RunBots(A, BA);
			const int64 StepsB = RunBots(B, BB);
			TestTrue(*FString::Printf(TEXT("%d bots finish"), N), A.Phase == EPhase::MatchOver && A.MatchWinner != INDEX_NONE);
			TestTrue(*FString::Printf(TEXT("%d bots: same match"), N), StepsA == StepsB && Fingerprint(A) == Fingerprint(B) && A.RoundWinners == B.RoundWinners);
			int32 Kos = 0, Draws = 0;
			for (const FPuck& P : A.Pucks) Kos += P.Knockouts;
			for (const int32 W : A.RoundWinners) Draws += W == INDEX_NONE ? 1 : 0;
			TestTrue(*FString::Printf(TEXT("%d bots fight (knockouts %d, draws %d)"), N, Kos, Draws), Kos >= A.RoundsPlayed && Draws < A.RoundsPlayed);
			// A round should take seconds, not the whole shrink: the bots push.
			const double MeanRound = (StepsA * StepMs) / FMath::Max(1, A.RoundsPlayed);
			TestTrue(*FString::Printf(TEXT("%d bots: rounds of %.1f s"), N, MeanRound / 1000.0), MeanRound < 70000.0);
			AddInfo(FString::Printf(TEXT("Sumo %d bots: %d rounds, winner %d, %lld steps, %d KOs, fingerprint %llx"), N, A.RoundsPlayed, A.MatchWinner, StepsA, Kos, Fingerprint(A)));
		}
	}
	{
		// Golden: this four-bot match, seed 81, plays out exactly so. A deliberate rules or bot change updates it.
		FMatch M = Make(4, 81);
		TArray<FBotBrain> B = Brains(4, 5);
		const int64 Steps = RunBots(M, B);
		int32 Kos = 0;
		for (const FPuck& P : M.Pucks) Kos += P.Knockouts;
		TestEqual(TEXT("golden steps"), Steps, static_cast<int64>(5022));
		TestEqual(TEXT("golden rounds"), M.RoundsPlayed, 3);
		TestEqual(TEXT("golden winner"), M.MatchWinner, 2);
		TestEqual(TEXT("golden knockouts"), Kos, 9);
	}
	{
		// Pacing: house bots settle a round in well under a minute (the shrink forces it, but they should push).
		for (const int32 N : { 2, 3, 4 })
		{
			double Total = 0.0, Longest = 0.0;
			int32 Rounds = 0;
			for (uint32 Seed = 1; Seed <= 10; ++Seed)
			{
				FMatch M = Make(N, Seed * 101u);
				TArray<FBotBrain> B;
				for (int32 i = 0; i < N; ++i) B.Add(MakeBot(0.45 + 0.17 * (i % 3), Seed * 7u + i));
				double FightAt = 0.0;
				while (M.Phase != EPhase::MatchOver && M.Steps < 60 * 60 * 10)
				{
					for (int32 i = 0; i < N; ++i) SetInput(M, i, ThinkBot(M, i, B[i]));
					const EPhase Before = M.Phase;
					StepMatch(M);
					if (Before == EPhase::Countdown && M.Phase == EPhase::Fight) FightAt = M.ElapsedMs;
					if (Before == EPhase::Fight && M.Phase == EPhase::RoundOver)
					{
						const double Len = (M.ElapsedMs - FightAt) / 1000.0;
						Total += Len;
						Longest = FMath::Max(Longest, Len);
						++Rounds;
					}
				}
			}
			const double Mean = Total / FMath::Max(1, Rounds);
			AddInfo(FString::Printf(TEXT("Sumo pacing, %d bots: %d rounds, mean fight %.1f s, longest %.1f s"), N, Rounds, Mean, Longest));
			TestTrue(*FString::Printf(TEXT("%d bots: rounds are brisk (mean %.1f s)"), N, Mean), Mean < 40.0);
		}
	}
	{
		// A sharp bot beats a sloppy one more often than not over a run of seeds.
		int32 SharpWins = 0;
		for (uint32 Seed = 1; Seed <= 12; ++Seed)
		{
			FMatch M = Make(2, Seed);
			TArray<FBotBrain> B = { MakeBot(0.95, Seed * 7u), MakeBot(0.2, Seed * 13u) };
			RunBots(M, B);
			SharpWins += M.MatchWinner == 0 ? 1 : 0;
		}
		TestTrue(*FString::Printf(TEXT("skill tells (%d/12)"), SharpWins), SharpWins >= 8);
	}
	return true;
}

#endif
