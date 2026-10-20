#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Race/KartBotDriver.h"
#include "Sim/KartSim.h"
#include "Sim/KartTrack.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	struct FBotRun
	{
		double Seconds = -1.0;
		uint32 Respawns = 0;
		uint32 DriftBoosts = 0;
	};

	/** One bot alone on the track for its full race distance. */
	FBotRun RunBot(const FTrack& Track, const FKartBotTuning& Tuning)
	{
		FKartSim Sim;
		Sim.Init(&Track, 0);
		FKartBotDriver Bot;
		Bot.Tuning = Tuning;
		int32 Steps = 0;
		while (!Sim.State().bFinished && Steps < 60 * 240)
		{
			Sim.Step(Bot.Update(Sim.State(), Track.Waypoints, true, Steps / 60.0));
			++Steps;
		}
		FBotRun Run;
		Run.Seconds = Sim.State().bFinished ? Steps / 60.0 : -1.0;
		Run.Respawns = Sim.State().TeleportCount;
		Run.DriftBoosts = Sim.State().DriftBoostCount;
		return Run;
	}
}

// Every difficulty finishes both tracks cleanly, and they rank Easy > Normal > Hard on time.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartBotSkillTest, "MovecadeKart.Race.BotSkills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartBotSkillTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* File : { TEXT("sunset-harbor"), TEXT("violet-loop") })
	{
		FTrack Track;
		FString Error;
		if (!FTrack::LoadFromJsonFile(FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks"), FString(File) + TEXT(".track.json")), Track, Error))
		{
			AddError(FString::Printf(TEXT("%s failed to load: %s"), File, *Error));
			return false;
		}
		const FBotRun Easy = RunBot(Track, FKartBotTuning::Easy());
		const FBotRun Normal = RunBot(Track, FKartBotTuning::Normal());
		const FBotRun Hard = RunBot(Track, FKartBotTuning::Hard());
		AddInfo(FString::Printf(TEXT("%s: easy %.2f s (%u boosts), normal %.2f s (%u), hard %.2f s (%u)"), File,
			Easy.Seconds, Easy.DriftBoosts, Normal.Seconds, Normal.DriftBoosts, Hard.Seconds, Hard.DriftBoosts));
		for (const FBotRun* Run : { &Easy, &Normal, &Hard })
		{
			TestTrue(FString::Printf(TEXT("%s: finishes"), File), Run->Seconds > 0.0);
			TestEqual(FString::Printf(TEXT("%s: no respawns"), File), Run->Respawns, 0u);
		}
		TestTrue(FString::Printf(TEXT("%s: easy is slower than normal"), File), Easy.Seconds > Normal.Seconds + 1.0);
		TestTrue(FString::Printf(TEXT("%s: hard is faster than normal"), File), Hard.Seconds < Normal.Seconds);
	}
	return true;
}

#endif
