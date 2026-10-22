// AFishingGame's flow: setup, the stages, the fixed rules step and handing new effects to presentation.
#include "Games/Fishing/FishingGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Games/Fishing/FishingGameInternal.h"
#include "Games/Fishing/FishingLake.h"
#include "Games/Fishing/SFishingHud.h"

DEFINE_LOG_CATEGORY(LogFishing);

using namespace MovecadeKart::Fishing;
using namespace MovecadeKart::Fishing::GameTuning;

namespace
{
	/** The HUD sits over the game's own widgets but under the arcade overlay (pause, results). */
	constexpr int32 HudZOrder = 10;
	/** Fixed seeds: the bots' and the effects' dice, the same every run (only the match seed varies). */
	constexpr int32 BotRngSeed = 0xf15e;
	constexpr int32 FxRngSeed = 0x5b1a;
	/** The rematch's next seed: a PCG step, so play again is a new lake but still reproducible from the first seed. */
	constexpr uint32 SeedMultiplier = 747796405u;
	constexpr uint32 SeedIncrement = 2891336453u;
	/** Presenting an effect can act on the rules (a bot's nervous hook), which adds effects: drain at most this deep. */
	constexpr int32 MaxDrainPasses = 4;

#if !UE_BUILD_SHIPPING
	/** -FishingDemo: the trophy bites this far into the round, already at the net. */
	constexpr double DemoAtMs = 300.0;
	constexpr double DemoCastM = 28.0;
	constexpr double DemoWindowMs = 500.0;
#endif
}

AFishingGame::AFishingGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(BotRngSeed);
	FxRng.Initialize(FxRngSeed);
}

void AFishingGame::Setup(AFishingLake* InLake, const TArray<FFishingSeat>& InSeats, APlayerController* InViewer, uint32 InSeed, const FFishingDebugFlags& InDebugFlags)
{
	Lake = InLake;
	Seats = InSeats;
	Viewer = InViewer;
	Seed = InSeed;
	DebugFlags = InDebugFlags;
	if (Lake)
	{
		Lake->SetLanes(Seats.Num());
	}
	BuildLanes();

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SFishingHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), HudZOrder);
	}
	StartRound();
}

void AFishingGame::StartRound()
{
	TArray<FSeed> MatchSeeds;
	for (const FFishingSeat& Seat : Seats)
	{
		MatchSeeds.Add({ Seat.Id, Seat.Name });
	}
	Match = CreateMatch(MatchSeeds, Seed);
	if (DebugFlags.RoundSeconds > 0.0)
	{
		Match.TimeRemainingMs = DebugFlags.RoundSeconds * 1000.0;
	}
	Lanes.Reset();
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		FLane& Lane = Lanes.AddDefaulted_GetRef();
		Lane.PlayerId = Seats[i].Id;
		Lane.Kind = Seats[i].Kind;
		// The house anglers' first casts, staggered down the dock.
		Lane.Bot.NextActAt = BotFirstCastMs + BotFirstCastStaggerMs * i;
	}
	LastPresentedEffectId = 0;
	bHornAnnounced = false;
	StepAccumulatorMs = 0.0;
	bResultsCommitted = false;
	bCamPlaced = false;
	SetStage(EFishingStage::Intro);
}

void AFishingGame::Rematch()
{
	Seed = Seed * SeedMultiplier + SeedIncrement;
	StartRound();
}

void AFishingGame::EndPlay(const EEndPlayReason::Type Reason)
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

const FFishingSeat* AFishingGame::SeatOf(const FString& Id) const
{
	return Seats.FindByPredicate([&Id](const FFishingSeat& S) { return S.Id == Id; });
}

bool AFishingGame::HasLocal() const
{
	return Seats.ContainsByPredicate([](const FFishingSeat& S) { return S.Kind == EAnglerKind::Local; });
}

double AFishingGame::ChargeOf(int32 Lane) const
{
	return Lanes.IsValidIndex(Lane) && Lanes[Lane].bHolding ? Lanes[Lane].Charge : -1.0;
}

void AFishingGame::SetStage(EFishingStage Next)
{
	Stage = Next;
	StageAt = Clock;
	if (Next == EFishingStage::Match && OnMatchStart)
	{
		OnMatchStart();
	}
	if (Next == EFishingStage::Finished)
	{
		Cues.Play(this, TEXT("match.over"));
	}
}

void AFishingGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, MaxFrameSeconds);
	Clock += Dt;
	ReadHands(Dt);
	TickStage(Dt);
	UpdateAnglers(Dt);
	UpdateCamera(Dt);
}

void AFishingGame::TickStage(float Dt)
{
	switch (Stage)
	{
	case EFishingStage::Intro:
		// Nothing counts before the round: a cast held through the intro would fly on the horn.
		ReleaseAllHolds();
		if (GetStageAge() >= IntroSeconds)
		{
			SetStage(EFishingStage::Match);
		}
		break;
	case EFishingStage::Match:
		StepAccumulatorMs += Dt * 1000.0;
		while (StepAccumulatorMs >= StepMs)
		{
			StepFixed();
			StepAccumulatorMs -= StepMs;
		}
		if (Match.Phase == EMatchPhase::Ended)
		{
			ReleaseAllHolds();
			SetStage(EFishingStage::Finished);
			LogResults();
		}
		break;
	case EFishingStage::Finished:
		if (GetStageAge() >= ShowcaseSeconds)
		{
			ArcadeCommitResults();
		}
		break;
	}
}

void AFishingGame::ReleaseAllHolds()
{
	for (FLane& Lane : Lanes)
	{
		Lane.bHolding = false;
	}
}

void AFishingGame::LogResults() const
{
	UE_LOG(LogFishing, Log, TEXT("Fishing: round over"));
	for (const FAngler* A : Leaderboard(Match))
	{
		UE_LOG(LogFishing, Log, TEXT("Fishing: %s %d (%d catches, %d junk, %d lost, biggest %s)"), *A->Name, A->Score, A->Catches, A->JunkCount, A->Losses, *FormatWeight(A->BiggestG));
	}
}

void AFishingGame::StepFixed()
{
	ApplyDemoCatch();
	FeedReels();
	StepMatch(Match, StepMs);
	DrainEffects();
}

void AFishingGame::ApplyDemoCatch()
{
#if !UE_BUILD_SHIPPING
	if (!DebugFlags.bDemo || Match.ElapsedMs < DemoAtMs || Match.ElapsedMs >= DemoAtMs + StepMs || Match.Anglers.Num() == 0)
	{
		return;
	}
	// The first angler's goldie bites, is hooked at once and is already at the net, its runs put off past the round.
	FAngler& Demo = Match.Anglers[0];
	Demo.State = EAnglerState::Bite;
	Demo.BiteSpecies = SpeciesIndex(TEXT("goldie"));
	Demo.LureM = DemoCastM;
	Demo.CastM = DemoCastM;
	Demo.WindowMs = DemoWindowMs;
	Hook(Match, Demo.PlayerId);
	Demo.Fight.DistanceM = LandM + 0.05;
	Demo.Fight.NextBurstMs = 99999.0;
#endif
}

void AFishingGame::DrainEffects()
{
	TArray<FEffect> Fresh;
	for (int32 Pass = 0; Pass < MaxDrainPasses; ++Pass)
	{
		// Copied out: presenting may add effects (and trim the oldest) while we walk them.
		Fresh.Reset();
		for (const FEffect& E : Match.Effects)
		{
			if (E.Id > LastPresentedEffectId)
			{
				Fresh.Add(E);
			}
		}
		if (Fresh.Num() == 0)
		{
			break;
		}
		LastPresentedEffectId = Fresh.Last().Id;
		for (const FEffect& E : Fresh)
		{
			PresentEffect(E);
			if (OnEffect)
			{
				OnEffect(E);
			}
		}
	}
	AnnounceAnglerStates();
}

void AFishingGame::AnnounceAnglerStates()
{
	// The horn: everyone hears their prompt again now that a cast counts.
	const bool bHorn = !bHornAnnounced && Match.Phase == EMatchPhase::Playing;
	bHornAnnounced |= bHorn;
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		const FAngler& A = Match.Anglers[i];
		if (bHorn && A.State == Lane.Show.State && OnAnglerState)
		{
			OnAnglerState(A);
		}
		if (A.State == Lane.Show.State)
		{
			continue;
		}
		Lane.Show.State = A.State;
		Lane.Show.StateAt = Clock;
		if (A.State != EAnglerState::Idle)
		{
			// The line went out (or the fish came in): a cast still held no longer means anything.
			Lane.bHolding = false;
		}
		if (OnAnglerState)
		{
			OnAnglerState(A);
		}
	}
}

void AFishingGame::ArcadeCommitResults()
{
	if (!bResultsCommitted && Stage == EFishingStage::Finished)
	{
		bResultsCommitted = true;
		if (OnFinished)
		{
			OnFinished();
		}
	}
}

void AFishingGame::ArcadeRestart()
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

void AFishingGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (const FAngler& A : Match.Anglers)
	{
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		const FFishingSeat* Seat = SeatOf(A.PlayerId);
		Row.Name = A.Name;
		Row.Color = AFishingLake::LaneColor(A.LaneIndex);
		Row.bHuman = Seat && Seat->Kind != EAnglerKind::Bot;
		Row.Score = A.Score;
	}
	RankByScore(Out);
}
