#include "Games/Fishing/FishingGame.h"

#include "Algo/StableSort.h"
#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Fishing/FishingArt.h"
#include "Games/Fishing/FishingLake.h"
#include "Games/Fishing/SFishingHud.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Net/RoomProtocol.h"

DEFINE_LOG_CATEGORY_STATIC(LogFishing, Log, All);

using namespace MovecadeKart::Fishing;
namespace Fx = MovecadeKart::Fx;
namespace FA = MovecadeKart::Fishing::Art;

namespace
{
	constexpr double IntroSeconds = 3.6;
	/** The rules step on a fixed 10 ms clock (the fight's own substep), so every machine agrees. */
	constexpr double StepMs = 10.0;
	constexpr double ShowcaseSeconds = 7.0;
	constexpr int32 RodSegments = 6;
	constexpr int32 LineSegments = 22;
	constexpr double SegmentCm = 40.0;
	/** A full turn of the local meter (empty, full, empty). */
	constexpr double MeterPeriod = 1.5;
	constexpr double PhoneStaleSeconds = 0.35;

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double EaseOut(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return 1.0 - FMath::Pow(1.0 - T, 3.0);
	}

	/** Revolutions per second to the rules' 0..MaxReel (the same scale the phone's detector uses). */
	double ReelFromRate(double RevPerSec)
	{
		return FMath::Clamp((RevPerSec - 0.12) / (1.6 - 0.12), 0.0, MaxReel);
	}

	FRotator Facing(double Yaw)
	{
		return FRotator(0.0, Yaw, 0.0);
	}
}

AFishingGame::AFishingGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(0xf15e);
	FxRng.Initialize(0x5b1a);
}

UStaticMeshComponent* AFishingGame::MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
	C->SetVisibility(false);
	C->RegisterComponent();
	return C;
}

UStaticMesh* AFishingGame::FishMesh(int32 Species)
{
	if (!FishMeshes.IsValidIndex(Species))
	{
		return nullptr;
	}
	if (!FishMeshes[Species])
	{
		FishMeshes[Species] = FA::BuildFish(this, Species);
	}
	return FishMeshes[Species];
}

void AFishingGame::ShowFish(int32 i, int32 Species, int32 WeightG, const FTransform& At)
{
	UStaticMeshComponent* C = Fish[i];
	if (FishShown[i] != Species)
	{
		FishShown[i] = Species;
		C->SetStaticMesh(FishMesh(Species));
		FA::DressFish(this, C, Species);
	}
	const double S = FA::LengthCm(Species, WeightG) / 100.0;
	C->SetWorldTransform(FTransform(At.GetRotation(), At.GetLocation(), FVector(S)));
	C->SetVisibility(true);
}

void AFishingGame::Setup(AFishingLake* InLake, const TArray<FFishingSeat>& InSeats, APlayerController* InViewer, uint32 InSeed)
{
	Lake = InLake;
	Seats = InSeats;
	Viewer = InViewer;
	Seed = InSeed;
	const int32 N = Seats.Num();
	if (Lake)
	{
		Lake->SetLanes(N);
	}
	TArray<FSeed> MatchSeeds;
	for (int32 i = 0; i < N; ++i)
	{
		MatchSeeds.Add({ Seats[i].Id, Seats[i].Name });
		FLane& L = Lanes.AddDefaulted_GetRef();
		L.PlayerId = Seats[i].Id;
		L.Kind = Seats[i].Kind;
		L.NextActAt = 400.0 + 500.0 * i;
	}
	Match = CreateMatch(MatchSeeds, Seed);

	using namespace MovecadeKart::Art;
	UStaticMesh* HandleMesh = FA::BuildRodHandle(this);
	UStaticMesh* SegmentMesh = FA::BuildRodSegment(this);
	UStaticMesh* LineMesh = FA::BuildRodLine(this);
	UStaticMesh* BobberMesh = FA::BuildBobber(this);
	UMaterialInterface* Cork = Surface(this, { Hex(TEXT("#c89a62")), TEXT("gravelly_sand"), 0.15, 0.5, 0.6, 0.4, 0.9 });
	UMaterialInterface* ReelMat = Plain(this, Hex(TEXT("#2a2c33")), 0.3, 0.6);
	UMaterialInterface* Metal = Plain(this, Hex(TEXT("#c9ccd2")), 0.25, 0.95);
	UMaterialInterface* Guide = Plain(this, Hex(TEXT("#d8d8dc")), 0.2, 1.0);
	UMaterialInterface* White = Plain(this, Hex(TEXT("#f4f1ea")), 0.35);
	LineMaterial = Glow(this, Hex(TEXT("#fff1d2")) * 0.9f);
	FishMeshes.SetNum(Species().Num());
	for (int32 i = 0; i < N; ++i)
	{
		const FLinearColor C = AFishingLake::LaneColor(i);
		UArcadeAvatarComponent* A = NewObject<UArcadeAvatarComponent>(this, *FString::Printf(TEXT("Angler%d"), i));
		A->AvatarScale = 0.84f;
		A->SetupAttachment(RootComponent);
		A->RegisterComponent();
		A->SetLook(Seats[i].Look);
		A->SetWorldLocation(AFishingLake::AnglerSpot(i, N));
		Avatars.Add(A);
		UStaticMeshComponent* H = MakeMesh(TEXT("RodHandle"), HandleMesh);
		H->SetMaterialByName(TEXT("Cork"), Cork);
		H->SetMaterialByName(TEXT("Reel"), ReelMat);
		H->SetMaterialByName(TEXT("Metal"), Metal);
		Handles.Add(H);
		UMaterialInterface* Blank = Plain(this, FMath::Lerp(C, Hex(TEXT("#1c1c22")), 0.35f), 0.25, 0.3);
		for (int32 k = 0; k < RodSegments; ++k)
		{
			UStaticMeshComponent* S = MakeMesh(TEXT("RodBlank"), SegmentMesh);
			S->SetMaterialByName(TEXT("Blank"), Blank);
			S->SetMaterialByName(TEXT("Guide"), Guide);
			RodPieces.Add(S);
		}
		for (int32 k = 0; k < LineSegments; ++k)
		{
			UStaticMeshComponent* S = MakeMesh(TEXT("Line"), LineMesh, false);
			S->SetMaterial(0, LineMaterial);
			LinePieces.Add(S);
		}
		UStaticMeshComponent* B = MakeMesh(TEXT("Bobber"), BobberMesh);
		B->SetMaterialByName(TEXT("Top"), Plain(this, C, 0.3, 0.0, 0.25));
		B->SetMaterialByName(TEXT("Bottom"), White);
		B->SetMaterialByName(TEXT("Stick"), Plain(this, Hex(TEXT("#2a2c33")), 0.5));
		Bobbers.Add(B);
		Fish.Add(MakeMesh(TEXT("Catch"), nullptr));
		FishShown.Add(-1);
	}

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SFishingHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}
	SetStage(EFishingStage::Intro);
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

bool AFishingGame::ScreenOf(const FVector& World, FVector2D& Out01) const
{
	FVector2D Pixels;
	int32 X = 0, Y = 0;
	if (!Viewer || !UGameplayStatics::ProjectWorldToScreen(Viewer, World, Pixels))
	{
		return false;
	}
	Viewer->GetViewportSize(X, Y);
	if (X <= 0 || Y <= 0)
	{
		return false;
	}
	Out01 = Pixels / FVector2D(X, Y);
	return true;
}

double AFishingGame::ChargeOf(int32 Lane) const
{
	return Lanes.IsValidIndex(Lane) && Lanes[Lane].bHolding ? Lanes[Lane].Charge : -1.0;
}

FVector AFishingGame::HeadOf(int32 Lane) const
{
	return AFishingLake::AnglerSpot(Lane, LaneCount()) + FVector(0.0, 0.0, 205.0);
}

FVector AFishingGame::BobberOf(int32 Lane) const
{
	return Bobbers.IsValidIndex(Lane) && Bobbers[Lane] ? Bobbers[Lane]->GetComponentLocation() : FVector::ZeroVector;
}

FVector AFishingGame::HoldPoint(int32 Lane) const
{
	const UArcadeAvatarComponent* A = Avatars.IsValidIndex(Lane) ? Avatars[Lane].Get() : nullptr;
	if (!A)
	{
		return FVector::ZeroVector;
	}
	return A->GetComponentLocation() + A->GetComponentRotation().RotateVector(FVector(42.0, 0.0, 118.0));
}

TArray<const FAngler*> AFishingGame::Standings() const
{
	return Leaderboard(Match);
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

void AFishingGame::Press(int32 i)
{
	if (Stage != EFishingStage::Match || bPaused || !Lanes.IsValidIndex(i))
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	switch (A.State)
	{
	case EAnglerState::Idle:
		if (Match.Phase == EMatchPhase::Playing && !Lane.bHolding)
		{
			Lane.bHolding = true;
			Lane.HoldAt = Clock;
			Lane.Charge = 0.0;
		}
		break;
	case EAnglerState::Waiting:
	case EAnglerState::Bite:
		Hook(Match, A.PlayerId);
		DrainEffects();
		break;
	default:
		break;
	}
}

void AFishingGame::Release(int32 i)
{
	if (!Lanes.IsValidIndex(i) || !Lanes[i].bHolding)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	Lane.bHolding = false;
	if (Stage == EFishingStage::Match && !bPaused && Cast(Match, Lane.PlayerId, FMath::Max(0.04, Lane.Charge)))
	{
		DrainEffects();
	}
}

void AFishingGame::PadPress(const FString& PlayerId)
{
	Press(Lanes.IndexOfByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; }));
}

void AFishingGame::PadRelease(const FString& PlayerId)
{
	Release(Lanes.IndexOfByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; }));
}

void AFishingGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	const int32 i = Lanes.IndexOfByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; });
	if (i == INDEX_NONE)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const TOptional<FFlick> Flick = Lane.Motion.Feed(FMotionSample::FromFrame(Frame));
	Lane.PhoneReel = Lane.Motion.Reel();
	Lane.LastFrameAt = Clock;
	if (!Flick || Stage != EFishingStage::Match || bPaused)
	{
		return;
	}
	const FAngler& A = Match.Anglers[i];
	if (A.State == EAnglerState::Idle && Flick->Direction == EFlick::Back)
	{
		// The wind-up before the cast: the avatar draws the rod back.
		Lane.WindupAt = Clock;
	}
	else if (A.State == EAnglerState::Idle && !Lane.bHolding)
	{
		Cast(Match, PlayerId, Flick->Power);
		DrainEffects();
	}
	else if ((A.State == EAnglerState::Waiting || A.State == EAnglerState::Bite) && Flick->Direction != EFlick::Forward)
	{
		Hook(Match, PlayerId);
		DrainEffects();
	}
}

void AFishingGame::ReadLocal(FLane& Lane, float Dt)
{
	const int32 i = Lanes.IndexOfByPredicate([&Lane](const FLane& L) { return &L == &Lane; });
	if (!Viewer || i == INDEX_NONE)
	{
		return;
	}
	APlayerController* PC = Viewer;
	const FKey Buttons[] = { EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom, EKeys::LeftMouseButton, EKeys::Enter, EKeys::Gamepad_RightTrigger };
	bool bJust = false, bDown = false;
	for (const FKey& K : Buttons)
	{
		bJust |= PC->WasInputKeyJustPressed(K);
		bDown |= PC->IsInputKeyDown(K);
	}
	if (bJust)
	{
		Press(i);
	}
	if (Lane.bHolding && !bDown && Lane.Kind == EAnglerKind::Local)
	{
		Release(i);
	}
	// The crank: the right (or left) stick going round, alternating keys, the mouse wheel, or the mouse circling.
	double Revs = 0.0;
	for (const TPair<FKey, FKey>& Axis : { TPair<FKey, FKey>(EKeys::Gamepad_RightX, EKeys::Gamepad_RightY), TPair<FKey, FKey>(EKeys::Gamepad_LeftX, EKeys::Gamepad_LeftY) })
	{
		const FVector2D Stick(PC->GetInputAnalogKeyState(Axis.Key), PC->GetInputAnalogKeyState(Axis.Value));
		if (Stick.Size() > 0.55)
		{
			const double Angle = FMath::Atan2(Stick.Y, Stick.X);
			if (Lane.bStickValid)
			{
				Revs = FMath::Max(Revs, FMath::Abs(FMath::UnwindRadians(Angle - Lane.StickAngle)) / (2.0 * UE_DOUBLE_PI));
			}
			Lane.StickAngle = Angle;
			Lane.bStickValid = true;
			break;
		}
		Lane.bStickValid = false;
	}
	const FKey CrankKeys[][2] = { { EKeys::A, EKeys::D }, { EKeys::Left, EKeys::Right }, { EKeys::Q, EKeys::E }, { EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder } };
	for (int32 Pair = 0; Pair < UE_ARRAY_COUNT(CrankKeys); ++Pair)
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			if (PC->WasInputKeyJustPressed(CrankKeys[Pair][Side]))
			{
				const int32 Key = Pair * 2 + Side;
				if (Lane.LastCrankKey >= 0 && Lane.LastCrankKey != Key && Lane.LastCrankKey / 2 == Pair)
				{
					Revs += 0.25;
				}
				Lane.LastCrankKey = Key;
			}
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) || PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		Revs += 0.16;
	}
	float MouseX = 0.f, MouseY = 0.f;
	PC->GetInputMouseDelta(MouseX, MouseY);
	if (FMath::Sqrt(MouseX * MouseX + MouseY * MouseY) > 2.5f)
	{
		const double Angle = FMath::Atan2(static_cast<double>(MouseY), static_cast<double>(MouseX));
		if (Lane.bMouseValid)
		{
			Revs += FMath::Min(0.25, FMath::Abs(FMath::UnwindRadians(Angle - Lane.MouseAngle)) / (2.0 * UE_DOUBLE_PI));
		}
		Lane.MouseAngle = Angle;
		Lane.bMouseValid = true;
	}
	else
	{
		Lane.bMouseValid = false;
	}
	const double Rate = Dt > 0.f ? Revs / Dt : 0.0;
	Lane.CrankRate += (FMath::Min(Rate, 4.0) - Lane.CrankRate) * (1.0 - FMath::Exp(-Dt / 0.22));
}

void AFishingGame::SteerBot(int32 i)
{
	FLane& Lane = Lanes[i];
	FAngler& A = Match.Anglers[i];
	const double S = Seats.IsValidIndex(i) ? Seats[i].Skill : 0.6;
	const double Now = Match.ElapsedMs;
	if (Match.Phase != EMatchPhase::Playing)
	{
		return;
	}
	switch (A.State)
	{
	case EAnglerState::Idle:
		SetReel(Match, A.PlayerId, 0.0);
		if (!Lane.bHolding && Now >= Lane.NextActAt)
		{
			// A wind-up, then the cast: sharp bots reach for the far water, sloppy ones scatter.
			Lane.bHolding = true;
			Lane.HoldAt = Clock;
			Lane.BotPower = FMath::Clamp(0.45 + 0.5 * S + BotRng.FRandRange(-1.f, 1.f) * (0.12 + 0.3 * (1.0 - S)), 0.12, 1.0);
		}
		if (Lane.bHolding)
		{
			Lane.Charge = Lane.BotPower * Smooth(0.0, 0.7, Clock - Lane.HoldAt);
			if (Clock - Lane.HoldAt >= 0.75)
			{
				Lane.bHolding = false;
				Cast(Match, A.PlayerId, Lane.BotPower);
			}
		}
		break;
	case EAnglerState::Bite:
		if (Lane.ReactAt < 0.0)
		{
			Lane.ReactAt = Now + 170.0 + (1.0 - S) * 430.0 + BotRng.FRand() * 220.0;
		}
		if (Now >= Lane.ReactAt)
		{
			Lane.ReactAt = -1.0;
			Hook(Match, A.PlayerId);
		}
		break;
	case EAnglerState::Fighting:
	{
		const FFight& F = A.Fight;
		Lane.BotError = FMath::Clamp(Lane.BotError + (BotRng.FRand() - 0.5) * 0.02, -0.2 * (1.0 - S) - 0.03, 0.2 * (1.0 - S) + 0.03);
		const bool bRunning = F.WarnMs > 0.0 || F.BurstMs > 0.0;
		if (bRunning && Lane.EaseFrom < 0.0)
		{
			// Ease off for the run, after a reaction; a careless bot keeps cranking.
			Lane.EaseFrom = BotRng.FRand() < 0.3 + 0.65 * S ? Now + 60.0 + (1.0 - S) * 300.0 : 1e12;
		}
		if (!bRunning)
		{
			Lane.EaseFrom = -1.0;
		}
		const bool bEase = bRunning && Now >= Lane.EaseFrom;
		const double Want = bEase ? 0.0 : ReelForTension(F.Pull, 0.52 + Lane.BotError);
		Lane.BotReel += (Want - Lane.BotReel) * (bEase ? 0.3 : 0.08);
		SetReel(Match, A.PlayerId, Lane.BotReel);
		break;
	}
	case EAnglerState::Landed:
	case EAnglerState::Lost:
		SetReel(Match, A.PlayerId, 0.0);
		Lane.BotReel = 0.0;
		Lane.NextActAt = Now + 500.0 + BotRng.FRand() * 900.0 * (1.3 - S) + (A.State == EAnglerState::Landed ? LandedHoldMs - A.StateMs : LostHoldMs - A.StateMs);
		break;
	default:
		SetReel(Match, A.PlayerId, 0.0);
		break;
	}
}

void AFishingGame::StepFixed()
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		const FAngler& A = Match.Anglers[i];
		switch (Lane.Kind)
		{
		case EAnglerKind::Local:
			SetReel(Match, A.PlayerId, ReelFromRate(Lane.CrankRate));
			break;
		case EAnglerKind::Phone:
			SetReel(Match, A.PlayerId, Clock - Lane.LastFrameAt < PhoneStaleSeconds ? Lane.PhoneReel : 0.0);
			break;
		default:
			SteerBot(i);
			break;
		}
	}
	StepMatch(Match, StepMs);
	DrainEffects();
}

void AFishingGame::DrainEffects()
{
	for (const FEffect& E : Match.Effects)
	{
		if (!SeenEffects.Contains(E.Id))
		{
			SeenEffects.Add(E.Id);
			PresentEffect(E);
			if (OnEffect)
			{
				OnEffect(E);
			}
		}
	}
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		const FAngler& A = Match.Anglers[i];
		if (A.State != Lanes[i].Shown)
		{
			Lanes[i].Shown = A.State;
			Lanes[i].ShownAt = Clock;
			if (A.State != EAnglerState::Idle)
			{
				Lanes[i].bHolding = Lanes[i].Kind == EAnglerKind::Bot ? false : Lanes[i].bHolding && A.State == EAnglerState::Idle;
			}
			if (OnAnglerState)
			{
				OnAnglerState(A);
			}
		}
	}
}

void AFishingGame::Splash(const FVector& At, double Size, bool bRing)
{
	TArray<Fx::FKartFxEmit> Out;
	Fx::FKartFxEmit Drops;
	Drops.Field = Fx::EField::Sparks;
	Drops.Position = At + FVector(0.0, 0.0, 4.0);
	Drops.Count = FMath::RoundToInt32(8.0 + 18.0 * Size);
	Drops.Spread = 0.55;
	Drops.Speed = FVector2D(1.2, 2.6) * (0.6 + 0.6 * Size);
	Drops.Life = FVector2D(0.35, 0.7);
	Drops.Size = FVector2D(0.015, 0.03);
	Drops.Color = FLinearColor(0.85f, 0.95f, 1.f);
	Drops.ColorEnd = FLinearColor(0.5f, 0.75f, 0.85f);
	Drops.Intensity = 1.4;
	Drops.Jitter = 0.08 * Size;
	Out.Add(Drops);
	Fx::FKartFxEmit Spray;
	Spray.Field = Fx::EField::Smoke;
	Spray.Position = At + FVector(0.0, 0.0, 6.0);
	Spray.Count = FMath::RoundToInt32(4.0 + 8.0 * Size);
	Spray.Spread = 0.5;
	Spray.Speed = FVector2D(0.4, 1.3) * (0.6 + 0.5 * Size);
	Spray.Life = FVector2D(0.35, 0.6);
	Spray.Size = FVector2D(0.06, 0.12) * (0.7 + 0.6 * Size);
	Spray.SizeEnd = 1.6;
	Spray.Color = FLinearColor(0.95f, 0.98f, 1.f);
	Spray.ColorEnd = FLinearColor(0.75f, 0.85f, 0.9f);
	Spray.Intensity = 1.0;
	Spray.Jitter = 0.06 * Size;
	Out.Add(Spray);
	if (bRing)
	{
		Fx::FKartFxEmit Ring;
		Ring.Field = Fx::EField::Ring;
		Ring.Position = At + FVector(0.0, 0.0, 1.0);
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.8, 0.8);
		Ring.Size = FVector2D(0.25, 0.25) * (0.6 + Size);
		Ring.SizeEnd = 4.0;
		Ring.Color = FLinearColor(0.9f, 0.95f, 1.f);
		Ring.ColorEnd = FLinearColor(0.6f, 0.8f, 0.9f);
		Ring.Intensity = 0.8;
		Out.Add(Ring);
	}
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		FxSys->Emit(Out);
	}
}

void AFishingGame::PresentEffect(const FEffect& E)
{
	const int32 i = E.LaneIndex;
	if (!Lanes.IsValidIndex(i))
	{
		return;
	}
	FLane& Lane = Lanes[i];
	const FAngler& A = Match.Anglers[i];
	const int32 N = LaneCount();
	Lane.Reaction = E.Kind;
	Lane.ReactionAt = Clock;
	const FVector Water = AFishingLake::WaterPoint(i, N, E.DistanceM, A.State == EAnglerState::Fighting ? A.Fight.Lateral : 0.0);
	const FSpecies* Sp = Species().IsValidIndex(E.Species) ? &Species()[E.Species] : nullptr;
	switch (E.Kind)
	{
	case EEffectKind::Cast:
		Lane.CastFrom = Lane.Tip;
		Cues.Play(this, TEXT("fishing.cast"), 0.7f + 0.3f * static_cast<float>(A.CastPower), 0.85f + 0.3f * static_cast<float>(A.CastPower));
		break;
	case EEffectKind::Splash:
		Lane.BobV = -190.0;
		Splash(Water, 0.5);
		Cues.Play(this, TEXT("fishing.plop"), 0.9f, 1.15f - 0.3f * static_cast<float>(E.DistanceM / MaxCastM));
		break;
	case EEffectKind::Nibble:
		Lane.BobV = -95.0;
		Splash(Water, 0.08);
		Cues.Play(this, TEXT("fishing.nibble"), 0.6f, 1.3f);
		if (Lane.Kind == EAnglerKind::Bot && Seats.IsValidIndex(i) && BotRng.FRand() < (1.0 - Seats[i].Skill) * 0.3)
		{
			Hook(Match, A.PlayerId);
		}
		break;
	case EEffectKind::Bite:
		Lane.BobV = -240.0;
		Splash(Water, 0.35);
		Cues.Play(this, TEXT("fishing.bite"));
		break;
	case EEffectKind::Hooked:
		Lane.HookedAt = Clock;
		Splash(Water, 0.7);
		Cues.Play(this, TEXT("fishing.hook"));
		Shake = FMath::Max(Shake, 0.25);
		break;
	case EEffectKind::TooEarly:
		Cues.Play(this, TEXT("fishing.early"), 0.7f, 1.2f);
		break;
	case EEffectKind::Burst:
		Lane.JumpBurst = A.Fight.Bursts + 1;
		Splash(Water, 0.9);
		Cues.Play(this, TEXT("fishing.burst"), 0.8f, Sp && Sp->Strength >= 1.0 ? 0.8f : 1.1f);
		if (Sp && Sp->Strength >= 0.5 && FxRng.FRand() < 0.65)
		{
			Lane.JumpAt = Clock + 0.45;
		}
		break;
	case EEffectKind::Snap:
	{
		Lane.SnapAt = Clock;
		Cues.Play(this, TEXT("fishing.snap"));
		Shake = FMath::Max(Shake, 0.6);
		TArray<Fx::FKartFxEmit> Out;
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = Lane.Tip;
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.18, 0.18);
		Flash.Size = FVector2D(0.6, 0.6);
		Flash.SizeEnd = 0.2;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = FLinearColor(1.f, 0.4f, 0.3f);
		Flash.Intensity = 2.5;
		Out.Add(Flash);
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
		{
			FxSys->Emit(Out);
		}
		Splash(Water, 0.6);
		break;
	}
	case EEffectKind::Escape:
	case EEffectKind::StolenBait:
		Splash(Water, 0.4);
		Cues.Play(this, TEXT("fishing.escape"));
		break;
	case EEffectKind::Catch:
	case EEffectKind::Junk:
	{
		Splash(AFishingLake::WaterPoint(i, N, LandM, 0.0), 1.0);
		const float Big = FMath::Clamp(static_cast<float>(E.WeightG) / 12000.f, 0.f, 1.f);
		Cues.Play(this, E.Kind == EEffectKind::Junk ? TEXT("fishing.junk") : TEXT("fishing.catch"), 0.8f + 0.2f * Big, E.Kind == EEffectKind::Junk ? 0.8f : 1.1f - 0.25f * Big);
		Shake = FMath::Max(Shake, 0.15 + 0.4 * Big);
		break;
	}
	case EEffectKind::Record:
	{
		Cues.Play(this, TEXT("fishing.record"));
		TArray<Fx::FKartFxEmit> Out;
		Fx::FKartFxEmit Confetti;
		Confetti.Field = Fx::EField::Confetti;
		Confetti.Position = HeadOf(i) + FVector(0.0, 0.0, 60.0);
		Confetti.Count = 70;
		Confetti.Spread = 0.9;
		Confetti.Speed = FVector2D(3.0, 6.0);
		Confetti.Life = FVector2D(1.4, 2.2);
		Confetti.Size = FVector2D(0.05, 0.09);
		Out.Add(Confetti);
		Fx::FKartFxEmit Glint;
		Glint.Field = Fx::EField::Flare;
		Glint.Position = HoldPoint(i);
		Glint.Count = 6;
		Glint.Spread = 1.2;
		Glint.Speed = FVector2D(0.3, 0.8);
		Glint.Life = FVector2D(0.5, 0.9);
		Glint.Size = FVector2D(0.25, 0.45);
		Glint.Color = MovecadeKart::Art::Hex(TEXT("#fff0b0"));
		Glint.ColorEnd = MovecadeKart::Art::Hex(TEXT("#ffb020"));
		Glint.Intensity = 3.0;
		Out.Add(Glint);
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
		{
			FxSys->Emit(Out);
		}
		break;
	}
	default:
		break;
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
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	Clock += Dt;
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		if (Lane.Kind == EAnglerKind::Local)
		{
			ReadLocal(Lane, Dt);
		}
		if (Lane.bHolding && Lane.Kind != EAnglerKind::Bot)
		{
			// The meter runs empty, full, empty: let go at the top for the far water.
			Lane.Charge = 0.5 - 0.5 * FMath::Cos(2.0 * UE_DOUBLE_PI * (Clock - Lane.HoldAt) / MeterPeriod);
		}
	}
	switch (Stage)
	{
	case EFishingStage::Intro:
		for (FLane& Lane : Lanes)
		{
			Lane.bHolding = false;
		}
		if (GetStageAge() >= IntroSeconds)
		{
			SetStage(EFishingStage::Match);
		}
		break;
	case EFishingStage::Match:
		Accumulator += Dt * 1000.0;
		while (Accumulator >= StepMs)
		{
			StepFixed();
			Accumulator -= StepMs;
		}
		if (Match.Phase == EMatchPhase::Ended)
		{
			for (FLane& Lane : Lanes)
			{
				Lane.bHolding = false;
			}
			SetStage(EFishingStage::Finished);
			UE_LOG(LogFishing, Log, TEXT("Fishing: round over"));
			for (const FAngler* A : Standings())
			{
				UE_LOG(LogFishing, Log, TEXT("Fishing: %s %d (%d catches, %d junk, %d lost, biggest %s)"), *A->Name, A->Score, A->Catches, A->JunkCount, A->Losses, *FormatWeight(A->BiggestG));
			}
		}
		break;
	case EFishingStage::Finished:
		if (GetStageAge() >= ShowcaseSeconds)
		{
			ArcadeCommitResults();
		}
		break;
	}
	UpdateAnglers(Dt);
	UpdateCamera(Dt);
}

void AFishingGame::UpdateLine(int32 i, const FVector& From, const FVector& To, double Sag, bool bShow)
{
	FVector Prev = From;
	for (int32 k = 0; k < LineSegments; ++k)
	{
		UStaticMeshComponent* S = LinePieces[i * LineSegments + k];
		if (!bShow)
		{
			S->SetVisibility(false);
			continue;
		}
		const double U = static_cast<double>(k + 1) / LineSegments;
		FVector P = FMath::Lerp(From, To, U) - FVector(0.0, 0.0, Sag * 4.0 * U * (1.0 - U));
		// The line lies on the water rather than through it, until it goes in near the end.
		if (U < 0.92)
		{
			P.Z = FMath::Max(P.Z, 0.6);
		}
		const FVector D = P - Prev;
		const double Len = D.Size();
		if (Len < 0.01)
		{
			S->SetVisibility(false);
			continue;
		}
		S->SetWorldTransform(FTransform(D.Rotation(), Prev, FVector(Len / 100.0, 0.42, 0.42)));
		S->SetVisibility(true);
		Prev = P;
	}
}

void AFishingGame::UpdateRod(int32 i, const FVector& Base, const FRotator& Face, double Pitch, double Yaw, double Bend, const FVector& Toward, bool bShow)
{
	FLane& Lane = Lanes[i];
	const FVector D0 = (Face + FRotator(Pitch, Yaw, 0.0)).Vector();
	UStaticMeshComponent* H = Handles[i];
	H->SetVisibility(bShow);
	H->SetWorldLocationAndRotation(Base - D0 * 9.0, D0.Rotation());
	// The blank bends toward the line, more at the tip; Bend is the total curve in degrees.
	FVector Axis = FVector::CrossProduct(D0, (Toward - Base).GetSafeNormal());
	if (Axis.SizeSquared() < 1e-6)
	{
		Axis = FVector::CrossProduct(D0, -FVector::UpVector);
	}
	Axis.Normalize();
	FVector P = Base + D0 * 31.0;
	for (int32 k = 0; k < RodSegments; ++k)
	{
		const double F = FMath::Pow(static_cast<double>(k + 1) / RodSegments, 1.7);
		const FVector Dk = FQuat(Axis, FMath::DegreesToRadians(Bend * F)).RotateVector(D0);
		const double R = FMath::Lerp(1.05, 0.38, static_cast<double>(k) / RodSegments);
		UStaticMeshComponent* S = RodPieces[i * RodSegments + k];
		S->SetVisibility(bShow);
		S->SetWorldTransform(FTransform(FRotationMatrix::MakeFromXZ(Dk, FVector::UpVector).ToQuat(), P, FVector(SegmentCm / 100.0, R, R)));
		P += Dk * SegmentCm;
	}
	Lane.Tip = P;
}

void AFishingGame::UpdateAnglers(float Dt)
{
	const int32 N = LaneCount();
	const bool bFinished = Stage == EFishingStage::Finished;
	for (int32 i = 0; i < N; ++i)
	{
		FLane& Lane = Lanes[i];
		const FAngler& A = Match.Anglers[i];
		UArcadeAvatarComponent* Av = Avatars[i];
		const FVector Spot = AFishingLake::AnglerSpot(i, N);
		const double InState = Clock - Lane.ShownAt;
		const FSpecies* Sp = A.State == EAnglerState::Fighting || A.State == EAnglerState::Landed ? &Species()[A.Fight.Species] : nullptr;
		const bool bJunk = Sp && Sp->Kind == EKind::Junk;
		// Landed: turn round to show the camera, then back. At the end everyone turns round.
		const bool bShowing = (A.State == EAnglerState::Landed && InState > 0.35 && InState < LandedHoldMs / 1000.0 - 0.35 && !bJunk) || bFinished;
		const double WantYaw = bShowing ? (162.0 + (i - (N - 1) / 2.0) * 6.0) : 0.0;
		Lane.TurnYaw += (WantYaw - Lane.TurnYaw) * (1.0 - FMath::Exp(-7.0 * Dt));
		Av->SetWorldLocationAndRotation(Spot, Facing(Lane.TurnYaw));
		FArcadePoseInput& Pose = Av->Pose();
		Pose = FArcadePoseInput();
		Pose.Idle = 1.f;
		Pose.LookWeight = 0.7f;

		// Where the line goes, and the rod's pitch and bend for the moment.
		const double Fresh = InState;
		double Pitch = 38.0 + 2.0 * FMath::Sin(Clock * 1.3 + i);
		double Bend = 6.0;
		double Sag = 0.0;
		bool bLine = true, bBobber = true, bRod = true;
		FVector End = FVector::ZeroVector;
		FVector Bob = FVector::ZeroVector;
		const FVector Out = AFishingLake::WaterPoint(i, N, A.State == EAnglerState::Fighting ? A.Fight.DistanceM : A.LureM, A.State == EAnglerState::Fighting ? A.Fight.Lateral : 0.0);
		// Hands (shoulder-relative, arm lengths): the right on the handle, the left on the reel.
		FVector Right(0.55, -0.08, -0.18), Left(0.48, 0.18, -0.32);
		const double Charge = Lane.bHolding ? Lane.Charge : (Clock - Lane.WindupAt < 0.4 ? Smooth(0.0, 0.15, Clock - Lane.WindupAt) * Smooth(0.4, 0.25, Clock - Lane.WindupAt) * 0.8 : 0.0);
		// The bobber's float: a spring on the swell.
		double Rest = 1.2 * FMath::Sin(Clock * 1.3 + i * 1.7);
		switch (A.State)
		{
		case EAnglerState::Idle:
		case EAnglerState::Lost:
		{
			Pitch = 40.0 + 92.0 * Charge;
			Bend = -8.0 * Charge;
			Right = FVector(0.5 - 0.35 * Charge, -0.05, -0.15 + 0.65 * Charge);
			Left = FVector(0.45 - 0.1 * Charge, 0.2, -0.3 + 0.35 * Charge);
			Pose.Lean = static_cast<float>(-0.12 * Charge);
			Pose.Twist = static_cast<float>(0.3 * Charge);
			End = Lane.Tip - FVector(0.0, 0.0, 55.0) + FVector(3.0 * FMath::Sin(Clock * 1.7 + i), 0.0, 0.0);
			Bob = End - FVector(0.0, 0.0, 12.0);
			Lane.BobZ = 0.0;
			Lane.BobV = 0.0;
			if (A.State == EAnglerState::Lost)
			{
				Pose.Slump = static_cast<float>(Smooth(0.0, 0.2, Fresh) * Smooth(LostHoldMs / 1000.0, LostHoldMs / 1000.0 - 0.4, Fresh)) * 0.85f;
				if (A.LastLoss == ELoss::Snap)
				{
					// Snapped: the line whips back, then a fresh float is tied on.
					bLine = Fresh > 1.0;
					bBobber = Fresh > 1.0;
					Bend = -18.0 * Smooth(0.25, 0.0, Fresh);
				}
			}
			break;
		}
		case EAnglerState::Casting:
		{
			// The whip forward (a quarter second), then the rod follows the line out.
			const double T = FMath::Clamp(A.StateMs / FMath::Max(1.0, A.FlightMs), 0.0, 1.0);
			const double Whip = EaseOut(Fresh / 0.22);
			Pitch = FMath::Lerp(130.0, 14.0, Whip);
			Bend = -26.0 * FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(Fresh / 0.3, 0.0, 1.0));
			Right = FVector(FMath::Lerp(0.15, 0.78, Whip), -0.02, FMath::Lerp(0.5, 0.05, Whip));
			Left = FVector(0.5, 0.15, -0.25);
			Pose.Lean = static_cast<float>(0.22 * Whip);
			const FVector Land = AFishingLake::WaterPoint(i, N, A.CastM, 0.0);
			const FVector From = Lane.CastFrom.IsNearlyZero() ? Lane.Tip : Lane.CastFrom;
			Bob = FMath::Lerp(From, Land, T) + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * T) * (140.0 + 360.0 * A.CastPower));
			End = Bob;
			Sag = 10.0;
			break;
		}
		case EAnglerState::Waiting:
		case EAnglerState::Bite:
		{
			const bool bBite = A.State == EAnglerState::Bite;
			Pitch = (bBite ? 17.0 : 24.0) + 2.0 * FMath::Sin(Clock * 0.9 + i);
			Bend = bBite ? 14.0 + 4.0 * FMath::Sin(Clock * 30.0) : 7.0;
			Right = FVector(0.6, -0.05, -0.12);
			Left = FVector(0.45, 0.18, -0.3);
			Pose.Lean = bBite ? 0.16f : 0.05f;
			Pose.Crouch = bBite ? 0.22f : 0.08f;
			if (bBite)
			{
				Rest = -15.0 + 3.0 * FMath::Sin(Clock * 22.0);
			}
			Bob = Out;
			End = Out;
			Sag = 18.0 + FVector::Dist(Lane.Tip, Out) * 0.06;
			break;
		}
		case EAnglerState::Fighting:
		{
			const FFight& F = A.Fight;
			const double T = FMath::Clamp(F.Tension, 0.0, 1.3);
			const double Jerk = Smooth(0.0, 0.08, Clock - Lane.HookedAt) * Smooth(0.35, 0.15, Clock - Lane.HookedAt);
			Pitch = 52.0 + 18.0 * T + 25.0 * Jerk;
			Bend = 10.0 + 48.0 * FMath::Min(1.1, T);
			Lane.ReelPhase += A.Reel * 1.6 * 2.0 * UE_DOUBLE_PI * Dt;
			Right = FVector(0.48, -0.04, 0.12 + 0.22 * T + 0.35 * Jerk);
			Left = FVector(0.4, 0.2, -0.05) + FVector(0.0, FMath::Cos(Lane.ReelPhase), FMath::Sin(Lane.ReelPhase)) * 0.11;
			Pose.Lean = static_cast<float>(-0.08 - 0.16 * T);
			Pose.Crouch = static_cast<float>(0.22 + 0.25 * T);
			Pose.Twist = static_cast<float>(-F.Lateral * 0.25);
			const double Near = Smooth(4.0, 1.2, F.DistanceM);
			End = Out + FVector(0.0, 0.0, -12.0 + 10.0 * Near);
			Bob = Out + FVector(-10.0, 6.0 * FMath::Sin(Clock * 9.0), 0.0);
			Rest = -6.0 + 2.0 * FMath::Sin(Clock * 11.0);
			Sag = FMath::Max(0.0, 1.0 - T / 0.75) * FVector::Dist(Lane.Tip, End) * 0.14;
			if (Lane.Kind == EAnglerKind::Local || Lane.Kind == EAnglerKind::Phone || Lane.Kind == EAnglerKind::Bot)
			{
				if (A.Reel > 0.1)
				{
					Cues.Play(this, TEXT("fishing.reel"), 0.35f + 0.25f * static_cast<float>(FMath::Min(1.0, A.Reel)), 0.9f + 0.5f * static_cast<float>(FMath::Min(1.0, A.Reel)));
				}
			}
			// Thrashing at the surface once it's close.
			if (F.DistanceM < 3.5)
			{
				Lane.ThrashCarry += Dt * 3.0;
				if (Lane.ThrashCarry >= 1.0)
				{
					Lane.ThrashCarry -= 1.0;
					Splash(Out, 0.25 + 0.3 * F.Stamina, false);
				}
			}
			break;
		}
		case EAnglerState::Landed:
		{
			bLine = Fresh < 0.5;
			bBobber = false;
			bRod = true;
			Pitch = 70.0;
			Bend = 4.0;
			End = HoldPoint(i);
			const double Hold = Smooth(0.25, 0.6, Fresh) * Smooth(LandedHoldMs / 1000.0, LandedHoldMs / 1000.0 - 0.3, Fresh);
			if (bJunk)
			{
				Right = FVector(0.85, 0.05, 0.15);
				Pose.Slump = static_cast<float>(0.55 * Hold);
			}
			else
			{
				Right = FVector(0.55, 0.28, 0.32);
				Left = FVector(0.55, -0.28, 0.32);
				Pose.Cheer = static_cast<float>(0.22 * Hold);
				Pose.Lean = -0.05f;
			}
			break;
		}
		}
		if (bFinished)
		{
			const bool bWin = Match.WinnerIds.Contains(A.PlayerId);
			bLine = false;
			bBobber = false;
			if (bWin || A.BiggestSpecies < 0)
			{
				Pose.Cheer = bWin ? 0.85f : 0.f;
				Pose.Slump = bWin ? 0.f : 0.35f;
			}
			else
			{
				Right = FVector(0.55, 0.28, 0.3);
				Left = FVector(0.55, -0.28, 0.3);
			}
		}
		Pose.Hand[1] = Right;
		Pose.Hand[0] = Left;
		const bool bHandsFree = Pose.Cheer > 0.3f;
		Pose.HandWeight[0] = Pose.HandWeight[1] = bHandsFree ? 0.f : 1.f;
		// Look at the float (or the camera when showing off).
		const FVector LookAt = bShowing ? (Camera ? Camera->GetActorLocation() : Spot - FVector(500.0, 0.0, -150.0)) : (A.IsLineOut() ? Out : Spot + FVector(300.0, 0.0, 40.0));
		Pose.Look = Av->GetComponentTransform().InverseTransformVectorNoScale(LookAt - Av->GetComponentLocation() - FVector(0.0, 0.0, 165.0)).GetSafeNormal();
		const double ReactAge = Clock - Lane.ReactionAt;
		if (Lane.Reaction == EEffectKind::Record && ReactAge < 2.5)
		{
			Pose.Cheer = FMath::Max(Pose.Cheer, static_cast<float>(0.6 * Smooth(1.2, 1.6, ReactAge) * Smooth(2.5, 2.2, ReactAge)));
		}

		// The rod: in the right hand, pitched in the facing frame toward the line, bending with the load.
		Lane.RodPitch += (Pitch - Lane.RodPitch) * (1.0 - FMath::Exp(-(A.State == EAnglerState::Casting ? 30.0 : 12.0) * Dt));
		Lane.RodBend += (Bend - Lane.RodBend) * (1.0 - FMath::Exp(-14.0 * Dt));
		const FVector Hand = Av->BoneWorld(TEXT("rightHand"));
		const double AimYaw = A.IsLineOut() ? FMath::RadiansToDegrees(FMath::Atan2(Out.Y - Spot.Y, FMath::Max(50.0, Out.X - Spot.X))) * 0.6 : 0.0;
		const bool bRodDown = bShowing && !bJunk;
		if (bRodDown)
		{
			// Showing a catch: the rod leans on the bucket.
			const FVector Bucket = AFishingLake::BucketSpot(i, N);
			UpdateRod(i, Bucket + FVector(30.0, 10.0, 10.0), FRotator::ZeroRotator, 62.0, -20.0, 3.0, Bucket + FVector(0.0, 0.0, 400.0), bRod);
		}
		else
		{
			UpdateRod(i, Hand.IsNearlyZero() ? Spot + FVector(40.0, 0.0, 110.0) : Hand, Facing(Lane.TurnYaw), Lane.RodPitch, AimYaw, Lane.RodBend, End.IsNearlyZero() ? Lane.Tip + FVector(0.0, 0.0, -100.0) : End, bRod);
		}
		if (A.State == EAnglerState::Idle || A.State == EAnglerState::Lost || bRodDown)
		{
			End = Lane.Tip - FVector(0.0, 0.0, 55.0);
			Bob = End - FVector(0.0, 0.0, 12.0);
		}
		UpdateLine(i, Lane.Tip, End, Sag, bLine && !bRodDown && !End.IsNearlyZero());

		// The bobber: in the air, hanging, or floating on its spring.
		UStaticMeshComponent* B = Bobbers[i];
		const bool bFloating = A.State == EAnglerState::Waiting || A.State == EAnglerState::Bite || A.State == EAnglerState::Fighting;
		if (bFloating)
		{
			const double Acc = -120.0 * (Lane.BobZ - Rest) - 9.0 * Lane.BobV;
			Lane.BobV += Acc * Dt;
			Lane.BobZ += Lane.BobV * Dt;
			Bob.Z = Lane.BobZ;
		}
		B->SetVisibility(bBobber && !bRodDown && !Bob.IsNearlyZero());
		const FRotator Tilt = bFloating ? FRotator(4.0 * FMath::Sin(Clock * 1.7 + i) + (A.State == EAnglerState::Bite ? 18.0 : 0.0), 0.0, 5.0 * FMath::Sin(Clock * 1.3 + i)) : FRotator::ZeroRotator;
		B->SetWorldLocationAndRotation(Bob, Tilt);

		// The fish: near the surface as it comes in, leaping on its runs, then held up and put away.
		bool bFish = false;
		if (bFinished && A.BiggestSpecies >= 0 && !Match.WinnerIds.Contains(A.PlayerId))
		{
			ShowFish(i, A.BiggestSpecies, A.BiggestG, FTransform(Facing(Lane.TurnYaw + 90.0) + FRotator(0.0, 0.0, 6.0 * FMath::Sin(Clock * 5.0 + i)), HoldPoint(i)));
			bFish = true;
		}
		else if (bFinished && A.BiggestSpecies >= 0)
		{
			// The winner holds their best catch high.
			ShowFish(i, A.BiggestSpecies, A.BiggestG, FTransform(Facing(Lane.TurnYaw + 90.0) + FRotator(70.0, 0.0, 0.0), Av->BoneWorld(TEXT("leftHand")) + FVector(0.0, 0.0, 12.0)));
			bFish = true;
		}
		else if (A.State == EAnglerState::Fighting)
		{
			const FFight& F = A.Fight;
			const double JumpAge = Clock - Lane.JumpAt;
			const double JumpLen = 0.8;
			if (JumpAge >= 0.0 && JumpAge < JumpLen)
			{
				const double U = JumpAge / JumpLen;
				const double Height = 50.0 + 60.0 * (Sp ? Sp->Strength : 0.5);
				const FVector P = Out + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * U) * Height - 10.0);
				ShowFish(i, F.Species, F.WeightG, FTransform(FRotator(FMath::Lerp(55.0, -55.0, U), 180.0 + F.Lateral * 40.0, 20.0 * FMath::Sin(Clock * 20.0)), P));
				bFish = true;
				if (JumpAge + Dt >= JumpLen * 0.95 && JumpAge < JumpLen * 0.95)
				{
					Splash(Out, 0.8);
				}
				if (JumpAge < Dt)
				{
					Splash(Out, 0.7);
				}
			}
			else if (F.DistanceM < 5.0)
			{
				const double Depth = -32.0 + 18.0 * Smooth(5.0, 1.2, F.DistanceM);
				ShowFish(i, F.Species, F.WeightG, FTransform(FRotator(0.0, 180.0 * 0.0 + F.Lateral * 35.0 + 14.0 * FMath::Sin(Clock * (10.0 + 8.0 * F.Stamina)), 0.0), Out + FVector(25.0, 0.0, Depth)));
				bFish = true;
			}
		}
		else if (A.State == EAnglerState::Landed)
		{
			const FCatch& C = A.LastCatch;
			const double T = Fresh;
			const FVector From = AFishingLake::WaterPoint(i, N, LandM, 0.0) + FVector(0.0, 0.0, -10.0);
			const FVector HoldAt = bJunk ? Av->BoneWorld(TEXT("rightHand")) + FVector(0.0, 0.0, -18.0) : HoldPoint(i);
			const double Lift = EaseOut(T / 0.5);
			const double TossT = FMath::Clamp((T - (LandedHoldMs / 1000.0 - 0.3)) / 0.3, 0.0, 1.0);
			const FVector TossTo = bJunk ? AFishingLake::WaterPoint(i, N, 3.5, 0.6) : AFishingLake::BucketSpot(i, N) + FVector(0.0, 0.0, 28.0);
			FVector P = FMath::Lerp(From, HoldAt, Lift) + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * FMath::Min(1.0, T / 0.5)) * 60.0);
			if (TossT > 0.0)
			{
				P = FMath::Lerp(HoldAt, TossTo, TossT) + FVector(0.0, 0.0, FMath::Sin(UE_DOUBLE_PI * TossT) * 70.0);
			}
			const double Wag = 14.0 * FMath::Sin(Clock * 16.0) * (1.0 - 0.6 * Lift);
			const FRotator Hang = bJunk ? FRotator(-60.0 + Wag * 0.3, Lane.TurnYaw, 0.0) : Facing(Lane.TurnYaw + 90.0) + FRotator(0.0, Wag, 0.0);
			ShowFish(i, C.Species, C.WeightG, FTransform(FMath::Lerp(FRotator(-70.0, Lane.TurnYaw, 0.0), Hang, Lift), P));
			bFish = TossT < 1.0;
			if (TossT > 0.0 && TossT < 1.0 && T + Dt >= LandedHoldMs / 1000.0 - Dt)
			{
				Splash(TossTo, bJunk ? 0.6 : 0.15, bJunk);
			}
		}
		Fish[i]->SetVisibility(bFish);
	}
}

void AFishingGame::UpdateCamera(float Dt)
{
	const double Aspect = [this]()
	{
		FVector2D Size(16.0, 9.0);
		if (const UGameViewportClient* V = GetWorld()->GetGameViewport())
		{
			V->GetViewportSize(Size);
		}
		return Size.Y > 0.0 ? Size.X / Size.Y : 16.0 / 9.0;
	}();
	const int32 N = FMath::Max(1, LaneCount());
	const double VFov = 40.0;
	const double HalfW = (N - 1) / 2.0 * AFishingLake::LaneSpacing + 190.0;
	const double TanH = FMath::Tan(FMath::DegreesToRadians(VFov / 2.0)) * Aspect;
	const double Back = FMath::Max(560.0, HalfW / (TanH * 0.88));
	const FVector Anglers(AFishingLake::EdgeX - 62.0, 0.0, AFishingLake::DeckZ + 120.0);
	FVector Pos = Anglers + FVector(-Back, 0.0, 120.0 + Back * 0.22);
	FVector Look = FVector(AFishingLake::EdgeX + 1500.0, 0.0, 20.0);
	double Fov = VFov;
	if (Stage == EFishingStage::Intro)
	{
		// A swing round from out on the lake (the anglers' faces, the sunset behind us) to behind the dock.
		const double T = Smooth(0.0, 1.0, GetStageAge() / (IntroSeconds - 0.4));
		const double Angle = FMath::DegreesToRadians(FMath::Lerp(150.0, 0.0, T));
		const double R = Back + 120.0;
		Pos = Anglers + FVector(-FMath::Cos(Angle) * R, FMath::Sin(Angle) * R * 0.85, FMath::Lerp(60.0, 120.0 + Back * 0.22, T));
		Look = FMath::Lerp(Anglers + FVector(0.0, 0.0, -10.0), Look, Smooth(0.35, 1.0, T));
		Fov = FMath::Lerp(46.0, VFov, T);
	}
	else if (Stage == EFishingStage::Finished)
	{
		// The anglers turn round: come in low and look at them with the lake and the sun behind.
		const double T = Smooth(0.0, 1.6, GetStageAge());
		const double Drift = FMath::Sin(GetStageAge() * 0.25) * 60.0;
		Pos = FMath::Lerp(Pos, Anglers + FVector(-Back * 0.95, Drift, 30.0), T);
		Look = FMath::Lerp(Look, Anglers + FVector(400.0, 0.0, 0.0), T);
	}
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-6.0 * FMath::Min<double>(Dt, 1.0 / 20.0));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * K;
	CamFov += (Fov - CamFov) * K;
	Shake *= FMath::Exp(-8.0 * Dt);
	const FVector Jolt(0.0, FMath::Sin(Clock * 61.0) * Shake * 6.0, FMath::Sin(Clock * 53.0 + 1.1) * Shake * 5.0);
	const FVector From = CamPos + Jolt;
	Camera->SetActorLocationAndRotation(From, (CamLook - From).Rotation());
	const double HFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HFov));
}

void AFishingGame::Rematch()
{
	TArray<FSeed> MatchSeeds;
	for (const FFishingSeat& Seat : Seats)
	{
		MatchSeeds.Add({ Seat.Id, Seat.Name });
	}
	Seed = Seed * 747796405u + 2891336453u;
	Match = CreateMatch(MatchSeeds, Seed);
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane Fresh;
		Fresh.PlayerId = Lanes[i].PlayerId;
		Fresh.Kind = Lanes[i].Kind;
		Fresh.NextActAt = 400.0 + 500.0 * i;
		Lanes[i] = Fresh;
	}
	SeenEffects.Reset();
	Accumulator = 0.0;
	bResultsCommitted = false;
	bCamPlaced = false;
	SetStage(EFishingStage::Intro);
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
	if (OnRematch) OnRematch(); else Rematch();
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
