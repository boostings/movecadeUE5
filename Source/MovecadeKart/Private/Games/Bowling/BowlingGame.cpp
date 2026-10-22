// ABowlingGame: setup, the turn flow (aim, delivery, roll, settle, reset) and the arcade loop's hooks.
#include "Games/Bowling/BowlingGame.h"

#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "BowlingGameTuning.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Games/Bowling/BowlingAlley.h"
#include "Games/Bowling/SBowlingHud.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Store/KartCosmetics.h"

DEFINE_LOG_CATEGORY_STATIC(LogBowling, Log, All);

using namespace MovecadeKart::Bowling;
using namespace MovecadeKart::Bowling::Tuning;

namespace
{
	/** The bot stream's seed: every local game's house bowlers throw the same sequence. */
	constexpr int32 BotSeed = 0xb041;
	/** The longest frame the game steps at once: a hitch never skips a phase. */
	constexpr double MaxStepSeconds = 0.1;
	/** The house murmurs along under everything, a little louder when the game is over. */
	constexpr double AmbienceSeconds = 6.5;
	constexpr float AmbienceVolume = 0.55f;
	constexpr float AmbienceVolumeFinished = 1.2f;
	constexpr float AmbiencePitch = 0.95f;
	/** A spare line reads on target within this of the outermost pins left; past WideX it reads wide. */
	constexpr double SpareTargetMargin = 0.12;
	constexpr double WideX = 0.3;
	/** presentation.ts: an open first ball that fells this many reads as a good ball, not an open one. */
	constexpr int32 GoodBeatPins = 7;
	/** The strike cam: the clock runs at this rate over the last metres before the rack. */
	constexpr double StrikeCamTimeScale = 0.36;
	/** The pinsetter's rake and setter table (web x, y, z sizes). */
	const FVector RakeSize(3.3, 0.42, 0.16);
	const FVector SetterSize(3.15, 0.26, 1.95);
	/** The ball skin's shell takes this share of the bowler's colour (store cosmetics may override it). */
	constexpr float BallTint = 0.8f;
	/** The HUD draws over the viewport's game layer. */
	constexpr int32 HudZOrder = 10;

	const FName CueRelease(TEXT("bowling.release"));
	const FName CueRoll(TEXT("bowling.roll"));
	const FName CueGutter(TEXT("bowling.gutter"));
	const FName CuePins(TEXT("bowling.pins"));
	const FName CueSweep(TEXT("bowling.sweep"));
	const FName CueStrike(TEXT("bowling.strike"));
	const FName CueSpare(TEXT("bowling.spare"));
	const FName CueMatchOver(TEXT("match.over"));
	/** The house: the pub murmur as the crowd on the other lanes. */
	const FName CueCrowd(TEXT("darts.pub-chatter"));

	const TArray<int32>& FullRack()
	{
		static const TArray<int32> Rack = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
		return Rack;
	}

	/** The beat a roll reads as (presentation.ts:40-58). */
	EBowlingBeat ClassifyBeat(const FShotResult& R, int32 StandingBefore, int32 Felled, bool bWasSplit, int32 Streak)
	{
		if (R.bGutter)
		{
			return EBowlingBeat::Gutter;
		}
		if (bWasSplit && Felled == StandingBefore)
		{
			return EBowlingBeat::SplitConvert;
		}
		if (StandingBefore == Pins && Felled == Pins)
		{
			return Streak >= 3 ? EBowlingBeat::Turkey : EBowlingBeat::Strike;
		}
		if (StandingBefore < Pins && Felled == StandingBefore)
		{
			return EBowlingBeat::Spare;
		}
		if (StandingBefore == Pins)
		{
			TArray<int32> Remaining;
			for (int32 Id = 1; Id <= Pins; ++Id)
			{
				if (!R.Felled.Contains(Id))
				{
					Remaining.Add(Id);
				}
			}
			if (IsSplit(Pins, Remaining))
			{
				return EBowlingBeat::Split;
			}
		}
		return Felled >= GoodBeatPins ? EBowlingBeat::Good : EBowlingBeat::Open;
	}

	/** An engine basic shape; a miss (a stripped engine install) leaves that lane part empty, with one warning. */
	UStaticMesh* LoadEngineMesh(const TCHAR* Path)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UE_CLOG(!Mesh, LogBowling, Warning, TEXT("No engine mesh %s: the lane part that uses it stays empty."), Path);
		return Mesh;
	}

	UStaticMeshComponent* NewLanePart(AActor& Owner, const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(&Owner, Name);
		Part->SetStaticMesh(Mesh);
		Part->SetMaterial(0, Material);
		Part->SetupAttachment(Owner.GetRootComponent());
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Part;
	}
}

ABowlingGame::ABowlingGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(BotSeed);
}

FShot ABowlingGame::ShowcaseShot()
{
	FShot Shot;
	Shot.Aim = -0.3;
	Shot.Curve = 0.14;
	Shot.Power = 0.8;
	return Shot;
}

EBowlingLine ABowlingGame::ReadLine(double EndX, bool bGutter, const TArray<int32>& Rack)
{
	if (bGutter)
	{
		return EBowlingLine::Gutter;
	}
	if (Rack.Num() > 0 && Rack.Num() < Pins)
	{
		// A spare: the target is what's left, not the pocket.
		double Lo = TNumericLimits<double>::Max(), Hi = -TNumericLimits<double>::Max();
		for (const int32 Id : Rack)
		{
			Lo = FMath::Min(Lo, PinPosition(Id).X);
			Hi = FMath::Max(Hi, PinPosition(Id).X);
		}
		if (EndX >= Lo - SpareTargetMargin && EndX <= Hi + SpareTargetMargin)
		{
			return EBowlingLine::OnTarget;
		}
		return FMath::Abs(EndX) > WideX ? EBowlingLine::Wide : EBowlingLine::Light;
	}
	if (FMath::Abs(EndX - PocketX) <= PocketWindow)
	{
		return EBowlingLine::Pocket;
	}
	if (FMath::Abs(EndX + PocketX) <= PocketWindow)
	{
		return EBowlingLine::Brooklyn;
	}
	if (FMath::Abs(EndX) < PocketX - PocketWindow)
	{
		return EBowlingLine::HeadOn;
	}
	return FMath::Abs(EndX) > WideX ? EBowlingLine::Wide : EBowlingLine::Light;
}

// ---- Setup ------------------------------------------------------------------------------------------------------

void ABowlingGame::Setup(ABowlingAlley* InAlley, const TArray<FBowlerSeat>& InSeats, APlayerController* InViewer,
	const FBowlingDebugFlags& InDebugFlags)
{
	check(InAlley);
	Alley = InAlley;
	Seats = InSeats;
	Viewer = InViewer;
	DebugFlags = InDebugFlags;
	TArray<FString> Order;
	for (const FBowlerSeat& Seat : Seats)
	{
		Order.Add(Seat.Id);
	}
	Match = CreateMatch(Order);
	RefreshBowler();
	Standing = FullRack();
	// Every bowling cue (and the crowd) loads now, not on its first play mid-game.
	for (const TCHAR* Prefix : { TEXT("bowling."), TEXT("darts.pub"), TEXT("stacker.perfect"), TEXT("lobby.go"), TEXT("match.") })
	{
		MovecadeKart::Games::FCuePlayer::Preload(Prefix);
	}

	CreateRack();
	CreateBall();
	Avatar = NewObject<UArcadeAvatarComponent>(this, TEXT("Bowler"));
	Avatar->SetupAttachment(RootComponent);
	Avatar->RegisterComponent();
	if (Seats.Num() > 0)
	{
		Avatar->SetLook(Seats[0].Look);
		DressedFor = Seats[0].Id;
	}
	CreateMachinery();
	CreateGuide();

	Solver.Reset(nullptr, Standing);
	PlaceRack();
	UpdatePinsetter();
	CreateCameraAndHud();
	SetPhase(EBowlingPhase::Intro);
}

void ABowlingGame::CreateRack()
{
	// The played rack: one instanced component (two sections, ten instances), moved in one batch per frame.
	PinRack = NewObject<UInstancedStaticMeshComponent>(this, TEXT("PinRack"));
	PinRack->SetStaticMesh(Alley->GetPinMesh());
	PinRack->SetMaterial(0, Alley->GetPinMaterial(0));
	PinRack->SetMaterial(1, Alley->GetPinMaterial(1));
	PinRack->SetupAttachment(RootComponent);
	PinRack->SetMobility(EComponentMobility::Movable);
	PinRack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PinRack->RegisterComponent();
	PinTransforms.Init(FTransform::Identity, Pins);
	for (int32 i = 0; i < Pins; ++i)
	{
		PinRack->AddInstance(FTransform::Identity, true);
	}
}

void ABowlingGame::CreateBall()
{
	using namespace MovecadeKart::Art;
	// A pearl shell (a swirl of the stone detail under a glossy coat), recoloured for each bowler; black finger holes.
	BallMaterial = Surface(this, { FLinearColor(0.2f, 0.05f, 0.3f), TEXT("rock_face"), 0.22, 0.85, 0.25, 0.1, 0.06, 0.15, 0.0, true, 0.2 });
	Ball = NewLanePart(*this, TEXT("Ball"), Alley->GetBallMesh(), BallMaterial);
	Ball->SetMaterial(1, Plain(this, FLinearColor(0.01f, 0.01f, 0.012f), 0.7));
	Ball->RegisterComponent();
}

void ABowlingGame::CreateMachinery()
{
	using namespace MovecadeKart::Art;
	UStaticMesh* Cube = LoadEngineMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto Machine = [this, Cube](const TCHAR* Name, const FVector& Size, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Part = NewLanePart(*this, Name, Cube, Material);
		// The engine cube is 1 m: web (x, y, z) sizes become UE (z, x, y) scales.
		Part->SetRelativeScale3D(FVector(Size.Z, Size.X, Size.Y));
		Part->RegisterComponent();
		return Part;
	};
	Rake = Machine(TEXT("Rake"), RakeSize, Plain(this, Hex(TEXT("#22242a")), 0.45, 0.3));
	Setter = Machine(TEXT("Setter"), SetterSize, Plain(this, Hex(TEXT("#2b2d35")), 0.5, 0.3));
}

void ABowlingGame::CreateGuide()
{
	// The dots along the live line, and a flat ring of light on the deck where it meets the rack. Their colours change
	// every frame, so each has its own material (not the alley's shared looks).
	UStaticMesh* Disc = LoadEngineMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Guide = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Guide"));
	Guide->SetStaticMesh(Disc);
	GuideMaterial = MovecadeKart::Art::GlowOpaque(this, FLinearColor::White);
	Guide->SetMaterial(0, GuideMaterial);
	Guide->SetupAttachment(RootComponent);
	Guide->SetMobility(EComponentMobility::Movable);
	Guide->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Guide->SetCastShadow(false);
	Guide->RegisterComponent();
	for (int32 i = 0; i < GuideDots; ++i)
	{
		Guide->AddInstance(FTransform::Identity);
	}
	TargetMaterial = MovecadeKart::Art::GlowOpaque(this, FLinearColor::White);
	Target = NewLanePart(*this, TEXT("Target"), Disc, TargetMaterial);
	Target->SetCastShadow(false);
	Target->SetVisibility(false);
	Target->RegisterComponent();
}

void ABowlingGame::CreateCameraAndHud()
{
	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SBowlingHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), HudZOrder);
	}
}

void ABowlingGame::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Hud.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
		}
		Hud.Reset();
	}
	// The lane camera is its own actor: it goes with the game.
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	if (Alley)
	{
		Alley->SetShow(0.0, FLinearColor::White, 0.0);
	}
	Super::EndPlay(Reason);
}

// ---- The turn flow ----------------------------------------------------------------------------------------------

void ABowlingGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	const double RealDt = FMath::Min(DeltaSeconds, static_cast<float>(MaxStepSeconds));
	const float Dt = static_cast<float>(RealDt * TimeScale());
	Clock += Dt;
	TickPhase(GetPhaseAge(), Dt);
	if (Clock >= NextAmbience)
	{
		NextAmbience = Clock + AmbienceSeconds;
		Crowd.Play(this, CueCrowd, Phase == EBowlingPhase::Finished ? AmbienceVolumeFinished : AmbienceVolume, AmbiencePitch);
	}
	UpdateBowler(Dt);
	UpdateBall(Dt);
	UpdateTrail(Dt);
	UpdatePins(Dt);
	UpdatePinsetter();
	UpdateGuide();
	UpdateShow();
	// The camera eases in real time: the strike cam's slow motion is the world's, not the lens's.
	UpdateCamera(static_cast<float>(RealDt));
}

void ABowlingGame::TickPhase(double Age, float Dt)
{
	switch (Phase)
	{
	case EBowlingPhase::Intro:
		if (Age >= IntroSeconds)
		{
			BeginTurn();
		}
		break;
	case EBowlingPhase::Aiming:
		UpdateLocalInput(Dt);
		UpdateBot();
		break;
	case EBowlingPhase::Delivery:
		if (Age >= DeliverySeconds)
		{
			SetPhase(EBowlingPhase::Rolling);
		}
		break;
	case EBowlingPhase::Rolling:
		if (Age >= RollSeconds)
		{
			SetPhase(EBowlingPhase::Settling);
		}
		break;
	case EBowlingPhase::Settling:
		if (!bFxFired)
		{
			bFxFired = true;
			EmitImpact();
		}
		if (Age >= SettleSeconds)
		{
			Settle();
		}
		break;
	case EBowlingPhase::Reset:
		TickReset(Age);
		break;
	case EBowlingPhase::Finished:
		break;
	}
}

void ABowlingGame::TickReset(double Age)
{
	// A fresh rack goes up halfway through the sweep, behind the rake.
	if (bRackResetPending && Age >= ResetSeconds * 0.5)
	{
		bRackResetPending = false;
		Standing = FullRack();
		Solver.Reset(nullptr, Standing);
	}
	// When the next ball is someone else's, the reaction plays out before they step up.
	const double Reaction = bTurnEndedPending ? (IsCelebration(LastBeat) ? CelebrationSeconds : ReactionSeconds) : 0.0;
	if (Age >= ResetSeconds && Clock - LastBeatAt >= Reaction)
	{
		// Only the pins still standing stay on the deck.
		Solver.Reset(nullptr, Standing);
		BeginTurn();
	}
}

void ABowlingGame::SetPhase(EBowlingPhase Next)
{
	Phase = Next;
	PhaseAt = Clock;
	bFxFired = false;
	PlayPhaseCue(Next);
}

void ABowlingGame::PlayPhaseCue(EBowlingPhase Next)
{
	// sound.ts:37-67: each phase's cue, scaled by the throw's power and how much of the rack it takes.
	const double Power = bHasShot ? Shot.Shot.Power : 0.5;
	switch (Next)
	{
	case EBowlingPhase::Delivery:
		Cues.Play(this, CueRelease, static_cast<float>(0.7 + 0.3 * Power));
		break;
	case EBowlingPhase::Rolling:
		Cues.Play(this, CueRoll, 1.f, static_cast<float>(0.88 + 0.25 * Power));
		break;
	case EBowlingPhase::Settling:
		if (Shot.bGutter)
		{
			Cues.Play(this, CueGutter);
		}
		else if (Standing.Num() > 0 && Shot.Felled.Num() > 0)
		{
			const double Impact = FMath::Clamp(static_cast<double>(Shot.Felled.Num()) / Standing.Num() * (0.72 + (Shot.Speed - 7.0) / 10.0), 0.0, 1.0);
			Cues.Play(this, CuePins, static_cast<float>(0.5 + 0.44 * Impact), static_cast<float>(0.9 + 0.16 * Impact));
		}
		break;
	case EBowlingPhase::Reset:
		Cues.Play(this, CueSweep);
		break;
	case EBowlingPhase::Finished:
		Cues.Play(this, CueMatchOver);
		break;
	default:
		break;
	}
}

void ABowlingGame::BeginTurn()
{
	if (Match.bComplete)
	{
		FinishMatch();
		return;
	}
	SetPhase(EBowlingPhase::Aiming);
	Local = FLocalBowl();
	bHasShot = false;
	bStrikeBound = false;
	BotGripAt = -1.0;
	PhoneGripAt = -1.0;
	PhoneCharge = 0.0;
	if (const FBowlerSeat* Bowler = GetBowler())
	{
		StepUp(*Bowler);
	}
	if (HeldRelease.IsSet())
	{
		const FShot Held = HeldRelease.GetValue();
		HeldRelease.Reset();
		Launch(Held);
	}
}

void ABowlingGame::FinishMatch()
{
	SetPhase(EBowlingPhase::Finished);
	UE_LOG(LogBowling, Log, TEXT("Game over"));
	// The house applauds the final frame.
	Crowd.Play(this, CueCrowd, 3.f, 1.05f);
	if (OnFinished)
	{
		OnFinished();
	}
}

void ABowlingGame::StepUp(const FBowlerSeat& Bowler)
{
	// Store cosmetics: a local bowler rolls their equipped ball skin.
	FKartCosmetics::ApplyBowlingBall(BallMaterial, Bowler.Kind == EBowlerKind::Local, Bowler.Color * BallTint);
	if (Bowler.Id != DressedFor)
	{
		DressedFor = Bowler.Id;
		Avatar->SetLook(Bowler.Look);
	}
	if (Bowler.Id != AnnouncedBowler)
	{
		AnnouncedBowler = Bowler.Id;
		bLocalAimed = false;
		bLocalHooked = false;
		if (OnTurn)
		{
			OnTurn(Bowler.Id);
		}
	}
}

void ABowlingGame::RefreshBowler()
{
	const FString* Id = Match.Bowler();
	BowlerSeat = Id ? Seats.IndexOfByPredicate([Id](const FBowlerSeat& Seat) { return Seat.Id == *Id; }) : INDEX_NONE;
}

void ABowlingGame::Launch(const FShot& InShot)
{
	const int32 Before = Standing.Num();
	Shot = ResolveShot(InShot, Standing);
	bHasShot = true;
	RollSeconds = MovecadeKart::Bowling::RollSeconds(Shot.Speed);
	UE_LOG(LogBowling, Log, TEXT("Throw: aim %.3f power %.2f curve %.3f -> %s, felled %d%s"), InShot.Aim, InShot.Power, InShot.Curve,
		Shot.bGutter ? TEXT("gutter") : TEXT("lane"), Shot.Felled.Num(), Shot.bGutter ? *FString::Printf(TEXT(" at %.1f m"), Shot.GutterAt) : TEXT(""));
	Solver.Reset(&Shot, Standing);
	ReleaseFrom = Ball->GetComponentLocation();
	SwingAtRelease = ArmSwing();
	ReleaseZ = StanceZ;
	StanceX = StanceForAim(Shot.Shot.Aim);
	bStrikeBound = Before == Pins && Shot.Felled.Num() == Pins && !Shot.bGutter;
	RecordSwing(Before);
	if (const FBowlerSeat* Bowler = GetBowler(); Bowler && Bowler->Kind == EBowlerKind::Local)
	{
		++LocalThrows;
	}
	TrailCarry = 0.0;
	SparkleCarry = 0.0;
	SetPhase(EBowlingPhase::Delivery);
}

void ABowlingGame::RecordSwing(int32 StandingBefore)
{
	Swing.At = Clock;
	Swing.Speed = Shot.Speed;
	Swing.Power = Shot.Shot.Power;
	Swing.Curve = Shot.Shot.Curve;
	Swing.Line = ReadLine(Shot.EndX, Shot.bGutter, Standing);
	// A ball that clears the rack found the pocket, whatever the line says (the read is the commentary, not the sim).
	if (StandingBefore == Pins && Shot.Felled.Num() == Pins && !Shot.bGutter && Swing.Line != EBowlingLine::Brooklyn)
	{
		Swing.Line = EBowlingLine::Pocket;
	}
}

void ABowlingGame::Settle()
{
	const int32 Before = Standing.Num();
	FBowlerSeat* Bowler = Seats.IsValidIndex(BowlerSeat) ? &Seats[BowlerSeat] : nullptr;
	const FString BowlerId = Bowler ? Bowler->Id : FString();
	const FRollResult R = Roll(Match, Shot.Felled.Num());
	RefreshBowler();
	for (const int32 Id : Shot.Felled)
	{
		Standing.Remove(Id);
	}
	if (Bowler && Before == Pins)
	{
		Bowler->Streak = R.Felled == Pins && !Shot.bGutter ? Bowler->Streak + 1 : 0;
	}
	// A split the first ball left converts when the next ball clears it.
	const bool bWasSplit = Before < Pins && bSplitLeft;
	LastStreak = Bowler ? Bowler->Streak : 0;
	LastBeat = ClassifyBeat(Shot, Before, R.Felled, bWasSplit, LastStreak);
	bSplitLeft = Before == Pins && !R.bTurnEnded && IsSplit(Pins, Standing);
	LastBeatAt = Clock;
	LastFelled = R.Felled;
	bRackResetPending = R.bRackReset;
	bTurnEndedPending = R.bTurnEnded;
	ResetSeconds = R.bRackReset ? RackResetSeconds : DeadwoodSeconds;
	UE_LOG(LogBowling, Log, TEXT("Roll: %s felled %d, beat %d, streak %d at %.2f s"), *BowlerId, R.Felled, static_cast<int32>(LastBeat), LastStreak, GetWorld()->GetTimeSeconds());
	PlayBeatCue(LastBeat);
	if (OnRoll)
	{
		OnRoll(BowlerId, R.Felled, LastBeat);
	}
	Celebrate(Bowler ? Bowler->Color : FLinearColor::White);
	SetPhase(EBowlingPhase::Reset);
}

void ABowlingGame::PlayBeatCue(EBowlingBeat Beat)
{
	switch (Beat)
	{
	case EBowlingBeat::Turkey: Cues.Play(this, CueStrike, 1.08f, 1.14f); break;
	case EBowlingBeat::Strike: Cues.Play(this, CueStrike); break;
	case EBowlingBeat::SplitConvert: Cues.Play(this, CueSpare, 1.12f, 1.18f); break;
	// A split is the spare chord, low and quiet: so close.
	case EBowlingBeat::Split: Cues.Play(this, CueSpare, 0.34f, 0.72f); break;
	case EBowlingBeat::Spare: Cues.Play(this, CueSpare); break;
	default: break;
	}
}

double ABowlingGame::TimeScale() const
{
	// The strike cam: the last metres before the rack run at a third of the speed, easing back as the pins fly.
	if (!bStrikeBound)
	{
		return 1.0;
	}
	if (Phase == EBowlingPhase::Rolling)
	{
		const double T = GetPhaseAge() / RollSeconds;
		return FMath::Lerp(1.0, StrikeCamTimeScale, Smooth(0.78, 0.86, T));
	}
	if (Phase == EBowlingPhase::Settling)
	{
		return FMath::Lerp(StrikeCamTimeScale, 1.0, Smooth(0.0, 0.5, GetPhaseAge()));
	}
	return 1.0;
}

bool ABowlingGame::LiveAim(double& OutAim, double& OutCurve, double& OutCharge) const
{
	const FBowlerSeat* Bowler = GetBowler();
	if (Phase != EBowlingPhase::Aiming || !Bowler)
	{
		return false;
	}
	if (Bowler->Kind == EBowlerKind::Local)
	{
		OutAim = Local.Aim;
		OutCurve = Local.Curve;
		OutCharge = Local.bGripped ? Local.Power : 0.0;
		return true;
	}
	if (Bowler->Kind == EBowlerKind::Phone)
	{
		// runtime.ts: only the bowler up's frames steer the guide.
		const FThrowDetector* Detector = PhoneDetectors.Find(Bowler->Id);
		OutAim = Detector ? Detector->Preview().Aim : 0.0;
		OutCurve = Detector ? Detector->Preview().Curve : 0.0;
		OutCharge = PhoneCharge;
		return true;
	}
	return false;
}

bool ABowlingGame::GetLiveLine(EBowlingLine& OutLine) const
{
	double Aim = 0.0, Curve = 0.0, Charge = 0.0;
	if (!LiveAim(Aim, Curve, Charge))
	{
		return false;
	}
	FShot Preview;
	Preview.Aim = Aim;
	Preview.Curve = Curve;
	OutLine = ReadLine(BallXAt(Preview, LaneLength), FirstGutter(Preview).IsSet(), Standing);
	return true;
}

// ---- The arcade loop --------------------------------------------------------------------------------------------

void ABowlingGame::Rematch()
{
	TArray<FString> Order;
	for (FBowlerSeat& Seat : Seats)
	{
		Order.Add(Seat.Id);
		Seat.Streak = 0;
	}
	Match = CreateMatch(Order);
	RefreshBowler();
	Standing = FullRack();
	Solver.Reset(nullptr, Standing);
	LastBeat = EBowlingBeat::None;
	LastStreak = 0;
	AnnouncedBowler.Reset();
	HeldRelease.Reset();
	bSplitLeft = false;
	bStrikeBound = false;
	ShowAt = -100.0;
	SetPhase(EBowlingPhase::Intro);
}

void ABowlingGame::ArcadeRestart()
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

void ABowlingGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (const FCard& Card : Match.Cards)
	{
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		const FBowlerSeat* Seat = Seats.FindByPredicate([&Card](const FBowlerSeat& S) { return S.Id == Card.PlayerId; });
		Row.Name = Seat ? Seat->Name : Card.PlayerId;
		Row.Color = Seat ? Seat->Color : FLinearColor::White;
		Row.bHuman = Seat && Seat->Kind != EBowlerKind::Bot;
		Row.Score = Card.Score;
	}
	RankByScore(Out);
}
