#include "Misc/AutomationTest.h"
#include "Sim/KartTrack.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart;

namespace
{
	constexpr EAutomationTestFlags KartTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** The smallest track the loader accepts: a centreline triangle, two waypoints, one checkpoint, one grid slot.
	 *  Extra is spliced into the root object. */
	FString MinimalTrackJson(const FString& Extra = FString())
	{
		return FString(TEXT(R"({
			"schemaVersion": 1,
			"track": { "id": "test", "name": "Test" },
			"samples": { "points": [ { "x": 0, "z": 0 }, { "x": 50, "z": 0 }, { "x": 25, "z": 40 } ] },
			"navigation": { "waypoints": [ { "x": 0, "z": 0 }, { "x": 50, "z": 0 } ] },
			"checkpoints": [ { "id": "start", "x": 0, "z": 0, "isFinish": true } ],
			"startCheckpointIndex": 0,
			"spawnGrid": [ { "x": 1, "z": 2, "yaw": 0.5 } ])")) + Extra + TEXT("}");
	}
}

// The loader's contract: fields a file leaves out keep FTrack's defaults, malformed entries are skipped, and a
// track the sim can't run is rejected with a reason instead of crashing later.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTrackJsonDefaultsTest, "MovecadeKart.Sim.TrackJson.Defaults", KartTestFlags)

bool FKartTrackJsonDefaultsTest::RunTest(const FString&)
{
	FTrack Track;
	FString Error;
	if (!TestTrue(TEXT("a minimal track loads"), FTrack::LoadFromJsonString(MinimalTrackJson(), Track, Error)))
	{
		AddError(Error);
		return false;
	}
	const FTrack Defaults;
	TestEqual(TEXT("laps default"), Track.Laps, Defaults.Laps);
	TestEqual(TEXT("kart radius default"), Track.KartRadius, Defaults.KartRadius);
	TestEqual(TEXT("sweep substep default"), Track.SweepSubstep, Defaults.SweepSubstep);
	TestEqual(TEXT("boost multiplier default"), Track.BoostMultiplier, Defaults.BoostMultiplier);
	TestEqual(TEXT("recovery distance default"), Track.RecoveryDistance, Defaults.RecoveryDistance);
	TestEqual(TEXT("checkpoint radius default"), Track.Checkpoints[0].Radius, FCheckpoint().Radius);
	TestEqual(TEXT("centre segments built"), Track.CentreSegments.Num(), Track.Centre.Num());
	TestTrue(TEXT("grid pose read"), Track.Grid[0].Position == FVector2D(1.0, 2.0) && Track.Grid[0].Yaw == 0.5);
	TestFalse(TEXT("no dirt grid"), Track.DirtGrid.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTrackJsonRejectsTest, "MovecadeKart.Sim.TrackJson.Rejects", KartTestFlags)

bool FKartTrackJsonRejectsTest::RunTest(const FString&)
{
	struct FCase
	{
		const TCHAR* What;
		FString Json;
	};
	const FCase Cases[] = {
		{ TEXT("not JSON"), TEXT("{ nope") },
		{ TEXT("wrong schema"), MinimalTrackJson().Replace(TEXT("\"schemaVersion\": 1"), TEXT("\"schemaVersion\": 2")) },
		{ TEXT("no header"), MinimalTrackJson().Replace(TEXT("\"track\":"), TEXT("\"untrack\":")) },
		{ TEXT("start checkpoint out of range"), MinimalTrackJson().Replace(TEXT("\"startCheckpointIndex\": 0"), TEXT("\"startCheckpointIndex\": 5")) },
		{ TEXT("zero sweep substep"), MinimalTrackJson(TEXT(R"(, "collision": { "substepM": 0 })")) },
		{ TEXT("no laps"), MinimalTrackJson().Replace(TEXT("\"name\": \"Test\""), TEXT("\"name\": \"Test\", \"laps\": 0")) },
	};
	for (const FCase& Case : Cases)
	{
		FTrack Track;
		FString Error;
		TestFalse(FString::Printf(TEXT("%s is rejected"), Case.What), FTrack::LoadFromJsonString(Case.Json, Track, Error));
		TestFalse(FString::Printf(TEXT("%s says why"), Case.What), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartTrackJsonMalformedTest, "MovecadeKart.Sim.TrackJson.SkipsMalformed", KartTestFlags)

bool FKartTrackJsonMalformedTest::RunTest(const FString&)
{
	// One good mesh wall segment between a too-short entry, a string and a zero-length segment; a dirt grid with
	// one in-range run and one out-of-range run; and a dirt grid too large to be real is ignored with a warning.
	const FString Json = MinimalTrackJson(TEXT(R"(, "collision": {
		"walls": [ { "segments": [ [1, 2, 3], "bad", [0, 0, 0, 0, 1, 0], [0, 0, 10, 0, 0, 1] ] } ],
		"dirtGrid": { "cellM": 1, "minX": 0, "minZ": 0, "cols": 4, "rows": 2, "runs": [ [1, 1, 99], [7, 0, 3], "x" ] } })"));
	FTrack Track;
	FString Error;
	if (!TestTrue(TEXT("loads despite bad entries"), FTrack::LoadFromJsonString(Json, Track, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("only the good wall segment"), Track.Walls.Segments().Num(), 1);
	TestTrue(TEXT("mesh walls push radially"), Track.bRadialWallPush);
	TestTrue(TEXT("dirt run clamped to the grid"), Track.DirtGrid.IsDirt(FVector2D(3.5, 1.5)));
	TestFalse(TEXT("cells outside the run stay clean"), Track.DirtGrid.IsDirt(FVector2D(0.5, 1.5)));
	TestFalse(TEXT("out-of-range rows are skipped"), Track.DirtGrid.IsDirt(FVector2D(0.5, 0.5)));

	AddExpectedError(TEXT("Ignoring a"), EAutomationExpectedErrorFlags::Contains, 1);
	const FString Huge = MinimalTrackJson(TEXT(R"(, "collision": { "dirtGrid": { "cellM": 1, "cols": 1e9, "rows": 1e9, "runs": [] } })"));
	FTrack HugeTrack;
	TestTrue(TEXT("a corrupt dirt grid doesn't fail the track"), FTrack::LoadFromJsonString(Huge, HugeTrack, Error));
	TestFalse(TEXT("the corrupt dirt grid is ignored"), HugeTrack.DirtGrid.IsValid());
	return true;
}

#endif
