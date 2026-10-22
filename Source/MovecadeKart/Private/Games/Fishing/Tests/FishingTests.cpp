// Golden tests for Fishing Frenzy's rules and motion detector.
#include "Games/Fishing/FishingGameMode.h"
#include "Games/Fishing/FishingMotion.h"
#include "Games/Fishing/FishingRules.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Fishing;

namespace
{
	/** Captured from the first run of these rules: any change to the RNG, the odds, the fight or the scoring shows here. */
	constexpr uint32 GoldenRng[3] = { 2693262067u, 11749833u, 2265367787u };
	constexpr int32 GoldenFarScore = 80829;
	constexpr int32 GoldenFarCatches = 5;
	constexpr int32 GoldenNearScore = 31766;
	constexpr int32 GoldenRecord = 27260;

	FMatch Started(const TArray<FString>& Ids = { TEXT("p1") }, uint32 Seed = 7)
	{
		TArray<FSeed> Seeds;
		for (const FString& Id : Ids)
		{
			Seeds.Add({ Id, Id.ToUpper() });
		}
		FMatch Match = CreateMatch(Seeds, Seed);
		while (Match.Phase == EMatchPhase::Countdown)
		{
			StepMatch(Match, 100.0);
		}
		return Match;
	}

	/** A bite of Species at Distance, hooked at once. */
	FAngler& Hooked(FMatch& M, const TCHAR* Species, double DistanceM, const FString& Id = TEXT("p1"))
	{
		FAngler& A = *M.Find(Id);
		A.State = EAnglerState::Bite;
		A.StateMs = 0.0;
		A.BiteSpecies = SpeciesIndex(Species);
		A.LureM = DistanceM;
		A.CastM = DistanceM;
		A.WindowMs = 500.0;
		Hook(M, Id);
		return A;
	}

	/** Steps until the angler leaves Fighting (or MaxMs), holding the reel at a tension target (< 0: a fixed reel of -Target). */
	void Fight(FMatch& M, FAngler& A, double Target, double MaxMs = 120000.0)
	{
		for (double T = 0.0; T < MaxMs && A.State == EAnglerState::Fighting; T += 10.0)
		{
			SetReel(M, A.PlayerId, Target < 0.0 ? -Target : ReelForTension(A.Fight.Pull, Target));
			StepMatch(M, 10.0);
		}
	}

	bool HasEffect(const FMatch& M, EEffectKind Kind)
	{
		return M.Effects.ContainsByPredicate([Kind](const FEffect& E) { return E.Kind == Kind; });
	}

	/** A perfect angler for a whole round: full casts, hooks on the bite, holds the tension mid-green. */
	FMatch PlayRound(uint32 Seed, double Power, double Target)
	{
		FMatch M = Started({ TEXT("p1"), TEXT("p2") }, Seed);
		while (M.Phase == EMatchPhase::Playing)
		{
			for (FAngler& A : M.Anglers)
			{
				switch (A.State)
				{
				case EAnglerState::Idle: Cast(M, A.PlayerId, A.LaneIndex == 0 ? Power : 0.3); break;
				case EAnglerState::Bite: Hook(M, A.PlayerId); break;
				case EAnglerState::Fighting: SetReel(M, A.PlayerId, ReelForTension(A.Fight.Pull, Target)); break;
				default: SetReel(M, A.PlayerId, 0.0); break;
				}
			}
			StepMatch(M, 10.0);
		}
		return M;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishingRulesTest, "MovecadeKart.Fishing.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFishingRulesTest::RunTest(const FString& Parameters)
{
	{
		FMatch M = CreateMatch({ { TEXT("p1"), TEXT("P1") } }, 1);
		TestFalse(TEXT("no cast in the countdown"), Cast(M, TEXT("p1"), 1.0));
		for (int32 i = 0; i < 29; ++i) StepMatch(M, 100.0);
		TestTrue(TEXT("counting"), M.Phase == EMatchPhase::Countdown);
		StepMatch(M, 100.0);
		TestTrue(TEXT("playing on the 30th"), M.Phase == EMatchPhase::Playing);
	}
	{
		TestEqual(TEXT("weakest cast"), CastDistance(0.0), MinCastM);
		TestEqual(TEXT("strongest cast"), CastDistance(1.0), MaxCastM);
		TestEqual(TEXT("half"), CastDistance(0.5), 17.0);
		TestTrue(TEXT("bands"), BandAt(5.0) == EBand::Shallows && BandAt(15.0) == EBand::Reeds && BandAt(22.0) == EBand::Deep && BandAt(28.0) == EBand::Far);
		TestTrue(TEXT("combo"), ComboMultiplier(1) == 1.0 && FMath::IsNearlyEqual(ComboMultiplier(3), 1.3) && FMath::IsNearlyEqual(ComboMultiplier(99), 1.6));
		TestEqual(TEXT("grams"), FormatWeight(850), FString(TEXT("850 g")));
		TestEqual(TEXT("kilos"), FormatWeight(2345), FString(TEXT("2.35 kg")));
	}
	{
		// A full cast flies 1.2 s, lands 30 m out in the far water and waits; a hook now is too early.
		FMatch M = Started();
		TestTrue(TEXT("cast"), Cast(M, TEXT("p1"), 1.0));
		TestFalse(TEXT("one line at a time"), Cast(M, TEXT("p1"), 1.0));
		FAngler& A = *M.Find(TEXT("p1"));
		for (int32 i = 0; i < 119; ++i) StepMatch(M, 10.0);
		TestTrue(TEXT("in the air"), A.State == EAnglerState::Casting && A.LureM > 25.0 && A.LureM < 30.0);
		StepMatch(M, 10.0);
		TestTrue(TEXT("lands far"), A.State == EAnglerState::Waiting && A.LureM == 30.0 && BandAt(A.LureM) == EBand::Far);
		const FEffect* Splash = M.Effects.FindByPredicate([](const FEffect& E) { return E.Kind == EEffectKind::Splash; });
		TestTrue(TEXT("a splash in the far water"), Splash && Splash->Label == TEXT("Far water"));
		const double Wait = A.BiteInMs;
		TestFalse(TEXT("too early"), Hook(M, TEXT("p1")));
		TestTrue(TEXT("spooked: a new wait"), A.State == EAnglerState::Waiting && HasEffect(M, EEffectKind::TooEarly) && A.BiteInMs != Wait && A.NibblesLeft == 0);
		int32 Steps = 0;
		while (A.State == EAnglerState::Waiting && Steps++ < 2000) StepMatch(M, 10.0);
		TestTrue(TEXT("then a bite"), A.State == EAnglerState::Bite && HasEffect(M, EEffectKind::Bite));
		TestTrue(TEXT("hooked"), Hook(M, TEXT("p1")) && A.State == EAnglerState::Fighting && A.Fight.DistanceM == 30.0);
	}
	{
		// The bobber stays down for the species' window; after it the bait is gone and so is the combo.
		FMatch M = Started();
		FAngler& A = *M.Find(TEXT("p1"));
		A.Combo = 3;
		A.State = EAnglerState::Bite;
		A.BiteSpecies = SpeciesIndex(TEXT("pike"));
		A.WindowMs = Species()[A.BiteSpecies].HookWindowMs;
		for (int32 i = 0; i < 61; ++i) StepMatch(M, 10.0);
		TestTrue(TEXT("still biting at 610 ms"), A.State == EAnglerState::Bite);
		StepMatch(M, 10.0);
		TestTrue(TEXT("stolen at 620"), A.State == EAnglerState::Lost && A.LastLoss == ELoss::StolenBait && A.Combo == 0 && HasEffect(M, EEffectKind::StolenBait));
		for (int32 i = 0; i < 150; ++i) StepMatch(M, 10.0);
		TestTrue(TEXT("ready again"), A.State == EAnglerState::Idle);
	}
	{
		// A perch in the green comes in; its weight is the score at combo 1.
		FMatch M = Started();
		FAngler& A = Hooked(M, TEXT("perch"), 8.0);
		const int32 W = A.Fight.WeightG;
		TestTrue(TEXT("perch weight in range"), W >= 150 && W <= 700);
		Fight(M, A, 0.55);
		TestTrue(TEXT("landed"), A.State == EAnglerState::Landed && A.Catches == 1 && A.Score == W && A.TotalG == W && A.BiggestG == W);
		TestTrue(TEXT("a catch effect, no record for a tiddler"), HasEffect(M, EEffectKind::Catch) && !HasEffect(M, EEffectKind::Record) && !A.LastCatch.bRecord);
		TestTrue(TEXT("held up, then idle"), A.LastCatch.Species == SpeciesIndex(TEXT("perch")));
		for (int32 i = 0; i < 261; ++i) StepMatch(M, 10.0);
		TestTrue(TEXT("idle after the reveal"), A.State == EAnglerState::Idle);
	}
	{
		// Cranking flat out on a pike pins the line in the red: it snaps.
		FMatch M = Started();
		FAngler& A = Hooked(M, TEXT("pike"), 22.0);
		Fight(M, A, -MaxReel, 10000.0);
		TestTrue(TEXT("snapped"), A.State == EAnglerState::Lost && A.LastLoss == ELoss::Snap && HasEffect(M, EEffectKind::Snap));
		TestTrue(TEXT("quickly"), A.StateMs <= 10.0 && A.Losses == 1);
	}
	{
		// Not reeling a perch leaves the line slack: it shakes the hook after two seconds.
		FMatch M = Started();
		FAngler& A = Hooked(M, TEXT("perch"), 10.0);
		Fight(M, A, -0.0, 10000.0);
		TestTrue(TEXT("escaped"), A.State == EAnglerState::Lost && A.LastLoss == ELoss::Escape);
		TestTrue(TEXT("after the slack window"), A.Fight.FightMs >= EscapeSlackMs && A.Fight.FightMs < EscapeSlackMs + 900.0);
	}
	{
		// Every species can be landed by holding the tension mid-green, bursts and all.
		for (const FSpecies& S : Species())
		{
			FMatch M = Started();
			FAngler& A = Hooked(M, S.Id, S.Kind == EKind::Fish && S.Odds[0] > 0.0 ? 9.0 : 28.0);
			Fight(M, A, 0.55);
			TestTrue(FString::Printf(TEXT("%s lands in the green"), S.Id), A.State == EAnglerState::Landed);
		}
	}
	{
		// A pike runs: the warning, then the pull; easing off rides it out where cranking snaps.
		FMatch M = Started();
		FAngler& A = Hooked(M, TEXT("pike"), 24.0);
		bool bWarned = false, bRan = false;
		for (double T = 0.0; T < 60000.0 && A.State == EAnglerState::Fighting; T += 10.0)
		{
			const bool bEase = A.Fight.WarnMs > 0.0 || A.Fight.BurstMs > 0.0;
			bWarned |= A.Fight.WarnMs > 0.0;
			bRan |= A.Fight.BurstMs > 0.0;
			SetReel(M, TEXT("p1"), bEase ? 0.0 : ReelForTension(A.Fight.Pull, 0.6));
			StepMatch(M, 10.0);
		}
		TestTrue(TEXT("warned and ran"), bWarned && bRan && A.Fight.Bursts > 0);
		TestTrue(TEXT("landed by easing off"), A.State == EAnglerState::Landed);
	}
	{
		// The combo: x1, x1.15, x1.3; junk keeps it and scores nothing; a loss ends it.
		FMatch M = Started();
		FAngler& A = *M.Find(TEXT("p1"));
		int32 Expected = 0;
		const double Mult[] = { 1.0, 1.15, 1.3 };
		for (int32 i = 0; i < 3; ++i)
		{
			Hooked(M, TEXT("trout"), 15.0);
			A.Fight.WeightG = 2000;
			Fight(M, A, 0.55);
			Expected += FMath::FloorToInt32(2000 * Mult[i] + 0.5);
			TestTrue(FString::Printf(TEXT("catch %d"), i + 1), A.State == EAnglerState::Landed && A.Combo == i + 1 && A.Score == Expected);
			A.State = EAnglerState::Idle;
		}
		TestTrue(TEXT("the first big fish is the record"), M.RecordG == 2000 && M.RecordHolder == TEXT("p1"));
		Hooked(M, TEXT("duck"), 5.0);
		Fight(M, A, 0.55);
		TestTrue(TEXT("a duck: no score, combo kept"), A.State == EAnglerState::Landed && A.Score == Expected && A.Combo == 3 && A.JunkCount == 1 && HasEffect(M, EEffectKind::Junk));
		A.State = EAnglerState::Idle;
		Hooked(M, TEXT("trout"), 15.0);
		A.Fight.WeightG = 2500;
		Fight(M, A, 0.55);
		TestTrue(TEXT("x1.45 and a new record"), A.Score == Expected + FMath::FloorToInt32(2500 * 1.45 + 0.5) && A.LastCatch.bRecord && M.RecordG == 2500);
		A.State = EAnglerState::Idle;
		Hooked(M, TEXT("pike"), 20.0);
		Fight(M, A, -MaxReel, 10000.0);
		TestTrue(TEXT("a snap ends the combo"), A.Combo == 0 && A.BestCombo == 4);
	}
	{
		// Reeling while waiting brings the lure home.
		FMatch M = Started();
		FAngler& A = *M.Find(TEXT("p1"));
		Cast(M, TEXT("p1"), 0.0);
		for (int32 i = 0; i < 60; ++i) StepMatch(M, 10.0);
		TestTrue(TEXT("waiting 4 m out"), A.State == EAnglerState::Waiting && A.LureM == MinCastM);
		A.BiteInMs = 99999.0;
		SetReel(M, TEXT("p1"), 1.0);
		for (int32 i = 0; i < 100 && A.State == EAnglerState::Waiting; ++i) StepMatch(M, 10.0);
		TestTrue(TEXT("retrieved"), A.State == EAnglerState::Idle && HasEffect(M, EEffectKind::Retrieved));
	}
	{
		// What bites depends on the band: no giants in the shallows; the far water holds the trophy.
		FAngler A;
		A.Rng.State = 99;
		TMap<int32, int32> Shallow, Far;
		for (int32 i = 0; i < 4000; ++i)
		{
			A.LureM = 6.0;
			A.Rng.Next();
			FMatch M;
			M.Phase = EMatchPhase::Playing;
			M.Anglers.Add(A);
			FAngler& B = M.Anglers[0];
			B.State = EAnglerState::Waiting;
			B.BiteInMs = 1.0;
			StepMatch(M, 10.0);
			Shallow.FindOrAdd(B.BiteSpecies)++;
			B.State = EAnglerState::Waiting;
			B.LureM = 29.0;
			B.BiteInMs = 1.0;
			StepMatch(M, 10.0);
			Far.FindOrAdd(B.BiteSpecies)++;
			A.Rng = B.Rng;
		}
		TestTrue(TEXT("no pike, catfish, sturgeon or goldie in the shallows"), !Shallow.Contains(SpeciesIndex(TEXT("pike"))) && !Shallow.Contains(SpeciesIndex(TEXT("catfish")))
			&& !Shallow.Contains(SpeciesIndex(TEXT("sturgeon"))) && !Shallow.Contains(SpeciesIndex(TEXT("goldie"))));
		TestTrue(TEXT("no perch far out"), !Far.Contains(SpeciesIndex(TEXT("perch"))) && !Far.Contains(SpeciesIndex(TEXT("bluegill"))));
		const int32 Goldies = Far.FindRef(SpeciesIndex(TEXT("goldie")));
		TestTrue(FString::Printf(TEXT("the trophy is rare far out (%d in 4000)"), Goldies), Goldies > 80 && Goldies < 320);
	}
	{
		// The clock: the round ends at 3:00 and the best score wins.
		FMatch M = Started({ TEXT("p1"), TEXT("p2") });
		M.Find(TEXT("p2"))->Score = 10;
		// The countdown's last step already played 100 ms.
		for (int32 i = 0; i < 1798; ++i) StepMatch(M, 100.0);
		TestTrue(TEXT("still on"), M.Phase == EMatchPhase::Playing);
		StepMatch(M, 100.0);
		TestTrue(TEXT("ended"), M.Phase == EMatchPhase::Ended && M.WinnerIds.Num() == 1 && M.WinnerIds[0] == TEXT("p2"));
		TestFalse(TEXT("no casts after the horn"), Cast(M, TEXT("p1"), 1.0));
	}
	{
		// Fights substep at 10 ms: the same fight stepped at 10 or 50 ms ends the same.
		auto Run = [](double Dt)
		{
			FMatch M = Started();
			FAngler& A = Hooked(M, TEXT("carp"), 18.0);
			for (double T = 0.0; T < 3000.0; T += Dt)
			{
				SetReel(M, TEXT("p1"), 0.5);
				StepMatch(M, Dt);
			}
			return A.Fight;
		};
		const FFight A = Run(10.0), B = Run(50.0);
		TestTrue(TEXT("dt invariant"), FMath::IsNearlyEqual(A.DistanceM, B.DistanceM, 1e-9) && FMath::IsNearlyEqual(A.Tension, B.Tension, 1e-9) && A.Bursts == B.Bursts);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishingGoldenTest, "MovecadeKart.Fishing.Golden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFishingGoldenTest::RunTest(const FString& Parameters)
{
	{
		FRng R;
		R.State = 1;
		const uint32 A = R.Next(), B = R.Next(), C = R.Next();
		AddInfo(FString::Printf(TEXT("rng %u %u %u"), A, B, C));
		TestTrue(TEXT("mulberry32 from 1"), A == GoldenRng[0] && B == GoldenRng[1] && C == GoldenRng[2]);
	}
	const FMatch A = PlayRound(1234, 1.0, 0.55);
	const FMatch B = PlayRound(1234, 1.0, 0.55);
	const FMatch C = PlayRound(4321, 1.0, 0.55);
	const FAngler& Far = A.Anglers[0];
	const FAngler& Near = A.Anglers[1];
	AddInfo(FString::Printf(TEXT("seed 1234: far %d g (%d catches, %d junk, %d lost, biggest %d sp %d), near %d g (%d catches, %d junk); record %d"),
		Far.Score, Far.Catches, Far.JunkCount, Far.Losses, Far.BiggestG, Far.BiggestSpecies, Near.Score, Near.Catches, Near.JunkCount, A.RecordG));
	TestTrue(TEXT("same seed, same round"), Far.Score == B.Anglers[0].Score && Near.Score == B.Anglers[1].Score && Far.Catches == B.Anglers[0].Catches);
	TestTrue(TEXT("another seed, another round"), Far.Score != C.Anglers[0].Score || Near.Score != C.Anglers[1].Score);
	TestTrue(TEXT("the far caster out-weighs the near one"), Far.Score > Near.Score);
	TestTrue(TEXT("a perfect angler never loses a fish"), Far.Losses == 0 && Near.Losses == 0);
	TestEqual(TEXT("golden far score"), Far.Score, GoldenFarScore);
	TestEqual(TEXT("golden far catches"), Far.Catches, GoldenFarCatches);
	TestEqual(TEXT("golden near score"), Near.Score, GoldenNearScore);
	TestEqual(TEXT("golden record"), A.RecordG, GoldenRecord);
	{
		// The motion pad prompts: flick to cast and hook, swing to reel.
		FAngler P;
		P.PlayerId = TEXT("p1");
		const FString Cast = AFishingGameMode::PromptJson(P, FLinearColor::Red);
		P.State = EAnglerState::Fighting;
		const FString Reel = AFishingGameMode::PromptJson(P, FLinearColor::Red);
		TestTrue(TEXT("cast prompt"), Cast.Contains(TEXT("\"title\":\"CAST!\"")) && Cast.Contains(TEXT("\"mode\":\"flick\"")) && Cast.Contains(TEXT("\"a\":\"HOLD TO CAST\"")) && Cast.Contains(TEXT("#ff0000")));
		TestTrue(TEXT("reel prompt"), Reel.Contains(TEXT("\"mode\":\"swing\"")) && Reel.Contains(TEXT("REEL IT IN!")));
		AddInfo(Cast);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFishingMotionTest, "MovecadeKart.Fishing.Motion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FMotionSample Gyro(double X, double Y, double Z, double DtMs = 10.0)
	{
		FMotionSample S;
		S.Gyro = FVector3d(X, Y, Z);
		S.Accel = FVector3d::ZeroVector;
		S.DtMs = DtMs;
		return S;
	}

	/** A half-sine pitch flick of Peak deg/s over Ms, then stillness; the flicks it produced. */
	TArray<FFlick> Flick(FFishingMotion& D, double Peak, double Ms, double Hz = 100.0, double StillMs = 300.0)
	{
		TArray<FFlick> Out;
		const double Dt = 1000.0 / Hz;
		for (double T = 0.0; T < Ms + StillMs; T += Dt)
		{
			const double V = T < Ms ? Peak * FMath::Sin(UE_DOUBLE_PI * T / Ms) : 0.0;
			if (TOptional<FFlick> F = D.Feed(Gyro(V, 0.0, 0.0, Dt)))
			{
				Out.Add(*F);
			}
		}
		return Out;
	}
}

bool FFishingMotionTest::RunTest(const FString& Parameters)
{
	{
		FFishingMotion D;
		FRandomStream R(3);
		int32 Flicks = 0;
		for (int32 i = 0; i < 300; ++i)
		{
			Flicks += D.Feed(Gyro(R.FRandRange(-30.f, 30.f), R.FRandRange(-20.f, 20.f), R.FRandRange(-20.f, 20.f))).IsSet() ? 1 : 0;
		}
		TestEqual(TEXT("a resting hand: nothing"), Flicks, 0);
		TestTrue(TEXT("and no reel"), D.Reel() < 0.05);
	}
	{
		FFishingMotion D;
		const TArray<FFlick> Strong = Flick(D, -950.0, 200.0);
		TestTrue(TEXT("one forward flick"), Strong.Num() == 1 && Strong[0].Direction == EFlick::Forward);
		TestTrue(FString::Printf(TEXT("strong: power %.2f"), Strong.Num() ? Strong[0].Power : -1.0), Strong.Num() == 1 && Strong[0].Power > 0.7);
		FFishingMotion E;
		const TArray<FFlick> Weak = Flick(E, -360.0, 200.0);
		TestTrue(FString::Printf(TEXT("weak: power %.2f"), Weak.Num() ? Weak[0].Power : -1.0), Weak.Num() == 1 && Weak[0].Power < 0.3 && Weak[0].Power < Strong[0].Power);
		FFishingMotion G;
		const TArray<FFlick> Soft = Flick(G, -200.0, 200.0);
		TestEqual(TEXT("a sway is not a flick"), Soft.Num(), 0);
	}
	{
		FFishingMotion D;
		const TArray<FFlick> Up = Flick(D, 700.0, 160.0);
		TestTrue(TEXT("the hook: a backward jerk"), Up.Num() == 1 && Up[0].Direction == EFlick::Back);
	}
	{
		// The wind-up and the cast: a back swing then a forward flick 600 ms later read as two flicks.
		FFishingMotion D;
		TArray<FFlick> All = Flick(D, 400.0, 250.0, 100.0, 400.0);
		All.Append(Flick(D, -900.0, 200.0));
		TestTrue(TEXT("back, then forward"), All.Num() == 2 && All[0].Direction == EFlick::Back && All[1].Direction == EFlick::Forward);
		// Follow-through straight after a flick is ignored.
		FFishingMotion E;
		TArray<FFlick> Twice = Flick(E, -900.0, 200.0, 100.0, 0.0);
		Twice.Append(Flick(E, 600.0, 150.0, 100.0, 0.0));
		TestEqual(TEXT("cooldown"), Twice.Num(), 1);
	}
	{
		// The same flick at 60 and 100 Hz.
		FFishingMotion A, B;
		const TArray<FFlick> F60 = Flick(A, -800.0, 200.0, 60.0);
		const TArray<FFlick> F100 = Flick(B, -800.0, 200.0, 100.0);
		TestTrue(TEXT("rate independent"), F60.Num() == 1 && F100.Num() == 1 && FMath::Abs(F60[0].Power - F100[0].Power) < 0.1);
	}
	{
		// Rolling the wrist at 1.2 rev/s reels at about three quarters, and stops soon after the hand does.
		FFishingMotion D;
		int32 Flicks = 0;
		for (int32 i = 0; i < 100; ++i) Flicks += D.Feed(Gyro(25.0, 432.0, 0.0)).IsSet() ? 1 : 0;
		const double Rolling = D.Reel();
		TestTrue(FString::Printf(TEXT("roll reel %.2f"), Rolling), FMath::IsNearlyEqual(Rolling, (1.2 - 0.12) / 1.48, 0.05));
		TestEqual(TEXT("rolling never flicks"), Flicks, 0);
		for (int32 i = 0; i < 60; ++i) D.Feed(Gyro(0.0, 0.0, 0.0));
		TestTrue(TEXT("stops"), D.Reel() < 0.05);
		FFishingMotion Fast;
		for (int32 i = 0; i < 100; ++i) Fast.Feed(Gyro(0.0, -1440.0, 0.0));
		TestEqual(TEXT("capped"), Fast.Reel(), 1.25);
	}
	{
		// Circling the phone (no gyro roll): the sideways acceleration going round once a second.
		FFishingMotion D;
		for (int32 i = 0; i < 150; ++i)
		{
			FMotionSample S;
			const double A = 2.0 * UE_DOUBLE_PI * i * 0.01;
			S.Gyro = FVector3d::ZeroVector;
			S.Accel = FVector3d(3.0 * FMath::Sin(A), 0.0, 3.0 * FMath::Cos(A));
			S.DtMs = 10.0;
			D.Feed(S);
		}
		TestTrue(FString::Printf(TEXT("circle reel %.2f"), D.Reel()), FMath::IsNearlyEqual(D.Reel(), (1.0 - 0.12) / 1.48, 0.05));
	}
	{
		// No gyro: the acceleration envelope flicks, direction unknown.
		FFishingMotion D;
		int32 N = 0;
		EFlick Dir = EFlick::Forward;
		for (double T = 0.0; T < 500.0; T += 10.0)
		{
			FMotionSample S;
			S.Accel = FVector3d(0.0, 0.0, T < 180.0 ? -26.0 * FMath::Sin(UE_DOUBLE_PI * T / 180.0) : 0.0);
			S.DtMs = 10.0;
			if (TOptional<FFlick> F = D.Feed(S))
			{
				++N;
				Dir = F->Direction;
			}
		}
		TestTrue(TEXT("accel-only flick"), N == 1 && Dir == EFlick::Unknown);
	}
	return true;
}

#endif
