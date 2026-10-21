// Golden tests for Samurai Slice, from movecade-v2 samurai/{engine,bladeContact,bladeIntent,contactReplay}.test.ts
// (Docs/Port/08-samurai.md §9). The RNG and spawn goldens were computed by the web's own engine.ts under Node.
#include "Games/Samurai/SamuraiBlade.h"
#include "Games/Samurai/SamuraiRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Samurai;

namespace
{
	void Advance(FMatch& Match, double TotalMs, double StepMs = 16.0)
	{
		for (double T = 0.0; T < TotalMs; T += StepMs)
		{
			StepMatch(Match, StepMs);
		}
	}

	FMatch Playing(int32 Players = 1)
	{
		TArray<FSeed> Seeds;
		for (int32 i = 0; i < Players; ++i)
		{
			Seeds.Add({ FString::Printf(TEXT("p%d"), i + 1), FString::Printf(TEXT("P%d"), i + 1) });
		}
		FMatch Match = CreateMatch(Seeds);
		Advance(Match, 3100.0);
		return Match;
	}

	FTarget& Plant(FMatch& Match, EVariant Variant = EVariant::Apple, double Progress = CutApex, const FString& Player = TEXT("p1"), int32 Lane = 0)
	{
		FTarget& T = Match.Targets.AddDefaulted_GetRef();
		const FTargetDef& D = Def(Variant);
		T.Id = Match.NextId++;
		T.PlayerId = Player;
		T.LaneIndex = Lane;
		T.Variant = Variant;
		T.Kind = D.Kind;
		T.Progress = Progress;
		T.Speed = 1.0 / 2000.0;
		T.Spin = 1.0;
		T.Radius = D.Radius;
		T.Score = D.Score;
		T.Preferred = D.Preferred;
		return T;
	}

	FSwing Swing(EDirection Direction = EDirection::Down, double Power = 0.8)
	{
		return FSwing{ Direction, Power };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiRngTest, "MovecadeKart.Samurai.Rng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSamuraiRngTest::RunTest(const FString& Parameters)
{
	// node: mulberry32 seeds 0x5a3c and 99, first six draws.
	const double Golden5a3c[] = { 0.317838707240, 0.736571870046, 0.644416995812, 0.745727151632, 0.639515717048, 0.450677268440 };
	const double Golden99[] = { 0.260465812404, 0.804822765524, 0.540871534962, 0.690243425779, 0.001138708321, 0.817543087760 };
	FMulberry32 A(0x5a3c), B(99);
	for (int32 i = 0; i < 6; ++i)
	{
		TestTrue(FString::Printf(TEXT("seed 0x5a3c draw %d"), i), FMath::IsNearlyEqual(A.Next(), Golden5a3c[i], 1e-11));
		TestTrue(FString::Printf(TEXT("seed 99 draw %d"), i), FMath::IsNearlyEqual(B.Next(), Golden99[i], 1e-11));
	}
	// node: two players, seed 0x5a3c, 194 + 1200 steps of 16 ms (engine.ts copy).
	FMatch M = CreateMatch({ { TEXT("p1"), TEXT("A") }, { TEXT("p2"), TEXT("B") } }, 0x5a3c);
	for (int32 i = 0; i < 194 + 1200; ++i)
	{
		StepMatch(M, 16.0);
	}
	TestEqual(TEXT("elapsed"), M.ElapsedMs, 19312.0);
	TestEqual(TEXT("next id"), M.NextId, 35);
	TestTrue(TEXT("cooldown p1"), FMath::IsNearlyEqual(M.SpawnCooldownMs[0], 124.968694, 1e-5));
	TestTrue(TEXT("cooldown p2"), FMath::IsNearlyEqual(M.SpawnCooldownMs[1], 754.990069, 1e-5));
	TestEqual(TEXT("launches p1"), M.Players[0].Launches, 16);
	TestEqual(TEXT("launches p2"), M.Players[1].Launches, 16);
	struct FGold { int32 Id; const TCHAR* Player; EVariant Variant; double Lateral; double Progress; double FlightMs; double Spin; };
	const FGold Gold[] = {
		{ 31, TEXT("p1"), EVariant::Melon, -0.6, 0.373311295, 2828.738414, -0.781296512 },
		{ 32, TEXT("p1"), EVariant::Melon, 0.6, 0.373311295, 2828.738414, 0.521482085 },
		{ 33, TEXT("p2"), EVariant::Melon, -0.6, 0.138229170, 2662.245598, 0.658565504 },
		{ 34, TEXT("p2"), EVariant::Melon, 0.6, 0.138229170, 2662.245598, -0.117622127 },
	};
	TestEqual(TEXT("targets"), M.Targets.Num(), 4);
	for (int32 i = 0; i < FMath::Min(M.Targets.Num(), 4); ++i)
	{
		const FTarget& T = M.Targets[i];
		TestEqual(TEXT("id"), T.Id, Gold[i].Id);
		TestEqual(TEXT("player"), T.PlayerId, FString(Gold[i].Player));
		TestTrue(TEXT("variant"), T.Variant == Gold[i].Variant);
		TestTrue(TEXT("lateral"), FMath::IsNearlyEqual(T.Lateral, Gold[i].Lateral, 1e-9));
		TestTrue(TEXT("progress"), FMath::IsNearlyEqual(T.Progress, Gold[i].Progress, 1e-8));
		TestTrue(TEXT("flight"), FMath::IsNearlyEqual(1.0 / T.Speed, Gold[i].FlightMs, 1e-5));
		TestTrue(TEXT("spin"), FMath::IsNearlyEqual(T.Spin, Gold[i].Spin, 1e-8));
	}
	// The first p1 fruit, cut by the live gesture: 24 + 5 timing + 6 direction + 2 power = 37.
	TSet<int32> Contact = { 31 };
	const TOptional<FSwingOutcome> O = ResolveSwing(M, TEXT("p1"), ContactSwing(), &Contact);
	TestTrue(TEXT("golden cut"), O.IsSet() && O->Kind == EOutcome::Hit && O->ScoreDelta == 37 && O->Combo == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiEngineTest, "MovecadeKart.Samurai.Engine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSamuraiEngineTest::RunTest(const FString& Parameters)
{
	{
		FMatch M = CreateMatch({ { TEXT("p1"), TEXT("A") } });
		Advance(M, 1000.0);
		TestTrue(TEXT("countdown holds"), M.Phase == EMatchPhase::Countdown && M.Targets.Num() == 0 && M.TimeRemainingMs == RoundDurationMs);
		Advance(M, 2100.0);
		TestTrue(TEXT("then plays"), M.Phase == EMatchPhase::Playing);
	}
	{
		FMatch M = Playing(3);
		for (int32 i = 0; i < 400; ++i) StepMatch(M, 16.0);
		for (const FPlayer& P : M.Players)
		{
			const int32 N = M.Targets.FilterByPredicate([&P](const FTarget& T) { return T.PlayerId == P.Id; }).Num();
			TestTrue(TEXT("every lane spawns, at most four"), N > 0 && N <= MaxTargetsPerLane);
		}
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		Plant(M);
		const TOptional<FSwingOutcome> O = ResolveSwing(M, TEXT("p1"), Swing(EDirection::Left));
		TestTrue(TEXT("a cut scores"), O.IsSet() && O->Kind == EOutcome::Hit && O->ScoreDelta > 16);
		TestEqual(TEXT("score"), M.Players[0].Score, O->ScoreDelta);
		TestEqual(TEXT("combo"), M.Players[0].Combo, 1);
		TestEqual(TEXT("cleared"), M.Targets.Num(), 0);
		const FEffect* Hit = M.Effects.FindByPredicate([](const FEffect& E) { return E.Kind == EEffectKind::Hit; });
		TestTrue(TEXT("hit effect names the apple"), Hit && Hit->Variant.IsSet() && Hit->Variant.GetValue() == EVariant::Apple && Hit->CutCount == 1);
	}
	{
		FMatch A = Playing(), B = Playing();
		A.Targets.Reset();
		B.Targets.Reset();
		Plant(A);
		Plant(B);
		const int32 With = ResolveSwing(A, TEXT("p1"), Swing(EDirection::Left))->ScoreDelta;
		const int32 Without = ResolveSwing(B, TEXT("p1"), Swing(EDirection::Up))->ScoreDelta;
		TestTrue(TEXT("direction pays"), With > Without && Without > 0);
	}
	{
		FMatch A = Playing(), B = Playing();
		A.Targets.Reset();
		B.Targets.Reset();
		Plant(A, EVariant::Apple, CutApex);
		Plant(B, EVariant::Apple, CutEnd - 0.01);
		TestTrue(TEXT("apex pays"), ResolveSwing(A, TEXT("p1"), Swing(EDirection::Up))->ScoreDelta > ResolveSwing(B, TEXT("p1"), Swing(EDirection::Up))->ScoreDelta);
		TestEqual(TEXT("quality 1 at the apex"), CutQuality(CutApex), 1.0);
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		Plant(M);
		Plant(M, EVariant::Apple, CutApex + 0.05);
		Plant(M, EVariant::Apple, CutApex - 0.05);
		const TOptional<FSwingOutcome> O = ResolveSwing(M, TEXT("p1"), Swing(EDirection::Left));
		TestTrue(TEXT("three at once"), O->CutTargets == 3 && M.Players[0].Combo == 3);
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		Plant(M, EVariant::Apple, CutStart - 0.05);
		TestTrue(TEXT("outside the window misses"), ResolveSwing(M, TEXT("p1"), Swing())->Kind == EOutcome::Miss && M.Targets.Num() == 1);
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		Plant(M);
		Plant(M, EVariant::Bomb);
		const TOptional<FSwingOutcome> O = ResolveSwing(M, TEXT("p1"), Swing(EDirection::Left));
		TestTrue(TEXT("a bomb voids the swing"), O->Kind == EOutcome::Bomb && M.Players[0].Score == 0 && M.Players[0].Lives == StartingLives - 1 && M.Players[0].Combo == 0);
	}
	{
		FMatch M = Playing();
		for (int32 Life = 0; Life < StartingLives; ++Life)
		{
			M.Targets.Reset();
			Plant(M, EVariant::Bomb);
			ResolveSwing(M, TEXT("p1"), Swing());
		}
		TestTrue(TEXT("out of lives"), M.Players[0].bEliminated && !ResolveSwing(M, TEXT("p1"), Swing()).IsSet());
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		FTarget& Bomb = Plant(M, EVariant::Bomb, 0.99);
		Bomb.Speed = 1.0 / 100.0;
		StepMatch(M, 200.0);
		TestEqual(TEXT("a fallen bomb is dodged"), M.Players[0].Lives, StartingLives);
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		Plant(M);
		ResolveSwing(M, TEXT("p1"), Swing(EDirection::Left));
		M.Targets.Reset();
		FTarget& Fruit = Plant(M, EVariant::Apple, 0.99);
		Fruit.Speed = 1.0 / 100.0;
		StepMatch(M, 200.0);
		TestTrue(TEXT("escaped fruit breaks the combo, keeps the life"), M.Players[0].Combo == 0 && M.Players[0].Lives == StartingLives);
	}
	{
		FMatch Cold = Playing(), Hot = Playing();
		Cold.Targets.Reset();
		Hot.Targets.Reset();
		Hot.Players[0].Combo = 40;
		Plant(Cold);
		Plant(Hot);
		const double Ratio = static_cast<double>(ResolveSwing(Hot, TEXT("p1"), Swing(EDirection::Left))->ScoreDelta) / ResolveSwing(Cold, TEXT("p1"), Swing(EDirection::Left))->ScoreDelta;
		TestTrue(TEXT("hot combo about 2x"), Ratio > 1.9 && Ratio < 2.1);
	}
	{
		FMatch M = Playing(2);
		M.Players[0].Score = 120;
		M.Players[1].Score = 90;
		Advance(M, RoundDurationMs + 200.0, 100.0);
		TestTrue(TEXT("ends on the clock"), M.Phase == EMatchPhase::Ended && M.Targets.Num() == 0 && M.WinnerIds == TArray<FString>{ TEXT("p1") });
	}
	TestTrue(TEXT("waves"), WaveNumber(0) == 1 && WaveNumber(15000) == 2 && WaveNumber(45000) == WaveCount && WaveNumber(120000) == WaveCount);
	{
		FMatch M = Playing();
		StepMatch(M, 60000.0);
		TestTrue(TEXT("a stall can't teleport the round"), M.Phase == EMatchPhase::Playing && M.TimeRemainingMs > RoundDurationMs - 1000.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiFocusTest, "MovecadeKart.Samurai.Focus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSamuraiFocusTest::RunTest(const FString& Parameters)
{
	{
		FMatch M = Playing();
		M.Targets.Reset();
		for (int32 i = 0; i < FocusCuts; ++i)
		{
			const TSet<int32> C = { Plant(M).Id };
			ResolveSwing(M, TEXT("p1"), Swing(), &C);
		}
		TestTrue(TEXT("three precise cuts start Focus"), M.Players[0].FocusRemainingMs == FocusDurationMs && M.Players[0].FocusCharge == 0);
		FMatch Control = Playing();
		Control.Targets.Reset();
		Control.Players[0].Combo = M.Players[0].Combo;
		const TSet<int32> N = { Plant(Control).Id };
		const TSet<int32> F = { Plant(M).Id };
		TestEqual(TEXT("Focus doubles"), ResolveSwing(M, TEXT("p1"), Swing(), &F)->ScoreDelta, ResolveSwing(Control, TEXT("p1"), Swing(), &N)->ScoreDelta * 2);
		Advance(M, FocusDurationMs + 100.0);
		TestEqual(TEXT("Focus expires"), M.Players[0].FocusRemainingMs, 0.0);
	}
	{
		FMatch M = Playing(2);
		M.Targets.Reset();
		M.Players[0].FocusRemainingMs = 3000.0;
		const int32 FruitId = Plant(M, EVariant::Apple, 0.3).Id;
		const int32 BombId = Plant(M, EVariant::Bomb, 0.3).Id;
		const int32 OtherId = Plant(M, EVariant::Apple, 0.3, TEXT("p2"), 1).Id;
		const double Remaining = M.TimeRemainingMs;
		StepMatch(M, 100.0);
		auto ProgressOf = [&M](int32 Id) { return M.Targets.FindByPredicate([Id](const FTarget& T) { return T.Id == Id; })->Progress; };
		TestTrue(TEXT("Focus slows the fruit"), FMath::IsNearlyEqual(ProgressOf(FruitId), 0.3 + 0.05 * FocusFlightScale, 1e-9));
		TestTrue(TEXT("not the bomb"), FMath::IsNearlyEqual(ProgressOf(BombId), 0.35, 1e-9));
		TestTrue(TEXT("not the other lane"), FMath::IsNearlyEqual(ProgressOf(OtherId), 0.35, 1e-9));
		TestEqual(TEXT("not the clock"), M.TimeRemainingMs, Remaining - 100.0);
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		M.Players[0].Lives = 2;
		TSet<int32> C = { Plant(M, EVariant::Scroll).Id };
		ResolveSwing(M, TEXT("p1"), Swing(), &C);
		TestEqual(TEXT("a scroll restores a life"), M.Players[0].Lives, 3);
		C = { Plant(M, EVariant::Scroll).Id };
		ResolveSwing(M, TEXT("p1"), Swing(), &C);
		TestEqual(TEXT("up to the cap"), M.Players[0].Lives, 3);
		M.Players[0].FocusRemainingMs = FocusDurationMs;
		C = { Plant(M, EVariant::Bomb).Id, Plant(M, EVariant::Scroll).Id };
		ResolveSwing(M, TEXT("p1"), Swing(), &C);
		TestTrue(TEXT("a bomb cancels healing and Focus"), M.Players[0].Lives == 2 && M.Players[0].FocusRemainingMs == 0.0 && M.Players[0].FocusCharge == 0);
	}
	{
		FMatch M = Playing();
		M.Targets.Reset();
		M.ElapsedMs = 16000.0;
		M.Players[0].Launches = 3;
		M.SpawnCooldownMs[0] = 0.0;
		StepMatch(M, 16.0);
		TestEqual(TEXT("a pair"), M.Targets.Num(), 2);
		if (M.Targets.Num() == 2)
		{
			const FTarget& L = M.Targets[0];
			const FTarget& R = M.Targets[1];
			TestTrue(TEXT("two fruit, same speed, apart"), L.Kind == EKind::Fruit && R.Kind == EKind::Fruit && L.Speed == R.Speed
				&& (R.Lateral - L.Lateral) * 1.1 > (L.Radius + R.Radius) * 1.15);
		}
	}
	{
		// A full four-player round keeps fruit inside reach and away from bombs.
		FMatch M = Playing(4);
		bool bOk = true;
		for (int32 Frame = 0; Frame < 3750 && bOk; ++Frame)
		{
			StepMatch(M, 16.0);
			for (const FPlayer& P : M.Players)
			{
				const TArray<FTarget> Lane = M.Targets.FilterByPredicate([&P](const FTarget& T) { return T.PlayerId == P.Id; });
				bOk &= Lane.Num() <= MaxTargetsPerLane;
				for (const FTarget& F : Lane)
				{
					if (F.Kind == EKind::Bomb)
					{
						continue;
					}
					bOk &= FMath::Abs(F.Lateral) * 1.1 + F.Radius * 1.15 < 1.5;
					for (const FTarget& B : Lane)
					{
						if (B.Kind == EKind::Bomb)
						{
							bOk &= FMath::Abs(F.Lateral - B.Lateral) * 1.1 > (F.Radius + B.Radius) * 1.15;
						}
					}
				}
			}
		}
		TestTrue(TEXT("four-player round stays separated"), bOk);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSamuraiBladeTest, "MovecadeKart.Samurai.Blade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSamuraiBladeTest::RunTest(const FString& Parameters)
{
	{
		const FVector A(0, 0, 0), B(0, 1, 0), C(0.025, 0, 0), D(0.025, 1, 0);
		TestTrue(TEXT("an inch of sweep cuts what it touches"), SweptBladeHits(A, B, C, D, FVector(0.02, 0.5, 0), 0.005));
		TestFalse(TEXT("not what it doesn't"), SweptBladeHits(A, B, C, D, FVector(1, 0.5, 0), 0.1));
		TestFalse(TEXT("a still blade never cuts"), SweptBladeHits(A, B, A, B, FVector(0, 0.5, 0), 0.1));
		TestTrue(TEXT("a fast crossing hits between frames"), SweptBladeHits(FVector(-1, 0, 0), FVector(-1, 1, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 0.5, 0), 0.05));
	}
	{
		FBladeIntent I;
		bool bAny = false;
		for (int32 i = 0; i < 400; ++i) bAny |= I.Feed(25, 0, 10);
		for (int32 i = 0; i < 3; ++i) bAny |= I.Feed(300, 0, 10);
		for (int32 i = 0; i < 30; ++i) bAny |= I.Feed(0, 0, 10);
		TestFalse(TEXT("slow aiming and tiny flicks aren't sweeps"), bAny);
	}
	for (const double Hz : { 30.0, 60.0, 100.0 })
	{
		FBladeIntent I;
		bool bCut = false;
		for (int32 i = 0; i < Hz * 0.2; ++i) bCut |= I.Feed(220, 0, 1000.0 / Hz);
		for (int32 i = 0; i < Hz; ++i) I.Feed(0, 0, 1000.0 / Hz);
		TestTrue(FString::Printf(TEXT("a real sweep at %.0f Hz, then settles"), Hz), bCut && !I.Feed(20, 0, 1000.0 / Hz));
	}
	{
		FBladeIntent I;
		bool bCut = false;
		for (int32 i = 0; i < 18; ++i) bCut |= I.Feed(0, 5, 10);
		TestTrue(TEXT("a straight-arm sweep counts"), bCut);
		FBladeIntent J;
		for (int32 i = 0; i < 5; ++i) J.Feed(220, 0, 10);
		TestFalse(TEXT("no bridging a dropout"), J.Feed(220, 0, 300) || J.Feed(220, 0, 10));
	}
	{
		// contactReplay: one vertical blade crossing, sampled at 30, 60 and 120 Hz, scores once and the same.
		TArray<int32> Scores;
		for (const double FrameMs : { 1000.0 / 30.0, 1000.0 / 60.0, 1000.0 / 120.0 })
		{
			FMatch M = CreateMatch({ { TEXT("p1"), TEXT("A") } });
			M.Phase = EMatchPhase::Playing;
			Plant(M, EVariant::Apple, 0.5);
			auto At = [](double T) { const double X = FMath::Lerp(-1.0, 1.0, FMath::Clamp(T / 600.0, 0.0, 1.0)); return TPair<FVector, FVector>(FVector(X, 0, 0), FVector(X, 1, 0)); };
			const int32 Frames = FMath::CeilToInt32(600.0 / FrameMs);
			int32 ContactFrames = 0;
			TOptional<FSwingOutcome> Last;
			for (int32 i = 1; i <= Frames; ++i)
			{
				const auto Prev = At((i - 1) * FrameMs);
				const auto Cur = At(FMath::Min(600.0, i * FrameMs));
				TSet<int32> Hits;
				for (const FTarget& T : M.Targets)
				{
					if (SweptBladeHits(Prev.Key, Prev.Value, Cur.Key, Cur.Value, FVector(0, 0.5, 0), T.Radius))
					{
						Hits.Add(T.Id);
					}
				}
				if (Hits.Num() > 0)
				{
					++ContactFrames;
					Last = ResolveSwing(M, TEXT("p1"), ContactSwing(), &Hits);
				}
			}
			TestEqual(TEXT("one contact frame"), ContactFrames, 1);
			TestTrue(TEXT("a hit"), Last.IsSet() && Last->Kind == EOutcome::Hit && M.Targets.Num() == 0);
			Scores.Add(Last.IsSet() ? Last->ScoreDelta : -1);
		}
		TestTrue(TEXT("the same score at every rate"), Scores.Num() == 3 && Scores[0] > 0 && Scores[1] == Scores[0] && Scores[2] == Scores[0]);
	}
	{
		// The rig: a raised blade stands over the shoulder, about 1.12 m long, and aims where it's told.
		const FBlade Up = BladeFor(0.0, FVector(0, 1, 0));
		TestTrue(TEXT("blade length"), FMath::IsNearlyEqual(FVector::Dist(Up.Hand, Up.Tip), 1.184, 1e-6));
		const FVector Goal(0.3, 1.6, TargetDepth);
		const FBlade Aimed = BladeFor(0.0, BladeDirectionToward(0.0, Goal));
		TestTrue(TEXT("aims at the goal"), FVector::CrossProduct((Goal - Aimed.Hand).GetSafeNormal(), Aimed.Direction).Size() < 1e-3);
		TestTrue(TEXT("phone at rest: blade up"), BladeDirectionFromQuaternion(FQuat::Identity).Equals(FVector(0, 1, 0), 1e-9));
		TestTrue(TEXT("tilt the top forward: blade forward"), BladeDirectionFromEuler(0.0, -90.0, 0.0).Equals(FVector(0, 0, -1), 1e-6));
	}
	return true;
}

#endif
