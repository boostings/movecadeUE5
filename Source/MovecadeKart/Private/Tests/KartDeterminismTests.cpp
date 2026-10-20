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
	/** FNV-1a over raw bytes: a stable, order-sensitive digest of exactly the bits the sim produced. */
	class FTraceHash
	{
	public:
		void Bytes(const void* Data, SIZE_T Size)
		{
			for (const uint8 Byte : TConstArrayView<uint8>(static_cast<const uint8*>(Data), static_cast<int32>(Size)))
			{
				Value = (Value ^ Byte) * 0x100000001b3ull;
			}
		}
		template <typename T> void Pod(const T& V) { static_assert(TIsPODType<T>::Value); Bytes(&V, sizeof(T)); }
		void Vec(const FVector2D& V) { Pod(V.X); Pod(V.Y); }
		void Str(const FString& S) { Bytes(*S, S.Len() * sizeof(TCHAR)); Pod(S.Len()); }

		/** Every field of the kart state, so any change in any result shows up on the step it happens. */
		void Kart(const FKartState& K)
		{
			Vec(K.Pos); Pod(K.Yaw); Pod(K.Speed); Vec(K.Dir);
			Pod(K.Turbo); Pod(K.DriftDirection); Pod(K.DriftPower); Pod(K.bJumpHeld); Pod(K.HopTime);
			Pod(K.BoostMultiplier); Pod(K.BoostTimeRemaining); Str(K.ActiveBoost);
			Pod(K.RubberBandBoost); Pod(K.SlipstreamBoost); Pod(K.SlipstreamCharge);
			Pod(K.bCollidingBarrier); Pod(K.CollisionIntensity); Pod(K.BarrierEvent); Pod(K.bBarrierEventSustained);
			Pod(K.LastBarrierEventTime); Pod(K.BumpEvent); Pod(K.BarrierEventCount); Pod(K.BumpEventCount); Vec(K.ImpactNormal);
			Pod(K.bOnDirt); Pod(K.bWrongWay); Pod(K.bHitWallThisStep); Pod(K.Frame); Pod(K.LastRadialFrame);
			Pod(K.LastWrongWayFrame); Pod(K.LastTerrainFrame); Vec(K.RadialPush); Pod(K.RadialStrength);
			Pod(K.bRadialCritical); Pod(K.NearestWall); Vec(K.LastCollisionNormal);
			for (const double T : K.PadLastBoostTime) { Pod(T); }
			Pod(K.Time); Pod(K.NextCheckpoint); Pod(K.bWasInsideNext); Pod(K.LapsCompleted); Pod(K.ProgressPercent);
			Pod(K.bFinished); Pod(K.FinishTime);
			Pod(K.OffTrackSince); Vec(K.AnchorPos); Pod(K.AnchorSince); Pod(K.AnchorSeen); Pod(K.WatchIndex);
			Pod(K.WatchSince); Pod(K.WatchSeen); Pod(K.TeleportCount); Str(K.LastRespawnReason);
			Pod(K.InputTurn); Pod(K.SlipYaw); Pod(K.SteerVisual); Pod(K.BodyYaw); Pod(K.GroupYaw); Pod(K.Lean);
			Pod(K.HopOffset); Str(K.LastDriftBoost); Pod(K.DriftBoostCount);
		}

		uint64 Value = 0xcbf29ce484222325ull;
	};

	/** What a race ends on: every kart's pose, speed and progress, and the item world's event trail. */
	struct FRaceOutcome
	{
		TArray<FKartState> Karts;
		TArray<int32> EventTrail;
		/** Digest of every kart's full state after every step, plus every input and item event. */
		uint64 Trace = 0;
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
		FTraceHash Trace;
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
				Trace.Pod(Input.SteeringX);
				Trace.Pod(Input.bLeft); Trace.Pod(Input.bRight); Trace.Pod(Input.bForward);
				Trace.Pod(Input.bBackward); Trace.Pod(Input.bJump); Trace.Pod(Input.bUseItem);
				Trace.Kart(Karts[i].State());
				Racers[i].Laps = Karts[i].State().LapsCompleted;
				Racers[i].Progress = Karts[i].State().ProgressPercent;
			}
			Events.Reset();
			Items.Step(Racers, Now, Events);
			for (const FKartItemEvent& E : Events)
			{
				Out.EventTrail.Append({ Step, static_cast<int32>(E.Type), E.Seat, static_cast<int32>(E.Item) });
				Trace.Pod(E.Type); Trace.Pod(E.Seat); Trace.Pod(E.Other); Trace.Pod(E.Item);
				Trace.Vec(E.Position); Trace.Pod(E.Radius);
			}
		}
		Out.Trace = Trace.Value;
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
		TestEqual(FString::Printf(TEXT("%s: every step of every kart is bit-identical"), File), A.Trace, B.Trace);
		TestTrue(FString::Printf(TEXT("%s: the pack interacted (items happened)"), File), A.EventTrail.Num() > 0);

		// A different item seed gives a different race, so the check above isn't vacuous.
		const FRaceOutcome C = RunPack(Track, 7);
		TestTrue(FString::Printf(TEXT("%s: the seed matters"), File), C.EventTrail != A.EventTrail);
		AddInfo(FString::Printf(TEXT("%s: %d item events, %u bumps"), File, A.EventTrail.Num() / 4, A.Karts[0].BumpEventCount + A.Karts[1].BumpEventCount));
	}
	return true;
}

// Prints a digest of every kart's full state on every step of the pack race. The digest moves when any sim result
// changes by a single bit, so a refactor or optimisation that claims "no behaviour change" can prove it: record the
// fingerprint before, compare after. (Not pinned to a constant: compilers may fuse multiply-adds differently per
// platform, so the value is only comparable on one toolchain.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartSimFingerprintTest, "MovecadeKart.Sim.Fingerprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartSimFingerprintTest::RunTest(const FString& Parameters)
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
		for (const int32 Seed : { 42, 7 })
		{
			AddInfo(FString::Printf(TEXT("KartSimFingerprint %s seed=%d trace=%016llx"), File, Seed, RunPack(Track, Seed).Trace));
		}
	}
	return true;
}

#endif
