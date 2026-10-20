#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Race/KartBotDriver.h"
#include "Sim/KartItems.h"
#include "Sim/KartSim.h"
#include "Sim/KartTrack.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	/** What a race ends on: every kart's pose, speed and progress, and the item world's event trail. */
	struct FRaceOutcome
	{
		TArray<FKartState> Karts;
		TArray<int32> EventTrail;
	};

	/** Four bots for a full minute, in a pack: kart contact, slipstream, rubber-banding, boxes and items. */
	FRaceOutcome RunPack(const FTrack& Track, int32 Seed)
	{
		constexpr int32 Count = 4;
		FKartSim Karts[Count];
		FKartBotDriver Bots[Count];
		const FKartBotTuning Skills[Count] = { FKartBotTuning::Hard(), FKartBotTuning::Normal(), FKartBotTuning::Normal(), FKartBotTuning::Easy() };
		TArray<FKartItemRacer> Racers;
		for (int32 i = 0; i < Count; ++i)
		{
			Karts[i].Init(&Track, i);
			Bots[i].Tuning = Skills[i];
			FKartItemRacer& Racer = Racers.AddDefaulted_GetRef();
			Racer.Sim = &Karts[i];
			Racer.bBot = true;
		}
		FKartItemWorld Items;
		Items.Init(&Track, Track.ItemBoxes, Count, Seed);

		FRaceOutcome Out;
		TArray<FKartItemEvent> Events;
		for (int32 Step = 1; Step <= 60 * 60; ++Step)
		{
			const double Now = Step * FKartSim::StepSeconds;
			// Everyone sees the others where they were at the start of the step (as AKartRaceManager does).
			FKartRaceContext Contexts[Count];
			for (int32 i = 0; i < Count; ++i)
			{
				FKartRaceContext& C = Contexts[i];
				C.PlayerCount = Count;
				C.SelfSeat = i;
				C.RacePosition = i + 1;
				for (int32 j = 0; j < Count; ++j)
				{
					if (j != i)
					{
						C.Others.Add({ j, Karts[j].State().Pos });
					}
				}
			}
			for (int32 i = 0; i < Count; ++i)
			{
				// The boxes this kart could collect, as AKartRaceManager offers them to its bots.
				TArray<FVector2D, TInlineAllocator<16>> Boxes;
				if (Items.Slots()[i].Held == EKartItem::None && !Items.Slots()[i].bRolling)
				{
					for (const FKartItemBox& Box : Items.Boxes())
					{
						if (Box.CollectedAt[i] < 0.0)
						{
							Boxes.Add(Box.Pos);
						}
					}
				}
				const FKartInput Input = Bots[i].Update(Karts[i].State(), Track.Waypoints, true, Now, Boxes);
				Racers[i].bUseItem = Input.bUseItem;
				Karts[i].Step(Input, Contexts[i]);
				Racers[i].Laps = Karts[i].State().LapsCompleted;
				Racers[i].Progress = Karts[i].State().ProgressPercent;
			}
			Events.Reset();
			Items.Step(Racers, Now, Events);
			for (const FKartItemEvent& E : Events)
			{
				Out.EventTrail.Append({ Step, static_cast<int32>(E.Type), E.Seat, static_cast<int32>(E.Item) });
			}
		}
		for (const FKartSim& Kart : Karts)
		{
			Out.Karts.Add(Kart.State());
		}
		return Out;
	}
}

// The whole race is a pure function of its inputs: run it twice and every double matches to the bit. This is
// what lets bots, phones and replays share one simulation, and what makes a desync reproducible.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartDeterminismTest, "MovecadeKart.Sim.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartDeterminismTest::RunTest(const FString& Parameters)
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
		const FRaceOutcome A = RunPack(Track, 42);
		const FRaceOutcome B = RunPack(Track, 42);
		for (int32 i = 0; i < A.Karts.Num(); ++i)
		{
			const FKartState& SA = A.Karts[i];
			const FKartState& SB = B.Karts[i];
			const bool bSame = SA.Pos == SB.Pos && SA.Yaw == SB.Yaw && SA.Speed == SB.Speed && SA.Dir == SB.Dir
				&& SA.LapsCompleted == SB.LapsCompleted && SA.ProgressPercent == SB.ProgressPercent
				&& SA.TeleportCount == SB.TeleportCount && SA.DriftBoostCount == SB.DriftBoostCount
				&& SA.BarrierEventCount == SB.BarrierEventCount && SA.BumpEventCount == SB.BumpEventCount;
			TestTrue(FString::Printf(TEXT("%s: kart %d ends bit-identical"), File, i), bSame);
		}
		TestTrue(FString::Printf(TEXT("%s: same item events in the same steps"), File), A.EventTrail == B.EventTrail);
		TestTrue(FString::Printf(TEXT("%s: the pack interacted (items happened)"), File), A.EventTrail.Num() > 0);

		// A different item seed gives a different race, so the check above isn't vacuous.
		const FRaceOutcome C = RunPack(Track, 7);
		TestTrue(FString::Printf(TEXT("%s: the seed matters"), File), C.EventTrail != A.EventTrail);
		AddInfo(FString::Printf(TEXT("%s: %d item events, %u bumps"), File, A.EventTrail.Num() / 4, A.Karts[0].BumpEventCount + A.Karts[1].BumpEventCount));
	}
	return true;
}

#endif
