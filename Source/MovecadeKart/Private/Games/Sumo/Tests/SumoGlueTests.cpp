// Tests for Sumo Bumpers' pure glue around the rules: whole bot matches fingerprinted bit for bit (so a refactor of
// the rules or the bots can't drift silently), the standings key the HUD sorts by and the debug
// flags. The rules' own golden tests live in SumoTests.cpp.
#include "Games/Sumo/SumoDebugFlags.h"
#include "Games/Sumo/SumoRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Sumo;

namespace
{
	FMatch MakeMatch(int32 Players, uint32 Seed)
	{
		TArray<FSeed> Seeds;
		for (int32 i = 0; i < Players; ++i)
		{
			Seeds.Add({ FString::Printf(TEXT("p%d"), i + 1), FString::Printf(TEXT("P%d"), i + 1) });
		}
		return CreateMatch(Seeds, Seed);
	}

	/** The rules tests' bot line-up (SumoTests.cpp Brains): skills 0.35, 0.55, ... */
	TArray<FBotBrain> MakeBrains(int32 Players, uint32 Seed)
	{
		TArray<FBotBrain> Out;
		for (int32 i = 0; i < Players; ++i)
		{
			Out.Add(MakeBot(0.35 + 0.2 * i, Seed * 31u + i));
		}
		return Out;
	}

	void PlayOut(FMatch& M, TArray<FBotBrain>& Brains)
	{
		while (M.Phase != EPhase::MatchOver && M.Steps < 60 * 60 * 8)
		{
			for (int32 i = 0; i < M.Pucks.Num(); ++i)
			{
				SetInput(M, i, ThinkBot(M, i, Brains[i]));
			}
			StepMatch(M);
		}
	}

	/** Slots ranked by StandingKey the way FArcadeStandingsOrder does it: stable, higher first. */
	TArray<int32> RankByKey(const FMatch& M)
	{
		TArray<int32> Order;
		for (int32 i = 0; i < M.Pucks.Num(); ++i)
		{
			Order.Add(i);
		}
		Order.StableSort([&M](int32 A, int32 B) { return StandingKey(M.Pucks[A]) > StandingKey(M.Pucks[B]); });
		return Order;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoFingerprintTest, "MovecadeKart.Sumo.Fingerprints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoFingerprintTest::RunTest(const FString& Parameters)
{
	// Whole house-bot matches (SumoTests.cpp's determinism line-up), bit for bit as before the code-quality pass.
	struct FGolden
	{
		int32 Players;
		uint64 Fingerprint;
	};
	const FGolden Goldens[] = { { 2, 0xc82ac944f1a93f24ull }, { 3, 0x09c36f1ff4c65b21ull }, { 4, 0xcf658447c9b3f1c3ull } };
	for (const FGolden& Golden : Goldens)
	{
		FMatch M = MakeMatch(Golden.Players, 77 + Golden.Players);
		TArray<FBotBrain> Brains = MakeBrains(Golden.Players, 5);
		PlayOut(M, Brains);
		const uint64 Got = Fingerprint(M);
		TestTrue(*FString::Printf(TEXT("%d bots: fingerprint %llx (golden %llx)"), Golden.Players, Got, Golden.Fingerprint), Got == Golden.Fingerprint);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoStandingKeyTest, "MovecadeKart.Sumo.StandingKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoStandingKeyTest::RunTest(const FString& Parameters)
{
	// Finished bot matches: the key ranks exactly as Standings.
	for (uint32 Seed = 1; Seed <= 12; ++Seed)
	{
		const int32 Players = 2 + Seed % 3;
		FMatch M = MakeMatch(Players, Seed * 101u);
		TArray<FBotBrain> Brains = MakeBrains(Players, Seed);
		PlayOut(M, Brains);
		TestTrue(*FString::Printf(TEXT("seed %u: key order is Standings"), Seed), RankByKey(M) == Standings(M));
	}
	// Staged ties: every tie-break in turn, and a full tie (slot order).
	FMatch M = MakeMatch(4, 3);
	const int32 Stats[4][4] = { { 1, 4, 1, 2 }, { 1, 4, 1, 1 }, { 1, 4, 2, 3 }, { 1, 4, 1, 1 } };
	for (int32 i = 0; i < 4; ++i)
	{
		M.Pucks[i].RoundWins = Stats[i][0];
		M.Pucks[i].Score = Stats[i][1];
		M.Pucks[i].Knockouts = Stats[i][2];
		M.Pucks[i].Falls = Stats[i][3];
	}
	TestTrue(TEXT("staged ties"), RankByKey(M) == Standings(M));
	TestTrue(TEXT("staged order"), Standings(M) == TArray<int32>({ 2, 1, 3, 0 }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSumoDebugFlagsTest, "MovecadeKart.Sumo.DebugFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSumoDebugFlagsTest::RunTest(const FString& Parameters)
{
	const FSumoDebugFlags None = FSumoDebugFlags::Parse(TEXT("-SumoPlayers=0 -SumoBots=3"));
	TestTrue(TEXT("defaults"), None.BotSkill < 0.f && !None.Seed.IsSet());
	const FSumoDebugFlags Set = FSumoDebugFlags::Parse(TEXT("-SumoSkill=0.3 -SumoSeed=4242"));
#if UE_BUILD_SHIPPING
	TestTrue(TEXT("shipping ignores them"), Set.BotSkill < 0.f && !Set.Seed.IsSet());
#else
	TestTrue(TEXT("skill"), FMath::IsNearlyEqual(Set.BotSkill, 0.3f));
	TestTrue(TEXT("seed"), Set.Seed.IsSet() && Set.Seed.GetValue() == 4242u);
#endif
	TestTrue(TEXT("null command line"), FSumoDebugFlags::Parse(nullptr).BotSkill < 0.f);
	return true;
}

#endif
