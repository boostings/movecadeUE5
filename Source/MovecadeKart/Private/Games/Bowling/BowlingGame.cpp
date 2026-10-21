#include "Games/Bowling/BowlingGame.h"

#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Bowling/BowlingAlley.h"
#include "Games/Bowling/SBowlingHud.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBowling, Log, All);

using namespace MovecadeKart::Bowling;

namespace
{
	// runtime.ts phase timing (Docs/Port/06 §3).
	constexpr double DeliverySeconds = 0.16;
	constexpr double SettleSeconds = 1.15;
	constexpr double DeadwoodSeconds = 0.56;
	constexpr double RackResetSeconds = 0.82;
	constexpr double IntroSeconds = 2.4;
	constexpr double BotThinkSeconds = 1.5;
	/** The keys and pad meter swings 0 -> 1 -> 0 over this period while the ball is gripped. */
	constexpr double MeterPeriod = 1.6;
	/** Ball centre above the deck while rolling (radius 0.19 on boards at 0.02). */
	constexpr double BallRollY = 0.21;
	constexpr double BallStartZ = 0.7;
	constexpr double BallTravel = 15.35;

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	FVector W(double X, double Y, double Z)
	{
		return ABowlingAlley::ToWorld(FVector(X, Y, Z));
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
		if (StandingBefore == 10 && Felled == 10)
		{
			return Streak >= 3 ? EBowlingBeat::Turkey : EBowlingBeat::Strike;
		}
		if (StandingBefore < 10 && Felled == StandingBefore)
		{
			return EBowlingBeat::Spare;
		}
		if (StandingBefore == 10)
		{
			TArray<int32> Remaining;
			for (int32 Id = 1; Id <= 10; ++Id)
			{
				if (!R.Felled.Contains(Id))
				{
					Remaining.Add(Id);
				}
			}
			if (IsSplit(10, Remaining))
			{
				return EBowlingBeat::Split;
			}
		}
		return Felled >= 7 ? EBowlingBeat::Good : EBowlingBeat::Open;
	}
}

ABowlingGame::ABowlingGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(0xb041);
}

void ABowlingGame::Setup(ABowlingAlley* InAlley, const TArray<FBowlerSeat>& InSeats, APlayerController* InViewer)
{
	Alley = InAlley;
	Seats = InSeats;
	Viewer = InViewer;
	TArray<FString> Order;
	for (const FBowlerSeat& Seat : Seats)
	{
		Order.Add(Seat.Id);
	}
	Match = CreateMatch(Order);
	Standing = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

	// The played rack, ball, pinsetter and lane guide.
	for (int32 Id = 1; Id <= 10; ++Id)
	{
		UStaticMeshComponent* Pin = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Pin%d"), Id));
		Pin->SetStaticMesh(Alley->GetPinMesh());
		Pin->SetMaterial(0, Alley->GetPinMaterial(0));
		Pin->SetMaterial(1, Alley->GetPinMaterial(1));
		Pin->SetupAttachment(RootComponent);
		Pin->SetMobility(EComponentMobility::Movable);
		Pin->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pin->RegisterComponent();
		Pins.Add(Pin);
	}
	Ball = NewObject<UStaticMeshComponent>(this, TEXT("Ball"));
	Ball->SetStaticMesh(Alley->GetBallMesh());
	BallMaterial = MovecadeKart::Art::Plain(this, FLinearColor(0.2f, 0.05f, 0.3f), 0.12, 0.0);
	Ball->SetMaterial(0, BallMaterial);
	Ball->SetMaterial(1, MovecadeKart::Art::Plain(this, FLinearColor(0.01f, 0.01f, 0.012f), 0.7));
	Ball->SetupAttachment(RootComponent);
	Ball->SetMobility(EComponentMobility::Movable);
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->RegisterComponent();

	Avatar = NewObject<UArcadeAvatarComponent>(this, TEXT("Bowler"));
	Avatar->SetupAttachment(RootComponent);
	Avatar->RegisterComponent();
	if (Seats.Num() > 0)
	{
		Avatar->SetLook(Seats[0].Look);
		DressedFor = Seats[0].Id;
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Disc = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	auto Machine = [this, Cube](const TCHAR* Name, const FVector& Size, UMaterialInterface* Material)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, Name);
		C->SetStaticMesh(Cube);
		C->SetMaterial(0, Material);
		C->SetRelativeScale3D(FVector(Size.Z, Size.X, Size.Y));
		C->SetupAttachment(RootComponent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		return C;
	};
	Rake = Machine(TEXT("Rake"), FVector(3.3, 0.42, 0.16), MovecadeKart::Art::Plain(this, MovecadeKart::Art::Hex(TEXT("#22242a")), 0.45, 0.3));
	Setter = Machine(TEXT("Setter"), FVector(3.15, 0.26, 1.95), MovecadeKart::Art::Plain(this, MovecadeKart::Art::Hex(TEXT("#2b2d35")), 0.5, 0.3));

	Guide = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Guide"));
	Guide->SetStaticMesh(Disc);
	GuideMaterial = MovecadeKart::Art::Glow(this, FLinearColor::White);
	Guide->SetMaterial(0, GuideMaterial);
	Guide->SetupAttachment(RootComponent);
	Guide->SetMobility(EComponentMobility::Movable);
	Guide->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Guide->SetCastShadow(false);
	Guide->RegisterComponent();
	for (int32 i = 0; i < 24; ++i)
	{
		Guide->AddInstance(FTransform::Identity);
	}

	Solver.Reset(nullptr, Standing);
	PlaceRack();
	UpdatePinsetter();

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SBowlingHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}
	SetPhase(EBowlingPhase::Intro);
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
	Super::EndPlay(Reason);
}

const FBowlerSeat* ABowlingGame::GetBowler() const
{
	const FString* Id = Match.Bowler();
	return Id ? Seats.FindByPredicate([Id](const FBowlerSeat& S) { return S.Id == *Id; }) : nullptr;
}

void ABowlingGame::SetPhase(EBowlingPhase Next)
{
	Phase = Next;
	PhaseAt = Clock;
	bFxFired = false;
	// sound.ts:37-67: each phase's cue.
	const double Power = bHasShot ? Shot.Shot.Power : 0.5;
	switch (Next)
	{
	case EBowlingPhase::Delivery:
		Cues.Play(this, TEXT("bowling.release"), static_cast<float>(0.7 + 0.3 * Power));
		break;
	case EBowlingPhase::Rolling:
		Cues.Play(this, TEXT("bowling.roll"), 1.f, static_cast<float>(0.88 + 0.25 * Power));
		break;
	case EBowlingPhase::Settling:
		if (Shot.bGutter)
		{
			Cues.Play(this, TEXT("bowling.gutter"));
		}
		else if (Standing.Num() > 0 && Shot.Felled.Num() > 0)
		{
			const double I = FMath::Clamp(static_cast<double>(Shot.Felled.Num()) / Standing.Num() * (0.72 + (Shot.Speed - 7.0) / 10.0), 0.0, 1.0);
			Cues.Play(this, TEXT("bowling.pins"), static_cast<float>(0.5 + 0.44 * I), static_cast<float>(0.9 + 0.16 * I));
		}
		break;
	case EBowlingPhase::Reset:
		Cues.Play(this, TEXT("bowling.sweep"));
		break;
	case EBowlingPhase::Finished:
		Cues.Play(this, TEXT("match.over"));
		break;
	default:
		break;
	}
}

void ABowlingGame::BeginTurn()
{
	if (Match.bComplete)
	{
		SetPhase(EBowlingPhase::Finished);
		UE_LOG(LogBowling, Log, TEXT("Game over"));
		if (OnFinished)
		{
			OnFinished();
		}
		return;
	}
	SetPhase(EBowlingPhase::Aiming);
	Local = FLocalBowl();
	bHasShot = false;
	BotGripAt = -1.0;
	PhoneCharge = 0.0;
	if (const FBowlerSeat* Bowler = GetBowler())
	{
		BallMaterial->SetVectorParameterValue(TEXT("Color"), Bowler->Color * 0.55f);
		if (Bowler->Id != DressedFor)
		{
			DressedFor = Bowler->Id;
			Avatar->SetLook(Bowler->Look);
		}
		if (Bowler->Id != AnnouncedBowler)
		{
			AnnouncedBowler = Bowler->Id;
			if (OnTurn)
			{
				OnTurn(Bowler->Id);
			}
		}
	}
	if (HeldRelease.IsSet())
	{
		const FShot Held = HeldRelease.GetValue();
		HeldRelease.Reset();
		Launch(Held);
	}
}

bool ABowlingGame::SubmitRelease(const FString& PlayerId, const FShot& InShot)
{
	const FString* Up = Match.Bowler();
	if (!Up || *Up != PlayerId || (Phase != EBowlingPhase::Aiming && Phase != EBowlingPhase::Intro))
	{
		return false;
	}
	// runtime.ts releaseGestureFromPayload: every field bounded to what the detector can produce.
	FShot Clamped = InShot;
	Clamped.Aim = FMath::Clamp(Clamped.Aim, -0.45, 0.45);
	Clamped.Power = FMath::Clamp(Clamped.Power, 0.0, 1.0);
	Clamped.Curve = FMath::Clamp(Clamped.Curve, -0.45, 0.45);
	Clamped.Loft = FMath::Clamp(Clamped.Loft, 0.0, 1.0);
	if (Phase == EBowlingPhase::Intro)
	{
		HeldRelease = Clamped;
		return true;
	}
	Launch(Clamped);
	return true;
}

void ABowlingGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	FThrowDetector& Detector = PhoneDetectors.FindOrAdd(PlayerId);
	Detector.Feed(Frame);
	const FString* Up = Match.Bowler();
	if (Up && *Up == PlayerId && Phase == EBowlingPhase::Aiming)
	{
		// Rises with the swing at once, settles over a few frames, so the arm doesn't flicker.
		const double Charge = Detector.Preview().Charge;
		PhoneCharge = Charge > PhoneCharge ? Charge : FMath::Lerp(PhoneCharge, Charge, 0.12);
	}
}

void ABowlingGame::Launch(const FShot& InShot)
{
	Shot = ResolveShot(InShot, Standing);
	bHasShot = true;
	RollSeconds = MovecadeKart::Bowling::RollSeconds(Shot.Speed);
	UE_LOG(LogBowling, Log, TEXT("Throw: aim %.3f power %.2f curve %.3f -> %s, felled %d%s"), InShot.Aim, InShot.Power, InShot.Curve,
		Shot.bGutter ? TEXT("gutter") : TEXT("lane"), Shot.Felled.Num(), Shot.bGutter ? *FString::Printf(TEXT(" at %.1f m"), Shot.GutterAt) : TEXT(""));
	Solver.Reset(&Shot, Standing);
	ReleaseFrom = Ball->GetComponentLocation();
	SwingAtRelease = ArmSwing();
	StanceX = Shot.Shot.Aim * ABowlingAlley::LaneVisualScale - 0.3;
	SetPhase(EBowlingPhase::Delivery);
}

void ABowlingGame::Settle()
{
	const int32 Before = Standing.Num();
	FBowlerSeat* Bowler = nullptr;
	if (const FString* Id = Match.Bowler())
	{
		Bowler = Seats.FindByPredicate([Id](const FBowlerSeat& S) { return S.Id == *Id; });
	}
	const FString BowlerId = Bowler ? Bowler->Id : FString();
	const FRollResult R = Roll(Match, Shot.Felled.Num());
	for (const int32 Id : Shot.Felled)
	{
		Standing.Remove(Id);
	}
	if (Bowler && Before == 10)
	{
		Bowler->Streak = R.Felled == 10 && !Shot.bGutter ? Bowler->Streak + 1 : 0;
	}
	// A split the first ball left converts when the next ball clears it.
	const bool bWasSplit = Before < 10 && bSplitLeft;
	LastBeat = ClassifyBeat(Shot, Before, R.Felled, bWasSplit, Bowler ? Bowler->Streak : 0);
	bSplitLeft = Before == 10 && !R.bTurnEnded && IsSplit(10, Standing);
	LastBeatAt = Clock;
	LastFelled = R.Felled;
	bRackResetPending = R.bRackReset;
	bTurnEndedPending = R.bTurnEnded;
	ResetSeconds = R.bRackReset ? RackResetSeconds : DeadwoodSeconds;
	switch (LastBeat)
	{
	case EBowlingBeat::Turkey: Cues.Play(this, TEXT("bowling.strike"), 1.08f, 1.14f); break;
	case EBowlingBeat::Strike: Cues.Play(this, TEXT("bowling.strike")); break;
	case EBowlingBeat::SplitConvert: Cues.Play(this, TEXT("bowling.spare"), 1.12f, 1.18f); break;
	case EBowlingBeat::Split: Cues.Play(this, TEXT("bowling.spare"), 0.34f, 0.72f); break;
	case EBowlingBeat::Spare: Cues.Play(this, TEXT("bowling.spare")); break;
	default: break;
	}
	if (OnRoll)
	{
		OnRoll(BowlerId, R.Felled, LastBeat);
	}
	// Celebrations.
	if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this))
	{
		TArray<MovecadeKart::Fx::FKartFxEmit> Out;
		if (LastBeat == EBowlingBeat::Strike || LastBeat == EBowlingBeat::Turkey)
		{
			MovecadeKart::Fx::ConfettiBurst(W(0.0, 0.6, ABowlingAlley::DeckZ + 1.6), FVector::ForwardVector, Out);
		}
		else if (LastBeat == EBowlingBeat::Spare || LastBeat == EBowlingBeat::SplitConvert)
		{
			MovecadeKart::Fx::ConfettiBurst(W(0.0, 0.6, ABowlingAlley::DeckZ + 1.6), FVector::ForwardVector, Out);
			for (MovecadeKart::Fx::FKartFxEmit& E : Out)
			{
				E.Count = FMath::Max(1, E.Count / 2);
			}
		}
		Fx->Emit(Out);
	}
	SetPhase(EBowlingPhase::Reset);
}

void ABowlingGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	Clock += Dt;
	const double Age = GetPhaseAge();
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
			// The impact (fxPlan.ts): sparks, chips and dust by the share of the rack felled.
			if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this); Fx && !Shot.bGutter)
			{
				const double S = Standing.Num() > 0 ? static_cast<double>(Shot.Felled.Num()) / Standing.Num() : 0.0;
				TArray<MovecadeKart::Fx::FKartFxEmit> Out;
				MovecadeKart::Fx::ImpactBurst(MovecadeKart::Fx::EImpact::Bump, W(Shot.EndX * ABowlingAlley::VisualPinScale, 0.0, ABowlingAlley::DeckZ - 0.2), 0.3 + 0.7 * S, Out);
				Fx->Emit(Out);
			}
		}
		if (Age >= SettleSeconds)
		{
			Settle();
		}
		break;
	case EBowlingPhase::Reset:
		if (bRackResetPending && Age >= ResetSeconds * 0.5)
		{
			bRackResetPending = false;
			Standing = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
			Solver.Reset(nullptr, Standing);
		}
		// When the next ball is someone else's, the reaction plays out before they step up.
		if (Age >= ResetSeconds && Clock - LastBeatAt >= (bTurnEndedPending ? 1.8 : 0.0))
		{
			// Only the pins still standing stay on the deck.
			Solver.Reset(nullptr, Standing);
			BeginTurn();
		}
		break;
	case EBowlingPhase::Finished:
		break;
	}
	UpdateBowler(Dt);
	UpdateBall(Dt);
	UpdatePins(Dt);
	UpdatePinsetter();
	UpdateGuide();
	UpdateCamera(Dt);
}

void ABowlingGame::UpdateLocalInput(float Dt)
{
	const FBowlerSeat* Bowler = GetBowler();
	if (!Viewer || !Bowler || Bowler->Kind != EBowlerKind::Local)
	{
		return;
	}
	APlayerController* PC = Viewer;
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };
	const FKey GripKeys[] = { EKeys::SpaceBar, EKeys::Gamepad_RightTrigger, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter };
	bool bGripDown = false;
	for (const FKey& K : GripKeys)
	{
		bGripDown |= Down(K);
	}
	// Aim (the stance line) and hook: keys, both sticks, the mouse.
	double AimInput = (Down(EKeys::D) || Down(EKeys::Right) ? 1.0 : 0.0) - (Down(EKeys::A) || Down(EKeys::Left) ? 1.0 : 0.0);
	AimInput += PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	Local.Aim = FMath::Clamp(Local.Aim + AimInput * 0.45 * Dt, -0.45, 0.45);
	float MouseX = 0.f, MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	Local.Aim = FMath::Clamp(Local.Aim + MouseX * 0.0016, -0.45, 0.45);
	const double HookInput = (Down(EKeys::E) ? 1.0 : 0.0) - (Down(EKeys::Q) ? 1.0 : 0.0);
	const double Stick = PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
	if (FMath::Abs(Stick) > 0.15)
	{
		Local.Curve = FMath::FInterpTo(Local.Curve, Stick * 0.45, Dt, 6.0);
	}
	else
	{
		Local.Curve = FMath::Clamp(Local.Curve + HookInput * 0.4 * Dt, -0.45, 0.45);
	}

	if (bGripDown && !Local.bGripped)
	{
		Local.bGripped = true;
		Local.GripAt = Clock;
		Local.bMouseFlick = false;
		MouseTrail.Reset();
	}
	if (Local.bGripped)
	{
		// Mouse: a flick up the screen throws, its speed over the last 220 ms is the power.
		MouseTrail.Add(FVector2D(Clock, MouseY));
		MouseTrail.RemoveAll([this](const FVector2D& S) { return S.X < Clock - 0.22; });
		double Up = 0.0;
		for (const FVector2D& S : MouseTrail)
		{
			Up += FMath::Max(0.0, S.Y);
		}
		const double FlickPower = FMath::Clamp(Up / 0.22 / 1800.0, 0.0, 1.0);
		if (FlickPower > 0.15)
		{
			Local.bMouseFlick = true;
		}
		Local.Power = Local.bMouseFlick ? FMath::Max(Local.Power * FMath::Exp(-Dt * 2.0), FlickPower)
			: 0.5 - 0.5 * FMath::Cos(2.0 * UE_DOUBLE_PI * (Clock - Local.GripAt) / MeterPeriod);
	}
	if (Local.bGripped && !bGripDown)
	{
		FShot Throw;
		Throw.Aim = Local.Aim;
		Throw.Curve = Local.Curve;
		Throw.Power = FMath::Max(0.04, Local.Power);
		Local.bGripped = false;
		Launch(Throw);
	}
}

void ABowlingGame::UpdateBot()
{
	const FBowlerSeat* Bowler = GetBowler();
	if (!Bowler || Bowler->Kind != EBowlerKind::Bot)
	{
		return;
	}
	if (BotGripAt < 0.0 && GetPhaseAge() >= BotThinkSeconds - 0.9)
	{
		BotGripAt = Clock;
	}
	if (GetPhaseAge() < BotThinkSeconds)
	{
		return;
	}
	// Aim the deck line (aim + curve) at the pocket on a full rack, or at the middle of what's left.
	double Target = 0.07;
	if (Standing.Num() < 10 && Standing.Num() > 0)
	{
		double Sum = 0.0;
		for (const int32 Id : Standing)
		{
			Sum += PinPosition(Id).X;
		}
		Target = Sum / Standing.Num();
	}
	const double Wild = 1.0 - Bowler->Skill;
	FShot Throw;
	Throw.Curve = Standing.Num() == 10 ? 0.22 + BotRng.FRandRange(-0.08, 0.08) * Wild : 0.05;
	Throw.Aim = FMath::Clamp(Target - Throw.Curve + BotRng.FRandRange(-0.16, 0.16) * Wild, -0.45, 0.45);
	Throw.Power = FMath::Clamp(0.78 + BotRng.FRandRange(-0.25, 0.22) * (0.4 + Wild), 0.2, 1.0);
	Launch(Throw);
}

FVector ABowlingGame::BallWorld(double T, double& OutY) const
{
	// The ball's line on the lane (sim metres -> lane units), easing to the rack's scale over the deck (W1), so
	// the ball meets the pins where the sim says it does.
	const double SimX = BallXAt(Shot.Shot, T * LaneLength);
	const double Scale = FMath::Lerp(ABowlingAlley::LaneVisualScale, ABowlingAlley::VisualPinScale, Smooth(0.8, 0.9, T));
	double X = SimX * Scale;
	OutY = BallRollY;
	const double Loft = Shot.Shot.Loft;
	const double Window = 0.05 + 0.13 * Loft;
	if (T < Window && Loft > 0.02)
	{
		const double A = T / Window;
		OutY += (1.0 - A * A) * (0.1 + 0.45 * Loft);
	}
	if (Shot.bGutter)
	{
		const double Capture = FMath::Clamp(Shot.GutterAt / LaneLength, 0.08, 0.94);
		const double In = Smooth(Capture, Capture + 0.03, T);
		X = FMath::Lerp(X, Shot.GutterSide * (ABowlingAlley::LaneVisualWidth / 2.0 + 0.18), In);
		OutY -= 0.32 * In;
	}
	return FVector(X, OutY, BallStartZ - T * BallTravel);
}

void ABowlingGame::UpdateBall(float Dt)
{
	const double Age = GetPhaseAge();
	FVector Web;
	double Y = BallRollY;
	switch (Phase)
	{
	case EBowlingPhase::Intro:
	case EBowlingPhase::Aiming:
	case EBowlingPhase::Finished:
	{
		// In the bowler's hands: cradled at the chest, or hanging from the swinging hand once gripped.
		const FVector R = Avatar->BoneWorld(TEXT("rightHand")), L = Avatar->BoneWorld(TEXT("leftHand"));
		const bool bSwinging = ArmSwing() < -0.05;
		const FVector At = bSwinging ? R + FVector(6.0, 0.0, -14.0) : (R + L) / 2.0 + FVector(12.0, 0.0, -4.0);
		Ball->SetWorldLocationAndRotation(At, FRotator(-FMath::RadiansToDegrees(BallSpin), 0.0, 0.0));
		return;
	}
	case EBowlingPhase::Delivery:
	{
		const double P = FMath::Clamp(Age / DeliverySeconds, 0.0, 1.0);
		const FVector Start = BallWorld(0.0, Y);
		const FVector From = FMath::Lerp(ReleaseFrom, W(Start.X, Start.Y, Start.Z), P);
		Ball->SetWorldLocationAndRotation(From + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * P) * (0.06 + 0.26 * Shot.Shot.Loft) * 100.0),
			FRotator(-FMath::RadiansToDegrees(BallSpin), 0.0, 0.0));
		return;
	}
	case EBowlingPhase::Rolling:
	{
		const double T = FMath::Clamp(Age / RollSeconds, 0.0, 1.0);
		Web = BallWorld(T, Y);
		BallSpin += Dt * Shot.Rps * 2.0 * UE_DOUBLE_PI;
		break;
	}
	case EBowlingPhase::Settling:
	case EBowlingPhase::Reset:
	{
		const double P = FMath::Clamp(Age / SettleSeconds, 0.0, 1.0);
		if (Phase == EBowlingPhase::Reset)
		{
			Web = FVector(0.0, -2.0, ABowlingAlley::DeckZ - 3.0); // gone to the return
		}
		else if (Shot.bGutter)
		{
			Web = FVector(Shot.GutterSide * 1.785, BallRollY - 0.32 - 0.4 * P, ABowlingAlley::DeckZ + 0.1 - 1.6 * P);
		}
		else
		{
			Web = FVector(Shot.EndX * ABowlingAlley::VisualPinScale, BallRollY - 0.6 * P, ABowlingAlley::DeckZ - 2.1 - 0.65 * P);
		}
		break;
	}
	}
	Ball->SetWorldLocationAndRotation(W(Web.X, Web.Y, Web.Z), FRotator(-FMath::RadiansToDegrees(BallSpin), 0.0, 0.0));
}

void ABowlingGame::UpdatePins(float Dt)
{
	// The ball in the rack's frame, while it is on the lane.
	if (Phase == EBowlingPhase::Rolling || Phase == EBowlingPhase::Settling)
	{
		double Y = 0.0;
		const double T = Phase == EBowlingPhase::Rolling ? FMath::Clamp(GetPhaseAge() / RollSeconds, 0.0, 1.0) : 1.0;
		const FVector Web = BallWorld(Phase == EBowlingPhase::Rolling ? T : 1.0, Y);
		if (Phase == EBowlingPhase::Rolling && !Shot.bGutter)
		{
			FVisualBall VB;
			VB.X = Web.X;
			VB.Z = ABowlingAlley::DeckZ - Web.Z;
			VB.Speed = Shot.Speed;
			Solver.Step(Dt, &VB);
		}
		else
		{
			Solver.Step(Dt);
		}
	}
	else
	{
		Solver.Step(Dt);
	}
	PlaceRack();
}

void ABowlingGame::PlaceRack()
{
	// Deadwood is raked off the deck during the reset.
	const bool bRaking = Phase == EBowlingPhase::Reset;
	const double RakeP = bRaking ? FMath::Clamp(GetPhaseAge() / ResetSeconds, 0.0, 1.0) : 0.0;
	const double RakeZ = FMath::Lerp(ABowlingAlley::DeckZ + 0.62, ABowlingAlley::DeckZ - 2.7, Smooth(0.10, 0.36, RakeP));
	for (const FPinPose& P : Solver.Poses())
	{
		UStaticMeshComponent* Pin = Pins[P.Id - 1];
		bool bShow = P.bVisible;
		FVector Web(P.Position.X, P.Position.Y, ABowlingAlley::DeckZ - P.Position.Z);
		if (bRaking && bShow && !Standing.Contains(P.Id))
		{
			if (Web.Z > RakeZ - 0.15)
			{
				Web.Z = RakeZ - 0.15;
			}
			if (Web.Z < ABowlingAlley::DeckZ - 1.95)
			{
				bShow = false; // into the pit
			}
		}
		Pin->SetVisibility(bShow);
		const double L = P.Tilt.Size();
		FQuat Rotation = FQuat::Identity;
		if (L > 1e-6)
		{
			const double S = FMath::Sin(L), C = FMath::Cos(L);
			// The tilt's (x, z) in the rack, with z running down the lane: in UE, down the lane is +X.
			const FVector Up(P.Tilt.Y / L * S, P.Tilt.X / L * S, C);
			Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Up.GetSafeNormal());
		}
		Rotation = Rotation * FQuat(FVector::UpVector, P.Spin);
		Pin->SetWorldLocationAndRotation(W(Web.X, Web.Y, Web.Z), Rotation);
	}
}

void ABowlingGame::UpdatePinsetter()
{
	// presentation.ts:196-291: the rake drops, sweeps the deck and lifts; the setter table comes down and back.
	// Between resets the machinery waits up behind the masking units, out of the shot.
	const bool bResetting = Phase == EBowlingPhase::Reset;
	Rake->SetVisibility(bResetting);
	Setter->SetVisibility(bResetting);
	const double P = bResetting ? FMath::Clamp(GetPhaseAge() / ResetSeconds, 0.0, 1.0) : 0.0;
	const double RakeY = FMath::Lerp(1.42, 0.34, Smooth(0.0, 0.13, P)) + (1.42 - 0.34) * Smooth(0.36, 0.52, P);
	const double RakeZ = FMath::Lerp(ABowlingAlley::DeckZ + 0.62, ABowlingAlley::DeckZ - 2.7, Smooth(0.10, 0.36, P));
	const double Back = Smooth(0.52, 1.0, P);
	Rake->SetWorldLocation(W(0.0, RakeY, FMath::Lerp(RakeZ, ABowlingAlley::DeckZ + 0.62, Back)));
	const double TableY = FMath::Lerp(2.2, 1.16, Smooth(0.28, 0.62, P)) + (2.2 - 1.16) * Smooth(0.70, 1.0, P);
	Setter->SetWorldLocation(W(0.0, TableY, ABowlingAlley::DeckZ - 0.88));
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

void ABowlingGame::UpdateGuide()
{
	const FBowlerSeat* Bowler = GetBowler();
	double Aim = 0.0, Curve = 0.0, Charge = 0.0;
	const bool bShow = LiveAim(Aim, Curve, Charge);
	Guide->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	// BowlingScene.tsx:1908-1951: 24 dots along the previewed line, brighter as the arm charges.
	GuideMaterial->SetVectorParameterValue(TEXT("Color"), Bowler->Color * static_cast<float>(1.4 + 1.6 * Charge));
	FShot Preview;
	Preview.Aim = Aim;
	Preview.Curve = Curve;
	for (int32 i = 0; i < 24; ++i)
	{
		const double T = (i + 0.5) / 24.0;
		const double Z = 0.15 - FMath::Pow(T, 0.78) * 13.3;
		const double Progress = (BallStartZ - Z) / BallTravel;
		const double X = BallXAt(Preview, FMath::Clamp(Progress, 0.0, 1.0) * LaneLength) * ABowlingAlley::LaneVisualScale;
		const double Size = (0.65 + 0.3 * Charge) * (0.75 + 0.45 * T) * 0.084;
		Guide->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, W(X, 0.024, Z), FVector(Size, Size, 0.002)), true, i == 23, true);
	}
}

void ABowlingGame::UpdateCamera(float Dt)
{
	// presentation.ts cameraPlan: the address tripod behind the bowler, the impact and result shots over the deck.
	FVector Pos(0.0, 3.65, 6.55), Look(0.0, 0.86, -9.55);
	double Fov = 39.0, Response = 3.05;
	const double EndX = bHasShot ? Shot.EndX * ABowlingAlley::VisualPinScale : 0.0;
	const double Age = GetPhaseAge();
	auto Impact = [&]() { Pos = FVector(0.0, 2.55, -6.85); Look = FVector(EndX * 0.14, 0.58, -13.6); Fov = 39.0; };
	auto Result = [&]() { Pos = FVector(0.0, 3.3, -6.3); Look = FVector(EndX * 0.16, 0.48, -13.6); Fov = 40.0; Response = 5.2; };
	switch (Phase)
	{
	case EBowlingPhase::Intro:
		// A slow push down the lane toward the rack before the first ball.
		Pos = FMath::Lerp(FVector(2.4, 2.6, -2.0), FVector(0.0, 3.65, 6.55), Smooth(0.0, 1.0, Age / IntroSeconds));
		Look = FMath::Lerp(FVector(0.0, 0.6, -13.6), FVector(0.0, 0.86, -9.55), Smooth(0.0, 1.0, Age / IntroSeconds));
		Response = 8.0;
		break;
	case EBowlingPhase::Settling:
		(Age / SettleSeconds <= 0.55) ? Impact() : Result();
		break;
	case EBowlingPhase::Reset:
		if (Age / ResetSeconds < 0.58)
		{
			Result();
		}
		break;
	case EBowlingPhase::Finished:
		Pos = FVector(1.6 * FMath::Sin(Clock * 0.15), 3.1, 4.6);
		Look = FVector(0.0, 1.0, -6.0);
		Fov = 46.0;
		Response = 1.2;
		break;
	default:
		break;
	}
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-Response * FMath::Min<double>(Dt, 1.0 / 20.0));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * K;
	CamFov += (Fov - CamFov) * (1.0 - FMath::Exp(-4.0 * Dt));
	const FVector From = W(CamPos.X, CamPos.Y, CamPos.Z);
	const FVector To = W(CamLook.X, CamLook.Y, CamLook.Z);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	// three.js gives a vertical field of view; UE's is horizontal, so convert at the viewport's aspect.
	double Aspect = 16.0 / 9.0;
	if (const UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		FVector2D Size;
		Viewport->GetViewportSize(Size);
		Aspect = Size.Y > 0.0 ? Size.X / Size.Y : Aspect;
	}
	const double HFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HFov));
}

double ABowlingGame::ArmSwing() const
{
	const double Age = GetPhaseAge();
	switch (Phase)
	{
	case EBowlingPhase::Aiming:
		if (Local.bGripped)
		{
			// The pendulum follows the meter (or the mouse flick's build-up).
			return -0.35 - 1.25 * Local.Power;
		}
		if (BotGripAt >= 0.0)
		{
			return -0.35 - 1.2 * Smooth(0.0, 1.0, (Clock - BotGripAt) / 0.8);
		}
		if (PhoneCharge > 0.02)
		{
			// A phone's swing: the backswing grows with its energy.
			return -0.35 * Smooth(0.0, 0.15, PhoneCharge) - 1.2 * PhoneCharge;
		}
		return 0.0;
	case EBowlingPhase::Delivery:
		return FMath::Lerp(SwingAtRelease, 0.25, Smooth(0.0, 1.0, Age / DeliverySeconds));
	case EBowlingPhase::Rolling:
		return FMath::Lerp(0.25, 1.75, Smooth(0.0, 1.0, Age / 0.45));
	case EBowlingPhase::Settling:
		return 1.75;
	default:
		return 0.0;
	}
}

void ABowlingGame::UpdateBowler(float Dt)
{
	FArcadePoseInput& Pose = Avatar->Pose();
	const FBowlerSeat* Bowler = GetBowler();
	const double Swing = ArmSwing();
	const double Age = GetPhaseAge();
	// Where the bowler stands: on the line (the right hand over it), sliding to the foul line as the ball goes.
	if (Phase == EBowlingPhase::Aiming || Phase == EBowlingPhase::Intro)
	{
		double Aim = 0.0, Curve = 0.0, Charge = 0.0;
		const double Line = (LiveAim(Aim, Curve, Charge) ? Aim : 0.0) * ABowlingAlley::LaneVisualScale - 0.3;
		StanceX = FMath::FInterpTo(StanceX, Line, Dt, 10.0);
	}
	const bool bThrown = Phase == EBowlingPhase::Delivery || Phase == EBowlingPhase::Rolling || Phase == EBowlingPhase::Settling;
	const double SinceRelease = Phase == EBowlingPhase::Delivery ? Age : (Phase == EBowlingPhase::Rolling ? DeliverySeconds + Age : 10.0);
	const double Slide = bThrown ? Smooth(0.0, 0.55, SinceRelease) : (Phase == EBowlingPhase::Reset ? 1.0 - Smooth(0.0, 1.0, Age / ResetSeconds) : 0.0);
	const double Z = FMath::Lerp(1.72, 0.95, Slide); // AVATAR_POSITION z 1.72, sliding to the foul line
	Avatar->SetWorldLocationAndRotation(W(StanceX, 0.0, Z), FRotator::ZeroRotator);

	// The bowling arm: a pendulum from the shoulder; the other hand cradles the ball until the swing starts.
	const double Reach = 0.97;
	Pose.Hand[1] = FVector(FMath::Sin(Swing) * Reach, 0.12, -FMath::Cos(Swing) * Reach);
	Pose.HandWeight[1] = 1.f;
	const bool bCradle = FMath::Abs(Swing) < 0.05 && (Phase == EBowlingPhase::Aiming || Phase == EBowlingPhase::Intro);
	if (bCradle)
	{
		Pose.Hand[1] = FVector(0.5, -0.05, -0.38);
		Pose.Hand[0] = FVector(0.5, 0.24, -0.4);
		Pose.HandWeight[0] = 1.f;
	}
	else
	{
		// Balance: the free arm swings out the other way.
		Pose.Hand[0] = FVector(-FMath::Sin(Swing) * 0.4, -0.55, -0.7);
		Pose.HandWeight[0] = bThrown ? 0.85f : 0.4f;
	}
	const double Charge = Swing < 0.0 ? FMath::Clamp(-Swing / 1.6, 0.0, 1.0) : 0.0;
	const double Settle = bThrown ? 1.0 - Smooth(1.0, 2.6, SinceRelease) : 0.0;
	Pose.Crouch = static_cast<float>(0.15 + 0.3 * Charge + 0.35 * Settle);
	Pose.Lean = static_cast<float>(0.12 + 0.25 * Charge + 0.4 * Settle);
	Pose.Lunge = static_cast<float>(bThrown ? Smooth(0.0, 0.25, SinceRelease) * (0.4 + 0.6 * Settle) : 0.0);
	Pose.Step = static_cast<float>(bThrown ? 0.8 * (1.0 - Smooth(0.0, 0.45, SinceRelease)) : 0.0);
	Pose.StepPhase = static_cast<float>(SinceRelease * 12.0);
	Pose.Twist = static_cast<float>(-0.12 * Charge);

	// Eyes on the pins, then on the ball. The avatar faces UE +X, so world directions are its own (X fwd, Y right).
	const FVector LookAt = Phase == EBowlingPhase::Rolling ? Ball->GetComponentLocation() : W(0.0, 0.4, ABowlingAlley::DeckZ);
	Pose.Look = (LookAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, 160.0)).GetSafeNormal();
	Pose.LookWeight = 0.75f;

	// Reactions (avatar/motion reactions.ts): a strike pumps both fists and jumps, a spare cheers, a gutter slumps.
	const double Since = Clock - LastBeatAt;
	const double React = Since < 1.8 ? FMath::Clamp(Since / 0.2, 0.0, 1.0) * FMath::Clamp((1.8 - Since) / 0.3, 0.0, 1.0) : 0.0;
	float Cheer = 0.f, Slump = 0.f, Lift = 0.f;
	switch (LastBeat)
	{
	case EBowlingBeat::Strike:
	case EBowlingBeat::Turkey:
		Cheer = 1.f;
		Lift = static_cast<float>(0.14 * FMath::Abs(FMath::Sin(Since * 7.5)));
		break;
	case EBowlingBeat::Spare:
	case EBowlingBeat::SplitConvert:
		Cheer = 0.75f;
		Lift = static_cast<float>(0.06 * FMath::Abs(FMath::Sin(Since * 6.0)));
		break;
	case EBowlingBeat::Gutter:
	case EBowlingBeat::Split:
		Slump = 1.f;
		break;
	case EBowlingBeat::Open:
		Slump = LastFelled <= 3 ? 0.6f : 0.f;
		break;
	default:
		break;
	}
	Pose.Cheer = Cheer * static_cast<float>(React) + (Phase == EBowlingPhase::Finished ? 0.4f : 0.f);
	Pose.Slump = Slump * static_cast<float>(React);
	Pose.Lift = Lift * static_cast<float>(React);
	Pose.Idle = 1.f;
}

void ABowlingGame::Rematch()
{
	TArray<FString> Order;
	for (FBowlerSeat& Seat : Seats)
	{
		Order.Add(Seat.Id);
		Seat.Streak = 0;
	}
	Match = CreateMatch(Order);
	Standing = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	Solver.Reset(nullptr, Standing);
	LastBeat = EBowlingBeat::None;
	AnnouncedBowler.Reset();
	HeldRelease.Reset();
	bSplitLeft = false;
	SetPhase(EBowlingPhase::Intro);
}

void ABowlingGame::ArcadeRestart()
{
	if (OnRematch) OnRematch(); else Rematch();
}

void ABowlingGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (const MovecadeKart::Bowling::FCard& Card : Match.Cards)
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
