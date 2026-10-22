// ADartsGame: setup, the stage flow, the fixed-step rules clock, turns and the IArcadeGame loop.

#include "Games/Darts/DartsGame.h"

#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Darts/DartsGameTuning.h"
#include "Games/Darts/DartsPub.h"
#include "Games/Darts/DartsRelease.h"
#include "Games/Darts/SDartsHud.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDarts, Log, All);

using namespace MovecadeKart::Darts;
using MovecadeKart::Darts::Tuning::ToWorld;
namespace Fx = MovecadeKart::Fx;

namespace
{
	/** The rules' fixed step (runtime.ts steps the engine at 60 Hz). */
	constexpr double StepMs = 1000.0 / 60.0;
	/** A long hitch is stepped as at most this much time, so a stall never fast-forwards a flight. */
	constexpr float MaxFrameSeconds = 0.1f;
	/** Stuck darts and flights in the air at once: a visit's three, a lingering visit's three, and headroom. */
	constexpr int32 DartPoolSize = 12;
	/** The house bots' scatter stream (fixed: the same bots throw the same darts every match). */
	constexpr int32 BotSeed = 0xda275;
	/** Each thrower's flights wear their colour, with a touch of glow so they read across the room. */
	constexpr double FlightRoughness = 0.45;
	constexpr double FlightEmissive = 0.18;
	/** The big hit's flash fades over this (e-folding seconds), gone after FlashSeconds. */
	constexpr double FlashDecaySeconds = 0.14;
	constexpr double FlashSeconds = 0.6;
	/** The coach marks fade over this once the first dart flies. */
	constexpr double CoachFadeSeconds = 0.7;
	/** Lifted off the face so a callout's anchor isn't inside the sisal. */
	constexpr double CalloutLift = 0.02;
	/** The game HUD's viewport layer: under SArcadeOverlay (30), as every party game's HUD. */
	constexpr int32 HudZOrder = 10;

	const FName CueRelease(TEXT("darts.release"));
	const FName CueMatchOver(TEXT("match.over"));
}

bool ADartsGame::bCoachMarksSeenThisSession = false;

ADartsGame::ADartsGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(BotSeed);
}

void ADartsGame::Setup(ADartsPub* InPub, const TArray<FDartsSeat>& InSeats, APlayerController* InViewer, bool bInShotOnBigHit)
{
	if (!ensureMsgf(InPub, TEXT("ADartsGame::Setup needs the pub (its meshes are the darts and the reticle)")))
	{
		return;
	}
	Pub = InPub;
	Seats = InSeats;
	Viewer = InViewer;
	bShotOnBigHit = bInShotOnBigHit;
	CreateFreshMatch();
	// Load the cue waves now, not on the first dart (a sync disk load mid-throw).
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("darts."));
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("match."));

	for (int32 i = 0; i < DartPoolSize; ++i)
	{
		Darts.Add(MakeDart(TEXT("Dart")));
	}
	HeldDart = MakeDart(TEXT("HeldDart"));
	for (const FDartsSeat& Seat : Seats)
	{
		FlightMaterials.Add(MovecadeKart::Art::Plain(this, Seat.Color, FlightRoughness, 0.0, FlightEmissive));
	}
	MakeReticle();
	MakeAvatar();
	MakeAmbientFx();
	MakeCameraAndHud();
	SetStage(EDartsStage::Intro);
}

UStaticMeshComponent* ADartsGame::MakeDart(FName Name)
{
	UStaticMeshComponent* Dart = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
	Dart->SetStaticMesh(Pub->GetDartMesh());
	for (int32 Slot = 0; Slot < Pub->GetDartMaterialCount(); ++Slot)
	{
		Dart->SetMaterial(Slot, Pub->GetDartMaterial(Slot));
	}
	Dart->SetupAttachment(RootComponent);
	Dart->SetMobility(EComponentMobility::Movable);
	Dart->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dart->SetCastShadow(false);
	Dart->SetVisibility(false);
	Dart->RegisterComponent();
	return Dart;
}

void ADartsGame::MakeReticle()
{
	Reticle = NewObject<UStaticMeshComponent>(this, TEXT("Reticle"));
	Reticle->SetStaticMesh(Pub->GetRingMesh());
	// Translucent glow: the ring sits over the sisal and breathes in colour every frame (its own instance).
	ReticleMaterial = MovecadeKart::Art::Glow(this, FLinearColor::White);
	Reticle->SetMaterial(0, ReticleMaterial);
	Reticle->SetupAttachment(RootComponent);
	Reticle->SetMobility(EComponentMobility::Movable);
	Reticle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Reticle->SetCastShadow(false);
	Reticle->SetVisibility(false);
	Reticle->RegisterComponent();
}

void ADartsGame::MakeAvatar()
{
	Avatar = NewObject<UArcadeAvatarComponent>(this, TEXT("Thrower"));
	Avatar->SetupAttachment(RootComponent);
	Avatar->RegisterComponent();
	if (Seats.Num() > 0)
	{
		Avatar->SetLook(Seats[0].Look);
		DressedFor = Seats[0].Id;
	}
}

void ADartsGame::MakeAmbientFx()
{
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	if (!FxSys)
	{
		return;
	}
	Trail = FxSys->MakeStream(Fx::EField::Fire, RootComponent, TEXT("DartTrail"));
	// Dust in the board's light: a slow drift of motes between the oche and the board.
	Motes = FxSys->MakeStream(Fx::EField::Flare, RootComponent, TEXT("DustMotes"), ToWorld(FVector(0.0, ADartsPub::BoardY + 0.5, 1.6)));
	if (!Motes)
	{
		return;
	}
	Fx::FKartFxEmit Dust;
	Dust.Field = Fx::EField::Flare;
	Dust.Position = Motes->GetComponentLocation();
	Dust.Spread = UE_DOUBLE_PI;
	Dust.Speed = FVector2D(0.015, 0.05);
	Dust.Life = FVector2D(4.0, 7.0);
	Dust.Size = FVector2D(0.006, 0.012);
	Dust.SizeEnd = 0.6;
	Dust.Color = Fx::Hex(TEXT("ffe2b0"));
	Dust.ColorEnd = Fx::Hex(TEXT("ffc98c"));
	Dust.Intensity = 0.55;
	Dust.Jitter = 1.2;
	constexpr double MotesPerSecond = 6.0;
	UKartFxSubsystem::UpdateStream(Motes, Dust, MotesPerSecond);
}

void ADartsGame::MakeCameraAndHud()
{
	Camera = GetWorld()->SpawnActor<ACameraActor>();
	// UpdateCamera sets a horizontal FOV for the viewport's own aspect: no letterbox.
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SDartsHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), HudZOrder);
	}
}

void ADartsGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Hud.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
		}
		Hud.Reset();
	}
	GetWorldTimerManager().ClearTimer(BigHitShotTimer);
	// The camera is our own actor: it goes with the game.
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	Super::EndPlay(Reason);
}

const FDartsSeat* ADartsGame::SeatOf(const FString& Id) const
{
	return Seats.FindByPredicate([&Id](const FDartsSeat& S) { return S.Id == Id; });
}

const FDartsSeat* ADartsGame::SeatOf(const FDartsPlayer& Player) const
{
	if (Seats.IsValidIndex(Player.LaneIndex) && Seats[Player.LaneIndex].Id == Player.Id)
	{
		return &Seats[Player.LaneIndex];
	}
	return SeatOf(Player.Id);
}

const FDartsSeat* ADartsGame::GetThrower() const
{
	const FDartsPlayer* Active = Match.ActivePlayer();
	if (!Active)
	{
		return nullptr;
	}
	if (Seats.IsValidIndex(ThrowerSeat) && Seats[ThrowerSeat].Id == Active->Id)
	{
		return &Seats[ThrowerSeat];
	}
	return SeatOf(*Active);
}

bool ADartsGame::GetReticle(FVector2D& OutAim) const
{
	const FDartsSeat* Thrower = GetThrower();
	if (Stage != EDartsStage::Match || Match.Phase != EMatchPhase::Playing || !Thrower || Match.TurnDarts >= DartsPerRound)
	{
		return false;
	}
	switch (Thrower->Kind)
	{
	case EThrowerKind::Local:
		OutAim = Local.Aim;
		return true;
	case EThrowerKind::Phone:
	{
		const FDartThrowDetector* Detector = PhoneDetectors.Find(Thrower->Id);
		OutAim = Detector ? FVector2D(Detector->Aim().X, Detector->Aim().Y) : FVector2D::ZeroVector;
		return true;
	}
	default:
		return false;
	}
}

FVector ADartsGame::BoardWorld(double X, double Y) const
{
	return ToWorld(ADartsPub::BoardPoint(X, Y, CalloutLift));
}

float ADartsGame::GetFlash() const
{
	const double Since = Clock - FlashAt;
	return Since < 0.0 || Since > FlashSeconds ? 0.f : FlashStrength * static_cast<float>(FMath::Exp(-Since / FlashDecaySeconds));
}

float ADartsGame::GetCoachAlpha() const
{
	if (!bCoachShown)
	{
		return 0.f;
	}
	if (CoachDoneAt < 0.0)
	{
		return 1.f;
	}
	return FMath::Clamp(1.f - static_cast<float>((Clock - CoachDoneAt) / CoachFadeSeconds), 0.f, 1.f);
}

void ADartsGame::CreateFreshMatch()
{
	TArray<FMatchSeed> Seeds;
	Seeds.Reserve(Seats.Num());
	for (const FDartsSeat& Seat : Seats)
	{
		Seeds.Add({ Seat.Id, Seat.Name });
	}
	Match = CreateMatch(Seeds);
}

void ADartsGame::SetStage(EDartsStage Next)
{
	Stage = Next;
	StageAt = Clock;
	if (Next == EDartsStage::Match && OnMatchStart)
	{
		OnMatchStart();
	}
	if (Next == EDartsStage::Finished)
	{
		Cues.Play(this, CueMatchOver);
	}
}

void ADartsGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Pub)
	{
		return;
	}
	if (bPaused)
	{
		// The camera keeps easing (and the punch settling) under the pause menu.
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, MaxFrameSeconds);
	Clock += Dt;
	switch (Stage)
	{
	case EDartsStage::Intro:
		if (GetStageAge() >= IntroSeconds)
		{
			SetStage(EDartsStage::Match);
		}
		break;
	case EDartsStage::Match:
		TickMatch(Dt);
		break;
	case EDartsStage::Finished:
		break;
	}
	UpdateAmbience();
	UpdateDarts();
	UpdateTrail();
	UpdateBoard(Dt);
	UpdateReticle();
	UpdateThrower();
	UpdateCamera(Dt);
	UpdateChalk();
}

void ADartsGame::TickMatch(float Dt)
{
	UpdateLocalInput(Dt);
	UpdateBot();
	// The rules step on a fixed 60 Hz clock, so a throw lands identically at any frame rate.
	Accumulator += Dt * 1000.0;
	StepPending();
	const FDartsPlayer* Active = Match.ActivePlayer();
	if ((Active ? Active->Id : FString()) != AnnouncedThrower)
	{
		HandleTurnChange();
	}
	if (Match.Phase == EMatchPhase::Ended)
	{
		SetStage(EDartsStage::Finished);
		UE_LOG(LogDarts, Log, TEXT("Game over"));
		if (OnFinished)
		{
			OnFinished();
		}
	}
}

void ADartsGame::StepPending()
{
	while (Accumulator >= StepMs)
	{
		StepFixed(StepMs);
		Accumulator -= StepMs;
	}
}

void ADartsGame::HandleTurnChange()
{
	const FDartsPlayer* Active = Match.ActivePlayer();
	const FString ActiveId = Active ? Active->Id : FString();
	AnnouncedThrower = ActiveId;
	ThrowerSeat = Active ? Seats.IndexOfByPredicate([&ActiveId](const FDartsSeat& S) { return S.Id == ActiveId; }) : INDEX_NONE;
	PhoneDetectors.Reset();
	Local = FLocalThrow();
	BotReadyAt = Clock + Tuning::BotFirstDelaySeconds;
	const FDartsSeat* Seat = Seats.IsValidIndex(ThrowerSeat) ? &Seats[ThrowerSeat] : nullptr;
	if (Seat && Seat->Id != DressedFor)
	{
		DressedFor = Seat->Id;
		Avatar->SetLook(Seat->Look);
		HandBone = INDEX_NONE;
	}
	// The first local visit of the session gets the coach marks.
	if (Seat && Seat->Kind == EThrowerKind::Local && !bCoachMarksSeenThisSession && !bCoachShown)
	{
		bCoachShown = true;
		CoachDoneAt = -1.0;
	}
	if (OnTurn && !ActiveId.IsEmpty())
	{
		OnTurn(ActiveId);
	}
}

void ADartsGame::StepFixed(double DtMs)
{
	StepMatch(Match, DtMs);
	// Present what the step changed: new flights (the release), arrivals, rounds and finishes.
	for (const FDartFlight& Flight : Match.Flights)
	{
		bool bAlreadySeen = false;
		SeenFlights.Add(Flight.Id, &bAlreadySeen);
		if (!bAlreadySeen)
		{
			// Harder throws whoosh louder and higher.
			Cues.Play(this, CueRelease, static_cast<float>(0.6 + 0.4 * Flight.Power), static_cast<float>(0.9 + 0.35 * Flight.Power));
		}
	}
	for (const FDartsEffect& Effect : Match.Effects)
	{
		bool bAlreadySeen = false;
		SeenEffects.Add(Effect.Id, &bAlreadySeen);
		if (!bAlreadySeen)
		{
			OnArrival(Effect);
		}
	}
}

bool ADartsGame::SubmitRelease(const FString& PlayerId, const FDartGesture& Gesture)
{
	const FDartsPlayer* Active = Match.ActivePlayer();
	// The intro card is a teaching moment: a release at a board nobody can see yet is dropped (runtime.ts).
	if (Stage != EDartsStage::Match || !Active || Active->Id != PlayerId)
	{
		return false;
	}
	const FDartGesture Bounded = BoundRelease(Gesture);
	// Settle the clock to now first, so a queued release cannot ride a flight that already landed (runtime.ts).
	StepPending();
	const FDartsPlayer* Still = Match.ActivePlayer();
	if (!Still || Still->Id != PlayerId)
	{
		return false;
	}
	Throw(PlayerId, Bounded);
	return true;
}

void ADartsGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	// Only the thrower up's frames reach a detector: a waiting phone must not move this turn's reticle.
	const FDartsPlayer* Active = Match.ActivePlayer();
	if (Active && Active->Id == PlayerId)
	{
		PhoneDetectors.FindOrAdd(PlayerId).Feed(FThrowSample::FromFrame(Frame));
	}
}

void ADartsGame::Throw(const FString& PlayerId, const FDartGesture& Gesture)
{
	const TOptional<FThrowOutcome> Outcome = ThrowDart(Match, PlayerId, Gesture);
	if (!Outcome.IsSet())
	{
		return;
	}
	LastReleaseAt = Clock;
	UE_LOG(LogDarts, Log, TEXT("Dart: %s aim (%.2f, %.2f) power %.2f -> %s (%d)%s"), *PlayerId, Gesture.AimX, Gesture.AimY, Gesture.Power,
		*Outcome->Landing.Hit.Label, Outcome->Landing.ScoreDelta, Outcome->RoundBonus > 0 ? *FString::Printf(TEXT(" +%d bonus"), Outcome->RoundBonus) : TEXT(""));
	if (OnThrow)
	{
		OnThrow(PlayerId, Outcome.GetValue());
	}
}

void ADartsGame::Rematch()
{
	CreateFreshMatch();
	SeenEffects.Reset();
	SeenFlights.Reset();
	AnnouncedThrower.Reset();
	ThrowerSeat = INDEX_NONE;
	PhoneDetectors.Reset();
	PendingBonus.Reset();
	LastVisit = FDartsVisitSummary();
	Local = FLocalThrow();
	Accumulator = 0.0;
	LastLandingAt = -10.0;
	LastLanding = FDartLanding();
	HeldOverPlayer = INDEX_NONE;
	ShownVisitor.Reset();
	Punch = 0.0;
	FlashAt = -10.0;
	bChalked = false;
	SetStage(EDartsStage::Intro);
}

void ADartsGame::ArcadeRestart()
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

void ADartsGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (const FDartsPlayer& P : Match.Players)
	{
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		const FDartsSeat* Seat = SeatOf(P);
		Row.Name = P.Name;
		Row.Color = Seat ? Seat->Color : FLinearColor::White;
		Row.bHuman = Seat && Seat->Kind != EThrowerKind::Bot;
		Row.Score = P.Score;
	}
	RankByScore(Out);
}
