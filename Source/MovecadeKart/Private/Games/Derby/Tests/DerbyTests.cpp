// Golden tests for Home Run Derby (Docs/Port/10-derby.md). The expected numbers come from an independent Python
// model of the same rules (Mulberry32, the pitch schedule, the contact model and the 10 ms flight).
#include "Games/Derby/DerbyRules.h"
#include "Games/Derby/DerbySwing.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Derby;

namespace
{
	constexpr uint32 GoldenSeed = 0xDE4B;
	constexpr EAutomationTestFlags DerbyFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FSwing Swing(double TimingMs, double Aim, double Strength)
	{
		FSwing S;
		S.bSwung = true;
		S.TimingMs = TimingMs;
		S.Aim = Aim;
		S.Strength = Strength;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDerbyScheduleTest, "MovecadeKart.Derby.Schedule", DerbyFlags)

bool FDerbyScheduleTest::RunTest(const FString& Parameters)
{
	FDerbyRng R(1);
	TestEqual(TEXT("mulberry32 #1"), R.Next(), 2693262067u);
	TestEqual(TEXT("mulberry32 #2"), R.Next(), 11749833u);
	TestEqual(TEXT("mulberry32 #3"), R.Next(), 2265367787u);
	TestEqual(TEXT("round seed 0"), RoundSeed(GoldenSeed, 0), 2654447602u);
	TestEqual(TEXT("round seed 1"), RoundSeed(GoldenSeed, 1), 1013853497u);

	const TArray<FPitch> S = PitchSchedule(RoundSeed(GoldenSeed, 0));
	TestEqual(TEXT("ten pitches"), S.Num(), PitchesPerTurn);
	if (S.Num() != PitchesPerTurn)
	{
		return false;
	}
	const EPitchType Types[] = { EPitchType::Fastball, EPitchType::Fastball, EPitchType::Fastball, EPitchType::Changeup, EPitchType::Fastball,
		EPitchType::Changeup, EPitchType::Changeup, EPitchType::Curve, EPitchType::Changeup, EPitchType::Curve };
	for (int32 i = 0; i < PitchesPerTurn; ++i)
	{
		TestTrue(FString::Printf(TEXT("type of pitch %d"), i), S[i].Type == Types[i]);
	}
	TestTrue(TEXT("p0 speed"), FMath::IsNearlyEqual(S[0].SpeedMph, 94.90512706665322, 1e-9));
	TestTrue(TEXT("p0 flight"), FMath::IsNearlyEqual(S[0].FlightMs, 424.73566211560234, 1e-7));
	TestTrue(TEXT("p0 windup"), FMath::IsNearlyEqual(S[0].WindupMs, 1742.0622848905623, 1e-7));
	TestTrue(TEXT("p0 bias"), FMath::IsNearlyEqual(S[0].ContactBias, -0.09650160884484649, 1e-9));
	TestTrue(TEXT("p7 curve"), FMath::IsNearlyEqual(S[7].SpeedMph, 75.77867667982355, 1e-9) && FMath::IsNearlyEqual(S[7].Break, 0.6179229726083576, 1e-9)
		&& S[7].PerfectMs == 24.0 && S[7].ContactMs == 140.0);
	TestTrue(TEXT("p9 curve flight"), FMath::IsNearlyEqual(S[9].FlightMs, 525.509112043708, 1e-7));
	TestTrue(TEXT("p3 changeup windows"), S[3].PerfectMs == 28.0 && S[3].ContactMs == 155.0);
	// The same seed is the same turn; another round is another.
	const TArray<FPitch> Again = PitchSchedule(RoundSeed(GoldenSeed, 0));
	const TArray<FPitch> Next = PitchSchedule(RoundSeed(GoldenSeed, 1));
	TestTrue(TEXT("deterministic"), Again[6].SpeedMph == S[6].SpeedMph && Again[9].WindupMs == S[9].WindupMs);
	TestTrue(TEXT("rounds differ"), Next[0].SpeedMph != S[0].SpeedMph);
	// Harder as the turn goes: no curve before the seventh pitch, fastballs only for three, over many seeds.
	bool bEarlyCurve = false, bEarlyOffspeed = false, bAnyCurve = false;
	for (uint32 Seed = 1; Seed < 200; ++Seed)
	{
		const TArray<FPitch> T = PitchSchedule(Seed);
		for (const FPitch& P : T)
		{
			bEarlyCurve |= P.Index < 6 && P.Type == EPitchType::Curve;
			bEarlyOffspeed |= P.Index < 3 && P.Type != EPitchType::Fastball;
			bAnyCurve |= P.Type == EPitchType::Curve;
		}
	}
	TestFalse(TEXT("no early curve"), bEarlyCurve);
	TestFalse(TEXT("fastballs first"), bEarlyOffspeed);
	TestTrue(TEXT("curves come"), bAnyCurve);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDerbyContactTest, "MovecadeKart.Derby.Contact", DerbyFlags)

bool FDerbyContactTest::RunTest(const FString& Parameters)
{
	const TArray<FPitch> S = PitchSchedule(RoundSeed(GoldenSeed, 0));
	const FPitch& P = S[0];
	{
		// A perfect, full swing up the middle: 117.08 mph at 31 degrees, a 501 ft moonshot for 752.
		const FOutcome O = ResolvePitch(P, Swing(0.0, 0.0, 1.0));
		TestTrue(TEXT("moonshot"), O.Result == EResult::Moonshot);
		TestEqual(TEXT("501 ft"), O.DistanceFt, 501);
		TestEqual(TEXT("752 points"), O.Points, 752);
		TestTrue(TEXT("exit"), FMath::IsNearlyEqual(O.Contact.ExitMph, 117.08292287199758, 1e-9));
		TestTrue(TEXT("launch"), FMath::IsNearlyEqual(O.Contact.LaunchDeg, 31.0, 1e-9));
		TestTrue(TEXT("distance"), FMath::IsNearlyEqual(O.Flight.DistanceM, 152.5663824150253, 1e-6));
		TestTrue(TEXT("hang"), FMath::IsNearlyEqual(O.Flight.HangS, 5.517446969935543, 1e-6));
		TestTrue(TEXT("apex"), FMath::IsNearlyEqual(O.Flight.ApexM, 33.52210944707277, 1e-6));
		TestTrue(TEXT("over the fence high"), FMath::IsNearlyEqual(O.Flight.HeightAtFenceM, 24.839150601501604, 1e-6) && O.Flight.bCleared);
		TestFalse(TEXT("no strike"), O.bStrike);
		TestTrue(TEXT("path lands"), O.Flight.Path.Num() > 100 && FMath::IsNearlyEqual(O.Flight.Path.Last().Y, 0.0, 1e-9));
		TestTrue(TEXT("At() midway is airborne"), O.Flight.At(O.Flight.HangS * 0.5).Y > 20.0);
	}
	{
		// Half strength, perfect: 396 ft to dead centre, short of the 400 ft fence: in play for 40.
		const FOutcome O = ResolvePitch(P, Swing(0.0, 0.0, 0.5));
		TestTrue(TEXT("in play"), O.Result == EResult::InPlay && O.DistanceFt == 396 && O.Points == 40);
		TestTrue(TEXT("never reached the fence"), O.Flight.HeightAtFenceM < 0.0 && !O.Flight.bOffWall);
	}
	{
		// 60 ms early pulls it: spray -12.22, 415 ft home run.
		const FOutcome O = ResolvePitch(P, Swing(-60.0, 0.0, 0.8));
		TestTrue(TEXT("pulled home run"), O.Result == EResult::HomeRun && O.DistanceFt == 415 && O.Points == 415);
		TestTrue(TEXT("quality"), FMath::IsNearlyEqual(O.Contact.Quality, 0.8474246560068802, 1e-9));
		TestTrue(TEXT("spray"), FMath::IsNearlyEqual(O.Contact.SprayDeg, -12.218344526229107, 1e-9));
		TestTrue(TEXT("pull side"), O.Contact.SprayDeg < 0.0);
	}
	{
		// 90 ms late and aimed away: 350 ft to right field, in play for 35.
		const FOutcome O = ResolvePitch(P, Swing(90.0, 0.4, 0.8));
		TestTrue(TEXT("opposite field"), O.Result == EResult::InPlay && O.DistanceFt == 350 && O.Points == 35);
		TestTrue(TEXT("spray right"), FMath::IsNearlyEqual(O.Contact.SprayDeg, 31.946726524504303, 1e-9));
	}
	{
		// 150 ms late: sliced foul; a strike with none or one, not with two.
		const FOutcome A = ResolvePitch(P, Swing(150.0, 0.0, 0.8), 0);
		const FOutcome B = ResolvePitch(P, Swing(150.0, 0.0, 0.8), 2);
		TestTrue(TEXT("foul"), A.Result == EResult::Foul && A.Points == 0 && FMath::IsNearlyEqual(A.Contact.SprayDeg, 47.71429756660416, 1e-9));
		TestTrue(TEXT("foul strike"), A.bStrike && !B.bStrike);
	}
	{
		// Strength .53, perfect: 400 ft but only 0.62 m up at the 400 ft wall: off the wall, 40.
		const FOutcome O = ResolvePitch(P, Swing(0.0, 0.0, 0.53));
		TestTrue(TEXT("off the wall"), O.Result == EResult::InPlay && O.Flight.bOffWall && O.DistanceFt == 400 && O.Points == 40);
		TestTrue(TEXT("wall height"), FMath::IsNearlyEqual(O.Flight.HeightAtFenceM, 0.6215202234130043, 1e-6));
		TestTrue(TEXT("stops at the wall"), FMath::IsNearlyEqual(O.Flight.HangS, 4.616186456551414, 1e-6));
	}
	{
		const FOutcome Take = ResolvePitch(P, FSwing());
		const FOutcome Whiff = ResolvePitch(P, Swing(-170.0, 0.0, 1.0));
		TestTrue(TEXT("take is a strike"), Take.Result == EResult::Take && Take.bStrike && Take.Points == 0);
		TestTrue(TEXT("whiff is a strike"), Whiff.Result == EResult::Whiff && Whiff.bStrike);
		TestTrue(TEXT("edge still makes contact"), ResolvePitch(P, Swing(165.0, 0.0, 1.0)).Result != EResult::Whiff);
	}
	{
		// Curves have the tightest window.
		const TArray<FPitch> T = PitchSchedule(RoundSeed(GoldenSeed, 0));
		TestTrue(TEXT("curve whiffs at 150"), ResolvePitch(T[7], Swing(150.0, 0.0, 1.0)).Result == EResult::Whiff);
		TestEqual(TEXT("perfect quality"), TimingQuality(-24.0, T[7]), 1.0);
		TestTrue(TEXT("fence"), FMath::IsNearlyEqual(FenceDistanceM(0.0), 121.92, 1e-9) && FMath::IsNearlyEqual(FenceDistanceM(45.0), 100.584, 1e-9));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDerbyMatchTest, "MovecadeKart.Derby.Match", DerbyFlags)

bool FDerbyMatchTest::RunTest(const FString& Parameters)
{
	FMatch M = CreateMatch({ TEXT("a"), TEXT("b") }, GoldenSeed, 2);
	TestEqual(TEXT("a up"), M.Batter()->Id, FString(TEXT("a")));
	TestEqual(TEXT("ten left"), M.PitchesLeft(), 10);
	const FOutcome Moon = ResolvePitch(*M.NextPitch(), Swing(0.0, 0.0, 1.0));
	TestEqual(TEXT("moonshot first pitch"), Moon.Points, 752);
	FRecord R = Record(M, Moon);
	TestFalse(TEXT("turn goes on"), R.bTurnEnded);
	// Fouls with two strikes don't end it; three takes do, before the tenth pitch.
	FOutcome Take;
	Take.Result = EResult::Take;
	Take.bStrike = true;
	Record(M, Take);
	Record(M, Take);
	const FOutcome Foul = ResolvePitch(*M.NextPitch(), Swing(150.0, 0.0, 0.8), M.Strikes);
	TestTrue(TEXT("two-strike foul"), Foul.Result == EResult::Foul && !Foul.bStrike);
	R = Record(M, Foul);
	TestFalse(TEXT("still up"), R.bTurnEnded);
	TestEqual(TEXT("two strikes"), M.Strikes, 2);
	R = Record(M, Take);
	TestTrue(TEXT("struck out"), R.bTurnEnded && R.bStruckOut && !R.bRoundEnded);
	TestEqual(TEXT("a has 752"), M.Batters[0].Score, 752);
	TestTrue(TEXT("a stats"), M.Batters[0].HomeRuns == 1 && M.Batters[0].Moonshots == 1 && M.Batters[0].LongestFt == 501);
	TestEqual(TEXT("b up"), M.Batter()->Id, FString(TEXT("b")));
	TestTrue(TEXT("fresh count"), M.Pitch == 0 && M.Strikes == 0);
	// b hits ten balls in play: the turn ends on the tenth, the round on b, and round two has a new schedule.
	const double FirstSpeed = M.Schedule[0].SpeedMph;
	for (int32 i = 0; i < 10; ++i)
	{
		FOutcome Hit;
		Hit.Result = EResult::InPlay;
		Hit.DistanceFt = 300;
		Hit.Points = PointsFor(EResult::InPlay, 300);
		R = Record(M, Hit);
		TestEqual(FString::Printf(TEXT("pitch %d turn"), i), R.bTurnEnded, i == 9);
	}
	TestTrue(TEXT("round ended"), R.bRoundEnded && !R.bMatchComplete && !R.bStruckOut);
	TestEqual(TEXT("b has 300"), M.Batters[1].Score, 300);
	TestEqual(TEXT("round two"), M.Round, 1);
	TestTrue(TEXT("new schedule"), M.Schedule[0].SpeedMph != FirstSpeed);
	TestEqual(TEXT("a up again"), M.Batter()->Id, FString(TEXT("a")));
	for (int32 Turn = 0; Turn < 2; ++Turn)
	{
		for (int32 i = 0; i < 3; ++i)
		{
			R = Record(M, Take);
		}
	}
	TestTrue(TEXT("complete"), R.bMatchComplete && M.bComplete && M.Batter() == nullptr && M.NextPitch() == nullptr);
	TestTrue(TEXT("round scores"), M.Batters[0].RoundScores[0] == 752 && M.Batters[0].RoundScores[1] == 0 && M.Batters[1].RoundScores[0] == 300);
	const TArray<int32> Rank = Ranking(M);
	TestTrue(TEXT("a wins"), Rank.Num() == 2 && Rank[0] == 0);
	// Record after the end changes nothing.
	Record(M, Moon);
	TestEqual(TEXT("frozen"), M.Batters[0].Score, 752);
	TestEqual(TEXT("points"), PointsFor(EResult::Moonshot, 455), 683);
	return true;
}

namespace
{
	/** A level, right-handed swing: the barrel sweeps from behind (heading 180) round to Final over Ms, peaking at Peak dps. */
	TArray<MovecadeKart::Net::FSensorFrame> SwingFrames(double FinalHeading, double PeakDps, double Hz, double Handed = 1.0, bool bQuaternion = true)
	{
		using MovecadeKart::Net::FSensorFrame;
		TArray<FSensorFrame> Out;
		const double Dt = 1000.0 / Hz;
		// Idle a moment, then a sine-bell of angular speed whose integral takes the barrel to FinalHeading.
		const double Sweep = Handed > 0.0 ? 180.0 - FinalHeading : FinalHeading + 180.0;
		const double DurationMs = Sweep / PeakDps * 1000.0 * UE_DOUBLE_PI / 2.0;
		double Heading = Handed > 0.0 ? 180.0 : -180.0;
		for (double T = -200.0; T < DurationMs + 200.0; T += Dt)
		{
			const double Rate = T >= 0.0 && T <= DurationMs ? PeakDps * FMath::Sin(UE_DOUBLE_PI * T / DurationMs) : 0.0;
			Heading -= Handed * Rate * Dt / 1000.0;
			// Barrel level: device +Y tipped to the screen (-Z), then turned about up so the heading is -angle.
			const FQuat Q = FQuat(FVector(0.0, 1.0, 0.0),FMath::DegreesToRadians(-Heading)) * FQuat(FVector(1.0, 0.0, 0.0), -UE_DOUBLE_HALF_PI);
			FSensorFrame F;
			F.DtMs = static_cast<uint16>(FMath::RoundToInt32(Dt));
			F.bHasQuaternion = bQuaternion;
			F.Quaternion = FVector4d(Q.X, Q.Y, Q.Z, Q.W);
			F.Gyro = Q.Inverse().RotateVector(FVector(0.0, Handed * Rate, 0.0));
			F.Gravity = Q.Inverse().RotateVector(FVector(0.0, -9.81, 0.0));
			Out.Add(F);
		}
		return Out;
	}

	TOptional<FSwingEvent> Run(FSwingDetector& D, const TArray<MovecadeKart::Net::FSensorFrame>& Frames)
	{
		TOptional<FSwingEvent> Got;
		for (const MovecadeKart::Net::FSensorFrame& F : Frames)
		{
			if (TOptional<FSwingEvent> E = D.Feed(F))
			{
				Got = E;
			}
		}
		return Got;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDerbySwingTest, "MovecadeKart.Derby.Swing", DerbyFlags)

bool FDerbySwingTest::RunTest(const FString& Parameters)
{
	{
		// The barrel model: square contact for a right-hander is heading +90; past it (toward the pitcher) pulls left.
		TestTrue(TEXT("square"), FMath::IsNearlyEqual(SwingDirection(90.0, 1.0, 55.0), 0.0, 1e-9));
		TestTrue(TEXT("early pulls left"), SwingDirection(35.0, 1.0, 55.0) < -0.99);
		TestTrue(TEXT("late goes right"), FMath::IsNearlyEqual(SwingDirection(117.5, 1.0, 55.0), 0.5, 1e-9));
		TestTrue(TEXT("lefty mirrored"), FMath::IsNearlyEqual(SwingDirection(-62.5, -1.0, 55.0), 0.5, 1e-9));
	}
	{
		// A hard right-handed swing at 60 Hz: one event, at the peak, strong, a right-hander's.
		const TArray<MovecadeKart::Net::FSensorFrame> Frames = SwingFrames(-90.0, 1200.0, 60.0);
		FSwingDetector D;
		double PeakClock = 0.0, Best = 0.0, Clock = 0.0;
		for (const MovecadeKart::Net::FSensorFrame& F : Frames)
		{
			Clock += F.DtMs;
			if (FVector3d(F.Gyro).Size() > Best)
			{
				Best = FVector3d(F.Gyro).Size();
				PeakClock = Clock;
			}
		}
		int32 Events = 0;
		TOptional<FSwingEvent> Got;
		for (const MovecadeKart::Net::FSensorFrame& F : Frames)
		{
			if (TOptional<FSwingEvent> E = D.Feed(F))
			{
				++Events;
				Got = E;
			}
		}
		TestEqual(TEXT("one swing"), Events, 1);
		if (Got.IsSet())
		{
			TestTrue(TEXT("peak time"), FMath::IsNearlyEqual(Got->PeakAtMs, PeakClock, 1e-6));
			TestTrue(TEXT("strong"), Got->Strength > 0.8 && Got->Strength <= 1.0);
			TestTrue(TEXT("right-handed"), Got->Handed > 0.0 && Got->bHasAttitude);
			// The peak of a sine-bell from 180 to -90 is half way: heading 45, 45 degrees past square: a pull (the
			// bell is flat on top, so at 60 Hz the peak frame may be one either side: 20 degrees a frame).
			TestTrue(FString::Printf(TEXT("heading at peak (%.2f)"), Got->HeadingDeg), FMath::IsNearlyEqual(Got->HeadingDeg, 45.0, 22.0));
			TestTrue(TEXT("pulled"), Got->Direction < -0.5);
		}
	}
	{
		// A swing that peaks right at square goes up the middle; a left-hander's mirror too.
		FSwingDetector D;
		const TOptional<FSwingEvent> Mid = Run(D, SwingFrames(0.0, 1000.0, 100.0));
		TestTrue(TEXT("square swing"), Mid.IsSet() && FMath::Abs(Mid->Direction) < 0.25);
		FSwingDetector L;
		const TOptional<FSwingEvent> Lefty = Run(L, SwingFrames(0.0, 1000.0, 100.0, -1.0));
		TestTrue(TEXT("lefty square"), Lefty.IsSet() && Lefty->Handed < 0.0 && FMath::Abs(Lefty->Direction) < 0.25);
	}
	{
		// A gentle wiggle is no swing; nor anything inside the refractory window.
		FSwingDetector D;
		TestFalse(TEXT("wiggle"), Run(D, SwingFrames(90.0, 380.0, 60.0)).IsSet());
		FSwingDetector Twice;
		TArray<MovecadeKart::Net::FSensorFrame> Frames = SwingFrames(-90.0, 1200.0, 60.0);
		const TArray<MovecadeKart::Net::FSensorFrame> Echo = SwingFrames(-90.0, 1200.0, 60.0);
		// The echo's idle lead-in is 200 ms: inside 450 ms of the first's end.
		Frames.SetNum(Frames.Num() - 10);
		Frames.Append(Echo);
		int32 Events = 0;
		FString Log;
		for (const MovecadeKart::Net::FSensorFrame& F : Frames)
		{
			if (const TOptional<FSwingEvent> E = Twice.Feed(F))
			{
				++Events;
				Log += FString::Printf(TEXT(" peak %.0f at clock %.0f;"), E->PeakAtMs, Twice.ClockMs());
			}
		}
		TestEqual(FString::Printf(TEXT("refractory:%s"), *Log), Events, 1);
	}
	{
		// Without attitude it still swings, through the middle.
		FSwingDetector D;
		const TOptional<FSwingEvent> E = Run(D, SwingFrames(-90.0, 1200.0, 60.0, 1.0, false));
		TestTrue(TEXT("gyro only"), E.IsSet() && !E->bHasAttitude && E->Direction == 0.0 && E->Strength > 0.8);
	}
	return true;
}

#endif
