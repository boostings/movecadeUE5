// Golden tests for Bowling, from movecade-v2 bowling/{engine,sim,throw}.test.ts (Docs/Port/06-bowling.md §7).
#include "Games/Bowling/BowlingRules.h"
#include "Games/Bowling/BowlingSim.h"
#include "Games/Bowling/BowlingThrow.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Bowling;

namespace
{
	FCard Score(std::initializer_list<int32> Rolls)
	{
		return ScoreRolls(TEXT("p"), TArray<int32>(Rolls));
	}

	TArray<int32> Repeat(int32 Value, int32 Count)
	{
		TArray<int32> Out;
		Out.Init(Value, Count);
		return Out;
	}

	TArray<int32> AllPins()
	{
		return { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingScoringTest, "MovecadeKart.Bowling.Scoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowlingScoringTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("gutter game"), ScoreRolls(TEXT("p"), Repeat(0, 20)).Score, 0);
	TestEqual(TEXT("all ones"), ScoreRolls(TEXT("p"), Repeat(1, 20)).Score, 20);
	const FCard Perfect = ScoreRolls(TEXT("p"), Repeat(10, 12));
	TestEqual(TEXT("perfect game"), Perfect.Score, 300);
	TestTrue(TEXT("perfect game done"), Perfect.bDone);

	const FCard Spare = Score({ 5, 5, 3 });
	TestEqual(TEXT("spare then 3: frame 1"), Spare.FrameList[0].Cumulative.Get(-1), 13);
	TestEqual(TEXT("spare then 3: score so far"), Spare.Score, 13);
	const FCard SpareOpen = Score({ 5, 5, 3, 0 });
	TestEqual(TEXT("spare, 3, 0"), SpareOpen.Score, 16);

	const FCard Strike = Score({ 10, 3, 4 });
	TestEqual(TEXT("strike then 3,4: frame 1"), Strike.FrameList[0].Cumulative.Get(-1), 17);
	TestEqual(TEXT("strike then 3,4: frame 2"), Strike.FrameList[1].Cumulative.Get(-1), 24);

	const FCard Turkey = Score({ 10, 10, 10, 0, 0 });
	TestEqual(TEXT("turkey 1"), Turkey.FrameList[0].Cumulative.Get(-1), 30);
	TestEqual(TEXT("turkey 2"), Turkey.FrameList[1].Cumulative.Get(-1), 50);
	TestEqual(TEXT("turkey 3"), Turkey.FrameList[2].Cumulative.Get(-1), 60);

	TArray<int32> Tenth = Repeat(0, 18);
	Tenth.Append({ 10, 10, 10 });
	TestEqual(TEXT("10th: three strikes"), ScoreRolls(TEXT("p"), Tenth).Score, 30);
	Tenth = Repeat(0, 18);
	Tenth.Append({ 5, 5, 10 });
	TestEqual(TEXT("10th: spare and fill"), ScoreRolls(TEXT("p"), Tenth).Score, 20);
	Tenth = Repeat(0, 18);
	Tenth.Append({ 4, 5 });
	const FCard Open = ScoreRolls(TEXT("p"), Tenth);
	TestEqual(TEXT("10th: open"), Open.Score, 9);
	TestTrue(TEXT("10th open is done"), Open.bDone);

	const FCard Pending = Score({ 3, 4, 10 });
	TestEqual(TEXT("closed frame keeps its total"), Pending.FrameList[0].Cumulative.Get(-1), 7);
	TestFalse(TEXT("pending strike has no total"), Pending.FrameList[1].Cumulative.IsSet());

	const FCard Kinds = Score({ 10, 5, 5, 3, 4 });
	TestTrue(TEXT("kinds"), Kinds.FrameList[0].Kind == EFrameKind::Strike && Kinds.FrameList[1].Kind == EFrameKind::Spare && Kinds.FrameList[2].Kind == EFrameKind::Open);

	// The 10th frame's marks: a ball after a spare is a fresh rack (the web shows 5,5,5 as "5 / /").
	Tenth = Repeat(0, 18);
	Tenth.Append({ 5, 5, 5 });
	TestEqual(TEXT("10th marks"), FString::Join(FrameMarks(ScoreRolls(TEXT("p"), Tenth).FrameList[9]), TEXT(" ")), FString(TEXT("5 / 5")));
	Tenth = Repeat(0, 18);
	Tenth.Append({ 10, 7, 3 });
	TestEqual(TEXT("10th marks X 7 /"), FString::Join(FrameMarks(ScoreRolls(TEXT("p"), Tenth).FrameList[9]), TEXT(" ")), FString(TEXT("X 7 /")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingTurnsTest, "MovecadeKart.Bowling.Turns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowlingTurnsTest::RunTest(const FString& Parameters)
{
	FMatch Solo = CreateMatch({ TEXT("a"), TEXT("b") });
	FRollResult R = Roll(Solo, 10);
	TestTrue(TEXT("a strike ends the turn"), R.bTurnEnded && Solo.TurnIndex == 1);

	FMatch M = CreateMatch({ TEXT("a") });
	R = Roll(M, 4);
	TestFalse(TEXT("an open first ball keeps the bowler"), R.bTurnEnded);
	TestEqual(TEXT("standing after 4"), M.Standing, 6);
	R = Roll(M, 9);
	TestEqual(TEXT("over-report clamps to the standing pins"), R.Felled, 6);
	TestTrue(TEXT("[4,6] is a spare"), M.Cards[0].FrameList[0].Kind == EFrameKind::Spare);

	FMatch C = CreateMatch({ TEXT("a") });
	TestEqual(TEXT("negative counts 0"), Roll(C, -3).Felled, 0);
	TestEqual(TEXT("fractions floor"), Roll(C, 4.9).Felled, 4);

	// The 10th: a strike racks a fresh set and the turn continues.
	FMatch T = CreateMatch({ TEXT("a") });
	for (int32 i = 0; i < 18; ++i)
	{
		Roll(T, 0);
	}
	R = Roll(T, 10);
	TestTrue(TEXT("10th strike resets the rack"), R.bRackReset && !R.bTurnEnded && T.Standing == 10);

	// Two players alternate frame by frame to a pair of perfect games.
	FMatch Two = CreateMatch({ TEXT("a"), TEXT("b") });
	TArray<int32> Turns;
	while (!Two.bComplete && Turns.Num() < 100)
	{
		Turns.Add(Two.TurnIndex);
		Roll(Two, 10);
	}
	TestEqual(TEXT("both perfect"), Two.Cards[0].Score + Two.Cards[1].Score, 600);
	TestTrue(TEXT("alternates"), Turns.Num() >= 4 && Turns[0] == 0 && Turns[1] == 1 && Turns[2] == 0 && Turns[3] == 1);

	// The bowler up leaving moves play on with a fresh rack; a bystander leaving changes nothing.
	FMatch Three = CreateMatch({ TEXT("a"), TEXT("b"), TEXT("c") });
	Roll(Three, 3);
	DropPlayer(Three, TEXT("c"));
	TestTrue(TEXT("bystander leaves"), Three.Bowler() && *Three.Bowler() == TEXT("a") && Three.Standing == 7);
	DropPlayer(Three, TEXT("a"));
	TestTrue(TEXT("bowler leaves"), Three.Bowler() && *Three.Bowler() == TEXT("b") && Three.Standing == 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingSimTest, "MovecadeKart.Bowling.Sim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowlingSimTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("speed at 0"), SpeedForPower(0.0), 7.0);
	TestEqual(TEXT("speed at 1"), SpeedForPower(1.0), 9.0);
	TestEqual(TEXT("speed at 0.65"), SpeedForPower(0.65), 8.3, 1e-9);
	TestTrue(TEXT("pin 1 at the head"), PinPosition(1).Equals(FVector2D::ZeroVector));
	TestTrue(TEXT("pin 7 back left"), PinPosition(7).Equals(FVector2D(-0.4572, 0.79190), 1e-4));
	TestTrue(TEXT("pin 10 back right"), PinPosition(10).Equals(FVector2D(0.4572, 0.79190), 1e-4));

	const FShotResult Pocket = ResolveShot({ -0.15, 1.0, 0.22 }, AllPins());
	TestEqual(TEXT("pocket strike"), Pocket.Felled.Num(), 10);
	const FPinImpact* Ten = Pocket.Impacts.FindByPredicate([](const FPinImpact& I) { return I.Id == 10; });
	TestTrue(TEXT("pin 10 falls by transfer"), Ten && Ten->DirectEnergy == 0.0);

	const FShotResult Soft = ResolveShot({ 0.0, 0.02, 0.0 }, AllPins());
	TestTrue(TEXT("soft ball: no gutter, no strike"), !Soft.bGutter && Soft.Felled.Num() < 10);

	const FShotResult Wide = ResolveShot({ LaneHalfWidth + 0.2, 1.0, 0.0 }, AllPins());
	TestTrue(TEXT("off the lane: gutter at release"), Wide.bGutter && Wide.GutterAt == 0.0 && Wide.Felled.Num() == 0);

	const FShotResult Hook = ResolveShot({ 0.3, 0.9, 2.5 }, AllPins());
	TestTrue(TEXT("big hook: right gutter on the back end"), Hook.bGutter && Hook.GutterSide == 1 && Hook.GutterAt > 12.0 && Hook.GutterAt < LaneLength);

	TestFalse(TEXT("left line leaves pin 10"), ResolveShot({ -0.42, 0.6, 0.0 }, AllPins()).Felled.Contains(10));
	const FShotResult Pick = ResolveShot({ -0.49, 1.0, 0.03 }, { 7, 10 });
	TestTrue(TEXT("7-10: picks the 7"), Pick.Felled.Num() == 1 && Pick.Felled[0] == 7);

	for (const double Aim : { -0.45, -0.3, 0.3, 0.45 })
	{
		TestTrue(*FString::Printf(TEXT("no strike from %.2f"), Aim), ResolveShot({ Aim, 1.0, 0.0 }, AllPins()).Felled.Num() < 10);
	}

	// Mirror symmetry: a mirrored shot fells the mirrored pins.
	static const int32 Mirror[] = { 0, 1, 3, 2, 6, 5, 4, 10, 9, 8, 7 };
	const FShotResult L = ResolveShot({ -0.12, 0.7, -0.1 }, AllPins());
	const FShotResult Rr = ResolveShot({ 0.12, 0.7, 0.1 }, AllPins());
	TArray<int32> Mirrored;
	for (const int32 Id : L.Felled)
	{
		Mirrored.Add(Mirror[Id]);
	}
	Mirrored.Sort();
	TestEqual(TEXT("mirror symmetry"), Mirrored, Rr.Felled);
	TestEqual(TEXT("deck x is aim + curve"), BallXAt({ 0.0, 0.5, 0.4 }, LaneLength), 0.4, 1e-12);
	TestEqual(TEXT("roll time at 7 m/s"), RollSeconds(7.0), LaneLength / 7.0, 1e-9);

	TestTrue(TEXT("7-10 split"), IsSplit(10, { 7, 10 }));
	TestFalse(TEXT("head pin standing is no split"), IsSplit(10, { 1, 7, 10 }));
	TestFalse(TEXT("only on a full rack"), IsSplit(6, { 7, 10 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBowlingThrowTest, "MovecadeKart.Bowling.Throw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBowlingThrowTest::RunTest(const FString& Parameters)
{
	using MovecadeKart::Net::FSensorFrame;
	namespace Net = MovecadeKart::Net;
	auto Rest = [](uint8 Flags = Net::SensorFlagHasGyro | Net::SensorFlagHasAccel | Net::SensorFlagHasGravity)
	{
		FSensorFrame F;
		F.Flags = Flags;
		F.Gravity = FVector3d(0.0, 0.0, -9.81);
		F.DtMs = 16;
		return F;
	};
	auto Swing = [&](FThrowDetector& D, double Peak, double RollRate, uint8 Flags)
	{
		// A half-sine push along y over 20 frames.
		for (int32 i = 0; i < 20; ++i)
		{
			FSensorFrame F = Rest(Flags);
			F.Accel = FVector3d(0.0, Peak * FMath::Sin(UE_PI * (i + 0.5) / 20.0), 0.0);
			F.Gyro = FVector3d(0.0, RollRate, 0.0);
			D.Feed(F);
		}
	};

	FThrowDetector Hard;
	Hard.Feed(Rest());
	Hard.Grip();
	Swing(Hard, 200.0, 2000.0, Rest().Flags);
	const FThrowRelease Full = Hard.Release();
	TestEqual(TEXT("a hard swing is full power"), Full.Shot.Power, 1.0, 1e-9);
	TestTrue(TEXT("hook is capped"), FMath::Abs(Full.Shot.Curve) <= 0.45 + 1e-9);
	TestEqual(TEXT("full sensors: confidence 1"), Full.Confidence, 1.0);

	FThrowDetector Still;
	Still.Feed(Rest());
	Still.Grip();
	Still.Feed(Rest());
	TestEqual(TEXT("a release without a swing is the minimum ball"), Still.Release().Shot.Power, 0.04, 1e-9);

	FThrowDetector NoGyro;
	NoGyro.Grip();
	Swing(NoGyro, 30.0, 300.0, Net::SensorFlagHasAccel | Net::SensorFlagHasGravity);
	const FThrowRelease Straight = NoGyro.Release();
	TestTrue(TEXT("no gyro: straight ball"), Straight.Shot.Curve == 0.0 && Straight.bHookUnavailable);

	FThrowDetector GyroOnly;
	GyroOnly.Grip();
	Swing(GyroOnly, 0.0, 300.0, Net::SensorFlagHasGyro);
	TestTrue(TEXT("gyro only: confidence <= 0.5"), GyroOnly.Release().Confidence <= 0.5);

	// Raising the arm through the swing lifts the ball (loft): gravity turns from -z to -y.
	FThrowDetector Lob;
	Lob.Feed(Rest());
	Lob.Grip();
	for (int32 i = 0; i <= 30; ++i)
	{
		FSensorFrame F = Rest();
		const double A = UE_HALF_PI * i / 30.0;
		F.Gravity = FVector3d(0.0, -9.81 * FMath::Sin(A), -9.81 * FMath::Cos(A));
		Lob.Feed(F);
	}
	TestTrue(TEXT("a full arm rise lofts the ball"), Lob.Release().Shot.Loft > 0.9);
	return true;
}

#endif
