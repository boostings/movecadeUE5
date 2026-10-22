// ASumoGame's flow: setup, the stages, the frame loop around the fixed step, rematch and IArcadeGame.

#include "Games/Sumo/SumoGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "FX/KartFx.h"
#include "GameFramework/PlayerController.h"
#include "Games/Sumo/SSumoHud.h"
#include "Games/Sumo/SumoArena.h"
#include "Games/Sumo/SumoPresentation.h"

using namespace MovecadeKart::Sumo;
using namespace MovecadeKart::Sumo::Presentation;
namespace Fx = MovecadeKart::Fx;

namespace
{
	/** A frame longer than this (a hitch, a breakpoint) only advances the game this much. */
	constexpr float MaxFrameSeconds = 0.1f;
	/** Fixed steps caught up per frame at most. */
	constexpr int32 MaxStepsPerFrame = 8;
	/** The HUD's z-order over the viewport (over the game, under the arcade overlay). */
	constexpr int32 HudZOrder = 10;
	/** The 3-2-1: the countdown ticks for these whole seconds left. */
	constexpr int32 FirstCountdownTick = 3;
	constexpr double ChampionCalloutSeconds = 3.5;
	/** The champion's confetti, cm over the puck. */
	constexpr double ChampionConfettiZ = 250.0;

	/** Each bot's brain gets its own stream from the match seed (a Knuth multiplicative hash and a slot stride). */
	constexpr uint32 BotSeedMultiplier = 2654435761u;
	constexpr uint32 BotSeedStride = 977u;
	/** A rematch's seed follows from the last one (Numerical Recipes' LCG). */
	constexpr uint32 RematchSeedMultiplier = 1664525u;
	constexpr uint32 RematchSeedIncrement = 1013904223u;
}

ASumoGame::ASumoGame()
{
	PrimaryActorTick.bCanEverTick = true;
	// After physics: the camera and the pucks place themselves from this frame's state.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

void ASumoGame::Setup(ASumoArena* InArena, const TArray<FSumoSeat>& InSeats, APlayerController* InViewer, uint32 InSeed)
{
	check(Pilots.Num() == 0);
	Arena = InArena;
	Seats = InSeats;
	Viewer = InViewer;
	Seed = InSeed;
	for (int32 i = 0; i < Seats.Num() && i < MaxPlayers; ++i)
	{
		FSumoPilot& Pilot = Pilots.AddDefaulted_GetRef();
		Pilot.Kind = Seats[i].Kind;
		Pilot.Brain = MakeBot(Seats[i].Skill, BotSeedFor(i));
	}
	Seats.SetNum(Pilots.Num());
	Match = CreateMatch(MatchSeeds(), Seed);
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		Pilots[i].PrevPos = Match.Pucks[i].Pos;
		Pilots[i].Yaw = FMath::RadiansToDegrees(FMath::Atan2(Match.Pucks[i].Facing.Y, Match.Pucks[i].Facing.X));
	}
	if (Arena)
	{
		Arena->SetPlayers(Pilots.Num());
		Arena->SetRing(0, false, 0.0);
	}
	BuildPucks();
	BuildPowerPool();
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		Seats[i].Color = ASumoArena::SlotColor(i);
	}
	SpawnCamera();
	AttachHud();
	SkipStartEvents();
	SetStage(ESumoStage::Intro);
}

void ASumoGame::SpawnCamera()
{
	Camera = GetWorld()->SpawnActor<ACameraActor>();
	UCameraComponent* Lens = Camera->GetCameraComponent();
	// The camera fills any window (its field of view is fitted to the aspect each frame), without motion blur.
	Lens->SetConstraintAspectRatio(false);
	Lens->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Lens->PostProcessSettings.MotionBlurAmount = 0.f;
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
}

void ASumoGame::AttachHud()
{
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SSumoHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), HudZOrder);
	}
}

void ASumoGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Hud.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
		}
		Hud.Reset();
	}
	Super::EndPlay(Reason);
}

TArray<FSeed> ASumoGame::MatchSeeds() const
{
	TArray<FSeed> Out;
	Out.Reserve(Seats.Num());
	for (const FSumoSeat& Seat : Seats)
	{
		Out.Add({ Seat.Id, Seat.Name });
	}
	return Out;
}

uint32 ASumoGame::BotSeedFor(int32 Slot) const
{
	return Seed * BotSeedMultiplier + BotSeedStride * static_cast<uint32>(Slot + 1);
}

void ASumoGame::SkipStartEvents()
{
	// A new match already carries its first round's start: presenting it would replay the round's setup.
	LastEventId = 0;
	for (const FRingEvent& E : Match.Events)
	{
		LastEventId = FMath::Max(LastEventId, E.Id);
	}
}

bool ASumoGame::HasLocal() const
{
	return Seats.ContainsByPredicate([](const FSumoSeat& S) { return S.Kind == ESumoPilot::Local; });
}

bool ASumoGame::IsPhoneQuiet(int32 Slot) const
{
	return Pilots.IsValidIndex(Slot) && Pilots[Slot].Kind == ESumoPilot::Phone && Clock - Pilots[Slot].PhoneAt > PhoneQuietSeconds;
}

FLinearColor ASumoGame::ColorOf(int32 Slot) const
{
	return ASumoArena::SlotColor(Slot);
}

double ASumoGame::GetFightAge() const
{
	return Match.Phase == EPhase::Countdown ? -1.0 : Match.RoundMs / 1000.0;
}

FVector2D ASumoGame::DrawnPos(int32 Slot, double Alpha) const
{
	return FMath::Lerp(Pilots[Slot].PrevPos, Match.Pucks[Slot].Pos, Alpha);
}

FVector ASumoGame::PuckWorld(int32 Slot) const
{
	return Parts.IsValidIndex(Slot) && Parts[Slot].Root ? Parts[Slot].Root->GetComponentLocation() : FVector::ZeroVector;
}

void ASumoGame::SetStage(ESumoStage Next)
{
	Stage = Next;
	StageAt = Clock;
	if (Next == ESumoStage::Match)
	{
		if (OnMatchStart)
		{
			OnMatchStart();
		}
		if (OnRoundStart)
		{
			OnRoundStart(Match.Round);
		}
	}
	if (Next == ESumoStage::Finished)
	{
		CelebrateChampion();
	}
}

void ASumoGame::CelebrateChampion()
{
	Cues.Play(this, TEXT("match.over"));
	const int32 Winner = Match.MatchWinner;
	if (!Match.Pucks.IsValidIndex(Winner))
	{
		return;
	}
	Callout(FString::Printf(TEXT("%s IS CHAMPION!"), *Match.Pucks[Winner].Name.ToUpper()), ColorOf(Winner), FVector::ZeroVector, true, FString(), ChampionCalloutSeconds);
	TArray<Fx::FKartFxEmit> Out;
	Fx::ConfettiBurst(PuckWorld(Winner) + FVector(0.0, 0.0, ChampionConfettiZ), FVector::ForwardVector, Out);
	EmitFx(Out);
}

void ASumoGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		// The camera keeps settling under the pause menu.
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, MaxFrameSeconds);
	Clock += Dt;
	for (FSumoPilot& Pilot : Pilots)
	{
		if (Pilot.Kind == ESumoPilot::Local)
		{
			ReadLocal(Pilot, Dt);
		}
	}
	switch (Stage)
	{
	case ESumoStage::Intro:
		TickIntro();
		break;
	case ESumoStage::Match:
		TickMatch(Dt);
		break;
	case ESumoStage::Finished:
		TickShowcase(Dt);
		break;
	}
	UpdateArenaRing();
	ExpireCallouts();
	const bool bHolding = HitStop > 0.0;
	const double Alpha = bHolding ? 1.0 : FMath::Clamp(Accumulator / StepMs, 0.0, 1.0);
	UpdatePucks(bHolding ? 0.f : Dt, Alpha);
	UpdatePowerUps();
	UpdateCamera(Dt);
}

void ASumoGame::TickIntro()
{
	// Presses during the intro don't carry into the first step.
	for (FSumoPilot& Pilot : Pilots)
	{
		Pilot.bDashQueued = false;
	}
	if (GetStageAge() >= IntroSeconds)
	{
		SetStage(ESumoStage::Match);
	}
}

void ASumoGame::TickMatch(float Dt)
{
	if (HitStop > 0.0)
	{
		HitStop -= Dt;
	}
	else
	{
		StepUntilCaughtUp(Dt);
	}
	PlayCountdownTicks();
	if (Match.Phase == EPhase::MatchOver)
	{
		SetStage(ESumoStage::Finished);
		UE_LOG(LogSumo, Log, TEXT("Match over: %s wins after %d rounds"), Match.Pucks.IsValidIndex(Match.MatchWinner) ? *Match.Pucks[Match.MatchWinner].Name : TEXT("nobody"),
			Match.RoundsPlayed);
	}
}

void ASumoGame::StepUntilCaughtUp(float Dt)
{
	Accumulator += Dt * 1000.0;
	int32 Steps = 0;
	while (Accumulator >= StepMs && Steps++ < MaxStepsPerFrame)
	{
		StepFixed();
		Accumulator -= StepMs;
		// A hit just started a hold: the rest of the backlog waits for it.
		if (HitStop > 0.0)
		{
			break;
		}
	}
	// Never bank more than a step: a slow frame doesn't fast-forward the next one.
	Accumulator = FMath::Min(Accumulator, StepMs);
}

void ASumoGame::PlayCountdownTicks()
{
	if (Match.Phase != EPhase::Countdown)
	{
		return;
	}
	const int32 SecondsLeft = FMath::CeilToInt32((CountdownMs - Match.PhaseMs) / 1000.0);
	if (SecondsLeft != LastCountdown && SecondsLeft >= 1 && SecondsLeft <= FirstCountdownTick)
	{
		LastCountdown = SecondsLeft;
		Cues.Play(this, TEXT("lobby.tick"));
	}
}

void ASumoGame::TickShowcase(float Dt)
{
	// The rules keep coasting the pucks to a stop under the showcase (no catch-up cap: Dt is already capped).
	Accumulator += Dt * 1000.0;
	while (Accumulator >= StepMs)
	{
		for (int32 i = 0; i < Pilots.Num(); ++i)
		{
			Pilots[i].PrevPos = Match.Pucks[i].Pos;
		}
		StepMatch(Match);
		Accumulator -= StepMs;
	}
	if (GetStageAge() >= ShowcaseSeconds)
	{
		ArcadeCommitResults();
	}
}

void ASumoGame::UpdateArenaRing() const
{
	if (!Arena)
	{
		return;
	}
	const bool bPending = Match.IsShrinkPending();
	// How far into the warning the pending band is (s): the arena flickers faster as the drop nears.
	const double WarnAge = bPending ? (Match.RoundMs - (ShrinkAtMs(Match.ShrinksDone) - ShrinkWarningMs)) / 1000.0 : 0.0;
	Arena->SetRing(Match.ShrinksDone, bPending && Match.Phase == EPhase::Fight, WarnAge);
}

void ASumoGame::ExpireCallouts()
{
	Callouts.RemoveAll([this](const FSumoCallout& C) { return Clock - C.At > C.Duration; });
}

void ASumoGame::Rematch()
{
	Seed = Seed * RematchSeedMultiplier + RematchSeedIncrement;
	Match = CreateMatch(MatchSeeds(), Seed);
	for (int32 i = 0; i < Pilots.Num(); ++i)
	{
		FSumoPilot& Pilot = Pilots[i];
		Pilot.Brain = MakeBot(Seats[i].Skill, BotSeedFor(i));
		Pilot.PrevPos = Match.Pucks[i].Pos;
		Pilot.PrevVel = FVector2D::ZeroVector;
		Pilot.bDashQueued = false;
		Pilot.LocalTilt = FVector2D::ZeroVector;
		Pilot.CheerAt = Pilot.HitAt = Pilot.DashAt = -10.0;
	}
	SkipStartEvents();
	if (Arena)
	{
		Arena->SetRing(0, false, 0.0);
	}
	Callouts.Reset();
	Feed.Reset();
	Accumulator = 0.0;
	HitStop = 0.0;
	bResultsCommitted = false;
	SetStage(ESumoStage::Intro);
}

void ASumoGame::ArcadeCommitResults()
{
	if (bResultsCommitted || Stage != ESumoStage::Finished)
	{
		return;
	}
	bResultsCommitted = true;
	if (OnFinished)
	{
		OnFinished();
	}
}

void ASumoGame::ArcadeRestart()
{
	if (OnRematch)
	{
		OnRematch();
	}
	else
	{
		Rematch();
	}
}

void ASumoGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	// The match's order (round wins, then score, knockouts, falls); places shared only on a true tie.
	const TArray<int32> Order = Standings(Match);
	const int32 Base = Out.Num();
	for (int32 k = 0; k < Order.Num(); ++k)
	{
		const FPuck& Puck = Match.Pucks[Order[k]];
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		Row.Name = Puck.Name;
		Row.Color = ColorOf(Puck.Slot);
		Row.bHuman = Seats.IsValidIndex(Puck.Slot) && Seats[Puck.Slot].Kind != ESumoPilot::Bot;
		Row.Score = Puck.Score;
		const FPuck* Prev = k > 0 ? &Match.Pucks[Order[k - 1]] : nullptr;
		Row.Place = Prev && Prev->RoundWins == Puck.RoundWins && Prev->Score == Puck.Score ? Out[Base + k - 1].Place : k + 1;
	}
}
