#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Audio/KartAudioMath.h"
#include "Audio/KartSoundObserver.h"
#include "Sim/KartSim.h"
#include "Sim/KartTrack.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;
using namespace MovecadeKart::Audio;

namespace
{
	constexpr EAutomationTestFlags KartAudioTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	struct FGoldenDrivetrainFrame
	{
		double Speed;
		int32 Accelerating;
		bool bNeutral;
		double Rpm;
		int32 Gear;
		double Load;
		bool bThrottle;
		bool bLimiter;
		int32 Events;
	};

	struct FGoldenEngineMix
	{
		double Rpm, Load, MixGain, OnBus, OffBus, Out;
		double OnGain[NumOnLayers];
		double OffGain[NumOffLayers];
		double OnRate[NumOnLayers];
		double OffRate[NumOffLayers];
	};

	struct FGoldenDriftBoost
	{
		const TCHAR* Label;
		double Charge;
		int32 Level;
		double Gain;
	};

	// Produced by the web code (Tools/audio/golden.mjs).
#include "KartAudioGolden.inl"

	template <int32 N>
	bool CheckDrivetrain(FAutomationTestBase& Test, const TCHAR* Name, const FGoldenDrivetrainFrame (&Frames)[N], int32 Seed)
	{
		FDrivetrain Drivetrain(static_cast<uint32>(Seed));
		int32 Mismatches = 0;
		for (int32 i = 0; i < N && Mismatches < 5; ++i)
		{
			const FGoldenDrivetrainFrame& G = Frames[i];
			FDrivetrainInput Input;
			Input.Speed = G.Speed;
			Input.Dt = 1.0 / 60.0;
			Input.Accelerating = static_cast<int8>(G.Accelerating);
			Input.bNeutral = G.bNeutral;
			const FDrivetrainFrame F = Drivetrain.Step(Input);
			if (!FMath::IsNearlyEqual(F.Rpm, G.Rpm, 1e-6) || F.Gear != G.Gear || !FMath::IsNearlyEqual(F.Load, G.Load, 1e-9) ||
				F.bThrottle != G.bThrottle || F.bLimiter != G.bLimiter || F.Events != G.Events)
			{
				++Mismatches;
				Test.AddError(FString::Printf(TEXT("%s frame %d: rpm %.9f/%.9f gear %d/%d load %.9f/%.9f throttle %d/%d limiter %d/%d events %d/%d (port/web)"),
					Name, i, F.Rpm, G.Rpm, F.Gear, G.Gear, F.Load, G.Load, F.bThrottle, G.bThrottle, F.bLimiter, G.bLimiter, F.Events, G.Events));
			}
		}
		return Mismatches == 0;
	}

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

	/** KartPlayerController's -KartAutoDrive: aim at waypoint nearest + 3, full gas. */
	FKartInput AutoDrive(const FTrack& Track, const FKartState& S)
	{
		const int32 Count = Track.Waypoints.Num();
		const FVector2D Target = Track.Waypoints[(Track.NearestWaypoint(S.Pos) + 3) % Count];
		const FVector2D D = Target - S.Pos;
		FKartInput Input;
		Input.SteeringX = FMath::Clamp(FMath::FindDeltaAngleRadians(S.Yaw, FMath::Atan2(-D.X, -D.Y)) * 2.5, -1.0, 1.0);
		Input.bForward = true;
		return Input;
	}

	int32 CountPlays(const FKartAudioCommands& Commands, const TCHAR* Cue)
	{
		int32 Count = 0;
		for (const FOneShot& Shot : Commands.Plays)
		{
			Count += Shot.Cue == FName(Cue) ? 1 : 0;
		}
		return Count;
	}

	const FOneShot* FindPlay(const FKartAudioCommands& Commands, const TCHAR* Cue)
	{
		for (const FOneShot& Shot : Commands.Plays)
		{
			if (Shot.Cue == FName(Cue))
			{
				return &Shot;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioDrivetrainTest, "MovecadeKart.Audio.DrivetrainMatchesWeb", KartAudioTestFlags)
bool FKartAudioDrivetrainTest::RunTest(const FString&)
{
	CheckDrivetrain(*this, TEXT("PhoneRunAndLift"), GoldenPhoneRunAndLift, GoldenPhoneRunAndLiftSeed);
	CheckDrivetrain(*this, TEXT("KeyboardInferred"), GoldenKeyboardInferred, GoldenKeyboardInferredSeed);
	CheckDrivetrain(*this, TEXT("GridRev"), GoldenGridRev, GoldenGridRevSeed);
	CheckDrivetrain(*this, TEXT("BoostKickdown"), GoldenBoostKickdown, GoldenBoostKickdownSeed);
	CheckDrivetrain(*this, TEXT("ReverseAndBrake"), GoldenReverseAndBrake, GoldenReverseAndBrakeSeed);

	// drivetrain.test.ts 'survives hitches and bad input'.
	FDrivetrain Drivetrain;
	FDrivetrainInput Bad;
	Bad.Speed = std::numeric_limits<double>::quiet_NaN();
	Bad.Dt = 5.0;
	TestTrue(TEXT("NaN speed keeps a finite rpm"), FMath::IsFinite(Drivetrain.Step(Bad).Rpm));
	FDrivetrainInput Reverse;
	Reverse.Speed = -8.0;
	Reverse.Dt = 1.0 / 60.0;
	Reverse.Accelerating = 0;
	TestEqual(TEXT("reverse gear"), Drivetrain.Step(Reverse).Gear, -1);
	return true;
}

// drivetrain.test.ts behaviours, on the port.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioGearboxShapeTest, "MovecadeKart.Audio.GearboxShape", KartAudioTestFlags)
bool FKartAudioGearboxShapeTest::RunTest(const FString&)
{
	FDrivetrain Drivetrain(7);
	double Speed = 0.0;
	TArray<double> Upshifts;
	FDrivetrainFrame Last;
	for (int32 i = 0; i < 360; ++i)
	{
		Speed = 40.0 + (Speed - 40.0) * FMath::Exp(-1.8 / 60.0);
		FDrivetrainInput Input;
		Input.Speed = Speed;
		Input.Dt = 1.0 / 60.0;
		Input.Accelerating = 1;
		Last = Drivetrain.Step(Input);
		if (Last.Events & DrivetrainUpshift)
		{
			Upshifts.Add(i / 60.0);
		}
		TestFalse(TEXT("no limiter on a normal run"), Last.bLimiter);
	}
	TestEqual(TEXT("three upshifts"), Upshifts.Num(), 3);
	if (Upshifts.Num() == 3)
	{
		TestTrue(TEXT("first upshift after 0.2 s"), Upshifts[0] > 0.2);
		TestTrue(TEXT("third upshift before 2 s"), Upshifts[2] < 2.0);
	}
	TestEqual(TEXT("cruise in 4th"), Last.Gear, 4);
	TestTrue(TEXT("cruise rpm 5500..6600"), Last.Rpm > 5500.0 && Last.Rpm < 6600.0);
	TestTrue(TEXT("cruise load > 0.9"), Last.Load > 0.9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioEngineLayersTest, "MovecadeKart.Audio.EngineLayersMatchWeb", KartAudioTestFlags)
bool FKartAudioEngineLayersTest::RunTest(const FString&)
{
	int32 Errors = 0;
	for (const FGoldenEngineMix& G : GoldenEngineMix)
	{
		const FEngineTargets T = EngineTargets(G.Rpm, G.Load, G.MixGain);
		bool bOk = FMath::IsNearlyEqual(T.OnBus, G.OnBus, 1e-12) && FMath::IsNearlyEqual(T.OffBus, G.OffBus, 1e-12) &&
			FMath::IsNearlyEqual(T.Out, G.Out, 1e-12);
		for (int32 i = 0; i < NumOnLayers; ++i)
		{
			bOk &= FMath::IsNearlyEqual(T.OnGain[i], G.OnGain[i], 1e-12) && FMath::IsNearlyEqual(T.OnRate[i], G.OnRate[i], 1e-12);
		}
		for (int32 i = 0; i < NumOffLayers; ++i)
		{
			bOk &= FMath::IsNearlyEqual(T.OffGain[i], G.OffGain[i], 1e-12) && FMath::IsNearlyEqual(T.OffRate[i], G.OffRate[i], 1e-12);
		}
		if (!bOk && ++Errors <= 5)
		{
			AddError(FString::Printf(TEXT("rpm %.1f load %.2f: on/off bus %.6f/%.6f (web %.6f/%.6f) out %.6f (web %.6f)"),
				G.Rpm, G.Load, T.OnBus, T.OffBus, G.OnBus, G.OffBus, T.Out, G.Out));
		}
	}
	TestEqual(TEXT("engine mix mismatches"), Errors, 0);

	// Equal power: the two bracketing layers' squares sum to 1.
	double W[NumOnLayers];
	LayerWeights(OnLayerRpm, NumOnLayers, 3100.0, W);
	TestNearlyEqual(TEXT("equal-power crossfade"), W[1] * W[1] + W[2] * W[2], 1.0, 1e-12);
	TestEqual(TEXT("outer layers silent"), W[0] + W[3] + W[4], 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioSoundMixTest, "MovecadeKart.Audio.SoundMixMatchesWeb", KartAudioTestFlags)
bool FKartAudioSoundMixTest::RunTest(const FString&)
{
	for (const auto& G : GoldenWind)
	{
		const FLoopMix W = WindMix(G[0]);
		TestNearlyEqual(*FString::Printf(TEXT("wind gain at %.0f"), G[0]), W.Gain, G[1], 1e-12);
		TestNearlyEqual(*FString::Printf(TEXT("wind rate at %.0f"), G[0]), W.Rate, G[2], 1e-12);
		TestEqual(*FString::Printf(TEXT("wind lowpass at %.0f"), G[0]), W.Lowpass, G[3]);
	}
	int32 SquealErrors = 0;
	for (const auto& G : GoldenSqueal)
	{
		const FLoopMix S = SquealMix(G[0], G[1] > 0.5, G[2], G[3]);
		if ((!FMath::IsNearlyEqual(S.Gain, G[4], 1e-12) || !FMath::IsNearlyEqual(S.Rate, G[5], 1e-12)) && ++SquealErrors <= 5)
		{
			AddError(FString::Printf(TEXT("squeal v=%.1f drift=%d dp=%.1f steer=%.2f: %.6f@%.6f, web %.6f@%.6f"), G[0], G[1] > 0.5, G[2], G[3], S.Gain, S.Rate, G[4], G[5]));
		}
	}
	int32 ImpactErrors = 0;
	for (const auto& G : GoldenImpact)
	{
		const double Strength = ImpactStrength(G[0], G[1], G[2] > 0.5);
		double Gain = 0.0;
		const EImpactTier Tier = ImpactTier(Strength, Gain);
		if ((!FMath::IsNearlyEqual(Strength, G[3], 1e-12) || static_cast<int32>(Tier) != static_cast<int32>(G[4]) || !FMath::IsNearlyEqual(Gain, G[5], 1e-12)) && ++ImpactErrors <= 5)
		{
			AddError(FString::Printf(TEXT("impact v=%.1f pen=%.2f crit=%d: s=%.6f tier %d gain %.6f, web s=%.6f tier %d gain %.6f"),
				G[0], G[1], G[2] > 0.5, Strength, static_cast<int32>(Tier), Gain, G[3], static_cast<int32>(G[4]), G[5]));
		}
	}
	for (const FGoldenDriftBoost& G : GoldenDriftBoost)
	{
		double Gain = 0.0;
		const int32 Level = DriftBoostLevel(G.Label, G.Charge, Gain);
		TestEqual(*FString::Printf(TEXT("drift boost level %s charge %.1f"), G.Label, G.Charge), Level, G.Level);
		TestNearlyEqual(*FString::Printf(TEXT("drift boost gain %s charge %.1f"), G.Label, G.Charge), Gain, G.Gain, 1e-12);
	}
	TestTrue(TEXT("impact cue ids"), ImpactCueId(EImpactTier::Medium) == FName(TEXT("kart.crash-medium")));
	TestNearlyEqual(TEXT("impact rate"), ImpactRate(0.5), 0.96 + FMath::Fmod(0.5 * 7919.0, 1.0) * 0.08, 1e-12);
	return SquealErrors == 0 && ImpactErrors == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioMixerTest, "MovecadeKart.Audio.MixerRules", KartAudioTestFlags)
bool FKartAudioMixerTest::RunTest(const FString&)
{
	// audioService.ts laneToPan: ±0.55 spread, 0 for one kart.
	TestEqual(TEXT("one kart centred"), LaneToPan(0, 1), 0.0);
	TestNearlyEqual(TEXT("4 karts lane 0"), LaneToPan(0, 4), -0.55, 1e-12);
	TestNearlyEqual(TEXT("4 karts lane 1"), LaneToPan(1, 4), -0.55 / 3.0, 1e-12);
	TestNearlyEqual(TEXT("4 karts lane 3"), LaneToPan(3, 4), 0.55, 1e-12);
	TestNearlyEqual(TEXT("countdown 5"), CountdownRate(5), 1.0, 1e-12);
	TestNearlyEqual(TEXT("countdown 2"), CountdownRate(2), 1.1, 1e-12);
	TestNearlyEqual(TEXT("countdown 1"), CountdownRate(1), 1.2, 1e-12);
	TestNearlyEqual(TEXT("engine character 0"), EngineCharacter(0), 0.13, 1e-12);
	TestNearlyEqual(TEXT("engine character 3"), EngineCharacter(3), FMath::Fmod(3 * 0.29 + 0.13, 1.0), 1e-12);

	// kartCues.ts: 45 kart cues plus lobby.tick / lobby.go, 14 loops.
	TestEqual(TEXT("cue count"), AllCues().Num(), 47);
	int32 Loops = 0;
	for (const FCueDef& Cue : AllCues())
	{
		Loops += Cue.bLoop ? 1 : 0;
		TestTrue(*FString::Printf(TEXT("%s loops have no throttle"), Cue.Id), !Cue.bLoop || Cue.ThrottleMs == 0);
	}
	TestEqual(TEXT("loop cues"), Loops, 14);
	const FCueDef* Warning = FindCue(TEXT("kart.warning"));
	const FCueDef* Heavy = FindCue(TEXT("kart.crash-heavy"));
	if (TestNotNull(TEXT("kart.warning"), Warning) && TestNotNull(TEXT("kart.crash-heavy"), Heavy))
	{
		TestEqual(TEXT("warning gain"), Warning->Gain, 0.34f);
		TestEqual(TEXT("warning throttle"), Warning->ThrottleMs, 850);
		TestEqual(TEXT("heavy gain"), Heavy->Gain, 0.62f);
		TestEqual(TEXT("heavy throttle"), Heavy->ThrottleMs, 90);

		FCueThrottle Throttle;
		TestTrue(TEXT("first play"), Throttle.TryPlay(*Heavy, 10.0));
		TestFalse(TEXT("inside 90 ms"), Throttle.TryPlay(*Heavy, 10.089));
		TestTrue(TEXT("after 90 ms"), Throttle.TryPlay(*Heavy, 10.0901));
		TestTrue(TEXT("other cues are independent"), Throttle.TryPlay(*Warning, 10.0901));
	}
	TestNull(TEXT("unknown cue"), FindCue(TEXT("kart.nope")));
	return true;
}

// The observer on the real sim: an autopilot race on Sunset Harbor.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioObserverRaceTest, "MovecadeKart.Audio.ObserverRace", KartAudioTestFlags)
bool FKartAudioObserverRaceTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	FKartSoundObserver Observer;
	FKartAudioContext Context;
	FKartAudioStatus Status;

	TMap<FName, int32> Plays;
	int32 MaxGear = 0;
	int32 FinishFrame = -1;
	bool bWindSeen = false;
	bool bEngineAlways = true;
	TArray<int32> LapCueAt;
	const int32 Steps = 60 * 60 * 4;
	for (int32 i = 0; i < Steps; ++i)
	{
		const FKartInput Input = AutoDrive(Track, Sim.State());
		Sim.Step(Input);
		const FKartAudioCommands C = Observer.Observe(Context, Sim.State(), Input, Status, &Track);
		for (const FOneShot& Shot : C.Plays)
		{
			Plays.FindOrAdd(Shot.Cue)++;
			if (Shot.Cue == FName(TEXT("kart.lap")) || Shot.Cue == FName(TEXT("kart.final-lap")))
			{
				LapCueAt.Add(Sim.State().LapsCompleted);
			}
		}
		if (Sim.State().bFinished)
		{
			FinishFrame = i;
			TestTrue(TEXT("finish stops the kart's sound"), C.bStopAll && !C.bEngine);
			TestEqual(TEXT("one finish cue"), CountPlays(C, TEXT("kart.finish")), 1);
			if (const FOneShot* Finish = FindPlay(C, TEXT("kart.finish")))
			{
				TestEqual(TEXT("finish plays at the cue's own gain"), Finish->Gain, 1.0);
			}
			break;
		}
		bEngineAlways &= C.bEngine;
		MaxGear = FMath::Max(MaxGear, C.Drivetrain.Gear);
		bWindSeen |= C.Loops[static_cast<int32>(ELoopLayer::Wind)].bActive && C.Loops[static_cast<int32>(ELoopLayer::Wind)].Lowpass > 700.0;
	}
	TestTrue(TEXT("the autopilot finished 3 laps"), FinishFrame > 0);
	TestTrue(TEXT("engine ran the whole race"), bEngineAlways);
	TestTrue(TEXT("reached 4th gear"), MaxGear >= 4);
	TestTrue(TEXT("upshift cues"), Plays.FindRef(TEXT("kart.shift")) >= 3);
	TestTrue(TEXT("wind loop with its lowpass"), bWindSeen);
	TestTrue(TEXT("lap cue after lap 1, final-lap cue after lap 2"), LapCueAt == TArray<int32>({ 1, 2 }));
	TestEqual(TEXT("one kart.lap"), Plays.FindRef(TEXT("kart.lap")), 1);
	TestEqual(TEXT("one kart.final-lap"), Plays.FindRef(TEXT("kart.final-lap")), 1);
	AddInfo(FString::Printf(TEXT("finished at step %d; plays: shift %d, backfire %d, boost %d, crash l/m/h %d/%d/%d, respawn %d"),
		FinishFrame, Plays.FindRef(TEXT("kart.shift")), Plays.FindRef(TEXT("kart.backfire-a")) + Plays.FindRef(TEXT("kart.backfire-b")),
		Plays.FindRef(TEXT("kart.boost")), Plays.FindRef(TEXT("kart.crash-light")), Plays.FindRef(TEXT("kart.crash-medium")),
		Plays.FindRef(TEXT("kart.crash-heavy")), Plays.FindRef(TEXT("kart.respawn"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioObserverDriftTest, "MovecadeKart.Audio.ObserverDriftHopAndBoost", KartAudioTestFlags)
bool FKartAudioObserverDriftTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	FKartSoundObserver Observer;
	FKartAudioContext Context;
	FKartAudioStatus Status;
	auto Run = [&](const FKartInput& Input, TMap<FName, int32>& Plays, FKartAudioCommands* Last = nullptr)
	{
		Sim.Step(Input);
		const FKartAudioCommands C = Observer.Observe(Context, Sim.State(), Input, Status, nullptr);
		for (const FOneShot& Shot : C.Plays)
		{
			Plays.FindOrAdd(Shot.Cue)++;
		}
		if (Last)
		{
			*Last = C;
		}
	};

	TMap<FName, int32> Plays;
	FKartInput Gas;
	Gas.bForward = true;
	Sim.MutableState().Speed = 40.0;
	for (int32 i = 0; i < 10; ++i)
	{
		Run(Gas, Plays);
	}
	TestTrue(TEXT("above drift speed"), Sim.State().Speed > 20.0);
	TestEqual(TEXT("no hop yet"), Plays.FindRef(TEXT("kart.hop")), 0);

	// Hop to the left, then hold the drift for 2.2 s, counter-steered to run straight down the start straight:
	// a hop, its landing, a squealing slide and two charge ticks.
	FKartInput Drift = Gas;
	Drift.bJump = true;
	Drift.bLeft = true;
	FKartAudioCommands Last;
	bool bSkid = false;
	for (int32 i = 0; i < 132; ++i)
	{
		Run(Drift, Plays, &Last);
		Drift.bLeft = false;
		Drift.bRight = true;
		Drift.SteeringX = -0.4;
		bSkid |= Last.Loops[static_cast<int32>(ELoopLayer::Skid)].bActive;
	}
	TestEqual(TEXT("still drifting"), Sim.State().DriftDirection, 1.4);
	TestEqual(TEXT("one hop"), Plays.FindRef(TEXT("kart.hop")), 1);
	TestEqual(TEXT("one land"), Plays.FindRef(TEXT("kart.land")), 1);
	TestTrue(TEXT("tire skid while sliding"), bSkid);
	TestEqual(TEXT("drift charge at 1 s and 2 s"), Plays.FindRef(TEXT("kart.drift-charge")), 2);

	// Release: a DRIFT_*_ULTRA/SUPER boost voiced by its grade, and the boost loop.
	Plays.Reset();
	Run(Gas, Plays, &Last);
	const FString Label = Sim.State().ActiveBoost;
	double Gain = 0.0;
	const int32 Level = DriftBoostLevel(Label, 2.2, Gain);
	TestTrue(*FString::Printf(TEXT("a drift boost (%s)"), *Label), Level >= 2);
	TestEqual(TEXT("one drift-boost whoosh of that level"), CountPlays(Last, *DriftBoostCueId(Level).ToString()), 1);
	TestEqual(TEXT("no generic boost cue"), CountPlays(Last, TEXT("kart.boost")), 0);
	TestTrue(TEXT("boost loop on"), Last.Loops[static_cast<int32>(ELoopLayer::Boost)].bActive);

	// Pause stops everything; the countdown runs the engine in neutral and revs it on the gas.
	Context.bPaused = true;
	FKartAudioCommands Paused = Observer.Observe(Context, Sim.State(), Gas, Status, nullptr);
	TestTrue(TEXT("pause stops all"), Paused.bStopAll && !Paused.bEngine);
	Context.bPaused = false;
	Context.Phase = ERacePhase::Countdown;
	double MaxRpm = 0.0;
	for (int32 i = 0; i < 90; ++i)
	{
		const FKartAudioCommands C = Observer.Observe(Context, Sim.State(), Gas, Status, nullptr);
		MaxRpm = FMath::Max(MaxRpm, C.EngineRpm);
		TestFalse(TEXT("no wind on the grid"), C.Loops[static_cast<int32>(ELoopLayer::Wind)].bActive);
		TestEqual(TEXT("neutral"), C.Drivetrain.Gear, 0);
	}
	TestEqual(TEXT("grid rev bounces on the limiter"), MaxRpm, LimiterRpm);
	Context.Phase = ERacePhase::Results;
	TestTrue(TEXT("results are silent"), Observer.Observe(Context, Sim.State(), Gas, Status, nullptr).bStopAll);

	// Item edges from the status the items layer sets.
	Context.Phase = ERacePhase::Racing;
	Plays.Reset();
	Run(Gas, Plays);
	Status.bRollingItem = true;
	Run(Gas, Plays, &Last);
	TestEqual(TEXT("item pickup"), CountPlays(Last, TEXT("kart.item-pickup")), 1);
	TestTrue(TEXT("roulette loop"), Last.Loops[static_cast<int32>(ELoopLayer::Roulette)].bActive);
	Status.bRollingItem = false;
	Status.bHasItem = true;
	Status.HeldItem = TEXT("LIGHTNING");
	Run(Gas, Plays, &Last);
	TestEqual(TEXT("item ready"), CountPlays(Last, TEXT("kart.item-ready")), 1);
	Status.bHasItem = false;
	Run(Gas, Plays, &Last);
	TestEqual(TEXT("own lightning used"), CountPlays(Last, TEXT("kart.lightning")), 1);
	Status.bBulletBill = true;
	Run(Gas, Plays, &Last);
	TestFalse(TEXT("no engine as a rocket"), Last.bEngine);
	TestTrue(TEXT("rocket loop"), Last.Loops[static_cast<int32>(ELoopLayer::Rocket)].bActive);
	TestEqual(TEXT("rocket start whoosh"), CountPlays(Last, TEXT("kart.boost")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartAudioObserverWallTest, "MovecadeKart.Audio.ObserverWallImpact", KartAudioTestFlags)
bool FKartAudioObserverWallTest::RunTest(const FString&)
{
	FTrack Track;
	if (!LoadHarbor(*this, Track)) return false;
	FKartSim Sim;
	Sim.Init(&Track, 0);
	FKartSoundObserver Observer;
	FKartAudioContext Context;
	FKartAudioStatus Status;
	// Turn the kart 90 degrees off the grid heading and floor it into the barrier.
	Sim.Teleport(Sim.State().Pos, Sim.State().Yaw + UE_DOUBLE_HALF_PI);
	FKartInput Gas;
	Gas.bForward = true;
	TArray<FOneShot> Crashes;
	int32 FirstContact = -1;
	for (int32 i = 0; i < 300; ++i)
	{
		Sim.Step(Gas);
		const FKartAudioCommands C = Observer.Observe(Context, Sim.State(), Gas, Status, &Track);
		for (const FOneShot& Shot : C.Plays)
		{
			if (Shot.Cue.ToString().StartsWith(TEXT("kart.crash-")))
			{
				Crashes.Add(Shot);
				FirstContact = FirstContact < 0 ? i : FirstContact;
			}
		}
	}
	TestTrue(TEXT("a crash cue when the kart meets the barrier"), Crashes.Num() >= 1);
	for (const FOneShot& Shot : Crashes)
	{
		TestTrue(TEXT("crash gain 0.6..1"), Shot.Gain >= 0.6 - 1e-9 && Shot.Gain <= 1.0 + 1e-9);
		TestTrue(TEXT("crash rate 0.96..1.04"), Shot.Rate >= 0.96 && Shot.Rate <= 1.04);
	}
	AddInfo(FString::Printf(TEXT("%d crash cue(s), first at step %d (%s)"), Crashes.Num(), FirstContact,
		Crashes.Num() ? *Crashes[0].Cue.ToString() : TEXT("-")));
	return true;
}

#endif
