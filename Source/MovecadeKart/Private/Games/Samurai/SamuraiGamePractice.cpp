// ASamuraiGame: the practice apple, the first round's onboarding. During the intro one apple hangs at the cut's apex
// in the local swordsman's lane; a sweep through it with intent cuts it (the same contact test as a round, judged
// here: the rules never see it), a blade dragged through too slowly says so. Space, Enter or the pad's A skips it,
// and it gives way after a while regardless.

#include "Games/Samurai/SamuraiGame.h"

#include "Components/StaticMeshComponent.h"
#include "Games/Samurai/SamuraiLog.h"
#include "Games/Samurai/SamuraiTuning.h"

using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Samurai::Tuning;

void ASamuraiGame::BeginPractice()
{
	PracticeLane = Lanes.IndexOfByPredicate([](const FLane& Lane) { return Lane.Kind == ESwordKind::Local; });
	if (PracticeLane == INDEX_NONE)
	{
		bPracticeDone = true;
		return;
	}
	FLane& Lane = Lanes[PracticeLane];
	Practice = ESamuraiPractice::Waiting;
	PracticeAt = Clock;
	PracticeWeb = FVector(Lane.LaneX + PracticeOffsetX, TargetHeight(CutApex), TargetDepth);
	Lane.TipGoal = FVector(Lane.LaneX + PracticeTipOffsetX, PracticeTipY, TargetDepth);
}

void ASamuraiGame::EndPractice()
{
	Practice = ESamuraiPractice::None;
	bPracticeDone = true;
}

void ASamuraiGame::UpdatePractice(float Dt)
{
	check(Lanes.IsValidIndex(PracticeLane));
	FLane& Lane = Lanes[PracticeLane];
	const double Age = GetPracticeAge();
	PracticeSpin = FQuat(FVector::UpVector, PracticeSpinRate * Dt) * PracticeSpin;
	const FVector Bob(0.0, PracticeBobAmplitude * FMath::Sin(Clock * PracticeBobRate), 0.0);
	const double Scale = Def(EVariant::Apple).Radius * TargetMeshScale;
	if (Practice == ESamuraiPractice::Cut)
	{
		PracticeMesh->SetVisibility(false);
		PracticeHalo->SetVisibility(false);
		if (Age > PracticeCutHoldSeconds)
		{
			EndPractice();
		}
		return;
	}
	const FVector At = ToWorld(PracticeWeb + Bob);
	const double Pop = Ease::OutBack(FMath::Clamp(Age / PracticePopSeconds, 0.0, 1.0));
	PracticeMesh->SetWorldTransform(FTransform(PracticeSpin, At, FVector(Scale * FMath::Max(MinVisibleScale, Pop))));
	PracticeMesh->SetVisibility(true);
	PracticeHalo->SetWorldTransform(FTransform(FRotator::ZeroRotator, At + FVector(Scale * HaloForwardCm, 0.0, 0.0),
		FVector(Scale * (1.0 + PracticeHaloPulseDepth * FMath::Sin(Clock * PracticeHaloPulseRate)) * Pop)));
	PracticeHalo->SetVisibility(true);
	if (DebugFlags.bPracticeDemo && Age > PracticeDemoFrom && Age < PracticeDemoUntil)
	{
		// The local blade sweeps through the apple by itself.
		const double Sweep = FMath::Clamp((Age - PracticeDemoFrom) / PracticeDemoSweepSeconds, 0.0, 1.0);
		Lane.TipGoal = FVector(Lane.LaneX + PracticeTipOffsetX + Sweep * PracticeDemoAcross, PracticeTipY - Sweep * PracticeDemoDown, TargetDepth);
	}
	if (Lane.bIntent)
	{
		Lane.TrailAt = Clock;
	}
	const bool bTouch = Age > PracticePopSeconds
		&& SweptBladeHits(Lane.Previous.Base, Lane.Previous.Tip, Lane.Blade.Base, Lane.Blade.Tip, PracticeWeb + Bob, Def(EVariant::Apple).Radius * TargetMeshScale);
	if (bTouch && Lane.bIntent)
	{
		CutPracticeApple(Lane, At, Age);
		return;
	}
	if (bTouch && Clock - Lane.HintAt > PracticeHintCooldownSeconds)
	{
		Lane.HintAt = Clock;
		Practice = ESamuraiPractice::TooSlow;
		Note(ToWorld(PracticeWeb + FVector(0.0, PracticeNoteLift, 0.0)), NSLOCTEXT("Samurai", "TooSlow", "Swing faster!"), Palette::Gold,
			TooSlowNoteLife, PracticeTooSlowNoteScale);
	}
	if (Age > PracticeTimeoutSeconds)
	{
		UE_LOG(LogSamurai, Log, TEXT("Practice timed out"));
		EndPractice();
		PracticeMesh->SetVisibility(false);
		PracticeHalo->SetVisibility(false);
	}
}

void ASamuraiGame::CutPracticeApple(FLane& Lane, const FVector& At, double Age)
{
	FTarget Apple;
	Apple.Variant = EVariant::Apple;
	Apple.Radius = Def(EVariant::Apple).Radius;
	SplitTarget(Apple, At, PracticeSpin, PracticeLane, /*bPerfect*/ true);
	Cues.Play(this, TEXT("samurai.slice"), 1.f, 1.f);
	Cues.Play(this, TEXT("samurai.hit"), 1.f);
	Flash(Palette::CutFlash, PracticeFlashPower, PracticeFlashFringe);
	FrozenUntil = Clock + PracticeHitStopSeconds;
	Punch = PracticePunch;
	SlowMo(PracticeSlowMoSeconds);
	Note(ToWorld(PracticeWeb + FVector(0.0, PracticeNoteLift, 0.0)), NSLOCTEXT("Samurai", "PracticeCut", "Clean cut!"), Palette::Jade,
		PracticeCutNoteLife, PracticeCutNoteScale);
	UE_LOG(LogSamurai, Log, TEXT("Practice apple cut after %.2f s"), Age);
	Practice = ESamuraiPractice::Cut;
	PracticeAt = Clock;
	Lane.LastCutAt = Clock;
	Lane.CutPower = 1.0;
}
