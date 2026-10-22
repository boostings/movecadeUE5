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
#include "Misc/CommandLine.h"
#include "Store/KartCosmetics.h" // store cosmetics: ball skin
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogBowling, Log, All);

using namespace MovecadeKart::Bowling;
namespace Fx = MovecadeKart::Fx;

namespace
{
	// runtime.ts phase timing (Docs/Port/06 §3).
	constexpr double DeliverySeconds = 0.16;
	constexpr double SettleSeconds = 1.15;
	constexpr double DeadwoodSeconds = 0.56;
	constexpr double RackResetSeconds = 0.82;
	constexpr double IntroSeconds = 2.4;
	/** A bot lines up, then winds up for 0.9 s before the ball goes (was 1.5 s: the wait read as dead air). */
	constexpr double BotThinkSeconds = 1.15;
	constexpr double BotWindUpSeconds = 0.9;
	/** After a turn ends, the reaction plays out before the next bowler steps up: longer for a celebration. */
	constexpr double ReactionSeconds = 1.2;
	constexpr double CelebrationSeconds = 2.0;
	/** The keys and pad meter swings 0 -> 1 -> 0 over this period while the ball is gripped. */
	constexpr double MeterPeriod = 1.6;
	/** Ball centre above the deck while rolling (radius 0.19 on boards at 0.02). */
	constexpr double BallRollY = 0.21;
	constexpr double BallStartZ = 0.7;
	constexpr double BallTravel = 15.35;
	/** Where the bowler addresses the lane, how far the approach walks them, and the slide's end at the foul line. */
	constexpr double AddressZ = 1.72;
	constexpr double ApproachZ = 1.36;
	constexpr double SlideZ = 0.95;
	/** The pocket (between the 1 and 3 pins, sim metres at the head pin) and its tolerance. */
	constexpr double PocketX = 0.07;
	constexpr double PocketWindow = 0.045;
	/** The lane guide's dots. */
	constexpr int32 GuideDots = 36;

	const FName ColorParam(TEXT("Color"));
	const FName RightHand(TEXT("rightHand"));
	const FName LeftHand(TEXT("leftHand"));
	const FName CueRelease(TEXT("bowling.release"));
	const FName CueRoll(TEXT("bowling.roll"));
	const FName CueGutter(TEXT("bowling.gutter"));
	const FName CuePins(TEXT("bowling.pins"));
	const FName CueSweep(TEXT("bowling.sweep"));
	const FName CueStrike(TEXT("bowling.strike"));
	const FName CueSpare(TEXT("bowling.spare"));
	const FName CueMatchOver(TEXT("match.over"));
	/** The house: the pub murmur as the crowd on the other lanes, and the fanfare stings for big moments. */
	const FName CueCrowd(TEXT("darts.pub-chatter"));
	const FName CueSparkle(TEXT("stacker.perfect"));
	const FName CueGo(TEXT("lobby.go"));

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	FVector W(double X, double Y, double Z)
	{
		return ABowlingAlley::ToWorld(FVector(X, Y, Z));
	}

	bool IsBig(EBowlingBeat Beat)
	{
		return Beat == EBowlingBeat::Strike || Beat == EBowlingBeat::Turkey || Beat == EBowlingBeat::SplitConvert;
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
	if (Rack.Num() > 0 && Rack.Num() < 10)
	{
		// A spare: the target is what's left, not the pocket.
		double Lo = TNumericLimits<double>::Max(), Hi = -TNumericLimits<double>::Max();
		for (const int32 Id : Rack)
		{
			Lo = FMath::Min(Lo, PinPosition(Id).X);
			Hi = FMath::Max(Hi, PinPosition(Id).X);
		}
		return EndX >= Lo - 0.12 && EndX <= Hi + 0.12 ? EBowlingLine::OnTarget : (FMath::Abs(EndX) > 0.3 ? EBowlingLine::Wide : EBowlingLine::Light);
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
	return FMath::Abs(EndX) > 0.3 ? EBowlingLine::Wide : EBowlingLine::Light;
}

void ABowlingGame::Setup(ABowlingAlley* InAlley, const TArray<FBowlerSeat>& InSeats, APlayerController* InViewer,
	const FBowlingDebugFlags& InDebugFlags)
{
	Alley = InAlley;
	Seats = InSeats;
	DebugFlags = InDebugFlags;
	Viewer = InViewer;
	TArray<FString> Order;
	for (const FBowlerSeat& Seat : Seats)
	{
		Order.Add(Seat.Id);
	}
	Match = CreateMatch(Order);
	RefreshBowler();
	Standing = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	// Every bowling cue (and the crowd) loads now, not on its first play mid-game.
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("bowling."));
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("darts.pub"));
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("stacker.perfect"));
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("lobby.go"));
	MovecadeKart::Games::FCuePlayer::Preload(TEXT("match."));

	// The played rack: one instanced component (two sections, ten instances), moved in one batch per frame.
	PinRack = NewObject<UInstancedStaticMeshComponent>(this, TEXT("PinRack"));
	PinRack->SetStaticMesh(Alley->GetPinMesh());
	PinRack->SetMaterial(0, Alley->GetPinMaterial(0));
	PinRack->SetMaterial(1, Alley->GetPinMaterial(1));
	PinRack->SetupAttachment(RootComponent);
	PinRack->SetMobility(EComponentMobility::Movable);
	PinRack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PinRack->RegisterComponent();
	PinTransforms.Init(FTransform::Identity, 10);
	for (int32 i = 0; i < 10; ++i)
	{
		PinRack->AddInstance(FTransform::Identity, true);
	}

	// The ball: a pearl shell (a swirl of the stone detail under a glossy coat) in the bowler's colour.
	Ball = NewObject<UStaticMeshComponent>(this, TEXT("Ball"));
	Ball->SetStaticMesh(Alley->GetBallMesh());
	{
		using namespace MovecadeKart::Art;
		BallMaterial = Surface(this, { FLinearColor(0.2f, 0.05f, 0.3f), TEXT("rock_face"), 0.22, 0.85, 0.25, 0.1, 0.06, 0.15, 0.0, true, 0.2 });
	}
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
	// The target: a flat ring of light on the deck where the line meets the rack.
	Target = NewObject<UStaticMeshComponent>(this, TEXT("Target"));
	Target->SetStaticMesh(Disc);
	TargetMaterial = MovecadeKart::Art::GlowOpaque(this, FLinearColor::White);
	Target->SetMaterial(0, TargetMaterial);
	Target->SetupAttachment(RootComponent);
	Target->SetMobility(EComponentMobility::Movable);
	Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Target->SetCastShadow(false);
	Target->SetVisibility(false);
	Target->RegisterComponent();

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

void ABowlingGame::RefreshBowler()
{
	const FString* Id = Match.Bowler();
	BowlerSeat = Id ? Seats.IndexOfByPredicate([Id](const FBowlerSeat& S) { return S.Id == *Id; }) : INDEX_NONE;
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

float ABowlingGame::GetFlash(FLinearColor& OutColor) const
{
	OutColor = FlashColor;
	const double Age = Clock - FlashAt;
	if (Age < 0.0 || Age > 0.6)
	{
		return 0.f;
	}
	return static_cast<float>(FlashStrength * FMath::Square(1.0 - Age / 0.6));
}

void ABowlingGame::Shake(double Metres)
{
	// A new hit only ever raises what is still shaking.
	const double Left = ShakeMetres * FMath::Max(0.0, 1.0 - (Clock - ShakeAt) / 0.55);
	if (Metres > Left)
	{
		ShakeMetres = Metres;
		ShakeAt = Clock;
	}
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
			const double I = FMath::Clamp(static_cast<double>(Shot.Felled.Num()) / Standing.Num() * (0.72 + (Shot.Speed - 7.0) / 10.0), 0.0, 1.0);
			Cues.Play(this, CuePins, static_cast<float>(0.5 + 0.44 * I), static_cast<float>(0.9 + 0.16 * I));
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
		SetPhase(EBowlingPhase::Finished);
		UE_LOG(LogBowling, Log, TEXT("Game over"));
		Crowd.Play(this, CueCrowd, 3.f, 1.05f);
		if (OnFinished)
		{
			OnFinished();
		}
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
		// Store cosmetics: a local bowler rolls their equipped ball skin.
		FKartCosmetics::ApplyBowlingBall(BallMaterial, Bowler->Kind == EBowlerKind::Local, Bowler->Color * 0.8f);
		if (Bowler->Id != DressedFor)
		{
			DressedFor = Bowler->Id;
			Avatar->SetLook(Bowler->Look);
		}
		if (Bowler->Id != AnnouncedBowler)
		{
			AnnouncedBowler = Bowler->Id;
			bLocalAimed = false;
			bLocalHooked = false;
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
		if (PhoneCharge > 0.02 && PhoneGripAt < 0.0)
		{
			PhoneGripAt = Clock;
		}
	}
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
	StanceX = Shot.Shot.Aim * ABowlingAlley::LaneVisualScale - 0.3;
	bStrikeBound = Before == 10 && Shot.Felled.Num() == 10 && !Shot.bGutter;
	// The read-out: how hard, how much hook, where the line meets the rack.
	Swing.At = Clock;
	Swing.Speed = Shot.Speed;
	Swing.Power = Shot.Shot.Power;
	Swing.Curve = Shot.Shot.Curve;
	Swing.Line = ReadLine(Shot.EndX, Shot.bGutter, Standing);
	// A ball that clears the rack found the pocket, whatever the line says (the read is the commentary, not the sim).
	if (Before == 10 && Shot.Felled.Num() == 10 && !Shot.bGutter && Swing.Line != EBowlingLine::Brooklyn)
	{
		Swing.Line = EBowlingLine::Pocket;
	}
	if (const FBowlerSeat* Bowler = GetBowler(); Bowler && Bowler->Kind == EBowlerKind::Local)
	{
		++LocalThrows;
	}
	TrailCarry = 0.0;
	SparkleCarry = 0.0;
	SetPhase(EBowlingPhase::Delivery);
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
	if (Bowler && Before == 10)
	{
		Bowler->Streak = R.Felled == 10 && !Shot.bGutter ? Bowler->Streak + 1 : 0;
	}
	// A split the first ball left converts when the next ball clears it.
	const bool bWasSplit = Before < 10 && bSplitLeft;
	LastStreak = Bowler ? Bowler->Streak : 0;
	LastBeat = ClassifyBeat(Shot, Before, R.Felled, bWasSplit, LastStreak);
	bSplitLeft = Before == 10 && !R.bTurnEnded && IsSplit(10, Standing);
	LastBeatAt = Clock;
	LastFelled = R.Felled;
	bRackResetPending = R.bRackReset;
	bTurnEndedPending = R.bTurnEnded;
	ResetSeconds = R.bRackReset ? RackResetSeconds : DeadwoodSeconds;
	UE_LOG(LogBowling, Log, TEXT("Roll: %s felled %d, beat %d, streak %d at %.2f s"), *BowlerId, R.Felled, static_cast<int32>(LastBeat), LastStreak, GetWorld()->GetTimeSeconds());
	switch (LastBeat)
	{
	case EBowlingBeat::Turkey: Cues.Play(this, CueStrike, 1.08f, 1.14f); break;
	case EBowlingBeat::Strike: Cues.Play(this, CueStrike); break;
	case EBowlingBeat::SplitConvert: Cues.Play(this, CueSpare, 1.12f, 1.18f); break;
	case EBowlingBeat::Split: Cues.Play(this, CueSpare, 0.34f, 0.72f); break;
	case EBowlingBeat::Spare: Cues.Play(this, CueSpare); break;
	default: break;
	}
	if (OnRoll)
	{
		OnRoll(BowlerId, R.Felled, LastBeat);
	}
	Celebrate(Bowler ? Bowler->Color : FLinearColor::White);
	SetPhase(EBowlingPhase::Reset);
}

void ABowlingGame::Celebrate(const FLinearColor& Mine)
{
	TArray<Fx::FKartFxEmit> Out;
	const FVector Deck = W(0.0, 1.0, ABowlingAlley::DeckZ - 0.8);
	auto Fountains = [&Out](const FLinearColor& Color, int32 Count, double Height)
	{
		// Spark fountains off the tops of the kickbacks either side of the deck.
		for (const double Side : { -1.775, 1.775 })
		{
			Fx::FKartFxEmit S;
			S.Field = Fx::EField::Sparks;
			S.Position = W(Side, 1.15, ABowlingAlley::DeckZ - 0.4);
			S.Count = Count;
			S.Direction = FVector::UpVector;
			S.Spread = 0.28;
			S.Speed = { 5.0 * Height, 8.5 * Height };
			S.Life = { 0.7, 1.2 };
			S.Size = { 0.05, 0.09 };
			S.Color = Color;
			S.ColorEnd = FLinearColor(1.f, 0.35f, 0.1f);
			S.Intensity = 6.0;
			S.Jitter = 0.15;
			Out.Add(S);
		}
	};
	switch (LastBeat)
	{
	case EBowlingBeat::Turkey:
		Fx::ConfettiBurst(Deck, FVector::ForwardVector, Out);
		Fx::ConfettiBurst(W(0.0, 2.2, ABowlingAlley::DeckZ + 0.2), FVector::ForwardVector, Out);
		Fountains(Fx::Hex(TEXT("ffd34d")), 70, 1.2);
		// Fireworks over the deck.
		for (int32 i = 0; i < 3; ++i)
		{
			Fx::FKartFxEmit Burst;
			Burst.Field = Fx::EField::Sparks;
			Burst.Position = W(-1.6 + 1.6 * i, 3.6 + 0.3 * (i % 2), ABowlingAlley::DeckZ + 0.8);
			Burst.Count = 60;
			Burst.Spread = UE_DOUBLE_PI;
			Burst.Speed = { 3.0, 6.5 };
			Burst.Life = { 0.8, 1.3 };
			Burst.Size = { 0.05, 0.08 };
			Burst.Color = i == 1 ? Fx::Hex(TEXT("fff2a8")) : (i == 0 ? Fx::Hex(TEXT("ff3fb4")) : Fx::Hex(TEXT("38e1ff")));
			Burst.ColorEnd = Fx::Hex(TEXT("ff7a2a"));
			Burst.Intensity = 7.0;
			Out.Add(Burst);
		}
		FlashColor = FLinearColor(1.f, 0.82f, 0.3f);
		FlashStrength = 0.75;
		FlashAt = Clock;
		ShowAt = Clock;
		ShowEnergy = 1.4;
		ShowSeconds = 3.2;
		ShowTint = FLinearColor(1.f, 0.75f, 0.2f);
		Crowd.Play(this, CueCrowd, 4.5f, 1.0f);
		CrowdHigh.Play(this, CueCrowd, 3.5f, 1.3f);
		Cues.Play(this, CueGo, 1.2f, 0.85f);
		Cues.Play(this, CueSparkle, 1.0f, 1.25f);
		Shake(0.08);
		break;
	case EBowlingBeat::Strike:
		Fx::ConfettiBurst(Deck, FVector::ForwardVector, Out);
		Fountains(Fx::Hex(TEXT("fff2c0")), 45, 1.0);
		FlashColor = FLinearColor::White;
		FlashStrength = 0.55;
		FlashAt = Clock;
		ShowAt = Clock;
		ShowEnergy = 1.0;
		ShowSeconds = 2.4;
		ShowTint = Mine;
		Crowd.Play(this, CueCrowd, 3.6f, 1.0f);
		CrowdHigh.Play(this, CueCrowd, 2.4f, 1.22f);
		Cues.Play(this, CueSparkle, 0.8f, 1.1f);
		Shake(0.05);
		break;
	case EBowlingBeat::Spare:
	case EBowlingBeat::SplitConvert:
		Fx::ConfettiBurst(Deck, FVector::ForwardVector, Out);
		for (Fx::FKartFxEmit& E : Out)
		{
			E.Count = FMath::Max(1, E.Count / 2);
		}
		if (LastBeat == EBowlingBeat::SplitConvert)
		{
			Fountains(Fx::Hex(TEXT("7dffcf")), 40, 1.0);
		}
		FlashColor = FLinearColor(0.5f, 1.f, 0.8f);
		FlashStrength = 0.3;
		FlashAt = Clock;
		ShowAt = Clock;
		ShowEnergy = LastBeat == EBowlingBeat::SplitConvert ? 1.0 : 0.55;
		ShowSeconds = 1.5;
		ShowTint = Mine;
		Crowd.Play(this, CueCrowd, LastBeat == EBowlingBeat::SplitConvert ? 3.4f : 2.4f, 1.08f);
		break;
	case EBowlingBeat::Gutter:
		// A sympathetic groan from the house.
		Crowd.Play(this, CueCrowd, 1.6f, 0.72f);
		break;
	default:
		break;
	}
	for (Fx::FKartFxEmit& E : Out)
	{
		// Paper in a dark room: lift it so it reads as colour, not as dark flecks across the lens.
		if (E.Field == Fx::EField::Confetti)
		{
			E.Intensity *= 3.0;
			E.Size *= 0.75;
		}
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this); FxSys && Out.Num() > 0)
	{
		FxSys->Emit(Out);
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
		return FMath::Lerp(1.0, 0.36, Smooth(0.78, 0.86, T));
	}
	if (Phase == EBowlingPhase::Settling)
	{
		return FMath::Lerp(0.36, 1.0, Smooth(0.0, 0.5, GetPhaseAge()));
	}
	return 1.0;
}

void ABowlingGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	const double RealDt = FMath::Min(DeltaSeconds, 0.1f);
	const float Dt = static_cast<float>(RealDt * TimeScale());
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
			// The impact (fxPlan.ts): sparks, chips and dust by the share of the rack felled, a shock ring across
			// the deck and a jolt through the camera.
			if (!Shot.bGutter)
			{
				const double S = Standing.Num() > 0 ? static_cast<double>(Shot.Felled.Num()) / Standing.Num() : 0.0;
				const FVector Hit = W(Shot.EndX * ABowlingAlley::VisualPinScale, 0.0, ABowlingAlley::DeckZ - 0.2);
				if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
				{
					TArray<Fx::FKartFxEmit> Out;
					Fx::ImpactBurst(Fx::EImpact::Bump, Hit, 0.3 + 0.7 * S, Out);
					// No kart dust on a polished deck: only the sparks of the hit.
					Out.RemoveAll([](const Fx::FKartFxEmit& E) { return E.Field == Fx::EField::Smoke; });
					if (Shot.Felled.Num() > 0)
					{
						Fx::FKartFxEmit Chips;
						Chips.Field = Fx::EField::Sparks;
						Chips.Position = Hit + FVector(0.0, 0.0, 40.0);
						Chips.Count = 12 + FMath::RoundToInt32(36.0 * S);
						Chips.Direction = (FVector::UpVector + FVector::ForwardVector * 0.6).GetSafeNormal();
						Chips.Spread = 1.1;
						Chips.Speed = { 2.5, 5.0 + 3.0 * S };
						Chips.Life = { 0.35, 0.7 };
						Chips.Size = { 0.05, 0.1 };
						Chips.Color = Fx::Hex(TEXT("fff6e0"));
						Chips.ColorEnd = Fx::Hex(TEXT("ffb35a"));
						Chips.Intensity = 5.0;
						Out.Add(Chips);
						Fx::FKartFxEmit Ring;
						Ring.Field = Fx::EField::Ring;
						Ring.Position = W(Shot.EndX * ABowlingAlley::VisualPinScale, 0.05, ABowlingAlley::DeckZ - 0.5);
						Ring.Count = 1;
						Ring.Speed = { 0.0, 0.0 };
						Ring.Life = { 0.45, 0.45 };
						Ring.Size = { 0.4, 0.4 };
						Ring.SizeEnd = 6.0 + 4.0 * S;
						Ring.Color = Fx::Hex(TEXT("bfe9ff"));
						Ring.ColorEnd = Fx::Hex(TEXT("6d8bff"));
						Ring.Intensity = 2.0 * S;
						Out.Add(Ring);
					}
					FxSys->Emit(Out);
				}
				Shake(0.02 + 0.06 * S);
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
		if (Age >= ResetSeconds && Clock - LastBeatAt >= (bTurnEndedPending ? (IsBig(LastBeat) ? CelebrationSeconds : ReactionSeconds) : 0.0))
		{
			// Only the pins still standing stay on the deck.
			Solver.Reset(nullptr, Standing);
			BeginTurn();
		}
		break;
	case EBowlingPhase::Finished:
		break;
	}
	// The house murmurs along under everything, a little louder when the game is over.
	if (Clock >= NextAmbience)
	{
		NextAmbience = Clock + 6.5;
		Crowd.Play(this, CueCrowd, Phase == EBowlingPhase::Finished ? 1.2f : 0.55f, 0.95f);
	}
	UpdateBowler(Dt);
	UpdateBall(Dt);
	UpdateTrail(Dt);
	UpdatePins(Dt);
	UpdatePinsetter();
	UpdateGuide();
	UpdateShow();
	UpdateCamera(static_cast<float>(RealDt));
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
	const double AimBefore = Local.Aim, CurveBefore = Local.Curve;
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
	bLocalAimed |= Local.Aim != AimBefore;
	bLocalHooked |= Local.Curve != CurveBefore;

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
	if (BotGripAt < 0.0 && GetPhaseAge() >= BotThinkSeconds - BotWindUpSeconds)
	{
		BotGripAt = Clock;
	}
	if (GetPhaseAge() < BotThinkSeconds)
	{
		return;
	}
	// Aim the deck line (aim + curve) at the pocket on a full rack, or at the middle of what's left.
	double AimAt = 0.07;
	if (Standing.Num() < 10 && Standing.Num() > 0)
	{
		double Sum = 0.0;
		for (const int32 Id : Standing)
		{
			Sum += PinPosition(Id).X;
		}
		AimAt = Sum / Standing.Num();
	}
	if (Standing.Num() == 10 && DebugFlags.bShowcase)
	{
		Launch(ShowcaseShot());
		return;
	}
	const double Wild = 1.0 - Bowler->Skill;
	FShot Throw;
	Throw.Curve = Standing.Num() == 10 ? 0.22 + BotRng.FRandRange(-0.08, 0.08) * Wild : 0.05;
	Throw.Aim = FMath::Clamp(AimAt - Throw.Curve + BotRng.FRandRange(-0.16, 0.16) * Wild, -0.45, 0.45);
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
	FVector Web = FVector::ZeroVector;
	double Y = BallRollY;
	switch (Phase)
	{
	case EBowlingPhase::Intro:
	case EBowlingPhase::Aiming:
	case EBowlingPhase::Finished:
	{
		// In the bowler's hands: cradled at the chest, or hanging from the swinging hand once gripped.
		const FVector R = Avatar->BoneWorld(RightHand);
		const bool bSwinging = ArmSwing() < -0.05;
		const FVector At = bSwinging ? R + FVector(6.0, 0.0, -14.0) : (R + Avatar->BoneWorld(LeftHand)) / 2.0 + FVector(12.0, 0.0, -4.0);
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

void ABowlingGame::UpdateTrail(float Dt)
{
	// The ball's comet: a glow in the bowler's colour hugging the lane behind it, with sparkles that kick up harder
	// the faster it goes. Off in the gutter.
	if (Phase != EBowlingPhase::Rolling || !bHasShot)
	{
		return;
	}
	const double T = FMath::Clamp(GetPhaseAge() / RollSeconds, 0.0, 1.0);
	if (Shot.bGutter && T * LaneLength > Shot.GutterAt)
	{
		return;
	}
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	const FBowlerSeat* Bowler = GetBowler();
	if (!FxSys || !Bowler)
	{
		return;
	}
	const FVector At = Ball->GetComponentLocation() - FVector(0.0, 0.0, 10.0);
	const double Hot = FMath::Clamp((Shot.Speed - 7.0) / 2.0, 0.0, 1.0);
	Fx::FKartFxEmit Glow;
	Glow.Field = Fx::EField::Fire;
	Glow.Position = At;
	Glow.Count = Fx::ParticlesDue(TrailCarry, 120.0, Dt);
	Glow.Direction = FVector::UpVector;
	Glow.Spread = 0.6;
	Glow.Speed = { 0.05, 0.25 };
	Glow.Life = { 0.3, 0.5 };
	Glow.Size = { 0.24, 0.34 };
	Glow.SizeEnd = 0.05;
	Glow.Color = Bowler->Color;
	Glow.ColorEnd = Bowler->Color * 0.4f;
	Glow.Intensity = 4.0 + 3.0 * Hot;
	Glow.Jitter = 0.04;
	if (Glow.Count > 0)
	{
		FxSys->Emit(Glow);
	}
	Fx::FKartFxEmit Sparkle;
	Sparkle.Field = Fx::EField::Flare;
	Sparkle.Position = At;
	Sparkle.Count = Fx::ParticlesDue(SparkleCarry, 14.0 + 26.0 * Hot, Dt);
	Sparkle.Spread = 1.2;
	Sparkle.Speed = { 0.4, 1.4 };
	Sparkle.Life = { 0.25, 0.5 };
	Sparkle.Size = { 0.08, 0.16 };
	Sparkle.SizeEnd = 0.1;
	Sparkle.Color = FLinearColor::White;
	Sparkle.ColorEnd = Bowler->Color;
	Sparkle.Intensity = 3.0;
	Sparkle.Jitter = 0.1;
	if (Sparkle.Count > 0)
	{
		FxSys->Emit(Sparkle);
	}
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
	else if (Phase != EBowlingPhase::Delivery)
	{
		// Not while the ball is still leaving the hand: a step with no ball arms the solver's fallback, and the
		// rack would start to fall before the ball is even on the lane.
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
		FTransform& Out = PinTransforms[P.Id - 1];
		FVector Web(P.Position.X, P.Position.Y, ABowlingAlley::DeckZ - P.Position.Z);
		bool bShow = P.bVisible;
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
		if (!bShow)
		{
			// A hidden pin is an empty instance under the deck: no pose to work out.
			Out = FTransform(FQuat::Identity, W(0.0, -3.0, ABowlingAlley::DeckZ - 2.0), FVector::ZeroVector);
			continue;
		}
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
		Out = FTransform(Rotation, W(Web.X, Web.Y, Web.Z));
	}
	PinRack->BatchUpdateInstancesTransforms(0, PinTransforms, true, true, true);
}

void ABowlingGame::UpdatePinsetter()
{
	// presentation.ts:196-291: the rake drops, sweeps the deck and lifts; the setter table comes down and back.
	// Between resets the machinery waits up behind the masking units, out of the shot (and is not moved).
	const bool bResetting = Phase == EBowlingPhase::Reset;
	Rake->SetVisibility(bResetting);
	Setter->SetVisibility(bResetting);
	if (!bResetting)
	{
		return;
	}
	const double P = FMath::Clamp(GetPhaseAge() / ResetSeconds, 0.0, 1.0);
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
	Target->SetVisibility(bShow);
	if (!bShow)
	{
		return;
	}
	FShot Preview;
	Preview.Aim = Aim;
	Preview.Curve = Curve;
	const double EndX = BallXAt(Preview, LaneLength);
	const EBowlingLine Line = ReadLine(EndX, FirstGutter(Preview).IsSet(), Standing);
	// The line turns mint in the pocket and red when it would drop into the gutter, so the read is instant.
	const bool bGood = Line == EBowlingLine::Pocket || Line == EBowlingLine::OnTarget;
	const FLinearColor LineColor = bGood ? MovecadeKart::Art::Hex(TEXT("#3dffb0"))
		: (Line == EBowlingLine::Gutter ? MovecadeKart::Art::Hex(TEXT("#ff3b4a")) : Bowler->Color);
	// BowlingScene.tsx:1908-1951: dots along the previewed line (the port draws more, closer), brighter as the arm charges.
	GuideMaterial->SetVectorParameterValue(ColorParam, LineColor * static_cast<float>(4.0 + 5.0 * Charge));
	for (int32 i = 0; i < GuideDots; ++i)
	{
		const double T = (i + 0.5) / GuideDots;
		const double Z = 0.15 - FMath::Pow(T, 0.78) * 13.3;
		const double Progress = (BallStartZ - Z) / BallTravel;
		const double X = BallXAt(Preview, FMath::Clamp(Progress, 0.0, 1.0) * LaneLength) * ABowlingAlley::LaneVisualScale;
		const double Size = (0.65 + 0.3 * Charge) * (0.75 + 0.45 * T) * 0.11;
		Guide->UpdateInstanceTransform(i, FTransform(FRotator::ZeroRotator, W(X, 0.024, Z), FVector(Size, Size, 0.002)), true, i == GuideDots - 1, true);
	}
	// The target ring in front of the head pin, pulsing.
	const double Pulse = 1.0 + 0.12 * FMath::Sin(Clock * 7.0);
	const double R = (bGood ? 0.34 : 0.26) * Pulse;
	Target->SetWorldLocationAndRotation(W(EndX * ABowlingAlley::VisualPinScale, 0.026, ABowlingAlley::DeckZ + 0.55), FRotator::ZeroRotator);
	Target->SetWorldScale3D(FVector(R, R, 0.002));
	TargetMaterial->SetVectorParameterValue(ColorParam, LineColor * static_cast<float>(6.0 + 4.0 * Charge));
}

void ABowlingGame::UpdateShow()
{
	if (!Alley)
	{
		return;
	}
	const double Age = Clock - ShowAt;
	if (Age < 0.0 || Age > ShowSeconds)
	{
		Alley->SetShow(0.0, ShowTint, 0.0);
		return;
	}
	const double Envelope = Smooth(0.0, 0.08, Age) * (1.0 - Smooth(ShowSeconds - 0.6, ShowSeconds, Age));
	Alley->SetShow(ShowEnergy * Envelope, ShowTint, Age);
}

void ABowlingGame::UpdateCamera(float Dt)
{
	// presentation.ts cameraPlan: the address tripod behind the bowler, the impact and result shots over the deck;
	// the port adds a drift after the ball, the slow-motion strike cam and an orbit over a celebrating deck.
	FVector Pos(0.0, 3.65, 6.55), Look(0.0, 0.86, -9.55);
	double Fov = 39.0, Response = 3.05;
	const double EndX = bHasShot ? Shot.EndX * ABowlingAlley::VisualPinScale : 0.0;
	const double Age = GetPhaseAge();
	auto Impact = [&]() { Pos = FVector(0.0, 2.55, -6.85); Look = FVector(EndX * 0.14, 0.58, -13.6); Fov = 39.0; };
	auto Result = [&]() { Pos = FVector(0.0, 3.3, -6.3); Look = FVector(EndX * 0.16, 0.48, -13.6); Fov = 40.0; Response = 5.2; };
	auto Celebration = [&](double Since)
	{
		// A low swing across the deck while the house goes off.
		const double A = -0.55 + 1.1 * Smooth(0.0, CelebrationSeconds + 0.4, Since);
		Pos = FVector(FMath::Sin(A) * 3.4, 1.45 + 0.4 * Smooth(0.0, 2.0, Since), ABowlingAlley::DeckZ + FMath::Cos(A) * 4.6);
		Look = FVector(0.0, 0.9, ABowlingAlley::DeckZ - 0.6);
		Fov = 44.0;
		Response = 3.4;
	};
	switch (Phase)
	{
	case EBowlingPhase::Intro:
		// A slow push down the lane toward the rack before the first ball.
		Pos = FMath::Lerp(FVector(2.4, 2.6, -2.0), FVector(0.0, 3.65, 6.55), Smooth(0.0, 1.0, Age / IntroSeconds));
		Look = FMath::Lerp(FVector(0.0, 0.6, -13.6), FVector(0.0, 0.86, -9.55), Smooth(0.0, 1.0, Age / IntroSeconds));
		Response = 8.0;
		break;
	case EBowlingPhase::Rolling:
	{
		// The tripod leans after the ball; a strike-bound ball gets the low cam at the deck as time slows.
		const double T = FMath::Clamp(Age / RollSeconds, 0.0, 1.0);
		Pos = FVector(0.0, 3.4, 6.2 - 3.6 * Smooth(0.0, 1.0, T));
		Look = FVector(EndX * 0.1, 0.7, -10.5);
		Fov = 37.0;
		if (bStrikeBound && T > 0.62)
		{
			Pos = FVector(EndX * 0.3 - 1.25, 0.75, ABowlingAlley::DeckZ + 3.4);
			Look = FVector(EndX * 0.5, 0.45, ABowlingAlley::DeckZ - 0.4);
			Fov = 40.0;
			Response = 4.2;
		}
		break;
	}
	case EBowlingPhase::Settling:
		if (bStrikeBound)
		{
			Pos = FVector(EndX * 0.3 - 1.25, 0.8, ABowlingAlley::DeckZ + 3.2);
			Look = FVector(EndX * 0.4, 0.55, ABowlingAlley::DeckZ - 0.6);
			Fov = 42.0;
			Response = 2.0;
		}
		else
		{
			(Age / SettleSeconds <= 0.55) ? Impact() : Result();
		}
		break;
	case EBowlingPhase::Reset:
	{
		const double Since = Clock - LastBeatAt;
		if (IsBig(LastBeat) && Since < CelebrationSeconds)
		{
			Celebration(Since);
		}
		else if (Age / ResetSeconds < 0.58)
		{
			Result();
		}
		break;
	}
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
	// The impact's jolt: a quick decaying wobble on top of the framing.
	FVector Jolt = FVector::ZeroVector;
	const double ShakeAge = Clock - ShakeAt;
	if (ShakeAge >= 0.0 && ShakeAge < 0.55 && !bPaused)
	{
		const double Amp = ShakeMetres * FMath::Square(1.0 - ShakeAge / 0.55);
		Jolt = FVector(FMath::Sin(ShakeAge * 71.0), FMath::Sin(ShakeAge * 53.0 + 1.3), FMath::Sin(ShakeAge * 61.0 + 2.1) * 0.5) * Amp;
	}
	const FVector From = W(CamPos.X + Jolt.X, CamPos.Y + Jolt.Y, CamPos.Z + Jolt.Z);
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

double ABowlingGame::GripAge() const
{
	if (Phase != EBowlingPhase::Aiming)
	{
		return -1.0;
	}
	if (Local.bGripped)
	{
		return Clock - Local.GripAt;
	}
	if (BotGripAt >= 0.0)
	{
		return Clock - BotGripAt;
	}
	return PhoneGripAt >= 0.0 && PhoneCharge > 0.02 ? Clock - PhoneGripAt : -1.0;
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
	const double Arm = ArmSwing();
	const double Age = GetPhaseAge();
	// Where the bowler stands: on the line (the right hand over it), walking the approach through the backswing and
	// sliding to the foul line as the ball goes.
	if (Phase == EBowlingPhase::Aiming || Phase == EBowlingPhase::Intro)
	{
		double Aim = 0.0, Curve = 0.0, Charge = 0.0;
		const double Line = (LiveAim(Aim, Curve, Charge) ? Aim : 0.0) * ABowlingAlley::LaneVisualScale - 0.3;
		StanceX = FMath::FInterpTo(StanceX, Line, Dt, 10.0);
	}
	const double Grip = GripAge();
	const bool bApproach = Grip >= 0.0;
	const bool bThrown = Phase == EBowlingPhase::Delivery || Phase == EBowlingPhase::Rolling || Phase == EBowlingPhase::Settling;
	const double SinceRelease = Phase == EBowlingPhase::Delivery ? Age : (Phase == EBowlingPhase::Rolling ? DeliverySeconds + Age : 10.0);
	if (bThrown)
	{
		StanceZ = FMath::Lerp(ReleaseZ, SlideZ, Smooth(0.0, 0.55, SinceRelease));
	}
	else if (Phase == EBowlingPhase::Reset)
	{
		StanceZ = FMath::Lerp(SlideZ, AddressZ, Smooth(0.0, 1.0, Age / ResetSeconds));
	}
	else
	{
		StanceZ = bApproach ? FMath::Lerp(AddressZ, ApproachZ, Smooth(0.0, 0.85, Grip)) : AddressZ;
	}
	Avatar->SetWorldLocationAndRotation(W(StanceX, 0.0, StanceZ), FRotator::ZeroRotator);

	// The bowling arm: a pendulum from the shoulder; the other hand cradles the ball until the swing starts.
	const double Reach = 0.97;
	Pose.Hand[1] = FVector(FMath::Sin(Arm) * Reach, 0.12, -FMath::Cos(Arm) * Reach);
	Pose.HandWeight[1] = 1.f;
	const bool bCradle = FMath::Abs(Arm) < 0.05 && (Phase == EBowlingPhase::Aiming || Phase == EBowlingPhase::Intro);
	if (bCradle)
	{
		Pose.Hand[1] = FVector(0.5, -0.05, -0.38);
		Pose.Hand[0] = FVector(0.5, 0.24, -0.4);
		Pose.HandWeight[0] = 1.f;
	}
	else
	{
		// Balance: the free arm swings out the other way.
		Pose.Hand[0] = FVector(-FMath::Sin(Arm) * 0.4, -0.55, -0.7);
		Pose.HandWeight[0] = bThrown ? 0.85f : 0.4f;
	}
	const double Charge = Arm < 0.0 ? FMath::Clamp(-Arm / 1.6, 0.0, 1.0) : 0.0;
	const double Settle = bThrown ? 1.0 - Smooth(1.0, 2.6, SinceRelease) : 0.0;
	Pose.Crouch = static_cast<float>(0.15 + 0.3 * Charge + 0.35 * Settle);
	Pose.Lean = static_cast<float>(0.12 + 0.25 * Charge + 0.4 * Settle);
	Pose.Lunge = static_cast<float>(bThrown ? Smooth(0.0, 0.25, SinceRelease) * (0.4 + 0.6 * Settle) : 0.0);
	// The approach steps through the backswing (they stop once the bowler reaches the line), then the slide.
	const double Walking = bApproach ? 0.75 * (1.0 - Smooth(0.7, 0.95, Grip)) : 0.0;
	Pose.Step = static_cast<float>(bThrown ? 0.8 * (1.0 - Smooth(0.0, 0.45, SinceRelease)) : Walking);
	Pose.StepPhase = static_cast<float>(bThrown ? SinceRelease * 12.0 : FMath::Max(0.0, Grip) * 9.0);
	Pose.Twist = static_cast<float>(-0.12 * Charge);

	// Eyes on the pins, then on the ball. The avatar faces UE +X, so world directions are its own (X fwd, Y right).
	const FVector LookAt = Phase == EBowlingPhase::Rolling ? Ball->GetComponentLocation() : W(0.0, 0.4, ABowlingAlley::DeckZ);
	Pose.Look = (LookAt - Avatar->GetComponentLocation() - FVector(0.0, 0.0, 160.0)).GetSafeNormal();
	Pose.LookWeight = 0.75f;

	// Reactions (avatar/motion reactions.ts): a strike pumps both fists and jumps, a spare cheers, a gutter slumps.
	const double Since = Clock - LastBeatAt;
	const double Hold = IsBig(LastBeat) ? CelebrationSeconds : 1.8;
	const double React = Since < Hold ? FMath::Clamp(Since / 0.2, 0.0, 1.0) * FMath::Clamp((Hold - Since) / 0.3, 0.0, 1.0) : 0.0;
	float Cheer = 0.f, Slump = 0.f, Lift = 0.f;
	switch (LastBeat)
	{
	case EBowlingBeat::Strike:
	case EBowlingBeat::Turkey:
		Cheer = 1.f;
		Lift = static_cast<float>((LastBeat == EBowlingBeat::Turkey ? 0.2 : 0.14) * FMath::Abs(FMath::Sin(Since * 7.5)));
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
	RefreshBowler();
	Standing = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
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
