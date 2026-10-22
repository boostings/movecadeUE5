// AStackerGame: setup, the round's flow (intro, the fixed-step match, the showcase), rematch and IArcadeGame.

#include "Games/Stacker/StackerGame.h"

#include "Art/KartMaterialCache.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Games/Stacker/SStackerHud.h"
#include "Games/Stacker/StackerRooftop.h"
#include "KartCoords.h"
#include "Misc/CommandLine.h"
#include "StackerBlockMesh.h"
#include "StackerScene.h"

DEFINE_LOG_CATEGORY_STATIC(LogStacker, Log, All);

using namespace MovecadeKart::Stacker;

namespace
{
	/** S1: the rules step on a fixed 10 ms clock (the balance's own substep), so every machine agrees. */
	constexpr double StepMs = 10.0;
	/** A long hitch is played as a tenth of a second, not caught up in one burst of steps. */
	constexpr float MaxFrameSeconds = 0.1f;
	/** compareHold.ts: the showcase holds eight seconds before the room hears the result. */
	constexpr double CompareHoldSeconds = 8.0;
	/** A chime for every band of this many rows the tallest tower climbs; its pitch rises a whole tone a band. */
	constexpr int32 AltitudeBandRows = 5;
	constexpr int32 AltitudeChimeSteps = 6;
	/** The HUD's slot on the viewport, over the game but under the arcade overlay. */
	constexpr int32 HudZOrder = 10;
	/** The bots' stream: fixed, so a local round with the same seats plays out the same. */
	constexpr int32 BotSeed = 0x57ac;
	/** A phone that hasn't sent a frame yet counts as missing (S3): its last input is older than the stale window. */
	constexpr double NoPhoneInputYetMs = -BalanceStaleMs - 1.0;
	/** The standings key: score first, then height (S7). Heights are small and never negative. */
	int64 StandingKey(const FTower& Tower)
	{
		return static_cast<int64>(Tower.Score) * (int64(1) << 32) + Tower.Height();
	}

	/** An engine basic shape; a missing one is reported once and its pieces simply don't draw. */
	UStaticMesh* LoadShape(const TCHAR* Path)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		UE_CLOG(!Mesh, LogStacker, Warning, TEXT("Stacker: engine mesh %s is missing; the pieces made of it won't draw"), Path);
		return Mesh;
	}
}

AStackerGame::AStackerGame()
{
	PrimaryActorTick.bCanEverTick = true;
	// After physics: the camera and the instance batches are written once the frame's input is in.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(BotSeed);
}

FVector AStackerGame::W(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

void AStackerGame::Setup(AStackerRooftop* InRooftop, const TArray<FStackerSeat>& InSeats, APlayerController* InViewer)
{
	check(Lanes.Num() == 0);
	Rooftop = InRooftop;
	Seats = InSeats;
	Viewer = InViewer;
	DebugFlags = FStackerDebugFlags::Parse(FCommandLine::Get());
	if (Rooftop)
	{
		Rooftop->SetLanes(Seats.Num());
	}
	SeedLanes();

	BlockMesh = BuildBlockMesh(this);
	CubeMesh = LoadShape(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		BuildTowerPieces(i);
		BuildBuilderPieces(i);
	}
	BatchState.SetNum(Batches.Num());
	Birds = MakeBatch(TEXT("Birds"), CubeMesh, { Materials.Plain(this, MovecadeKart::Art::Hex(TEXT("#4c3973")), 0.7) }, false);
	// The birds are parked between flights and fly fast: no motion-vector history worth keeping.
	Birds->SetHasPerInstancePrevTransforms(false);
	BuildCameraAndHud();
	SetStage(EStackerStage::Intro);
}

void AStackerGame::SeedLanes()
{
	TArray<FSeed> MatchSeeds;
	for (const FStackerSeat& Seat : Seats)
	{
		MatchSeeds.Add({ Seat.Id, Seat.Name });
		FLane& Lane = Lanes.AddDefaulted_GetRef();
		Lane.PlayerId = Seat.Id;
		Lane.Kind = Seat.Kind;
	}
	Match = CreateMatch(MatchSeeds);
	TowerByLane.Init(INDEX_NONE, AStackerRooftop::MaxLanes);
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		const int32 LaneIndex = Match.Towers[i].LaneIndex;
		Lanes[i].LaneX = LaneXOf(LaneIndex);
		if (TowerByLane.IsValidIndex(LaneIndex))
		{
			TowerByLane[LaneIndex] = i;
		}
		if (Lanes[i].Kind == EBuilderKind::Phone)
		{
			Match.Towers[i].Balance.LastInputMs = NoPhoneInputYetMs;
		}
	}
}

void AStackerGame::BuildCameraAndHud()
{
	Camera = GetWorld()->SpawnActor<ACameraActor>();
	// The camera fits the lanes to whatever aspect the window has, so it never letterboxes.
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SStackerHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), HudZOrder);
	}
}

void AStackerGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Hud.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
		}
		Hud.Reset();
	}
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	Super::EndPlay(Reason);
}

const FStackerSeat* AStackerGame::SeatOf(const FString& Id) const
{
	return Seats.FindByPredicate([&Id](const FStackerSeat& S) { return S.Id == Id; });
}

bool AStackerGame::HasLocal() const
{
	return Seats.ContainsByPredicate([](const FStackerSeat& S) { return S.Kind == EBuilderKind::Local; });
}

int32 AStackerGame::LaneSpan() const
{
	int32 Span = 1;
	for (const FTower& T : Match.Towers)
	{
		Span = FMath::Max(Span, T.LaneIndex + 1);
	}
	return FMath::Min(Span, AStackerRooftop::MaxLanes);
}

double AStackerGame::LaneXOf(int32 LaneIndex) const
{
	return AStackerRooftop::LaneX(LaneIndex, LaneSpan());
}

int32 AStackerGame::TallestHeight() const
{
	int32 Tallest = 0;
	for (const FTower& T : Match.Towers)
	{
		Tallest = FMath::Max(Tallest, T.Height());
	}
	return Tallest;
}

double AStackerGame::TopY(int32 TowerIndex) const
{
	if (!Match.Towers.IsValidIndex(TowerIndex))
	{
		return 0.0;
	}
	return (Match.Towers[TowerIndex].Top().Index - ScrollOf(TowerIndex)) * Scene::RowHeight + Scene::RowHeight;
}

void AStackerGame::RankStandings()
{
	StandingOrder.Update(Match.Towers.Num(), [this](int32 i) { return StandingKey(Match.Towers[i]); });
}

void AStackerGame::SetStage(EStackerStage Next)
{
	Stage = Next;
	StageAt = Clock;
	if (Next == EStackerStage::Match && OnMatchStart)
	{
		OnMatchStart();
	}
	if (Next == EStackerStage::Finished)
	{
		RankStandings();
		Cues.Play(this, TEXT("match.over"));
	}
}

void AStackerGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		// The camera keeps settling (and the shake decaying) under the pause menu.
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, MaxFrameSeconds);
	Clock += Dt;
	SteerLocals();
	AdvanceStage(Dt);
	PlayAltitudeChime();

	ResetBatches();
	UpdateTowers(Dt);
	UpdateShards();
	UpdateBuilders(Dt);
	UpdateBeats(Dt);
	UpdateCoach(Dt);
	UpdateCamera(Dt);
	FlushBatches();
}

void AStackerGame::AdvanceStage(float Dt)
{
	switch (Stage)
	{
	case EStackerStage::Intro:
		// Nothing dropped during the intro carries into the match.
		for (FLane& Lane : Lanes)
		{
			Lane.bDropQueued = false;
		}
		if (GetStageAge() >= IntroSeconds)
		{
			SetStage(EStackerStage::Match);
		}
		break;
	case EStackerStage::Match:
		Accumulator += Dt * 1000.0;
		while (Accumulator >= StepMs)
		{
			StepFixed();
			Accumulator -= StepMs;
		}
		if (Match.Phase == EMatchPhase::Ended)
		{
			SetStage(EStackerStage::Finished);
			UE_LOG(LogStacker, Log, TEXT("Round over"));
		}
		break;
	case EStackerStage::Finished:
		if (GetStageAge() >= CompareHoldSeconds)
		{
			ArcadeCommitResults();
		}
		break;
	}
}

void AStackerGame::StepFixed()
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		FTower& Tower = Match.Towers[i];
		if (Lane.Kind == EBuilderKind::Local)
		{
			// The local palm is always live (a held stick is input even when it doesn't change).
			Tower.Balance.LastInputMs = Match.ElapsedMs;
		}
		else if (Lane.Kind == EBuilderKind::Bot && Seats.IsValidIndex(i))
		{
			SteerBot(Lane, Tower, Seats[i]);
		}
	}
	StepMatch(Match, StepMs);
	DropQueued();
	PresentNewEffects();
}

void AStackerGame::PlayAltitudeChime()
{
	const int32 Band = TallestHeight() / AltitudeBandRows;
	if (Band <= AltitudeBand)
	{
		return;
	}
	AltitudeBand = Band;
	const int32 Step = FMath::Min(AltitudeChimeSteps, Band - 1);
	Cues.Play(this, TEXT("stacker.altitude"), 0.75f + 0.25f * Step / AltitudeChimeSteps, FMath::Pow(2.f, 2.f * Step / 12.f));
}

void AStackerGame::Rematch()
{
	TArray<FSeed> MatchSeeds;
	for (const FStackerSeat& Seat : Seats)
	{
		MatchSeeds.Add({ Seat.Id, Seat.Name });
	}
	Match = CreateMatch(MatchSeeds);
	ResetLanesForRound();
	LastSeenEffectId = 0;
	LastSeenShardId = 0;
	StandingOrder.Reset();
	Beats = FBeats();
	Accumulator = 0.0;
	AltitudeBand = 0;
	bResultsCommitted = false;
	SetStage(EStackerStage::Intro);
}

void AStackerGame::ResetLanesForRound()
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		Lane.Scroll = 0.0;
		Lane.AimedRow = -1;
		Lane.bDropQueued = false;
		Lane.MouseRoll = Lane.MousePitch = 0.0;
		Lane.Topple = 0.0;
		Lane.ShownLean = 0.0;
		Lane.PerfectAt = -10.0;
		Lane.Reaction = EEffectKind::Land;
		Lane.ReactionAt = -10.0;
		if (Lane.Kind == EBuilderKind::Phone)
		{
			Match.Towers[i].Balance.LastInputMs = NoPhoneInputYetMs;
		}
	}
}

void AStackerGame::ArcadeCommitResults()
{
	if (!bResultsCommitted && Stage == EStackerStage::Finished)
	{
		bResultsCommitted = true;
		if (OnFinished)
		{
			OnFinished();
		}
	}
}

void AStackerGame::ArcadeRestart()
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

void AStackerGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (const FTower& T : Match.Towers)
	{
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		const FStackerSeat* Seat = SeatOf(T.PlayerId);
		Row.Name = T.Name;
		Row.Color = AStackerRooftop::LaneColor(T.LaneIndex);
		Row.bHuman = Seat && Seat->Kind != EBuilderKind::Bot;
		Row.Score = T.Score;
	}
	RankByScore(Out);
}
