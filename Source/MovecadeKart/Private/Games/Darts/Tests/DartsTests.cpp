// Golden tests for Bullseye Bash, from movecade-v2 darts/{board,engine,throw}.test.ts (Docs/Port/07-darts.md §10).
#include "Games/Darts/DartsRules.h"
#include "Games/Darts/DartsThrow.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Darts;

namespace
{
	FDartGesture Gesture(double AimX = 0.0, double AimY = 0.0, double Power = SweetSpot, double Spin = 0.0, double Steadiness = 1.0)
	{
		FDartGesture G;
		G.AimX = AimX;
		G.AimY = AimY;
		G.Power = Power;
		G.SpinDegPerSec = Spin;
		G.Steadiness = Steadiness;
		return G;
	}

	/** StepMatch clamps each step to 100 ms, so the clock is run rather than jumped. */
	void Run(FDartsMatch& Match, double Ms, double Step = 100.0)
	{
		for (double T = 0.0; T < Ms; T += Step)
		{
			StepMatch(Match, Step);
		}
	}

	FDartsMatch Playing(const TArray<FString>& Ids = { TEXT("ada"), TEXT("grace") })
	{
		TArray<FMatchSeed> Seeds;
		for (const FString& Id : Ids)
		{
			Seeds.Add({ Id, Id });
		}
		FDartsMatch Match = CreateMatch(Seeds);
		Run(Match, CountdownMs + 100.0);
		return Match;
	}

	/** Aim that lands in the single-20 band. */
	const double AimSingle20 = 0.5 / AimReach;

	FThrowSample Sample(FVector3d Gravity = FVector3d(0.0, 0.0, -9.81))
	{
		FThrowSample S;
		S.Gyro = FVector3d::ZeroVector;
		S.Accel = FVector3d::ZeroVector;
		S.Gravity = Gravity;
		S.DtMs = 16.0;
		return S;
	}

	void Settle(FDartThrowDetector& D, int32 Samples = 40)
	{
		for (int32 i = 0; i < Samples; ++i)
		{
			D.Feed(Sample());
		}
	}

	/** A half-sine of forward user acceleration, then a still tail. */
	TOptional<FThrowGesture> FeedThrow(FDartThrowDetector& D, double Peak = 22.0, int32 Steps = 9, FVector3d Gyro = FVector3d::ZeroVector)
	{
		TOptional<FThrowGesture> Out;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double Env = FMath::Sin(static_cast<double>(i) / Steps * UE_DOUBLE_PI);
			FThrowSample S = Sample();
			S.Accel = FVector3d(0.0, Peak * Env, 0.0);
			S.Gyro = Gyro * Env;
			if (TOptional<FThrowGesture> R = D.Feed(S))
			{
				Out = R;
			}
		}
		for (int32 i = 0; i < 8; ++i)
		{
			if (TOptional<FThrowGesture> R = D.Feed(Sample()))
			{
				Out = R;
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsBoardTest, "MovecadeKart.Darts.Board",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDartsBoardTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("20 at the top"), Sectors()[SectorIndexAt(0.0, 0.5)], 20);
	TestEqual(TEXT("6 at 3 o'clock"), Sectors()[SectorIndexAt(0.5, 0.0)], 6);
	TestEqual(TEXT("3 at 6 o'clock"), Sectors()[SectorIndexAt(0.0, -0.5)], 3);
	TestEqual(TEXT("11 at 9 o'clock"), Sectors()[SectorIndexAt(-0.5, 0.0)], 11);
	for (int32 i = 0; i < SectorCount; ++i)
	{
		const double A = SectorCenterAngle(i);
		TestEqual(FString::Printf(TEXT("sector %d centre"), i), SectorIndexAt(FMath::Cos(A) * 0.5, FMath::Sin(A) * 0.5), i);
	}
	TestEqual(TEXT("bullseye"), ScoreAt(0.0, 0.0).Score, 50);
	TestEqual(TEXT("outer bull"), ScoreAt(0.0, 0.06).Score, 25);
	const FBoardHit Treble = ScoreAt(0.0, (TripleInnerRadius + TripleOuterRadius) / 2.0);
	TestEqual(TEXT("treble 20"), Treble.Label, FString(TEXT("T20")));
	TestEqual(TEXT("treble 20 score"), Treble.Score, 60);
	const FBoardHit Double = ScoreAt(0.0, (DoubleInnerRadius + 1.0) / 2.0);
	TestEqual(TEXT("double 20"), Double.Label, FString(TEXT("D20")));
	TestEqual(TEXT("double 20 score"), Double.Score, 40);
	TestEqual(TEXT("single 20"), ScoreAt(0.0, 0.5).Label, FString(TEXT("20")));
	TestTrue(TEXT("off the board"), ScoreAt(0.0, 1.05).Ring == ERing::Miss);
	TestTrue(TEXT("NaN is a miss"), ScoreAt(std::numeric_limits<double>::quiet_NaN(), 0.0).Ring == ERing::Miss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsLandingTest, "MovecadeKart.Darts.Landing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDartsLandingTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("sweet spot at the aim"), LandingPoint(Gesture()) == FVector2D(0.0, 0.0));
	const FVector2D Aimed = LandingPoint(Gesture(0.4, 0.25));
	TestTrue(TEXT("aimed x"), FMath::IsNearlyEqual(Aimed.X, 0.4 * AimReach, 1e-6));
	TestTrue(TEXT("aimed y"), FMath::IsNearlyEqual(Aimed.Y, 0.25 * AimReach, 1e-6));
	TestTrue(TEXT("soft drops"), LandingPoint(Gesture(0, 0, SweetSpot - 0.3)).Y < -0.1);
	TestTrue(TEXT("hard sails"), LandingPoint(Gesture(0, 0, SweetSpot + 0.3)).Y > 0.1);
	TestTrue(TEXT("spin right"), LandingPoint(Gesture(0, 0, SweetSpot, 300)).X > 0.2);
	TestTrue(TEXT("spin left"), LandingPoint(Gesture(0, 0, SweetSpot, -300)).X < -0.2);
	TestTrue(TEXT("unsteady carries along"), LandingPoint(Gesture(0.5, 0, SweetSpot, 0, 0)).X > LandingPoint(Gesture(0.5, 0, SweetSpot, 0, 1)).X);
	FDartGesture Wild = Gesture(1, 1, 1, 5000, 0);
	Wild.RollDegPerSec = 5000;
	TestTrue(TEXT("never past the surround"), LandingPoint(Wild).Size() <= CatchRadius + 1e-9);
	FDartGesture Broken = Gesture(std::numeric_limits<double>::quiet_NaN(), TNumericLimits<double>::Max() * 2.0, std::numeric_limits<double>::quiet_NaN(), 0, std::numeric_limits<double>::quiet_NaN());
	const FVector2D B = LandingPoint(Broken);
	TestTrue(TEXT("garbage stays finite"), FMath::IsFinite(B.X) && FMath::IsFinite(B.Y));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsMatchTest, "MovecadeKart.Darts.Match",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDartsMatchTest::RunTest(const FString& Parameters)
{
	{
		FDartsMatch M = CreateMatch({ { TEXT("ada"), TEXT("Ada") } });
		TestFalse(TEXT("no throws in the countdown"), ThrowDart(M, TEXT("ada"), Gesture()).IsSet());
		Run(M, CountdownMs + 100.0);
		TestTrue(TEXT("throws once playing"), ThrowDart(M, TEXT("ada"), Gesture()).IsSet());
	}
	{
		FDartsMatch M = Playing({ TEXT("ada") });
		const TOptional<FThrowOutcome> O = ThrowDart(M, TEXT("ada"), Gesture());
		TestEqual(TEXT("bull 50"), O->Landing.Hit.Score, 50);
		TestEqual(TEXT("in the air"), M.Flights.Num(), 1);
		for (int32 i = 0; i < 60; ++i)
		{
			StepMatch(M, 16.0);
		}
		TestEqual(TEXT("landed"), M.Flights.Num(), 0);
		TestEqual(TEXT("score unchanged by the arrival"), M.FindPlayer(TEXT("ada"))->Score, 50);
	}
	{
		// Finishes after exactly nine darts and refuses more.
		FDartsMatch M = Playing({ TEXT("ada") });
		for (int32 i = 0; i < DartsPerPlayer; ++i)
		{
			TestTrue(TEXT("dart accepted"), ThrowDart(M, TEXT("ada"), Gesture()).IsSet());
			if ((i + 1) % DartsPerRound == 0)
			{
				Run(M, FlightMs + 100.0);
			}
		}
		TestTrue(TEXT("finished"), M.FindPlayer(TEXT("ada"))->bFinished);
		TestEqual(TEXT("round count"), M.FindPlayer(TEXT("ada"))->Round, RoundCount);
		TestFalse(TEXT("refuses a tenth"), ThrowDart(M, TEXT("ada"), Gesture()).IsSet());
	}
	{
		// Turns: only the active player, rotating once the third dart has landed.
		FDartsMatch M = Playing();
		TestEqual(TEXT("ada first"), M.ActivePlayer()->Id, FString(TEXT("ada")));
		TestFalse(TEXT("grace waits"), ThrowDart(M, TEXT("grace"), Gesture()).IsSet());
		for (int32 i = 0; i < DartsPerRound; ++i)
		{
			ThrowDart(M, TEXT("ada"), Gesture());
		}
		TestFalse(TEXT("no fourth dart"), ThrowDart(M, TEXT("ada"), Gesture()).IsSet());
		TestEqual(TEXT("still ada while settling"), M.ActivePlayer()->Id, FString(TEXT("ada")));
		Run(M, FlightMs + 100.0);
		TestEqual(TEXT("grace up"), M.ActivePlayer()->Id, FString(TEXT("grace")));
		TestEqual(TEXT("turn darts reset"), M.TurnDarts, 0);
	}
	{
		// Ends when everyone has finished; winners and leaderboard.
		FDartsMatch M = Playing();
		for (int32 Round = 0; Round < RoundCount; ++Round)
		{
			for (int32 i = 0; i < DartsPerRound; ++i) ThrowDart(M, TEXT("ada"), Gesture());
			Run(M, FlightMs + 100.0);
			for (int32 i = 0; i < DartsPerRound; ++i) ThrowDart(M, TEXT("grace"), Gesture(0, 1, 1));
			Run(M, FlightMs + 100.0);
		}
		TestTrue(TEXT("ended"), M.Phase == EMatchPhase::Ended);
		TestTrue(TEXT("ada wins"), M.WinnerIds == TArray<FString>{ TEXT("ada") });
		TestEqual(TEXT("leaderboard"), Leaderboard(M)[0]->Id, FString(TEXT("ada")));
	}
	{
		FDartsMatch M = Playing();
		Run(M, MatchDurationMs * 2 + 100.0);
		TestTrue(TEXT("backstop clock"), M.Phase == EMatchPhase::Ended);
		const double Elapsed = M.ElapsedMs;
		StepMatch(M, 100.0);
		TestEqual(TEXT("inert once ended"), M.ElapsedMs, Elapsed);
	}
	TestEqual(TEXT("allowance per player"), MatchDurationForPlayers(4), MatchDurationMs * 4);
	{
		FDartsMatch M = Playing();
		const double Before = M.TimeRemainingMs;
		StepMatch(M, 60000.0);
		TestTrue(TEXT("dt clamped"), Before - M.TimeRemainingMs <= 100.0);
	}
	{
		// The final dart stays in flight until it lands; the bull callout is there.
		FDartsMatch M = Playing({ TEXT("ada") });
		for (int32 Dart = 0; Dart < DartsPerPlayer; ++Dart)
		{
			ThrowDart(M, TEXT("ada"), Gesture());
			StepMatch(M, Dart == DartsPerPlayer - 1 ? 100.0 : 700.0);
			if ((Dart + 1) % DartsPerRound == 0 && Dart < DartsPerPlayer - 1)
			{
				Run(M, FlightMs + 100.0);
			}
		}
		TestTrue(TEXT("still playing in the last flight"), M.Phase == EMatchPhase::Playing);
		Run(M, FlightMs + 100.0);
		TestTrue(TEXT("ended after it lands"), M.Phase == EMatchPhase::Ended);
		TestTrue(TEXT("bullseye callout"), M.Effects.ContainsByPredicate([](const FDartsEffect& E) { return E.Kind == EEffectKind::Bull && E.Label == TEXT("Bullseye"); }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsScoringTest, "MovecadeKart.Darts.Scoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDartsScoringTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("bounty r0"), BountyForRound(0), 19);
	TestEqual(TEXT("bounty r1"), BountyForRound(1), 16);
	TestEqual(TEXT("bounty r2"), BountyForRound(2), 20);
	TestEqual(TEXT("bounty r3 wraps"), BountyForRound(3), 19);
	TestEqual(TEXT("bounty negative"), BountyForRound(-1), 19);
	{
		// Round 3 calls 20: single 20 doubles there, not in round 1.
		FDartsMatch M = Playing({ TEXT("ada") });
		const TOptional<FThrowOutcome> First = ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
		TestFalse(TEXT("20 isn't round 1's bounty"), First->Landing.bBounty);
		TestEqual(TEXT("plain 20"), First->Landing.ScoreDelta, 20);
		ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
		ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
		Run(M, FlightMs + 100.0);
		for (int32 i = 0; i < DartsPerRound; ++i) ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
		Run(M, FlightMs + 100.0);
		const int32 Before = M.FindPlayer(TEXT("ada"))->Score;
		const TOptional<FThrowOutcome> Bounty = ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
		TestTrue(TEXT("bounty"), Bounty->Landing.bBounty);
		TestEqual(TEXT("doubled"), M.FindPlayer(TEXT("ada"))->Score - Before, 40);
		TestEqual(TEXT("bounty label"), Bounty->Label, FString(TEXT("Bounty 20!")));
	}
	{
		FDartsMatch M = Playing();
		const TOptional<FThrowOutcome> Miss = ThrowDart(M, TEXT("ada"), Gesture(0, 1, 1));
		TestEqual(TEXT("miss scores 0"), Miss->Landing.Hit.Score, 0);
		TestFalse(TEXT("never a bounty"), Miss->Landing.bBounty);
	}
	{
		// Three single 20s: 60 + the tidy bonus.
		FDartsMatch M = Playing();
		int32 Bonus = 0;
		for (int32 i = 0; i < DartsPerRound; ++i)
		{
			const TOptional<FThrowOutcome> O = ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
			Bonus = O->RoundBonus;
			TestTrue(TEXT("round completes on the third"), O->bRoundComplete == (i == DartsPerRound - 1));
		}
		TestEqual(TEXT("tidy bonus"), Bonus, TidyRoundBonus);
		TestEqual(TEXT("80"), M.FindPlayer(TEXT("ada"))->Score, 80);
		TestEqual(TEXT("best round"), M.FindPlayer(TEXT("ada"))->BestRound, 80);
		TestEqual(TEXT("tallies reset"), M.FindPlayer(TEXT("ada"))->RoundScore, 0);
	}
	{
		// Three bulls: 150 + tidy + ton = 210.
		FDartsMatch M = Playing();
		for (int32 i = 0; i < DartsPerRound; ++i) ThrowDart(M, TEXT("ada"), Gesture());
		TestEqual(TEXT("3 bulls"), M.FindPlayer(TEXT("ada"))->Score, 210);
	}
	{
		// A miss, then two 20s: 40, no bonus.
		FDartsMatch M = Playing();
		ThrowDart(M, TEXT("ada"), Gesture(0, 1, 1));
		ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
		const TOptional<FThrowOutcome> Last = ThrowDart(M, TEXT("ada"), Gesture(0, AimSingle20));
		TestEqual(TEXT("no tidy bonus"), Last->RoundBonus, 0);
		TestEqual(TEXT("40"), M.FindPlayer(TEXT("ada"))->Score, 40);
	}
	{
		// The perfect match: 3x T19, T16, T20 on their bounty rounds = 1170.
		FDartsMatch M = Playing({ TEXT("ada") });
		const double R = (TripleInnerRadius + TripleOuterRadius) / 2.0;
		for (const int32 Sector : { 19, 16, 20 })
		{
			int32 Index = 0;
			while (Sectors()[Index] != Sector) ++Index;
			const double A = SectorCenterAngle(Index);
			for (int32 i = 0; i < DartsPerRound; ++i)
			{
				TestTrue(TEXT("perfect dart"), ThrowDart(M, TEXT("ada"), Gesture(FMath::Cos(A) * R / AimReach, FMath::Sin(A) * R / AimReach)).IsSet());
			}
			Run(M, FlightMs + 100.0);
		}
		TestEqual(TEXT("perfect match 1170"), M.FindPlayer(TEXT("ada"))->Score, 1170);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDartsThrowTest, "MovecadeKart.Darts.Throw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDartsThrowTest::RunTest(const FString& Parameters)
{
	{
		FDartThrowDetector D;
		TestFalse(TEXT("not centred at first"), D.Aim().bCentred);
		Settle(D);
		TestTrue(TEXT("centred after 40 still frames"), D.Aim().bCentred);
		TestTrue(TEXT("dead centre"), FMath::Abs(D.Aim().X) < 0.01 && FMath::Abs(D.Aim().Y) < 0.01);
	}
	{
		FDartThrowDetector Up, Down;
		Settle(Up);
		Settle(Down);
		for (int32 i = 0; i < 60; ++i)
		{
			Up.Feed(Sample(FVector3d(0.0, -3.4, -9.2)));
			Down.Feed(Sample(FVector3d(0.0, 3.4, -9.2)));
		}
		TestTrue(TEXT("nose up raises the reticle"), Up.Aim().Y > 0.3);
		TestTrue(TEXT("nose down drops it"), Down.Aim().Y < -0.3);
	}
	{
		// Yaw integrates and holds (D1: no leak).
		FDartThrowDetector D;
		Settle(D);
		for (int32 i = 0; i < 25; ++i)
		{
			FThrowSample S = Sample();
			S.Gyro = FVector3d(0.0, 0.0, -60.0);
			D.Feed(S);
		}
		const double Swung = D.Aim().X;
		TestTrue(TEXT("yaw swings the reticle"), FMath::Abs(Swung) > 0.05);
		for (int32 i = 0; i < 300; ++i) D.Feed(Sample());
		TestTrue(TEXT("yaw held"), FMath::Abs(D.Aim().X) >= FMath::Abs(Swung) * 0.95);
	}
	{
		FDartThrowDetector D;
		Settle(D);
		D.Recentre();
		TestFalse(TEXT("recentre drops the pose"), D.Aim().bCentred);
		Settle(D);
		TestTrue(TEXT("and recaptures it"), D.Aim().bCentred);
	}
	{
		// The captured round-one sector-19 called shot.
		FDartThrowDetector D;
		TOptional<FThrowGesture> G;
		auto Feed = [&](double AccelY, double Rotate, double GravityY)
		{
			FThrowSample S = Sample(FVector3d(0.0, GravityY, -FMath::Sqrt(9.81 * 9.81 - GravityY * GravityY)));
			S.Gyro = FVector3d(0.0, 0.0, Rotate);
			S.Accel = FVector3d(0.0, AccelY, 0.0);
			if (TOptional<FThrowGesture> R = D.Feed(S)) G = R;
		};
		for (int32 i = 0; i < 24; ++i) Feed(0.0, 0.0, 0.0);
		for (int32 i = 0; i < 50; ++i) Feed(0.0, 5.8, 2.0);
		for (int32 i = 0; i <= 9; ++i) Feed(17.0 * FMath::Sin(i / 9.0 * UE_DOUBLE_PI), 5.8, 2.0);
		for (int32 i = 0; i < 10; ++i) Feed(0.0, 5.8, 2.0);
		TestTrue(TEXT("captured throw released"), G.IsSet());
		if (G.IsSet())
		{
			TestTrue(TEXT("aim x"), G->AimX > -0.25 && G->AimX < -0.05);
			TestTrue(TEXT("aim y"), G->AimY > -0.6 && G->AimY < -0.3);
			TestTrue(TEXT("peak"), G->PeakMs2 >= 11.0);
			const FVector2D P = LandingPoint(*G);
			TestEqual(TEXT("sector 19"), ScoreAt(P.X, P.Y).Sector, 19);
		}
	}
	{
		FDartThrowDetector D;
		Settle(D);
		int32 Count = 0;
		for (int32 i = 0; i <= 9; ++i)
		{
			FThrowSample S = Sample();
			S.Accel = FVector3d(0.0, 22.0 * FMath::Sin(i / 9.0 * UE_DOUBLE_PI), 0.0);
			Count += D.Feed(S).IsSet() ? 1 : 0;
		}
		for (int32 i = 0; i < 8; ++i) Count += D.Feed(Sample()).IsSet() ? 1 : 0;
		TestEqual(TEXT("one gesture per throw"), Count, 1);
		TestFalse(TEXT("no second on the return"), FeedThrow(D, 18.0, 7).IsSet());
		for (int32 i = 0; i < 48; ++i) D.Feed(Sample());
		TestTrue(TEXT("again after the cooldown"), FeedThrow(D).IsSet());
	}
	{
		FDartThrowDetector D;
		Settle(D);
		TestFalse(TEXT("a jostle isn't a throw"), FeedThrow(D, 8.0).IsSet());
	}
	{
		FDartThrowDetector D;
		Settle(D);
		TOptional<FThrowGesture> G;
		for (int32 i = 0; i <= 9; ++i)
		{
			FThrowSample S = Sample();
			S.Accel = FVector3d(0.0, 0.0, -26.0 * FMath::Sin(i / 9.0 * UE_DOUBLE_PI));
			if (TOptional<FThrowGesture> R = D.Feed(S)) G = R;
		}
		for (int32 i = 0; i < 8; ++i) if (TOptional<FThrowGesture> R = D.Feed(Sample())) G = R;
		TestFalse(TEXT("setting the phone down isn't a throw"), G.IsSet());
	}
	{
		FDartThrowDetector Soft, Hard;
		Settle(Soft);
		Settle(Hard);
		const TOptional<FThrowGesture> S = FeedThrow(Soft, 14.0);
		const TOptional<FThrowGesture> H = FeedThrow(Hard, 30.0);
		TestTrue(TEXT("power scales"), S.IsSet() && H.IsSet() && H->Power > S->Power);
	}
	{
		FDartThrowDetector Right, Left;
		Settle(Right);
		Settle(Left);
		const TOptional<FThrowGesture> R = FeedThrow(Right, 22.0, 9, FVector3d(0.0, 0.0, 300.0));
		const TOptional<FThrowGesture> L = FeedThrow(Left, 22.0, 9, FVector3d(0.0, 0.0, -300.0));
		TestTrue(TEXT("signed spin right"), R.IsSet() && R->SpinDegPerSec > 100.0);
		TestTrue(TEXT("signed spin left"), L.IsSet() && L->SpinDegPerSec < -100.0);
	}
	{
		// The aim from before the arm moved, not from the peak.
		FDartThrowDetector D;
		Settle(D);
		for (int32 i = 0; i < 90; ++i) D.Feed(Sample(FVector3d(0.0, -3.4, -9.2)));
		TestTrue(TEXT("aimed high"), D.Aim().Y > 0.3);
		TOptional<FThrowGesture> G;
		for (int32 i = 0; i <= 9; ++i)
		{
			FThrowSample S = Sample(FVector3d(0.0, 2.0, -9.6));
			S.Accel = FVector3d(0.0, 24.0 * FMath::Sin(i / 9.0 * UE_DOUBLE_PI), 0.0);
			if (TOptional<FThrowGesture> R = D.Feed(S)) G = R;
		}
		for (int32 i = 0; i < 8; ++i) if (TOptional<FThrowGesture> R = D.Feed(Sample(FVector3d(0.0, 2.0, -9.6)))) G = R;
		TestTrue(TEXT("lookback keeps the chosen aim"), G.IsSet() && G->AimY > 0.25);
	}
	{
		FDartThrowDetector D;
		FThrowSample S = Sample();
		S.Accel.Reset();
		S.Gyro.Reset();
		D.Feed(S);
		TestFalse(TEXT("no motion channels"), D.IsAvailable());
	}
	{
		FDartThrowDetector D;
		Settle(D);
		D.Grip();
		TestFalse(TEXT("grip never auto-commits"), FeedThrow(D).IsSet());
		TestTrue(TEXT("still gripped"), D.IsGripped());
	}
	{
		FDartThrowDetector D;
		Settle(D);
		D.Grip();
		for (int32 i = 0; i <= 4; ++i)
		{
			FThrowSample S = Sample();
			S.Accel = FVector3d(0.0, 22.0 * FMath::Sin(i / 9.0 * UE_DOUBLE_PI), 0.0);
			D.Feed(S);
		}
		const TOptional<FThrowGesture> G = D.Release();
		TestTrue(TEXT("mid-flick lift"), G.IsSet() && G->Power > 0.3);
	}
	{
		FDartThrowDetector D;
		Settle(D);
		D.Grip();
		const TOptional<FThrowGesture> G = D.Release();
		TestTrue(TEXT("a bare lift is the weakest legal dart"), G.IsSet() && G->Power >= 0.05 && G->Power < 0.1);
		TestFalse(TEXT("release without a grip is inert"), D.Release().IsSet());
	}
	{
		FDartThrowDetector D;
		Settle(D);
		D.Grip();
		for (int32 i = 0; i < 60; ++i)
		{
			FThrowSample S = Sample();
			S.Accel = FVector3d(0.0, 14.0, 0.0);
			D.Feed(S);
		}
		const TOptional<FThrowGesture> G = D.Release();
		TestTrue(TEXT("a long wind-up is accepted"), G.IsSet() && G->Power > 0.3);
	}
	return true;
}

#endif
