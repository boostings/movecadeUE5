#include "Games/Stacker/StackerGame.h"

#include "Algo/StableSort.h"
#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Stacker/SStackerHud.h"
#include "Games/Stacker/StackerRooftop.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "KartCoords.h"
#include "Net/RoomProtocol.h"
#include "Store/KartCosmetics.h" // store cosmetics: block skin

DEFINE_LOG_CATEGORY_STATIC(LogStacker, Log, All);

using namespace MovecadeKart::Stacker;
namespace Fx = MovecadeKart::Fx;

namespace
{
	constexpr double IntroSeconds = 3.4;
	/** S1: the rules step on a fixed 10 ms clock (the balance's own substep), so every machine agrees. */
	constexpr double StepMs = 10.0;
	constexpr double RowHeight = 0.5;
	constexpr double CompareRowHeight = 0.15;
	constexpr int32 WorkRows = 4;
	constexpr int32 VisibleRows = 16;
	constexpr int32 PoolPerLane = MaxTowerHeight + 1;
	constexpr double FloorY = -0.7;
	constexpr double BuilderZ = 2.2;
	constexpr double BuilderSide = 2.6;
	constexpr double CompareHoldSeconds = 8.0;
	/** The block: a rounded box 1 x .43 x 1.5 (web), radius .11. */
	const FVector BlockSize(1.0, 0.43, 1.5);
	constexpr double ArmLength = 0.525;

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** One axis of the rounded box: the flat run and the quarter arcs at each end, as coordinates. */
	TArray<double> RoundedAxis(double Half, double Radius, int32 ArcSteps)
	{
		TArray<double> Out;
		for (int32 i = 0; i <= ArcSteps; ++i)
		{
			Out.Add(-(Half - Radius) - Radius * FMath::Cos(HALF_PI * i / ArcSteps));
		}
		for (int32 i = ArcSteps; i >= 0; --i)
		{
			Out.Add((Half - Radius) + Radius * FMath::Cos(HALF_PI * i / ArcSteps));
		}
		return Out;
	}

	/**
	 * blockSurface.ts as geometry: a rounded panel (UE cm, X deep, Y wide, Z tall) with a border and ribs pressed
	 * into its two broad faces in a darker trim. Slot 0 the panel (the lane's paint), slot 1 the trim.
	 */
	UStaticMesh* BuildBlockMesh(UObject* Outer)
	{
		using MovecadeKart::FMeshBuilder;
		FMeshBuilder B;
		const FVector H(BlockSize.Z * 50.0, BlockSize.X * 50.0, BlockSize.Y * 50.0);
		const double R = 11.0;
		const int32 Panel = B.Section(TEXT("Panel"));
		const int32 Trim = B.Section(TEXT("Trim"));
		const TArray<double> Axes[3] = { RoundedAxis(H.X, R, 4), RoundedAxis(H.Y, R, 4), RoundedAxis(H.Z, R, 4) };
		const FVector Inner = H - FVector(R);
		auto Round = [&Inner, R](const FVector& P, FVector& N)
		{
			const FVector Q(FMath::Clamp(P.X, -Inner.X, Inner.X), FMath::Clamp(P.Y, -Inner.Y, Inner.Y), FMath::Clamp(P.Z, -Inner.Z, Inner.Z));
			const FVector D = P - Q;
			N = D.GetSafeNormal();
			return Q + N * R;
		};
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const int32 U = (Axis + 1) % 3, V = (Axis + 2) % 3;
			for (const double Side : { -1.0, 1.0 })
			{
				const TArray<double>& Us = Axes[U];
				const TArray<double>& Vs = Axes[V];
				TArray<int32> Idx;
				for (int32 j = 0; j < Vs.Num(); ++j)
				{
					for (int32 i = 0; i < Us.Num(); ++i)
					{
						FVector P;
						P[Axis] = Side * H[Axis];
						P[U] = Us[i];
						P[V] = Vs[j];
						FVector N;
						const FVector Out = Round(P, N);
						Idx.Add(B.Vert(Panel, Out, N, FVector2D(Us[i] / (2.0 * H[U]) + 0.5, Vs[j] / (2.0 * H[V]) + 0.5), FVector2D::ZeroVector));
					}
				}
				for (int32 j = 0; j + 1 < Vs.Num(); ++j)
				{
					for (int32 i = 0; i + 1 < Us.Num(); ++i)
					{
						const int32 A = Idx[j * Us.Num() + i], Bb = Idx[j * Us.Num() + i + 1], C = Idx[(j + 1) * Us.Num() + i + 1], D = Idx[(j + 1) * Us.Num() + i];
						B.Tri(Panel, A, Bb, C);
						B.Tri(Panel, A, C, D);
					}
				}
			}
		}
		// The trim on the broad faces (toward and away from the camera): a frame and the ribs, standing proud.
		for (const double Side : { -1.0, 1.0 })
		{
			const double X = Side * (H.X + 0.5);
			const double FlatY = H.Y - R, FlatZ = H.Z - R;
			B.Box(TEXT("Trim"), FVector(X, 0.0, FlatZ - 1.2), FVector(0.8, FlatY, 1.2), FVector2D::ZeroVector);
			B.Box(TEXT("Trim"), FVector(X, 0.0, -FlatZ + 1.2), FVector(0.8, FlatY, 1.2), FVector2D::ZeroVector);
			for (int32 k = 0; k < 7; ++k)
			{
				const double Y = FMath::Lerp(-FlatY + 6.0, FlatY - 6.0, k / 6.0);
				B.Box(TEXT("Trim"), FVector(X, Y, 0.0), FVector(0.6, 1.1, FlatZ - 3.0), FVector2D::ZeroVector);
			}
		}
		(void)Trim;
		return B.Build(Outer, TEXT("StackerBlock"));
	}
}

AStackerGame::AStackerGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(0x57ac);
}

FVector AStackerGame::W(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

double AStackerGame::LaneXOf(int32 LaneIndex) const
{
	return AStackerRooftop::LaneX(LaneIndex, LaneSpan());
}

int32 AStackerGame::LaneSpan() const
{
	int32 Span = 1;
	for (const FTower& T : Match.Towers)
	{
		Span = FMath::Max(Span, T.LaneIndex + 1);
	}
	return FMath::Min(Span, 4);
}

UStaticMeshComponent* AStackerGame::MakeMesh(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows)
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

void AStackerGame::Setup(AStackerRooftop* InRooftop, const TArray<FStackerSeat>& InSeats, APlayerController* InViewer)
{
	Rooftop = InRooftop;
	Seats = InSeats;
	Viewer = InViewer;
	const int32 N = Seats.Num();
	if (Rooftop)
	{
		Rooftop->SetLanes(N);
	}
	TArray<FSeed> MatchSeeds;
	for (int32 i = 0; i < N; ++i)
	{
		MatchSeeds.Add({ Seats[i].Id, Seats[i].Name });
		FLane& L = Lanes.AddDefaulted_GetRef();
		L.PlayerId = Seats[i].Id;
		L.Kind = Seats[i].Kind;
		Reactions.Add(TPair<EEffectKind, double>(EEffectKind::Land, -10.0));
	}
	Match = CreateMatch(MatchSeeds);
	for (int32 i = 0; i < N; ++i)
	{
		Lanes[i].LaneX = LaneXOf(Match.Towers[i].LaneIndex);
		// S3: a phone that hasn't sent a frame yet is missing (frozen, can't drop), not balancing on roll 0.
		if (Lanes[i].Kind == EBuilderKind::Phone)
		{
			Match.Towers[i].Balance.LastInputMs = -BalanceStaleMs - 1.0;
		}
	}

	using namespace MovecadeKart::Art;
	BlockMesh = BuildBlockMesh(this);
	UMaterialInterface* TrimMat = Plain(this, Hex(TEXT("#2a2c33")), 0.5, 0.6);
	UMaterialInterface* Dim = Plain(this, Hex(TEXT("#4f5566")), 0.6, 0.2);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* LiftMat = Plain(this, Hex(TEXT("#d39a32")), 0.5, 0.3);
	UMaterialInterface* PistonMat = Plain(this, Hex(TEXT("#c9ccd2")), 0.25, 0.95);
	for (int32 i = 0; i < N; ++i)
	{
		// Store cosmetics: a local builder's equipped block colour.
		const FLinearColor C = FKartCosmetics::StackerBlockColor(Lanes[i].Kind == EBuilderKind::Local, AStackerRooftop::LaneColor(Match.Towers[i].LaneIndex));
		LaneMaterials.Add(Plain(this, C * 0.95f, 0.42, 0.25));
		LaneMaterials.Add(Plain(this, C * 0.74f, 0.45, 0.25));
		LaneMaterials.Add(Plain(this, FMath::Lerp(C, FLinearColor::White, 0.22f), 0.35, 0.25, 0.18));
		LaneMaterials.Add(Plain(this, C, 0.35, 0.25, 0.3));
		LaneMaterials.Add(Dim);
		auto Dress = [this, TrimMat](UStaticMeshComponent* M, UMaterialInterface* Paint)
		{
			M->SetMaterial(0, Paint);
			M->SetMaterial(1, TrimMat);
		};
		for (int32 k = 0; k < PoolPerLane; ++k)
		{
			UStaticMeshComponent* M = MakeMesh(TEXT("Block"), BlockMesh);
			Dress(M, LaneMaterials[i * 5]);
			BlockPool.Add(M);
		}
		UStaticMeshComponent* Cursor = MakeMesh(TEXT("Cursor"), BlockMesh);
		Dress(Cursor, LaneMaterials[i * 5 + 3]);
		Cursors.Add(Cursor);
		UStaticMeshComponent* Held = MakeMesh(TEXT("Held"), BlockMesh);
		Dress(Held, LaneMaterials[i * 5 + 3]);
		HeldBlocks.Add(Held);
		UStaticMeshComponent* Lift = MakeMesh(TEXT("Lift"), Cube);
		Lift->SetMaterial(0, LiftMat);
		Lift->SetVisibility(true);
		Lifts.Add(Lift);
		UStaticMeshComponent* Piston = MakeMesh(TEXT("Piston"), Cylinder);
		Piston->SetMaterial(0, PistonMat);
		Piston->SetVisibility(true);
		Pistons.Add(Piston);
		UArcadeAvatarComponent* A = NewObject<UArcadeAvatarComponent>(this, *FString::Printf(TEXT("Builder%d"), i));
		A->AvatarScale = N >= 4 ? 0.78f : 0.84f;
		A->SetupAttachment(RootComponent);
		A->RegisterComponent();
		A->SetLook(Seats[i].Look);
		Avatars.Add(A);
	}
	for (int32 i = 0; i < MaxShards; ++i)
	{
		UStaticMeshComponent* M = MakeMesh(TEXT("Shard"), BlockMesh);
		M->SetMaterial(1, TrimMat);
		ShardPool.Add(M);
	}

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SStackerHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}
	SetStage(EStackerStage::Intro);
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

bool AStackerGame::ScreenOf(const FVector& World, FVector2D& Out01) const
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

double AStackerGame::TopY(const FTower& Tower) const
{
	const int32 i = Match.Towers.IndexOfByPredicate([&Tower](const FTower& T) { return T.PlayerId == Tower.PlayerId; });
	return (Tower.Top().Index - ScrollOf(i)) * RowHeight + RowHeight;
}

TArray<const FTower*> AStackerGame::Standings() const
{
	TArray<const FTower*> Order;
	for (const FTower& T : Match.Towers)
	{
		Order.Add(&T);
	}
	Algo::StableSort(Order, [](const FTower* A, const FTower* B) { return A->Score != B->Score ? A->Score > B->Score : A->Height() > B->Height(); });
	return Order;
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
		Cues.Play(this, TEXT("match.over"));
	}
}

void AStackerGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	const int32 i = Lanes.IndexOfByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; });
	if (i == INDEX_NONE)
	{
		return;
	}
	FLane& Lane = Lanes[i];
	// runtime.ts 501-508: the palm's normal from the frame's quaternion; roll across the lane, pitch toward the city.
	if (Frame.bHasQuaternion)
	{
		const double X = Frame.Quaternion.X, Y = Frame.Quaternion.Y, Z = Frame.Quaternion.Z, Wq = Frame.Quaternion.W;
		const double Nx = 2.0 * (X * Z + Wq * Y);
		const double Ny = 2.0 * (Y * Z - Wq * X);
		const double Nz = 1.0 - 2.0 * (X * X + Y * Y);
		Lane.PhoneRoll = FMath::RadiansToDegrees(FMath::Atan2(Nx, Nz));
		Lane.PhonePitch = FMath::RadiansToDegrees(FMath::Atan2(Ny, Nz));
	}
	else
	{
		Lane.PhoneRoll = Frame.Gamma;
		Lane.PhonePitch = Frame.Beta;
	}
	FTower& Tower = Match.Towers[i];
	Tower.Balance.Roll = Lane.PhoneRoll;
	Tower.Balance.Pitch = Lane.PhonePitch;
	Tower.Balance.LastInputMs = Match.ElapsedMs;
}

void AStackerGame::Drop(const FString& PlayerId)
{
	if (Stage != EStackerStage::Match || bPaused)
	{
		return;
	}
	if (DropBlock(Match, PlayerId).IsSet())
	{
		for (const FEffect& E : Match.Effects)
		{
			if (!SeenEffects.Contains(E.Id))
			{
				SeenEffects.Add(E.Id);
				PresentEffect(E);
				if (OnEffect) OnEffect(E);
			}
		}
	}
}

void AStackerGame::SteerLocal(FLane& Lane, FTower& Tower, float Dt)
{
	// The palm: the left stick or A/D and W/S hold a tilt, the mouse nudges it and leaves it there.
	float StickX = 0.f, StickY = 0.f;
	if (Viewer)
	{
		float MouseX = 0.f, MouseY = 0.f;
		Viewer->GetInputMouseDelta(MouseX, MouseY);
		Lane.MouseRoll = FMath::Clamp(Lane.MouseRoll + MouseX * 0.12, -30.0, 30.0);
		Lane.MousePitch = FMath::Clamp(Lane.MousePitch - MouseY * 0.12, -30.0, 30.0);
		StickX = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightX);
		StickY = Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftY) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightY);
		StickX += (Viewer->IsInputKeyDown(EKeys::D) || Viewer->IsInputKeyDown(EKeys::Right) ? 1.f : 0.f) - (Viewer->IsInputKeyDown(EKeys::A) || Viewer->IsInputKeyDown(EKeys::Left) ? 1.f : 0.f);
		StickY += (Viewer->IsInputKeyDown(EKeys::W) || Viewer->IsInputKeyDown(EKeys::Up) ? 1.f : 0.f) - (Viewer->IsInputKeyDown(EKeys::S) || Viewer->IsInputKeyDown(EKeys::Down) ? 1.f : 0.f);
		if (Viewer->WasInputKeyJustPressed(EKeys::SpaceBar) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom) || Viewer->WasInputKeyJustPressed(EKeys::LeftMouseButton)
			|| Viewer->WasInputKeyJustPressed(EKeys::Enter))
		{
			Lane.bDropQueued = true;
		}
	}
	// Leaning the palm is "roll" on the phone: a stick to the right tips the tower right.
	Tower.Balance.Roll = FMath::Clamp(FMath::Clamp(StickX, -1.f, 1.f) * 30.0 + Lane.MouseRoll, -35.0, 35.0);
	Tower.Balance.Pitch = FMath::Clamp(FMath::Clamp(StickY, -1.f, 1.f) * 30.0 + Lane.MousePitch, -35.0, 35.0);
	(void)Dt;
}

void AStackerGame::SteerBot(FLane& Lane, FTower& Tower, const FStackerSeat& Seat)
{
	if (Tower.IsDone() || Match.Phase != EMatchPhase::Playing)
	{
		Tower.Balance.LastInputMs = Match.ElapsedMs;
		return;
	}
	const double S = Seat.Skill;
	const int32 H = Tower.Height();
	// The palm: counter the wind with a hand that wanders (pacing.test.ts plays the same way).
	const FVector2D Wind = WindAt(Match.ElapsedMs, H);
	const double Wander = (1.0 - S) * 10.0 + 1.5;
	Lane.PalmError = FMath::Clamp(Lane.PalmError + (BotRng.FRand() - 0.5) * Wander * 0.08, -Wander, Wander);
	Tower.Balance.Roll = -Wind.X + Lane.PalmError;
	Tower.Balance.Pitch = -Wind.Y + Lane.PalmError * 0.5;
	Tower.Balance.LastInputMs = Match.ElapsedMs;
	// The drop: pick a mark for this row, then let go as the block crosses it.
	if (Lane.AimedRow != H)
	{
		Lane.AimedRow = H;
		const double Spread = 0.25 + 0.9 * (1.0 - S) + 0.1 * FMath::Max(0.0, H - 30.0) / 30.0;
		Lane.AimError = FMath::Clamp(static_cast<double>(BotRng.FRandRange(-1.f, 1.f) + BotRng.FRandRange(-1.f, 1.f)) * 0.5 * Spread, -1.2, 1.2);
		Lane.NextDropAt = Match.ElapsedMs + DropCooldownMs + BotRng.FRandRange(150.f, 900.f) * (1.15 - S);
	}
	if (Match.ElapsedMs < Lane.NextDropAt || Tower.Balance.RecoveryMs > 0.0)
	{
		return;
	}
	const double Support = Tower.Top().Center + BalanceDisplacement(Tower.Balance.LeanX);
	const double Step = SweepSpeed(H) * StepMs / 1000.0;
	if (FMath::Abs(Tower.CursorCenter - (Support + Lane.AimError)) <= Step * 0.75)
	{
		Lane.bDropQueued = true;
	}
}

void AStackerGame::StepFixed()
{
	const float Dt = static_cast<float>(StepMs / 1000.0);
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		FTower& Tower = Match.Towers[i];
		if (Lane.Kind == EBuilderKind::Local)
		{
			Tower.Balance.LastInputMs = Match.ElapsedMs;
		}
		else if (Lane.Kind == EBuilderKind::Bot && Seats.IsValidIndex(i))
		{
			SteerBot(Lane, Tower, Seats[i]);
		}
	}
	StepMatch(Match, StepMs);
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		if (Lanes[i].bDropQueued)
		{
			Lanes[i].bDropQueued = false;
			DropBlock(Match, Lanes[i].PlayerId);
		}
	}
	for (const FEffect& E : Match.Effects)
	{
		if (!SeenEffects.Contains(E.Id))
		{
			SeenEffects.Add(E.Id);
			PresentEffect(E);
			if (OnEffect) OnEffect(E);
		}
	}
	(void)Dt;
}

void AStackerGame::PresentEffect(const FEffect& E)
{
	const int32 i = Match.Towers.IndexOfByPredicate([&E](const FTower& T) { return T.PlayerId == E.PlayerId; });
	if (i == INDEX_NONE)
	{
		return;
	}
	const FTower& Tower = Match.Towers[i];
	Reactions[i] = TPair<EEffectKind, double>(E.Kind, Clock);
	const float P = static_cast<float>(E.Intensity);
	const FLinearColor Color = AStackerRooftop::LaneColor(Tower.LaneIndex);
	const double Y = (E.Row - Lanes[i].Scroll) * RowHeight + RowHeight / 2.0;
	const FVector At = W(FVector(Lanes[i].LaneX + Tower.Top().Center + BalanceDisplacement(Tower.Balance.LeanX), Y, 0.0));
	TArray<Fx::FKartFxEmit> Out;
	double Punch = 0.0;
	switch (E.Kind)
	{
	case EEffectKind::Land:
		Cues.Play(this, TEXT("stacker.drop"), 0.7f + 0.3f * P, 1.f + FMath::Min(24, E.Row) * 0.02f);
		Punch = 0.2 + 0.3 * P;
		break;
	case EEffectKind::Perfect:
	case EEffectKind::Complete:
	{
		Cues.Play(this, TEXT("stacker.drop"), 1.f, 1.f + FMath::Min(24, E.Row) * 0.02f);
		const int32 Step = FMath::Clamp(Tower.Combo - 1, 0, 12);
		Cues.Play(this, TEXT("stacker.perfect"), 0.7f + 0.3f * Step / 12.f, FMath::Pow(2.f, Step / 12.f));
		Punch = E.Kind == EEffectKind::Complete ? 1.2 : 0.45 + 0.75 * P;
		Fx::FKartFxEmit Sparks;
		Sparks.Field = Fx::EField::Sparks;
		Sparks.Position = At + FVector(-80.0, 0.0, 20.0);
		Sparks.Count = FMath::RoundToInt32(8.f + 16.f * P) * (E.Kind == EEffectKind::Complete ? 3 : 1);
		Sparks.Spread = 1.2;
		Sparks.Speed = FVector2D(2.0, 4.5);
		Sparks.Life = FVector2D(0.35, 0.7);
		Sparks.Size = FVector2D(0.02, 0.04);
		Sparks.Color = FMath::Lerp(Color, FLinearColor::White, 0.5f);
		Sparks.ColorEnd = Color;
		Sparks.Intensity = 2.2;
		Sparks.Jitter = 0.3;
		Out.Add(Sparks);
		Fx::FKartFxEmit Flash;
		Flash.Field = Fx::EField::Flare;
		Flash.Position = At + FVector(-80.0, 0.0, 0.0);
		Flash.Speed = FVector2D::ZeroVector;
		Flash.Life = FVector2D(0.22, 0.22);
		Flash.Size = FVector2D(1.4, 1.4);
		Flash.SizeEnd = 0.4;
		Flash.Color = FLinearColor::White;
		Flash.ColorEnd = Color;
		Flash.Intensity = 2.4;
		Out.Add(Flash);
		if (P >= 0.99f || E.Kind == EEffectKind::Complete)
		{
			Fx::FKartFxEmit Ring;
			Ring.Field = Fx::EField::Ring;
			Ring.Position = At + FVector(0.0, 0.0, 22.0);
			Ring.Speed = FVector2D::ZeroVector;
			Ring.Life = FVector2D(0.45, 0.45);
			Ring.Size = FVector2D(1.2, 1.2);
			Ring.SizeEnd = 2.6;
			Ring.Color = FLinearColor::White;
			Ring.ColorEnd = Color;
			Ring.Intensity = 2.0;
			Out.Add(Ring);
		}
		if (E.Kind == EEffectKind::Complete)
		{
			Fx::FKartFxEmit Confetti;
			Confetti.Field = Fx::EField::Confetti;
			Confetti.Position = At + FVector(0.0, 0.0, 120.0);
			Confetti.Count = 60;
			Confetti.Spread = 0.9;
			Confetti.Speed = FVector2D(3.0, 6.0);
			Confetti.Life = FVector2D(1.4, 2.2);
			Confetti.Size = FVector2D(0.06, 0.1);
			Out.Add(Confetti);
		}
		break;
	}
	case EEffectKind::Miss:
	case EEffectKind::Topout:
	{
		Cues.Play(this, TEXT("stacker.miss"));
		Punch = 1.2;
		Fx::FKartFxEmit Dust;
		Dust.Field = Fx::EField::Smoke;
		Dust.Position = At;
		Dust.Count = 22;
		Dust.Spread = 1.3;
		Dust.Speed = FVector2D(0.6, 1.8);
		Dust.Life = FVector2D(0.5, 0.9);
		Dust.Size = FVector2D(0.12, 0.22);
		Dust.SizeEnd = 2.0;
		Dust.Color = MovecadeKart::Art::Hex(TEXT("#cfc6b8"));
		Dust.ColorEnd = MovecadeKart::Art::Hex(TEXT("#8c857a"));
		Dust.Intensity = 0.9;
		Dust.Jitter = 0.4;
		Out.Add(Dust);
		break;
	}
	}
	Shake = FMath::Max(Shake, FMath::Min(0.6, Punch * 0.45));
	if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
	{
		if (Out.Num() > 0)
		{
			FxSys->Emit(Out);
		}
	}
}

void AStackerGame::Tick(float DeltaSeconds)
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
		if (Lanes[i].Kind == EBuilderKind::Local)
		{
			SteerLocal(Lanes[i], Match.Towers[i], Dt);
		}
	}
	switch (Stage)
	{
	case EStackerStage::Intro:
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
		// compareHold.ts: the showcase holds eight seconds before the room hears the result.
		if (GetStageAge() >= CompareHoldSeconds)
		{
			ArcadeCommitResults();
		}
		break;
	}
	// Altitude: a chime once per five-row band of the tallest tower.
	int32 Tallest = 0;
	for (const FTower& T : Match.Towers)
	{
		Tallest = FMath::Max(Tallest, T.Height());
	}
	const int32 Band = Tallest / 5;
	if (Band > AltitudeBand)
	{
		AltitudeBand = Band;
		const int32 Step = FMath::Min(6, Band - 1);
		Cues.Play(this, TEXT("stacker.altitude"), 0.75f + 0.25f * Step / 6.f, FMath::Pow(2.f, 2.f * Step / 12.f));
	}
	UpdateTowers(Dt);
	UpdateShards();
	UpdateBuilders(Dt);
	UpdateCamera(Dt);
}

void AStackerGame::UpdateTowers(float Dt)
{
	// The end showcase compresses each whole tower to .15 a row and lets the scroll fall back to the foundation.
	const double Compare = Stage == EStackerStage::Finished ? Smooth(0.0, 1.0, GetStageAge() / 1.2) : 0.0;
	const double RowH = FMath::Lerp(RowHeight, CompareRowHeight, Compare);
	const TArray<FString>& Winners = Match.WinnerIds;
	const bool bDimLosers = Stage == EStackerStage::Finished && GetStageAge() > 1.2;
	double MaxScroll = 0.0;
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		const FTower& Tower = Match.Towers[i];
		const double Want = FMath::Max(0.0, Tower.Height() - WorkRows);
		Lane.Scroll = FMath::Min(Want, Lane.Scroll + (Want - Lane.Scroll) * (1.0 - FMath::Exp(-8.0 * Dt)));
		MaxScroll = FMath::Max(MaxScroll, Lane.Scroll);
		const double Scroll = FMath::Lerp(Lane.Scroll, 0.0, Compare);
		const int32 Len = Tower.Blocks.Num();
		const double Sway = BalanceDisplacement(Tower.Balance.LeanX);
		const bool bDim = bDimLosers && !Winners.Contains(Tower.PlayerId);
		const int32 First = Compare > 0.0 ? 0 : FMath::Max(0, Len - VisibleRows);
		for (int32 k = 0; k < PoolPerLane; ++k)
		{
			UStaticMeshComponent* M = BlockPool[i * PoolPerLane + k];
			if (k < First || k >= Len)
			{
				M->SetVisibility(false);
				continue;
			}
			const FBlock& B = Tower.Blocks[k];
			// presentation.ts: the landing squash over 280 ms, the lean carried up the tower.
			const double T = FMath::Min(1.0, B.AgeMs / 280.0);
			const double Sq = T < 1.0 ? FMath::Pow(1.0 - T, 1.5) * FMath::Cos(3.0 * PI * T) : 0.0;
			const double X = Lane.LaneX + B.Center + Sway * (k + 1) / Len;
			const double Y = (k - Scroll) * RowH + RowH / 2.0 - FMath::Max(0.0, Sq) * 0.08;
			if (Y < -1.0)
			{
				M->SetVisibility(false);
				continue;
			}
			const FVector Scale(1.0 + 0.2 * Sq, B.Width * (1.0 + 0.32 * Sq), (1.0 - 0.5 * Sq) * RowH / RowHeight);
			M->SetWorldTransform(FTransform(FRotator::ZeroRotator, W(FVector(X, Y, 0.0)), Scale));
			M->SetMaterial(0, LaneMaterials[i * 5 + (bDim ? 4 : (B.bPerfect ? 2 : (k % 2)))]);
			M->SetVisibility(true);
		}
		// The sweeping block: hidden while it reloads (the cooldown) and while the tower recovers.
		UStaticMeshComponent* Cursor = Cursors[i];
		const bool bCooling = Match.ElapsedMs - Tower.LastDropAtMs < DropCooldownMs;
		const bool bShow = Stage != EStackerStage::Finished && Match.Phase != EMatchPhase::Ended && !Tower.IsDone() && !bCooling && Tower.Balance.RecoveryMs <= 0.0;
		Cursor->SetVisibility(bShow);
		if (bShow)
		{
			const FBlock& Top = Tower.Top();
			const double Limit = BalanceLimit(Tower.Height(), Top.Width);
			const double Y = (Top.Index + 1.35 - Scroll) * RowHeight + RowHeight / 2.0 + 0.06 * FMath::Sin(6.0 * Clock + Tower.LaneIndex);
			const double Urgency = 1.0 - FMath::Min(1.0, (Top.Width - MinWidth) / 1.6);
			const double Pulse = 1.0 + 0.05 * Urgency * FMath::Sin(Clock * 12.0);
			const double Roll = FMath::Clamp(Tower.Balance.LeanX / Limit, -1.5, 1.5) * 0.12;
			const double Pitch = FMath::Clamp(Tower.Balance.LeanY / Limit, -1.5, 1.5) * 0.1;
			Cursor->SetWorldTransform(FTransform(FRotator(FMath::RadiansToDegrees(Pitch), 0.0, FMath::RadiansToDegrees(Roll)), W(FVector(Lane.LaneX + Tower.CursorCenter, Y, 0.0)),
				FVector(Pulse, Top.Width * Pulse, Pulse)));
		}
	}
	if (Rooftop)
	{
		Rooftop->SetScroll(MaxScroll);
	}
}

void AStackerGame::UpdateShards()
{
	for (int32 s = 0; s < ShardPool.Num(); ++s)
	{
		UStaticMeshComponent* M = ShardPool[s];
		// The rules stop stepping at the end: a shard still in the air would hang there through the showcase.
		if (!Match.Shards.IsValidIndex(s) || Stage == EStackerStage::Finished)
		{
			M->SetVisibility(false);
			continue;
		}
		const FShard& Shard = Match.Shards[s];
		const int32 i = Match.Towers.IndexOfByPredicate([&Shard](const FTower& T) { return T.LaneIndex == Shard.LaneIndex; });
		if (i == INDEX_NONE)
		{
			M->SetVisibility(false);
			continue;
		}
		if (!SeenEffects.Contains(-Shard.Id))
		{
			// Debris for a real chunk: its pitch drops as it gets wider.
			SeenEffects.Add(-Shard.Id);
			if (Shard.Width >= 0.3)
			{
				Cues.Play(this, TEXT("stacker.debris"), 0.5f + FMath::Min(1.f, static_cast<float>(Shard.Width)) * 0.5f, 1.15f - FMath::Min(1.f, static_cast<float>(Shard.Width) / 2.f) * 0.35f);
			}
		}
		const double Life = FMath::Max(0.0, Shard.TtlMs / Shard.DurationMs);
		const double Tumble = 1.0 - Life;
		const FVector Pos(Lanes[i].LaneX + Shard.X, (Shard.Row - Lanes[i].Scroll) * RowHeight + RowHeight / 2.0 + Shard.Y, 0.0);
		M->SetWorldTransform(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(Tumble * Shard.Spin * 0.45), -FMath::RadiansToDegrees(Tumble * Shard.Spin)), W(Pos),
			FVector(FMath::Max(0.01, Life), FMath::Max(0.01, Shard.Width * Life), FMath::Max(0.01, Life))));
		M->SetMaterial(0, LaneMaterials[i * 5]);
		M->SetVisibility(true);
	}
}

void AStackerGame::UpdateBuilders(float Dt)
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		const FTower& Tower = Match.Towers[i];
		UArcadeAvatarComponent* A = Avatars[i];
		// The lift rides with the work rows: (height - scroll) rows up, smoothed by the scroll itself.
		const double LiftY = FMath::Max(0.0, Tower.Height() - Lane.Scroll) * RowHeight * (Stage == EStackerStage::Finished ? 0.0 : 1.0);
		// Beside the lane rather than square in front of it (the web's spot hides a lone tower), turned toward it.
		const FVector Base(Lane.LaneX - BuilderSide, FloorY, BuilderZ);
		Lifts[i]->SetWorldTransform(FTransform(FRotator(0.0, 35.0, 0.0), W(Base + FVector(0.0, LiftY - 0.09, 0.0)), FVector(1.4, 1.9, 0.12)));
		Pistons[i]->SetWorldTransform(FTransform(FRotator::ZeroRotator, W(Base + FVector(0.0, LiftY / 2.0 - 0.1, 0.0)), FVector(0.22, 0.22, FMath::Max(0.02, LiftY))));
		A->SetWorldLocationAndRotation(W(Base + FVector(0.0, LiftY, 0.0)), FRotator(0.0, 35.0, 0.0));
		// presentation.ts: the throw over 420 ms after a drop: reach, brace, recoil, reload.
		const double Age = FMath::Max(0.0, Match.ElapsedMs - Tower.LastDropAtMs);
		const double T = FMath::Clamp(Age / DropCooldownMs, 0.0, 1.0);
		const double Reach = FMath::Sin(FMath::Min(1.0, T / 0.4) * PI);
		const double Brace = FMath::Sin(FMath::Min(1.0, T / 0.7) * PI);
		const double Recoil = FMath::Sin(FMath::Clamp((T - 0.3) / 0.7, 0.0, 1.0) * PI);
		const bool bRecovering = Tower.Balance.RecoveryMs > 0.0;
		const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
		const double BalX = FMath::Clamp(Tower.Balance.LeanX / Limit, -1.5, 1.5);
		const double BalY = FMath::Clamp(Tower.Balance.LeanY / Limit, -1.5, 1.5);
		const double Strain = FMath::Min(1.0, FMath::Sqrt(BalX * BalX + BalY * BalY));
		const double Sweep = FMath::Abs(Tower.CursorCenter) / FMath::Max(0.1, SweepTrackHalfWidth(Tower.Height()));
		const double Drive = FMath::Min(0.95, 0.18 + Sweep * 0.08 + Strain * 0.32 + Brace * 0.5 + (bRecovering ? 0.25 : 0.0));
		const double Lateral = Tower.CursorCenter / 3.2 * 0.3 - BalX * 0.7;
		Lane.LateralSmooth += (Lateral - Lane.LateralSmooth) * (1.0 - FMath::Exp(-14.0 * Dt));
		FArcadePoseInput& Pose = A->Pose();
		// The block held out in both hands, over the sweep: the hands follow the cursor, push on the drop.
		const double HandSide = FMath::Clamp(Tower.CursorCenter * 0.42, -0.6, 0.6);
		const double Fwd = 0.62 + 0.18 * Reach - 0.08 * Recoil;
		const double Up = -0.05 - 0.19 * Reach + 0.1 * Recoil - 0.08 * FMath::Abs(BalY);
		Pose.Hand[0] = FVector(Fwd, HandSide / ArmLength * 0.4 + 0.28, Up) / 1.0;
		Pose.Hand[1] = FVector(Fwd, HandSide / ArmLength * 0.4 - 0.28, Up) / 1.0;
		Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
		Pose.Crouch = static_cast<float>(0.12 + 0.3 * Drive);
		Pose.Lean = static_cast<float>(0.1 + 0.2 * Reach + 0.1 * Strain);
		Pose.Twist = static_cast<float>(-Lane.LateralSmooth * 0.6);
		const FVector CursorAt = W(FVector(Lane.LaneX + Tower.CursorCenter, (Tower.Top().Index + 1.35 - Lane.Scroll) * RowHeight, 0.0));
		Pose.Look = A->GetComponentTransform().InverseTransformVectorNoScale(CursorAt - A->GetComponentLocation() - FVector(0.0, 0.0, 95.0)).GetSafeNormal();
		Pose.LookWeight = 0.85f;
		float Cheer = 0.f, Slump = 0.f;
		const double ReactionAge = Clock - Reactions[i].Value;
		const float In = static_cast<float>(Smooth(0.0, 0.12, ReactionAge) * Smooth(0.9, 0.6, ReactionAge));
		switch (Reactions[i].Key)
		{
		case EEffectKind::Perfect: Cheer = Tower.Combo >= 3 ? 0.55f * In : 0.f; break;
		case EEffectKind::Complete: Cheer = In; break;
		case EEffectKind::Miss: Slump = 0.6f * In; break;
		case EEffectKind::Topout: Slump = In; break;
		default: break;
		}
		if (Tower.bEliminated)
		{
			Slump = FMath::Max(Slump, 0.7f);
		}
		if (Stage == EStackerStage::Finished)
		{
			const bool bWin = Match.WinnerIds.Contains(Tower.PlayerId);
			Cheer = FMath::Max(Cheer, bWin ? 0.9f : 0.f);
			Slump = FMath::Max(Slump, bWin ? 0.f : 0.45f);
		}
		Pose.Cheer = Cheer;
		Pose.Slump = Slump;
		Pose.Idle = 1.f;
		if (Cheer > 0.3f)
		{
			Pose.HandWeight[0] = Pose.HandWeight[1] = 0.f;
		}
		// The next block between the hands: small, thick, hidden while it reloads or the tower recovers.
		UStaticMeshComponent* Held = HeldBlocks[i];
		const bool bHold = Stage == EStackerStage::Match && !Tower.IsDone() && Age >= DropCooldownMs && !bRecovering && Cheer <= 0.3f;
		Held->SetVisibility(bHold);
		if (bHold)
		{
			const double Width = FMath::Min(0.5, FMath::Max(MinWidth, Tower.Top().Width) * 0.16);
			const FTransform Body = A->GetComponentTransform();
			const double Sc = A->AvatarScale / 0.64;
			const FVector Local = FVector(Fwd * 46.0, HandSide * 40.0, 92.0 + Up * 50.0) * Sc;
			Held->SetWorldTransform(FTransform(Body.GetRotation(), Body.GetLocation() + Body.GetRotation().RotateVector(Local), FVector(0.24, Width * 2.2, 0.16 / BlockSize.Y)));
		}
	}
}

void AStackerGame::UpdateCamera(float Dt)
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
	const int32 Span = LaneSpan();
	double Fov = 46.0;
	// StackerScene.tsx: home [0, 5.6, 10 + span*2.5] on [0, 1.25, 0]; in the arena the lanes must fit.
	FVector Pos(0.0, 5.6, 10.0 + Span * 2.5), Look(0.0, 1.25, 0.0);
	if (Span > 1 && Stage != EStackerStage::Finished)
	{
		const double HalfWidth = (Span - 1) * 4.0 + TrackHalfWidth + 0.5;
		Pos.Z = FMath::Max(Pos.Z, BuilderZ + HalfWidth / (FMath::Tan(FMath::DegreesToRadians(Fov / 2.0)) * Aspect * 0.92));
	}
	if (Stage == EStackerStage::Intro)
	{
		const double T = Smooth(0.0, 1.0, GetStageAge() / 3.0);
		Pos = FMath::Lerp(FVector(6.0, 6.5, 24.0), Pos, T);
		Look = FMath::Lerp(FVector(0.0, 2.0, 0.0), Look, T);
		Fov = FMath::Lerp(48.0, 46.0, T);
	}
	else if (Stage == EStackerStage::Finished)
	{
		// The showcase: every compressed tower in frame, then after 2.5 s an orbit of the winner (score, then height).
		const TArray<const FTower*> Order = Standings();
		double TallestY = 1.0;
		for (const FTower& T : Match.Towers)
		{
			TallestY = FMath::Max(TallestY, T.Blocks.Num() * CompareRowHeight);
		}
		const double HalfWidth = (Span - 1) * 4.0 + 3.0;
		const double FitZ = HalfWidth / (FMath::Tan(FMath::DegreesToRadians(Fov / 2.0)) * Aspect * 0.9) + 3.0;
		Pos = FVector(0.0, 2.2 + TallestY * 0.6, FMath::Max(9.0, FitZ));
		Look = FVector(0.0, TallestY * 0.45, 0.0);
		if (GetStageAge() > 2.5 && Order.Num() > 0)
		{
			const FTower& Winner = *Order[0];
			const double Top = Winner.Blocks.Num() * CompareRowHeight;
			const double Radius = 6.5 + Top * 0.9;
			// The orbit at .32 rad/s, swung back and forth across the front so the city stays behind the winner.
			const double A = 0.85 * FMath::Sin((GetStageAge() - 2.5) * 0.32);
			const FVector Centre(LaneXOf(Winner.LaneIndex), Top * 0.55 + 0.2, 0.0);
			Pos = Centre + FVector(FMath::Sin(A) * Radius, 0.8 + Top * 0.15, FMath::Cos(A) * Radius);
			Look = Centre;
		}
	}
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-7.0 * FMath::Min<double>(Dt, 1.0 / 20.0));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * K;
	CamFov += (Fov - CamFov) * K;
	Shake *= FMath::Exp(-9.0 * Dt);
	const FVector Jolt(FMath::Sin(Clock * 61.0) * Shake * 0.12, FMath::Sin(Clock * 53.0 + 1.1) * Shake * 0.1, 0.0);
	const FVector From = W(CamPos + Jolt);
	const FVector To = W(CamLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	const double HFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HFov));
}

void AStackerGame::Rematch()
{
	TArray<FSeed> MatchSeeds;
	for (const FStackerSeat& Seat : Seats)
	{
		MatchSeeds.Add({ Seat.Id, Seat.Name });
	}
	Match = CreateMatch(MatchSeeds);
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		Lane.Scroll = 0.0;
		Lane.AimedRow = -1;
		Lane.bDropQueued = false;
		Lane.MouseRoll = Lane.MousePitch = 0.0;
		if (Lane.Kind == EBuilderKind::Phone)
		{
			Match.Towers[i].Balance.LastInputMs = -BalanceStaleMs - 1.0;
		}
		Reactions[i] = TPair<EEffectKind, double>(EEffectKind::Land, -10.0);
	}
	SeenEffects.Reset();
	Accumulator = 0.0;
	AltitudeBand = 0;
	bResultsCommitted = false;
	SetStage(EStackerStage::Intro);
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
	if (OnRematch) OnRematch(); else Rematch();
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
