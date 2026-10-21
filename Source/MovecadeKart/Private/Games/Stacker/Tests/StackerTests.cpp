// Golden tests for Sky Stacker, from movecade-v2 stacker/{engine,balance}.test.ts (Docs/Port/09-stacker.md §8).
#include "Games/Stacker/StackerRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Stacker;

namespace
{
	FMatch Started(const TArray<FString>& Ids = { TEXT("p1") })
	{
		TArray<FSeed> Seeds;
		for (const FString& Id : Ids)
		{
			Seeds.Add({ Id, Id.ToUpper() });
		}
		FMatch Match = CreateMatch(Seeds);
		while (Match.Phase == EMatchPhase::Countdown)
		{
			StepMatch(Match, 100.0);
		}
		return Match;
	}

	void Aim(FMatch& Match, const FString& Id, double Offset)
	{
		FTower* T = Match.FindTower(Id);
		T->CursorCenter = T->Top().Center + Offset;
	}

	void Idle(FMatch& Match)
	{
		for (FTower& T : Match.Towers)
		{
			T.LastDropAtMs = -DropCooldownMs;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackerEngineTest, "MovecadeKart.Stacker.Engine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStackerEngineTest::RunTest(const FString& Parameters)
{
	{
		FMatch M = CreateMatch({ { TEXT("p1"), TEXT("P1") } });
		TestFalse(TEXT("no drop in the countdown"), DropBlock(M, TEXT("p1")).IsSet());
		for (int32 i = 0; i < 29; ++i) StepMatch(M, 100.0);
		TestTrue(TEXT("still counting after 29 steps"), M.Phase == EMatchPhase::Countdown);
		StepMatch(M, 100.0);
		TestTrue(TEXT("playing on the 30th"), M.Phase == EMatchPhase::Playing);
	}
	{
		// Lean 0 is steady: a first drop at offset 0 is perfect for +64, a second for round(34 * 1.12 * 2) = 76.
		FMatch M = Started();
		Aim(M, TEXT("p1"), 0.0);
		const TOptional<FDropOutcome> A = DropBlock(M, TEXT("p1"));
		TestTrue(TEXT("first perfect +64"), A.IsSet() && A->Kind == EDropKind::Perfect && A->ScoreDelta == 64);
		Idle(M);
		Aim(M, TEXT("p1"), 0.0);
		const TOptional<FDropOutcome> B = DropBlock(M, TEXT("p1"));
		TestTrue(TEXT("second perfect +76"), B.IsSet() && B->ScoreDelta == 76);
	}
	{
		// Offset 1.0 on the foundation: a landing, width 2.6, +25; the shard 1 wide at 2.3 flying (2.05, 2.4), spin 4.5259.
		FMatch M = Started();
		Aim(M, TEXT("p1"), 1.0);
		const TOptional<FDropOutcome> O = DropBlock(M, TEXT("p1"));
		TestTrue(TEXT("a landing"), O.IsSet() && O->Kind == EDropKind::Land && O->ScoreDelta == 25);
		TestTrue(TEXT("width 2.6"), FMath::IsNearlyEqual(M.Towers[0].Top().Width, 2.6, 1e-9));
		TestEqual(TEXT("one shard"), M.Shards.Num(), 1);
		if (M.Shards.Num() == 1)
		{
			const FShard& S = M.Shards[0];
			TestTrue(TEXT("shard"), FMath::IsNearlyEqual(S.Width, 1.0, 1e-9) && FMath::IsNearlyEqual(S.X, 2.3, 1e-9) && FMath::IsNearlyEqual(S.Vx, 2.05, 1e-9)
				&& FMath::IsNearlyEqual(S.Vy, 2.4, 1e-9) && FMath::IsNearlyEqual(S.Spin, 4.5259, 1e-4));
		}
	}
	{
		FMatch M = Started();
		Aim(M, TEXT("p1"), BaseWidth - MinWidth / 2.0);
		const TOptional<FDropOutcome> O = DropBlock(M, TEXT("p1"));
		TestTrue(TEXT("a sliver is a miss"), O.IsSet() && O->Kind == EDropKind::Miss && M.Towers[0].Height() == 0 && M.Towers[0].Lives == 2 && M.Towers[0].Score == 0);
	}
	{
		// A 4 degree lean sways the support .162; a cursor there is perfect and stored unswayed.
		FMatch M = Started();
		M.Towers[0].Balance.LeanX = 4.0;
		M.Towers[0].CursorCenter = 0.162;
		const TOptional<FDropOutcome> O = DropBlock(M, TEXT("p1"));
		TestTrue(TEXT("swayed perfect"), O.IsSet() && O->Kind == EDropKind::Perfect && M.Towers[0].Top().Center == 0.0);
	}
	{
		FMatch M = Started();
		Aim(M, TEXT("p1"), 0.3);
		M.Towers[0].Balance.VelocityX = 2.0;
		TestTrue(TEXT("a steady perfect"), DropBlock(M, TEXT("p1"))->Kind == EDropKind::Perfect);
		TestTrue(TEXT("damps the momentum to 1.3"), FMath::IsNearlyEqual(M.Towers[0].Balance.VelocityX, 1.3, 1e-9));
		FMatch L = Started();
		L.Towers[0].Balance.LeanY = 8.0;
		Aim(L, TEXT("p1"), 0.3);
		TestTrue(TEXT("unsteady: the narrow window"), DropBlock(L, TEXT("p1"))->Kind == EDropKind::Land);
	}
	{
		// The reload: the cursor at the sweep's edge, holding for the cooldown.
		FMatch M = Started();
		Aim(M, TEXT("p1"), 0.0);
		DropBlock(M, TEXT("p1"));
		const double Entry = M.Towers[0].CursorCenter;
		TestEqual(TEXT("reload at the edge"), FMath::Abs(Entry), SweepLimit(M.Towers[0].Top().Width, 1));
		StepMatch(M, 100.0);
		TestEqual(TEXT("holds"), M.Towers[0].CursorCenter, Entry);
		TestFalse(TEXT("cooldown"), DropBlock(M, TEXT("p1")).IsSet());
		for (int32 i = 0; i < 4; ++i) StepMatch(M, 100.0);
		TestNotEqual(TEXT("then sweeps"), M.Towers[0].CursorCenter, Entry);
	}
	{
		// Completing row 60 from a raised tower: 300 + 100 per life.
		FMatch M = Started();
		FTower& T = M.Towers[0];
		for (int32 i = 1; i < MaxTowerHeight; ++i)
		{
			FBlock B;
			B.Index = i;
			T.Blocks.Add(B);
		}
		T.Combo = 0;
		Aim(M, TEXT("p1"), 0.0);
		const TOptional<FDropOutcome> O = DropBlock(M, TEXT("p1"));
		TestTrue(TEXT("tower complete"), O.IsSet() && T.bCompleted && O->Label == TEXT("Tower complete"));
		const FEffect* E = M.Effects.FindByPredicate([](const FEffect& X) { return X.Kind == EEffectKind::Complete; });
		TestTrue(TEXT("bonus 600"), E && E->Label == TEXT("Tower complete! +600"));
		TestTrue(TEXT("the match ends"), M.Phase == EMatchPhase::Ended);
	}
	{
		// No RNG: two towers mirror each other.
		FMatch A = Started({ TEXT("p1"), TEXT("p2") }), B = Started({ TEXT("p1"), TEXT("p2") });
		for (int32 i = 0; i < 40; ++i)
		{
			StepMatch(A, 16.0);
			StepMatch(B, 16.0);
		}
		TestEqual(TEXT("identical"), A.Towers[0].CursorCenter, B.Towers[0].CursorCenter);
		TestTrue(TEXT("mirrored"), FMath::IsNearlyEqual(A.Towers[0].CursorCenter, -A.Towers[1].CursorCenter, 1e-10));
	}
	{
		const double Limit = SweepLimit(BaseWidth);
		const FSweepPose Out = Sweep(Limit - 0.1, 1, 1.0, 1000.0, Limit);
		TestTrue(TEXT("reflects"), Out.Direction == -1 && FMath::IsNearlyEqual(Out.Center, Limit - 0.9, 1e-5));
		const FSweepPose Forward = Sweep(0.4, 1, 3.1, 1700.0, Limit);
		TestTrue(TEXT("invertible"), FMath::IsNearlyEqual(Sweep(Forward.Center, Forward.Direction, 3.1, -1700.0, Limit).Center, 0.4, 1e-5));
		TestEqual(TEXT("too wide stays centred"), Sweep(0.0, 1, 5.0, 500.0, 0.0).Center, 0.0);
	}
	{
		FMatch M = Started();
		for (int32 i = 0; i < StartingLives; ++i)
		{
			Idle(M);
			Aim(M, TEXT("p1"), BaseWidth * 2.0);
			DropBlock(M, TEXT("p1"));
		}
		TestTrue(TEXT("out of lives"), M.Towers[0].bEliminated);
		StepMatch(M, 16.0);
		TestTrue(TEXT("ends early"), M.Phase == EMatchPhase::Ended);
	}
	{
		FMatch M = Started();
		FTower& T = M.Towers[0];
		T.Balance.LastInputMs = -2000.0;
		T.Balance.DangerMs = 400.0;
		const double Cursor = T.CursorCenter;
		StepMatch(M, 100.0);
		TestTrue(TEXT("a silent phone freezes"), T.CursorCenter == Cursor && !DropBlock(M, TEXT("p1")).IsSet() && T.Balance.DangerMs == 400.0);
	}
	{
		FMatch M = Started();
		FTower& T = M.Towers[0];
		for (int32 i = 0; i < 35 && T.Lives == StartingLives; ++i)
		{
			T.Balance.Roll = 35.0;
			T.Balance.LastInputMs = M.ElapsedMs;
			StepMatch(M, 100.0);
		}
		TestTrue(TEXT("a topple costs a life and recovers"), T.Lives == 2 && T.Balance.RecoveryMs > 0.0 && M.Effects.Last().Kind == EEffectKind::Topout && !DropBlock(M, TEXT("p1")).IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStackerBalanceTest, "MovecadeKart.Stacker.Balance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStackerBalanceTest::RunTest(const FString& Parameters)
{
	{
		// Countering the wind exactly keeps a tall, narrow tower for the whole round.
		FBalance S;
		int32 Losses = 0;
		for (double T = 0.0; T < 90000.0; T += 20.0)
		{
			const FVector2D W = WindAt(T, 40.0);
			S.Roll = -W.X;
			S.Pitch = -W.Y;
			S.LastInputMs = T;
			Losses += StepBalance(S, T, 40.0, 20.0, 0.6) ? 1 : 0;
		}
		TestEqual(TEXT("no losses"), Losses, 0);
		TestTrue(TEXT("level"), FMath::Sqrt(S.LeanX * S.LeanX + S.LeanY * S.LeanY) < 0.01);
	}
	{
		FBalance S;
		bool bOk = true;
		for (double T = 0.0; T < 3000.0; T += 20.0) bOk &= !StepBalance(S, T, 0.0, 20.0);
		int32 Losses = 0;
		for (double T = 3000.0; T < 30000.0; T += 20.0) Losses += StepBalance(S, T, 30.0, 20.0, 0.6) ? 1 : 0;
		TestTrue(TEXT("ignoring the wind: fine low, lost high"), bOk && Losses > 0 && BalanceLimit(30.0, 0.6) < BalanceLimit(0.0, 3.6));
	}
	{
		FBalance S;
		int32 Losses = 0;
		for (double T = 0.0; T < 3400.0; T += 20.0)
		{
			S.Roll = 40.0;
			S.LastInputMs = T;
			Losses += StepBalance(S, T, 0.0, 20.0) ? 1 : 0;
		}
		TestEqual(TEXT("sustained imbalance costs once"), Losses, 1);
		FBalance Tap;
		bool bTap = false;
		for (double T = 0.0; T < 1000.0; T += 20.0)
		{
			Tap.Roll = T < 60.0 ? 40.0 : 0.0;
			Tap.LastInputMs = T;
			bTap |= StepBalance(Tap, T, 0.0, 20.0);
		}
		TestFalse(TEXT("a tap doesn't"), bTap);
	}
	{
		FBalance S;
		S.LeanX = 35.0;
		S.Roll = 35.0;
		bool bEarly = false;
		for (double T = 0.0; T < BalanceFailureMs - 20.0; T += 20.0)
		{
			S.LastInputMs = T;
			bEarly |= StepBalance(S, T, 0.0, 20.0);
		}
		TestTrue(TEXT("the full warning window"), !bEarly && S.DangerMs == BalanceFailureMs - 20.0);
		S.LastInputMs = BalanceFailureMs;
		TestTrue(TEXT("then the fall"), StepBalance(S, BalanceFailureMs, 0.0, 20.0));
	}
	{
		FBalance S;
		S.Roll = 30.0;
		S.LastInputMs = 0.0;
		S.DangerMs = 300.0;
		S.VelocityX = 4.0;
		bool bAny = false;
		for (double T = BalanceStaleMs + 1.0; T < 10000.0; T += 20.0) bAny |= StepBalance(S, T, 10.0, 20.0);
		TestTrue(TEXT("a stale sensor freezes danger and momentum"), !bAny && S.DangerMs == 300.0 && S.VelocityX == 4.0);
	}
	{
		FBalance S;
		ApplyLandingImpulse(S, 0.8, 1.5, 20.0);
		TestTrue(TEXT("kick toward the overhang"), S.VelocityX > 8.0);
		const double Before = S.VelocityX;
		ApplyLandingImpulse(S, 0.0, 1.5, 20.0);
		TestTrue(TEXT("a perfect damps"), S.VelocityX < Before);
	}
	{
		auto Run = [](double Dt)
		{
			FBalance S;
			for (double T = 0.0; T < 600.0; T += Dt)
			{
				S.Roll = 6.0;
				S.LastInputMs = T;
				StepBalance(S, 0.0, 12.0, Dt);
			}
			return S.LeanX;
		};
		TestTrue(TEXT("inertia the same at 10 and 20 ms"), FMath::IsNearlyEqual(Run(10.0), Run(20.0), 1e-6));
		TestTrue(TEXT("and at 100 ms"), FMath::IsNearlyEqual(Run(20.0), Run(100.0), 1e-6));
	}
	return true;
}

#endif
