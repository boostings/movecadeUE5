#include "Games/Darts/DartsGame.h"

#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Darts/DartsPub.h"
#include "Games/Darts/SDartsHud.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "SceneView.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogDarts, Log, All);

using namespace MovecadeKart::Darts;
namespace Fx = MovecadeKart::Fx;

#define LOCTEXT_NAMESPACE "Darts"

namespace
{
	constexpr double StepMs = 1000.0 / 60.0;
	/** The keys and pad meter swings 0 -> 1 -> 0 over this period while the dart is gripped. */
	constexpr double MeterPeriod = 1.5;
	constexpr int32 DartPool = 12;
	/** Where the dart leaves the hand (writeFlightPosition): the thrower's line + 0.28, at shoulder height. */
	constexpr double LaunchX = ADartsPub::ThrowerX + 0.28;
	constexpr double LaunchY = ADartsPub::FloorY + 1.55;
	constexpr double ThrowDistance = 4.3;
	/** The tip sits a centimetre into the sisal. */
	constexpr double StuckZ = -0.012;
	/** A bot's first dart after the board passes to it, and its next darts (the old 1.4 s / 1.15 s dragged). */
	constexpr double DartsBotFirstDelay = 0.95;
	constexpr double DartsBotNextDelay = 0.85;
	/** The reaction shot holds this long after a landing (longer on the visit's last dart). */
	constexpr double DartsReactSeconds = 0.95;
	constexpr double DartsReactLastSeconds = 1.3;

	/** The coach marks show once per session (play again and next game don't repeat them). */
	bool DartsGCoachDone = false;

	const FName DartsNameColor(TEXT("Color"));
	const FName DartsNameRightHand(TEXT("rightHand"));
	const FName DartsCueRelease(TEXT("darts.release"));
	const FName DartsCueThud(TEXT("darts.thud"));
	const FName DartsCueMiss(TEXT("darts.miss"));
	const FName DartsCueBull(TEXT("darts.bull"));
	const FName DartsCueWire(TEXT("darts.wire"));
	const FName DartsCueBounty(TEXT("darts.bounty"));
	const FName DartsCueRound(TEXT("darts.round"));
	const FName DartsCueChatter(TEXT("darts.pub-chatter"));
	const FName DartsCueMatchOver(TEXT("match.over"));

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double SmootherStep(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return T * T * T * (T * (6.0 * T - 15.0) + 10.0);
	}

	FVector W(const FVector& Web)
	{
		return ADartsPub::ToWorld(Web);
	}

	/** A normal-ish deviate from two uniforms (bots' scatter). */
	double Gauss(FRandomStream& Rng)
	{
		const double U = FMath::Max(1e-6, static_cast<double>(Rng.FRand()));
		const double V = Rng.FRand();
		return FMath::Sqrt(-2.0 * FMath::Loge(U)) * FMath::Cos(2.0 * UE_DOUBLE_PI * V);
	}
}

bool FDartsView::Project(const FVector& World, FVector2D& Out01) const
{
	FVector2D Pixels;
	if (!bValid || Rect.Width() <= 0 || Rect.Height() <= 0 || !FSceneView::ProjectWorldToScreen(World, Rect, ViewProjection, Pixels))
	{
		return false;
	}
	Out01 = (Pixels - FVector2D(Rect.Min)) / FVector2D(Rect.Size());
	return true;
}

ADartsGame::ADartsGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(0xda275);
}

void ADartsGame::Setup(ADartsPub* InPub, const TArray<FDartsSeat>& InSeats, APlayerController* InViewer)
{
	Pub = InPub;
	Seats = InSeats;
	Viewer = InViewer;
	TArray<FMatchSeed> Seeds;
	for (const FDartsSeat& Seat : Seats)
	{
		Seeds.Add({ Seat.Id, Seat.Name });
	}
	Match = CreateMatch(Seeds);
	// Load the cue waves now, not on the first dart (a sync disk load mid-throw).
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("darts."));
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("match."));

	auto MakeDart = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* D = NewObject<UStaticMeshComponent>(this, Name);
		D->SetStaticMesh(Pub->GetDartMesh());
		for (int32 Slot = 0; Slot < Pub->GetDartMaterialCount(); ++Slot)
		{
			D->SetMaterial(Slot, Pub->GetDartMaterial(Slot));
		}
		D->SetupAttachment(RootComponent);
		D->SetMobility(EComponentMobility::Movable);
		D->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		D->SetCastShadow(false);
		D->SetVisibility(false);
		D->RegisterComponent();
		return D;
	};
	for (int32 i = 0; i < DartPool; ++i)
	{
		Darts.Add(MakeDart(*FString::Printf(TEXT("Dart%d"), i)));
	}
	HeldDart = MakeDart(TEXT("HeldDart"));
	// Each thrower's flights wear their colour, with a touch of glow so they read across the room.
	for (const FDartsSeat& Seat : Seats)
	{
		FlightMaterials.Add(MovecadeKart::Art::Plain(this, Seat.Color, 0.45, 0.0, 0.18));
	}

	Reticle = NewObject<UStaticMeshComponent>(this, TEXT("Reticle"));
	Reticle->SetStaticMesh(Pub->GetRingMesh());
	ReticleMaterial = MovecadeKart::Art::Glow(this, FLinearColor::White);
	Reticle->SetMaterial(0, ReticleMaterial);
	Reticle->SetupAttachment(RootComponent);
	Reticle->SetMobility(EComponentMobility::Movable);
	Reticle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Reticle->SetCastShadow(false);
	Reticle->SetVisibility(false);
	Reticle->RegisterComponent();

	Avatar = NewObject<UArcadeAvatarComponent>(this, TEXT("Thrower"));
	Avatar->SetupAttachment(RootComponent);
	Avatar->RegisterComponent();
	if (Seats.Num() > 0)
	{
		Avatar->SetLook(Seats[0].Look);
		DressedFor = Seats[0].Id;
	}

	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		Trail = FxSys->MakeStream(Fx::EField::Fire, RootComponent, TEXT("DartTrail"));
		// Dust in the board's light: a slow drift of motes between the oche and the board.
		Motes = FxSys->MakeStream(Fx::EField::Flare, RootComponent, TEXT("DustMotes"), W(FVector(0.0, ADartsPub::BoardY + 0.5, 1.6)));
		if (Motes)
		{
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
			UKartFxSubsystem::UpdateStream(Motes, Dust, 6.0);
		}
	}

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SDartsHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}
	SetStage(EDartsStage::Intro);
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

FVector ADartsGame::BoardWorld(double X, double Y) const
{
	return W(ADartsPub::BoardPoint(X, Y, 0.02));
}

FDartsView ADartsGame::MakeView() const
{
	FDartsView View;
	const ULocalPlayer* Player = Viewer ? Viewer->GetLocalPlayer() : nullptr;
	if (Player && Player->ViewportClient && Player->ViewportClient->Viewport)
	{
		FSceneViewProjectionData Data;
		if (Player->GetProjectionData(Player->ViewportClient->Viewport, Data))
		{
			View.ViewProjection = Data.ComputeViewProjectionMatrix();
			View.Rect = Data.GetConstrainedViewRect();
			View.bValid = true;
		}
	}
	return View;
}

float ADartsGame::GetFlash() const
{
	const double Since = Clock - FlashAt;
	return Since < 0.0 || Since > 0.6 ? 0.f : FlashStrength * static_cast<float>(FMath::Exp(-Since / 0.14));
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
	return FMath::Clamp(1.f - static_cast<float>((Clock - CoachDoneAt) / 0.7), 0.f, 1.f);
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
		Cues.Play(this, DartsCueMatchOver);
		// The results don't change once the match ends: rank them once for the HUD.
		StandingsOrder.Reset();
		for (const FDartsPlayer* P : Leaderboard(Match))
		{
			StandingsOrder.Add(static_cast<int32>(P - Match.Players.GetData()));
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
	// darts:release payload bounds (runtime.ts dartsGestureFromPayload).
	FDartGesture G = Gesture;
	auto Bound = [](double V, double Fallback, double Lo, double Hi) { return FMath::Clamp(FMath::IsFinite(V) ? V : Fallback, Lo, Hi); };
	G.AimX = Bound(G.AimX, 0.0, -1.5, 1.5);
	G.AimY = Bound(G.AimY, 0.0, -1.5, 1.5);
	G.Power = Bound(G.Power, 0.0, 0.0, 1.0);
	G.SpinDegPerSec = Bound(G.SpinDegPerSec, 0.0, -2000.0, 2000.0);
	G.RollDegPerSec = Bound(G.RollDegPerSec, 0.0, -2000.0, 2000.0);
	G.Confidence = Bound(G.Confidence, 0.5, 0.0, 1.0);
	G.Steadiness = Bound(G.Steadiness, 0.5, 0.0, 1.0);
	G.PeakMs2 = Bound(G.PeakMs2, 0.0, 0.0, 120.0);
	G.DurationMs = Bound(G.DurationMs, 0.0, 0.0, 1000.0);
	// Settle the clock to now first, so a queued release cannot ride a flight that already landed (runtime.ts).
	while (Accumulator >= StepMs)
	{
		StepFixed(StepMs);
		Accumulator -= StepMs;
	}
	const FDartsPlayer* Still = Match.ActivePlayer();
	if (!Still || Still->Id != PlayerId)
	{
		return false;
	}
	Throw(PlayerId, G);
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

void ADartsGame::StepFixed(double DtMs)
{
	StepMatch(Match, DtMs);
	// Present what the step changed: new flights (the release), arrivals, rounds and finishes.
	for (const FDartFlight& Flight : Match.Flights)
	{
		if (!SeenFlights.Contains(Flight.Id))
		{
			SeenFlights.Add(Flight.Id);
			Cues.Play(this, DartsCueRelease, static_cast<float>(0.6 + 0.4 * Flight.Power), static_cast<float>(0.9 + 0.35 * Flight.Power));
		}
	}
	for (const FDartsEffect& Effect : Match.Effects)
	{
		if (!SeenEffects.Contains(Effect.Id))
		{
			SeenEffects.Add(Effect.Id);
			OnArrival(Effect);
		}
	}
}

void ADartsGame::OnArrival(const FDartsEffect& Effect)
{
	switch (Effect.Kind)
	{
	case EEffectKind::Round:
		// The engine announces the round as the third dart leaves the hand; the visit card pays it off when it sticks.
		PendingBonus.Add(Effect.PlayerId, Effect.ScoreDelta);
		return;
	case EEffectKind::Finished:
		Celebrate(48.0, SeatOf(Effect.PlayerId) ? SeatOf(Effect.PlayerId)->Color : FLinearColor::White);
		return;
	default:
		break;
	}
	// A landing: find it (by position and player) among the darts on the board.
	const FDartsPlayer* Player = Match.FindPlayer(Effect.PlayerId);
	const FDartLanding* Landing = nullptr;
	if (Player)
	{
		for (int32 i = Player->Landings.Num() - 1; i >= 0; --i)
		{
			if (Player->Landings[i].X == Effect.X && Player->Landings[i].Y == Effect.Y)
			{
				Landing = &Player->Landings[i];
				break;
			}
		}
	}
	if (!Landing)
	{
		return;
	}
	LastLanding = *Landing;
	LastLandingAt = Clock;
	// The next dart in the board retires the visit card.
	if (LastVisit.At > 0.0 && Clock > LastVisit.At && LastVisit.EndAt > Clock)
	{
		LastVisit.EndAt = Clock;
	}
	WobbleSeed = Landing->Id;
	WobbleTarget = FMath::Max(WobbleTarget, Landing->ScoreDelta == 0 ? 0.3 : 0.72 * Effect.Intensity);
	// sound.ts: the thud, then the bull, the wire's ring on trebles and doubles, the bounty's sting.
	if (Landing->ScoreDelta == 0)
	{
		Cues.Play(this, DartsCueMiss);
	}
	else
	{
		Cues.Play(this, DartsCueThud, 1.f, static_cast<float>(0.92 + 0.18 * FMath::Min(1.0, Landing->ScoreDelta / 60.0)));
		switch (Landing->Hit.Ring)
		{
		case ERing::Bull: Cues.Play(this, DartsCueBull); break;
		case ERing::OuterBull: Cues.Play(this, DartsCueBull, 0.55f, 0.75f); break;
		case ERing::Triple: Cues.Play(this, DartsCueWire, 1.f); break;
		case ERing::Double: Cues.Play(this, DartsCueWire, 0.8f); break;
		default: break;
		}
		if (Landing->bBounty)
		{
			Cues.Play(this, DartsCueBounty);
		}
	}
	// The big ones land with a punch: the camera kicks in, the screen flashes, the pub cheers.
	const FDartsSeat* Seat = SeatOf(*Player);
	double Kick = 0.0;
	float Flash = 0.f;
	FLinearColor Tint = Fx::Hex(TEXT("fff2c4"));
	if (Landing->Hit.Ring == ERing::Bull)
	{
		Kick = 1.0;
		Flash = 0.55f;
	}
	else if (Landing->bBounty)
	{
		Kick = 0.65;
		Flash = 0.3f;
		Tint = Seat ? FMath::Lerp(Seat->Color, FLinearColor::White, 0.5f) : Tint;
	}
	else if (Landing->Hit.Ring == ERing::OuterBull)
	{
		Kick = 0.5;
		Flash = 0.16f;
	}
	else if (Landing->Hit.Ring == ERing::Triple)
	{
		Kick = 0.3;
	}
	Punch = FMath::Max(Punch, Kick);
	if (Flash > 0.f)
	{
		FlashAt = Clock;
		FlashStrength = Flash;
		FlashColor = Tint;
	}
	// Test hook: -DartsShotBig grabs the frame just after a big hit lands (the punch and the flash).
	static const bool bShotBig = FParse::Param(FCommandLine::Get(), TEXT("DartsShotBig"));
	if (bShotBig && Kick >= 0.6 && Viewer)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (Viewer)
			{
				Viewer->ConsoleCommand(TEXT("Shot showui"));
			}
		}), 0.07f, false);
	}
	if (Kick >= 0.6)
	{
		// The crowd: the pub's murmur, lifted and quickened into a cheer, on its own player so it isn't throttled.
		CrowdCues.Play(this, DartsCueChatter, Kick >= 1.0 ? 3.2f : 2.4f, 1.18f);
		Celebrate(Kick >= 1.0 ? 22.0 : 12.0, Seat ? Seat->Color : FLinearColor::White);
	}
	ImpactFx(*Landing);
	if (Landing->DartIndex == DartsPerRound - 1)
	{
		FinishVisit(*Player, *Landing);
	}
}

void ADartsGame::FinishVisit(const FDartsPlayer& Player, const FDartLanding& Last)
{
	FDartsVisitSummary V;
	V.PlayerId = Player.Id;
	V.Round = Last.Round;
	int32 Bulls = 0;
	for (const FDartLanding& L : Player.Landings)
	{
		if (L.Round != Last.Round)
		{
			continue;
		}
		V.Labels.Add(L.ScoreDelta == 0 ? FString(TEXT("MISS")) : L.Hit.Label);
		V.Points.Add(L.ScoreDelta);
		V.Total += L.ScoreDelta;
		V.Bounties += L.bBounty ? 1 : 0;
		Bulls += L.Hit.Ring == ERing::Bull || L.Hit.Ring == ERing::OuterBull ? 1 : 0;
	}
	V.Bonus = PendingBonus.FindRef(Player.Id);
	PendingBonus.Remove(Player.Id);
	// The checkout board's calls, scaled to this game's bounty-doubled scores.
	if (V.Bounties >= DartsPerRound)
	{
		V.Call = LOCTEXT("CallHatTrick", "BOUNTY HAT-TRICK!");
	}
	else if (Bulls >= DartsPerRound)
	{
		V.Call = LOCTEXT("CallBulls", "THREE IN THE BULL!");
	}
	else if (V.Total >= 180)
	{
		V.Call = LOCTEXT("Call180", "ONE HUNDRED AND EIGHTY!");
	}
	else if (V.Total >= 140)
	{
		V.Call = LOCTEXT("Call140", "TON-FORTY!");
	}
	else if (V.Total >= 100)
	{
		V.Call = LOCTEXT("CallTon", "TON!");
	}
	else if (V.Total == 0)
	{
		V.Call = LOCTEXT("CallBlank", "BLANK VISIT");
	}
	else
	{
		V.Call = FText::Format(LOCTEXT("CallVisit", "VISIT {0}"), V.Round + 1);
	}
	V.At = Clock;
	LastVisit = MoveTemp(V);
	const bool bBig = LastVisit.Total >= 100 || LastVisit.Bonus > 0;
	Cues.Play(this, DartsCueRound, bBig ? 1.f : 0.7f, bBig ? 1.12f : 1.f);
	if (bBig)
	{
		const FDartsSeat* Seat = SeatOf(Player);
		Celebrate(LastVisit.Total >= 180 ? 40.0 : 21.0, Seat ? Seat->Color : FLinearColor::White);
	}
}

void ADartsGame::ImpactFx(const FDartLanding& L)
{
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	if (!FxSys)
	{
		return;
	}
	// impactFx.ts tiers: dust, sparks, spark speed, ring (m), flash; the port adds sisal chips knocked out.
	struct FTier { int32 Dust; int32 Sparks; double Speed; double Ring; double Flash; int32 Chips; };
	const double R = L.Hit.Radius;
	FTier T = { 3, 0, 0.0, 0.0, 0.0, 3 };
	switch (L.Hit.Ring)
	{
	case ERing::Bull: T = { 12, 40, 3.6, 0.55, 0.28, 10 }; break;
	case ERing::OuterBull: T = { 10, 24, 3.0, 0.36, 0.16, 8 }; break;
	case ERing::Triple: T = { 9, 18, 2.6, 0.30, 0.12, 7 }; break;
	case ERing::Double: T = { 9, 12, 2.2, 0.26, 0.08, 7 }; break;
	case ERing::Single: T = { 8, 0, 0.0, 0.0, 0.0, 5 }; break;
	default: T = R <= 1.32 ? FTier{ 5, 0, 0.0, 0.0, 0.0, 6 } : FTier{ 3, 0, 0.0, 0.0, 0.0, 6 }; break;
	}
	const FDartsSeat* Seat = SeatOf(L.PlayerId);
	const bool bBull = L.Hit.Ring == ERing::Bull || L.Hit.Ring == ERing::OuterBull;
	FLinearColor Accent = bBull ? Fx::Hex(TEXT("#ffd166")) : FLinearColor::White;
	if (L.bBounty)
	{
		T.Sparks = FMath::Max(14, FMath::RoundToInt32(T.Sparks * 1.3));
		T.Speed = FMath::Max(2.4, T.Speed);
		T.Ring = FMath::Max(0.32, T.Ring);
		T.Flash = FMath::Min(0.32, T.Flash + 0.08);
		Accent = Seat ? Seat->Color : Accent;
	}
	const FVector At = W(ADartsPub::BoardPoint(L.X, L.Y, 0.03));
	const FVector Out(-1.0, 0.0, 0.0);
	TArray<Fx::FKartFxEmit, TInlineAllocator<5>> Emits;
	{
		Fx::FKartFxEmit Dust;
		Dust.Field = Fx::EField::Smoke;
		Dust.Position = At;
		Dust.Count = T.Dust;
		Dust.Direction = Out;
		Dust.Spread = 1.1;
		Dust.Speed = FVector2D(0.2, 0.7);
		Dust.Life = FVector2D(0.35, 0.7);
		Dust.Size = FVector2D(0.025, 0.06);
		Dust.SizeEnd = 1.8;
		Dust.Color = Fx::Hex(TEXT("#d9c7ae"));
		Dust.ColorEnd = Fx::Hex(TEXT("#a8977f"));
		Dust.Intensity = 0.7;
		Emits.Add(Dust);
	}
	{
		// Sisal (or, off the board, wood) chips flicked out of the hole, tumbling down.
		const bool bWood = L.Hit.Ring == ERing::Miss && R > 1.32;
		Fx::FKartFxEmit Chips;
		Chips.Field = Fx::EField::Confetti;
		Chips.Position = At;
		Chips.Count = T.Chips;
		Chips.Direction = (Out + FVector(0.0, 0.0, 0.45)).GetSafeNormal();
		Chips.Spread = 0.9;
		Chips.Speed = FVector2D(0.8, 1.9);
		Chips.Life = FVector2D(0.45, 0.8);
		Chips.Size = FVector2D(0.009, 0.018);
		Chips.SizeEnd = 0.8;
		Chips.Color = bWood ? Fx::Hex(TEXT("#6b4129")) : Fx::Hex(TEXT("#e6d6b8"));
		Chips.ColorEnd = Chips.Color;
		Chips.Intensity = 2.0;
		Emits.Add(Chips);
	}
	if (T.Sparks > 0)
	{
		Fx::FKartFxEmit Sparks;
		Sparks.Field = Fx::EField::Sparks;
		Sparks.Position = At;
		Sparks.Count = T.Sparks;
		Sparks.Direction = Out;
		Sparks.Spread = 1.25;
		Sparks.Speed = FVector2D(T.Speed * 0.5, T.Speed);
		Sparks.Life = FVector2D(0.2, 0.42);
		Sparks.Size = FVector2D(0.01, 0.018);
		Sparks.Color = Accent;
		Sparks.ColorEnd = Accent * 0.6f;
		Sparks.Intensity = 3.0;
		Emits.Add(Sparks);
	}
	if (T.Ring > 0.0)
	{
		Fx::FKartFxEmit Ring;
		Ring.Field = Fx::EField::Ring;
		Ring.Position = At;
		Ring.Count = 1;
		Ring.Direction = Out;
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.45, 0.45);
		Ring.Size = FVector2D(T.Ring * 0.3, T.Ring * 0.3);
		Ring.SizeEnd = 3.3;
		Ring.Color = Accent;
		Ring.ColorEnd = Accent;
		Ring.Intensity = 2.0;
		Emits.Add(Ring);
	}
	if (T.Flash > 0.0)
	{
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = At;
		Flash.Count = 1;
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.18, 0.18);
		Flash.Size = FVector2D(T.Flash * 2.2, T.Flash * 2.2);
		Flash.SizeEnd = 0.4;
		Flash.Color = Accent;
		Flash.ColorEnd = Accent;
		Flash.Intensity = 3.0;
		Emits.Add(Flash);
	}
	for (const Fx::FKartFxEmit& E : Emits)
	{
		FxSys->Emit(E);
	}
}

void ADartsGame::Celebrate(double Confetti, const FLinearColor& Color)
{
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		TArray<Fx::FKartFxEmit> Out;
		Fx::ConfettiBurst(W(ADartsPub::BoardPoint(0.0, 1.5, 0.3)), FVector(-1.0, 0.0, 0.4).GetSafeNormal(), Out);
		for (Fx::FKartFxEmit& E : Out)
		{
			E.Count = FMath::Max(1, FMath::RoundToInt32(E.Count * Confetti / 48.0));
			// The paper is lit: in the pub's dim exposure it needs lifting or it reads as dark scraps.
			if (E.Field == Fx::EField::Confetti)
			{
				E.Intensity *= 3.5;
			}
		}
		FxSys->Emit(Out);
	}
}

void ADartsGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
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
	{
		UpdateLocalInput(Dt);
		UpdateBot();
		// The rules step on a fixed 60 Hz clock, so a throw lands identically at any frame rate.
		Accumulator += Dt * 1000.0;
		while (Accumulator >= StepMs)
		{
			StepFixed(StepMs);
			Accumulator -= StepMs;
		}
		const FDartsPlayer* Active = Match.ActivePlayer();
		const FString& ActiveId = Active ? Active->Id : FString();
		if (ActiveId != AnnouncedThrower)
		{
			AnnouncedThrower = ActiveId;
			ThrowerSeat = Active ? Seats.IndexOfByPredicate([&ActiveId](const FDartsSeat& S) { return S.Id == ActiveId; }) : INDEX_NONE;
			PhoneDetectors.Reset();
			Local = FLocalThrow();
			BotReadyAt = Clock + DartsBotFirstDelay;
			const FDartsSeat* Seat = Seats.IsValidIndex(ThrowerSeat) ? &Seats[ThrowerSeat] : nullptr;
			if (Seat && Seat->Id != DressedFor)
			{
				DressedFor = Seat->Id;
				Avatar->SetLook(Seat->Look);
				HandBone = INDEX_NONE;
			}
			// The first local visit of the session gets the coach marks.
			if (Seat && Seat->Kind == EThrowerKind::Local && !DartsGCoachDone && !bCoachShown)
			{
				bCoachShown = true;
				CoachDoneAt = -1.0;
			}
			if (OnTurn && !ActiveId.IsEmpty())
			{
				OnTurn(ActiveId);
			}
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
		break;
	}
	case EDartsStage::Finished:
		break;
	}
	UpdateAmbience();
	UpdateDarts();
	UpdateTrail();
	UpdateBoard(Dt);
	UpdateReticle();
	UpdateThrower(Dt);
	UpdateCamera(Dt);
	UpdateChalk();
}

void ADartsGame::UpdateChalk()
{
	if (!Pub)
	{
		return;
	}
	// The chalkboard re-chalks only when a score moves (SetText rebuilds the text's render state).
	uint32 Key = 0;
	for (const FDartsPlayer& P : Match.Players)
	{
		Key = HashCombine(Key, ::GetTypeHash(P.Score));
	}
	if (bChalked && Key == ChalkKey)
	{
		return;
	}
	bChalked = true;
	ChalkKey = Key;
	TArray<FString> Chalk;
	for (const FDartsPlayer* P : Leaderboard(Match))
	{
		Chalk.Add(FString::Printf(TEXT("%s  %d"), *P->Name.Left(10).ToUpper(), P->Score));
	}
	Pub->SetChalk(Chalk);
}

void ADartsGame::UpdateLocalInput(float Dt)
{
	const FDartsSeat* Thrower = GetThrower();
	if (!Viewer || !Thrower || Thrower->Kind != EThrowerKind::Local || Match.Phase != EMatchPhase::Playing || Match.TurnDarts >= DartsPerRound)
	{
		return;
	}
	APlayerController* PC = Viewer;
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };
	// Aim: keys, the left stick, the mouse (D8). The reticle's speed is the hand's unsteadiness.
	FVector2D Move((Down(EKeys::D) || Down(EKeys::Right) ? 1.0 : 0.0) - (Down(EKeys::A) || Down(EKeys::Left) ? 1.0 : 0.0),
		(Down(EKeys::W) || Down(EKeys::Up) ? 1.0 : 0.0) - (Down(EKeys::S) || Down(EKeys::Down) ? 1.0 : 0.0));
	Move.X += PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Move.Y += PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY);
	float MouseX = 0.f, MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	const FVector2D Before = Local.Aim;
	Local.Aim += Move * 0.75 * Dt;
	if (!Local.bGripped)
	{
		Local.Aim += FVector2D(MouseX, MouseY) * 0.0022;
	}
	Local.Aim.X = FMath::Clamp(Local.Aim.X, -1.0, 1.0);
	Local.Aim.Y = FMath::Clamp(Local.Aim.Y, -1.0, 1.0);
	const double Speed = Dt > 0.f ? (Local.Aim - Before).Size() / Dt : 0.0;
	Local.AimSpeed += (Speed - Local.AimSpeed) * 0.3;

	const FKey GripKeys[] = { EKeys::SpaceBar, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter };
	bool bGripDown = bTestGrip;
	for (const FKey& K : GripKeys)
	{
		bGripDown |= Down(K);
	}
	if (bGripDown && !Local.bGripped)
	{
		Local.bGripped = true;
		Local.GripAt = Clock;
		Local.bMouseFlick = false;
		Local.FlickSide = 0.0;
		MouseTrail.Reset();
	}
	if (Local.bGripped)
	{
		// The mouse: a flick up the screen throws, its speed over the last 200 ms is the power, its sideways drift the twist.
		MouseTrail.Add(FVector2D(Clock, MouseY));
		MouseTrail.RemoveAll([this](const FVector2D& S) { return S.X < Clock - 0.2; });
		double Up = 0.0;
		for (const FVector2D& S : MouseTrail)
		{
			Up += FMath::Max(0.0, static_cast<double>(S.Y));
		}
		const double FlickPower = FMath::Clamp(Up / 0.2 / 1500.0, 0.0, 1.0);
		if (FlickPower > 0.15)
		{
			Local.bMouseFlick = true;
		}
		Local.FlickSide = FMath::Lerp(Local.FlickSide, static_cast<double>(MouseX), 0.3);
		Local.Power = Local.bMouseFlick ? FMath::Max(Local.Power * FMath::Exp(-Dt * 2.0), FlickPower)
			: 0.5 - 0.5 * FMath::Cos(2.0 * UE_DOUBLE_PI * (Clock - Local.GripAt) / MeterPeriod);
	}
	if (Local.bGripped && !bGripDown)
	{
		FDartGesture G;
		G.AimX = Local.Aim.X;
		G.AimY = Local.Aim.Y;
		G.Power = FMath::Max(0.05, Local.Power);
		G.SpinDegPerSec = Local.bMouseFlick ? FMath::Clamp(Local.FlickSide * 25.0, -400.0, 400.0) : PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX) * 180.0;
		G.Steadiness = FMath::Clamp(1.0 - Local.AimSpeed / 2.1, 0.0, 1.0);
		Local.bGripped = false;
		Local.Power = 0.0;
		Throw(Thrower->Id, G);
		// The first dart is away: the coach marks have done their job.
		if (bCoachShown && CoachDoneAt < 0.0)
		{
			CoachDoneAt = Clock;
			DartsGCoachDone = true;
		}
	}
}

void ADartsGame::UpdateBot()
{
	const FDartsSeat* Thrower = GetThrower();
	const FDartsPlayer* Player = Match.ActivePlayer();
	if (!Thrower || !Player || Thrower->Kind != EThrowerKind::Bot || Match.Phase != EMatchPhase::Playing
		|| Match.TurnDarts >= DartsPerRound || Clock < BotReadyAt)
	{
		return;
	}
	// Aim for the called shot: the treble when sharp, its fat single when not, the bull when wild.
	const double S = Thrower->Skill;
	const int32 Bounty = BountyForRound(Player->Round);
	int32 Index = 0;
	while (Index < SectorCount && Sectors()[Index] != Bounty)
	{
		++Index;
	}
	const double Angle = SectorCenterAngle(Index);
	const double Radius = S > 0.7 ? (TripleInnerRadius + TripleOuterRadius) / 2.0 : (S > 0.45 ? 0.78 : 0.0);
	const FVector2D Target(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
	const double Scatter = 0.05 + 0.2 * (1.0 - S);
	FDartGesture G;
	G.AimX = Target.X / AimReach + Gauss(BotRng) * Scatter;
	G.AimY = Target.Y / AimReach + Gauss(BotRng) * Scatter;
	G.Power = SweetSpot + Gauss(BotRng) * (0.02 + 0.07 * (1.0 - S));
	G.SpinDegPerSec = Gauss(BotRng) * 30.0 * (1.0 - S);
	G.Steadiness = 0.85 + 0.15 * S;
	BotReadyAt = Clock + DartsBotNextDelay;
	Throw(Thrower->Id, G);
}

FVector ADartsGame::FlightWeb(const FDartFlight& F, double T) const
{
	// DartsScene.tsx writeFlightPosition: a scripted arc to the decided point, no physics.
	T = FMath::Clamp(T, 0.0, 1.0);
	const double Travel = 1.0 - FMath::Pow(1.0 - T, 3.0);
	const double Arc = FMath::Sin(UE_DOUBLE_PI * SmootherStep(T));
	return FVector(
		LaunchX * (1.0 - Travel) + F.X * ADartsPub::BoardRadius * Travel + F.Curve * 0.9 * Arc,
		LaunchY * (1.0 - Travel) + (ADartsPub::BoardY + F.Y * ADartsPub::BoardRadius) * Travel + (1.0 - F.Power) * 1.1 * Arc,
		ThrowDistance * (1.0 - Travel) + StuckZ * Travel);
}

void ADartsGame::PlaceDart(UStaticMeshComponent* Dart, const FVector& TipWeb, double Pitch, double Yaw)
{
	// The web's Euler (-pi/2 + pitch, yaw, 0, YXZ) on a +Y dart is UE pitch +pitch, yaw -yaw on a +X dart.
	Dart->SetWorldLocationAndRotation(W(TipWeb), FRotator(FMath::RadiansToDegrees(Pitch), -FMath::RadiansToDegrees(Yaw), 0.0));
	Dart->SetVisibility(true);
}

void ADartsGame::SetFlightColor(UStaticMeshComponent* Dart, int32 Lane)
{
	if (FlightMaterials.IsValidIndex(Lane))
	{
		// SetMaterial early-outs on the same material.
		Dart->SetMaterial(ADartsPub::DartFlightSlot, FlightMaterials[Lane]);
	}
}

void ADartsGame::UpdateDarts()
{
	int32 Used = 0;
	auto Stick = [this, &Used](const FDartLanding& L)
	{
		if (Used >= Darts.Num())
		{
			return;
		}
		const double Age = L.AgeMs;
		const double Bounce = FMath::Sin(Age / 38.0) * FMath::Exp(-Age / 260.0) * (1.0 - FMath::Exp(-Age / 70.0));
		const FVector Tip(L.X * ADartsPub::BoardRadius, ADartsPub::BoardY + L.Y * ADartsPub::BoardRadius + Bounce * 0.08,
			StuckZ + Bounce * 0.008 + FMath::Min(1.0, Age / 420.0) * 0.002);
		UStaticMeshComponent* D = Darts[Used++];
		SetFlightColor(D, L.LaneIndex);
		// A stuck dart shadows the board under the board spot; one in flight doesn't bother.
		D->SetCastShadow(true);
		PlaceDart(D, Tip, 0.14 + Bounce * 0.035, -0.08 + Bounce * 0.025);
	};
	// Only the current visit's darts are on the board (DartsScene.tsx:1369-1392).
	const FDartsPlayer* Visitor = Match.Players.IsValidIndex(Match.CurrentPlayerIndex) ? &Match.Players[Match.CurrentPlayerIndex] : nullptr;
	if (Visitor && Stage != EDartsStage::Intro)
	{
		const int32 Visit = (Visitor->DartsThrown - Match.TurnDarts) / DartsPerRound;
		const bool bEnded = Match.Phase == EMatchPhase::Ended;
		auto IsCurrent = [this, Visitor, Visit, bEnded](const FDartLanding& L)
		{
			const bool bThisVisit = bEnded ? L.Round == Visitor->Round - 1 : L.Round == Visit;
			return bThisVisit && !Match.Flights.ContainsByPredicate([&L](const FDartFlight& F) { return F.Landing.Id == L.Id; });
		};
		if (Visitor->Id != ShownVisitor)
		{
			// The board passed: the finished visit lingers until the next dart lands or a beat goes by.
			// The third dart lands in the same step that passes the board, so read the visit from the match.
			HeldOverPlayer = Match.Players.IndexOfByPredicate([this](const FDartsPlayer& P) { return P.Id == ShownVisitor; });
			if (Match.Players.IsValidIndex(HeldOverPlayer) && Match.Players[HeldOverPlayer].DartsThrown > 0)
			{
				HeldOverVisit = (Match.Players[HeldOverPlayer].DartsThrown - 1) / DartsPerRound;
			}
			else
			{
				HeldOverPlayer = INDEX_NONE;
			}
			HeldOverUntil = Clock + 1.6;
			ShownVisitor = Visitor->Id;
		}
		bool bAny = false;
		for (const FDartLanding& L : Visitor->Landings)
		{
			if (IsCurrent(L))
			{
				bAny = true;
				break;
			}
		}
		if (!bAny && Clock < HeldOverUntil && Match.Players.IsValidIndex(HeldOverPlayer))
		{
			for (const FDartLanding& L : Match.Players[HeldOverPlayer].Landings)
			{
				if (L.Round == HeldOverVisit)
				{
					Stick(L);
				}
			}
		}
		else
		{
			for (const FDartLanding& L : Visitor->Landings)
			{
				if (IsCurrent(L))
				{
					Stick(L);
				}
			}
		}
	}
	for (const FDartFlight& F : Match.Flights)
	{
		if (Used >= Darts.Num())
		{
			break;
		}
		const double T = F.Progress;
		const double S = SmootherStep(T);
		const double Pitch = FMath::Lerp(0.18 + (1.0 - F.Power) * 0.18 * FMath::Cos(UE_DOUBLE_PI * T), 0.14, S);
		const double Yaw = FMath::Lerp(F.Curve * 0.4 * (1.0 - T), -0.08, S);
		UStaticMeshComponent* D = Darts[Used++];
		SetFlightColor(D, F.LaneIndex);
		D->SetCastShadow(false);
		PlaceDart(D, FlightWeb(F, T), Pitch, Yaw);
	}
	for (int32 i = Used; i < Darts.Num(); ++i)
	{
		Darts[i]->SetVisibility(false);
	}
}

void ADartsGame::UpdateTrail()
{
	if (!Trail)
	{
		return;
	}
	// The newest flight streams a short glowing tail in its thrower's colour.
	const FDartFlight* F = Match.Flights.Num() > 0 ? &Match.Flights.Last() : nullptr;
	Fx::FKartFxEmit Tail;
	Tail.Field = Fx::EField::Fire;
	double Rate = 0.0;
	if (F && F->Progress < 0.97)
	{
		const FDartsSeat* Seat = Seats.IsValidIndex(F->LaneIndex) ? &Seats[F->LaneIndex] : nullptr;
		const FVector Pos = W(FlightWeb(*F, FMath::Max(0.0, F->Progress - 0.035)));
		Trail->SetWorldLocation(Pos);
		Tail.Position = Pos;
		Tail.Spread = 0.2;
		Tail.Speed = FVector2D(0.0, 0.05);
		Tail.Life = FVector2D(0.14, 0.22);
		Tail.Size = FVector2D(0.035, 0.05);
		Tail.SizeEnd = 0.15;
		Tail.Color = Seat ? FMath::Lerp(Seat->Color, FLinearColor::White, 0.35f) : FLinearColor::White;
		Tail.ColorEnd = Seat ? Seat->Color : FLinearColor::White;
		Tail.Intensity = 2.2;
		Rate = 140.0;
	}
	UKartFxSubsystem::UpdateStream(Trail, Tail, Rate);
}

void ADartsGame::UpdateBoard(float Dt)
{
	if (!Pub)
	{
		return;
	}
	// The board shivers when a dart lands (DartsScene.tsx board wobble).
	WobbleTarget *= FMath::Exp(-2.8 * Dt);
	Wobble += (WobbleTarget - Wobble) * (1.0 - FMath::Exp(-28.0 * Dt));
	const double A = Wobble;
	const double RotZ = FMath::Sin(WobbleSeed * 0.001 + Clock * 18.0) * A * 0.016;
	const double RotX = FMath::Cos(WobbleSeed * 0.001 + Clock * 11.0) * A * 0.009;
	if (USceneComponent* Pivot = Pub->GetBoardPivot())
	{
		Pivot->SetRelativeRotation(FRotator(FMath::RadiansToDegrees(RotX), 0.0, FMath::RadiansToDegrees(RotZ)));
	}
	// The called shot: the visit's bounty wedge, pulsing, with its outline brighter.
	const FDartsPlayer* Active = Match.ActivePlayer();
	if (Stage != EDartsStage::Match || !Active)
	{
		Pub->HideBounty();
		return;
	}
	const int32 Visit = FMath::Min((Active->DartsThrown - Match.TurnDarts) / DartsPerRound, RoundCount - 1);
	const int32 Bounty = BountyForRound(Visit);
	int32 Index = 0;
	while (Index < SectorCount && Sectors()[Index] != Bounty)
	{
		++Index;
	}
	Pub->ShowBounty(Index, static_cast<float>(0.5 + 0.5 * FMath::Sin(Clock * 4.0)));
}

bool ADartsGame::GetReticle(FVector2D& OutAim) const
{
	const FDartsSeat* Thrower = GetThrower();
	if (Stage != EDartsStage::Match || Match.Phase != EMatchPhase::Playing || !Thrower || Match.TurnDarts >= DartsPerRound)
	{
		return false;
	}
	if (Thrower->Kind == EThrowerKind::Local)
	{
		OutAim = Local.Aim;
		return true;
	}
	if (Thrower->Kind == EThrowerKind::Phone)
	{
		const FDartThrowDetector* D = PhoneDetectors.Find(Thrower->Id);
		OutAim = D ? FVector2D(D->Aim().X, D->Aim().Y) : FVector2D::ZeroVector;
		return true;
	}
	return false;
}

void ADartsGame::UpdateReticle()
{
	FVector2D Aim;
	const bool bShow = GetReticle(Aim) && Clock - LastLandingAt > 0.25;
	Reticle->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	const FDartsSeat* Thrower = GetThrower();
	// The ring breathes; while the dart is gripped it tightens with the power and turns mint in the sweet spot.
	const bool bSweet = Local.bGripped && FMath::Abs(Local.Power - SweetSpot) < 0.05;
	const double Breath = (1.0 + 0.08 * FMath::Sin(Clock * 3.0)) * (Local.bGripped ? 1.0 - 0.25 * Local.Power : 1.0);
	Reticle->SetWorldTransform(FTransform(FRotator::ZeroRotator, W(ADartsPub::BoardPoint(Aim.X * AimReach, Aim.Y * AimReach, 0.03)), FVector(Breath)));
	const FLinearColor Color = bSweet ? MovecadeKart::Art::Hex(TEXT("#3ee6a8")) : (Thrower ? Thrower->Color : FLinearColor::White);
	ReticleMaterial->SetVectorParameterValue(DartsNameColor, Color * ((Local.bGripped ? 3.5f : 2.2f) * ADartsPub::GlowGain));
}

void ADartsGame::UpdateThrower(float Dt)
{
	// DartsAvatar.tsx: on the oche left of the board's line, facing the board.
	const FVector Feet = W(FVector(ADartsPub::ThrowerX, ADartsPub::FloorY + 0.024, ADartsPub::OcheZ));
	const FVector Board = W(FVector(0.0, ADartsPub::BoardY, 0.0));
	const FRotator Facing(0.0, (Board - Feet).Rotation().Yaw + 6.0, 0.0);
	Avatar->SetWorldLocationAndRotation(Feet, Facing);
	FArcadePoseInput& Pose = Avatar->Pose();

	FVector2D Aim = FVector2D::ZeroVector;
	const bool bAiming = GetReticle(Aim);
	const FDartsSeat* Thrower = GetThrower();
	const bool bBotAiming = Thrower && Thrower->Kind == EThrowerKind::Bot && Stage == EDartsStage::Match && Match.Phase == EMatchPhase::Playing && Match.TurnDarts < DartsPerRound;
	// The throw: a snap forward over 90 ms from the release (the dart leaves at the cheek, the arm chases it), then
	// the follow-through held and eased back over the next half second.
	const double Since = Clock - LastReleaseAt;
	const double Flick = Since < 0.0 || Since > 0.7 ? 0.0 : (Since < 0.09 ? Smooth(0.0, 0.09, Since) : FMath::Exp(-(Since - 0.09) / 0.2));
	// Bots draw back over the 0.55 s before they throw; a local hand draws back with the meter.
	const double Windup = Local.bGripped ? 0.35 + 0.65 * Local.Power : (bBotAiming ? Smooth(BotReadyAt - 0.55, BotReadyAt - 0.05, Clock) : 0.0);
	const double Drive = FMath::Min(1.0, 0.28 + FMath::Abs(Aim.Y) * 0.38);
	const bool bActive = bAiming || bBotAiming || Flick > 0.02;
	// The dart by the cheek, drawn back with the wind-up, punched out along the line by the flick.
	FVector Hand(0.42 - 0.24 * Windup + 0.6 * Flick, 0.16 + Aim.X * 0.17, 0.42 + Aim.Y * 0.2 + 0.09 * Windup - 0.12 * Flick);
	Pose.Hand[1] = Hand;
	Pose.HandWeight[1] = bActive ? 1.f : 0.f;
	Pose.Hand[0] = FVector(0.25, -0.35, -0.55);
	Pose.HandWeight[0] = bActive ? 0.45f : 0.f;
	Pose.Lean = static_cast<float>(bActive ? 0.06 + 0.1 * Drive + 0.14 * Flick - 0.03 * Windup : 0.02);
	Pose.Twist = static_cast<float>(bActive ? -0.18 - 0.06 * Windup + 0.08 * Flick : 0.0);
	Pose.Crouch = 0.04f;
	const FVector LookAt = W(ADartsPub::BoardPoint(Aim.X * 0.5, Aim.Y * 0.5 + 0.12));
	Pose.Look = (LookAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, 150.0)).GetSafeNormal();
	Pose.Look = Facing.UnrotateVector(Pose.Look);
	Pose.LookWeight = bActive ? 0.85f : 0.5f;

	// Reactions: the bull and the bounty cheer, a miss slumps, the end wins or loses.
	const double SinceLanding = Clock - LastLandingAt;
	const double React = SinceLanding < 1.5 ? Smooth(0.0, 0.15, SinceLanding) * Smooth(1.5, 1.2, SinceLanding) : 0.0;
	float Cheer = 0.f, Slump = 0.f;
	if (LastLanding.Hit.Ring == ERing::Bull || LastLanding.bBounty)
	{
		Cheer = 1.f;
	}
	else if (LastLanding.ScoreDelta == 0)
	{
		Slump = 0.7f;
	}
	else if (LastLanding.ScoreDelta >= 40)
	{
		Cheer = 0.45f;
	}
	if (Stage == EDartsStage::Finished)
	{
		const bool bWon = Thrower ? Match.WinnerIds.Contains(Thrower->Id) : false;
		Cheer = FMath::Max(Cheer, bWon ? 0.8f : 0.f);
	}
	Pose.Cheer = Cheer * static_cast<float>(React) + (Stage == EDartsStage::Finished ? 0.35f : 0.f);
	Pose.Slump = Slump * static_cast<float>(React);
	Pose.Idle = 1.f;

	// The held dart, in the throwing hand, pointing at the board, while the hand has one to throw.
	const bool bHeld = bActive && Since > 0.32 && Match.Flights.Num() == 0;
	HeldDart->SetVisibility(bHeld);
	if (bHeld)
	{
		FVector HandWorld = Avatar->GetComponentLocation();
		if (USkeletalMeshComponent* Mesh = Avatar->GetMesh())
		{
			if (HandBone == INDEX_NONE)
			{
				HandBone = Mesh->GetBoneIndex(DartsNameRightHand);
			}
			HandWorld = HandBone != INDEX_NONE ? Mesh->GetBoneTransform(HandBone).GetLocation() : Avatar->BoneWorld(DartsNameRightHand);
		}
		const FVector ToBoard = (W(ADartsPub::BoardPoint(Aim.X * AimReach, Aim.Y * AimReach)) - HandWorld).GetSafeNormal();
		HeldDart->SetWorldLocationAndRotation(HandWorld + ToBoard * 14.0, ToBoard.Rotation() + FRotator(10.0, 0.0, 0.0));
		SetFlightColor(HeldDart, ThrowerSeat);
	}
}

void ADartsGame::UpdateCamera(float Dt)
{
	// cameraDirector.ts + DartsScene overrides: home, the aim over the shoulder, the reaction at the board, results.
	const FVector S(ADartsPub::ThrowerX, ADartsPub::FloorY, ADartsPub::OcheZ);
	FVector Pos(0.16 * FMath::Sin(Clock * 0.24), 1.1, 6.7), Look(0.0, 0.55, 0.0);
	double Fov = 44.0;
	bool bCut = false;
	FVector2D Aim;
	const FDartsSeat* Thrower = GetThrower();
	const bool bBotUp = Thrower && Thrower->Kind == EThrowerKind::Bot;
	const double Since = Clock - LastLandingAt;
	const double Hold = LastLanding.DartIndex == DartsPerRound - 1 ? DartsReactLastSeconds : DartsReactSeconds;
	bReacting = false;
	switch (Stage)
	{
	case EDartsStage::Intro:
	{
		const double T = Smooth(0.0, 1.0, GetStageAge() / IntroSeconds);
		Pos = FMath::Lerp(FVector(1.7, 1.7, 8.2), Pos, T * 0.6);
		Look = FMath::Lerp(FVector(0.0, ADartsPub::BoardY, -0.75), Look, T);
		Fov = FMath::Lerp(46.0, 44.0, T);
		break;
	}
	case EDartsStage::Match:
		// A thrower winding up for the next dart (a bot drawing back, a local hand on the grip) ends the reaction early,
		// so the camera is back over the shoulder by the time the dart leaves.
		if (Since < Hold && Match.Flights.Num() == 0 && !Local.bGripped && !(bBotUp && Clock >= BotReadyAt - 0.55))
		{
			// The reaction: a hard cut to a close look at the dart in the board from off its right shoulder (in
			// front of the thrower, so nobody's head fills the frame), easing in.
			const double Settle = Smooth(0.0, 1.0, Since / 0.9);
			const FVector Landing = ADartsPub::BoardPoint(LastLanding.X, LastLanding.Y);
			Pos = FVector(Landing.X * 0.4 + 0.55, Landing.Y * 0.4 + ADartsPub::BoardY * 0.6 + 0.3, 3.5 - 0.4 * Settle);
			Look = FMath::Lerp(FVector(0.0, ADartsPub::BoardY, 0.0), Landing, 0.45);
			Fov = 32.0 - 2.0 * Settle;
			bCut = Since < 0.05;
			bReacting = true;
		}
		else if ((GetReticle(Aim) || bBotUp) && Match.Phase == EMatchPhase::Playing)
		{
			// Over the throwing shoulder, high enough that the board sits clear of the head (the web's aim shot,
			// lifted and opened: at TV distance the board has to read whole while the thrower lines up).
			Pos = FVector(S.X + 1.02, S.Y + 2.1, S.Z + 2.3);
			Look = FVector(-0.12, ADartsPub::BoardY - 0.12, 0.0);
			Fov = 40.0;
		}
		break;
	case EDartsStage::Finished:
		Pos = FVector(2.15, 1.5, 6.4);
		Look = FVector(-0.3, 0.55, 0.3);
		Fov = 40.0;
		break;
	}
	if (!bCamPlaced || bCut)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-8.0 * FMath::Min<double>(Dt, 1.0 / 20.0));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * K;
	CamFov += (Fov - CamFov) * K;
	// The impact's shake (impactFx.ts), decaying with the board's wobble, plus the big hit's punch: a quick push in
	// and a harder shake that settle in a quarter second.
	Punch *= FMath::Exp(-5.5 * Dt);
	const FVector Shake = FVector(FMath::Sin(Clock * 53.0), FMath::Sin(Clock * 47.0 + 1.3), 0.0) * (Wobble * 0.006 + Punch * 0.012);
	const FVector From = W(CamPos + Shake);
	const FVector To = W(CamLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	double Aspect = 16.0 / 9.0;
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		FVector2D Size;
		Viewport->GetViewportSize(Size);
		Aspect = Size.Y > 0.0 ? Size.X / Size.Y : Aspect;
	}
	// three.js gives a vertical field of view; UE's is horizontal.
	const double VFov = FMath::Max(10.0, CamFov - 6.0 * Punch);
	const double HFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HFov));
}

void ADartsGame::UpdateAmbience()
{
	// The pub murmurs: chatter at 2.5 s, then every 9 s (sound.ts).
	if (Clock >= NextChatterAt)
	{
		Cues.Play(this, DartsCueChatter);
		NextChatterAt = Clock + 9.0;
	}
}

void ADartsGame::Rematch()
{
	TArray<FMatchSeed> Seeds;
	for (const FDartsSeat& Seat : Seats)
	{
		Seeds.Add({ Seat.Id, Seat.Name });
	}
	Match = CreateMatch(Seeds);
	SeenEffects.Reset();
	SeenFlights.Reset();
	AnnouncedThrower.Reset();
	ThrowerSeat = INDEX_NONE;
	PhoneDetectors.Reset();
	PendingBonus.Reset();
	StandingsOrder.Reset();
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
	if (OnRematch) OnRematch(); else Rematch();
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

#undef LOCTEXT_NAMESPACE
