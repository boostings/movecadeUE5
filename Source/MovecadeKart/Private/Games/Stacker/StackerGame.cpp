#include "Games/Stacker/StackerGame.h"

#include "Algo/StableSort.h"
#include "Art/KartMaterials.h"
#include "Art/KartMeshBuilder.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Stacker/SStackerHud.h"
#include "Games/Stacker/StackerRooftop.h"
#include "InputCoreTypes.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/RoomProtocol.h"
#include "SceneView.h"

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
	constexpr double FloorY = -0.7;
	constexpr double BuilderZ = 2.2;
	constexpr double BuilderSide = 2.6;
	constexpr double CompareHoldSeconds = 8.0;
	/** The block: a rounded box 1 x .43 x 1.5 (web), radius .11. */
	const FVector BlockSize(1.0, 0.43, 1.5);
	constexpr double ArmLength = 0.525;
	/** Batches per lane: even, odd, perfect, dimmed, flashing blocks; then the lane's shards. */
	constexpr int32 BlockKinds = 5;
	constexpr int32 BatchesPerLane = BlockKinds + 1;
	enum EBlockKind : int32 { Even = 0, Odd = 1, Bright = 2, Dimmed = 3, Flashing = 4 };
	/** StackerScene.tsx: four birds (eight wings) per beat, 1.8 s; the peek lasts 1.2 s from row ten. */
	constexpr int32 BirdCount = 4;
	constexpr double BirdBeatSeconds = 1.8;
	constexpr double PeekSeconds = 1.2;
	constexpr int32 HeightMilestone = 5;
	constexpr int32 PeekMilestone = 10;

	/** The coach marks teach once per session: a later round (or a rematch) doesn't repeat them. */
	bool GCoachDropDone = false;
	bool GCoachLeanDone = false;

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
	 * blockSurface.ts as geometry: a rounded panel (UE cm, X deep, Y wide, Z tall) with a frame, nine ribs and four
	 * corner bolts pressed into its two broad faces in a darker trim, and a light edge catching the sun along the top.
	 * Slot 0 the panel (the lane's paint), slot 1 the trim, slot 2 the highlight.
	 */
	UStaticMesh* BuildBlockMesh(UObject* Outer)
	{
		using MovecadeKart::FMeshBuilder;
		FMeshBuilder B;
		const FVector H(BlockSize.Z * 50.0, BlockSize.X * 50.0, BlockSize.Y * 50.0);
		const double R = 11.0;
		const int32 Panel = B.Section(TEXT("Panel"));
		B.Section(TEXT("Trim"));
		B.Section(TEXT("Light"));
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
		// The broad faces (toward and away from the camera): the frame, the ribs, the bolts and the lit top edge.
		for (const double Side : { -1.0, 1.0 })
		{
			const double X = Side * (H.X + 0.5);
			const double FlatY = H.Y - R + 4.0, FlatZ = H.Z - 3.0;
			B.Box(TEXT("Trim"), FVector(X, 0.0, FlatZ - 1.0), FVector(0.8, FlatY, 1.0), FVector2D::ZeroVector);
			B.Box(TEXT("Trim"), FVector(X, 0.0, -FlatZ + 1.0), FVector(0.8, FlatY, 1.0), FVector2D::ZeroVector);
			B.Box(TEXT("Trim"), FVector(X, FlatY - 0.9, 0.0), FVector(0.8, 0.9, FlatZ), FVector2D::ZeroVector);
			B.Box(TEXT("Trim"), FVector(X, -FlatY + 0.9, 0.0), FVector(0.8, 0.9, FlatZ), FVector2D::ZeroVector);
			for (int32 k = 0; k < 9; ++k)
			{
				const double Y = FMath::Lerp(-FlatY + 7.5, FlatY - 7.5, k / 8.0);
				B.Box(TEXT("Trim"), FVector(X, Y, 0.0), FVector(0.55, 0.7, FlatZ - 4.5), FVector2D::ZeroVector);
				B.Box(TEXT("Light"), FVector(X, Y + 1.1, 0.0), FVector(0.45, 0.35, FlatZ - 4.5), FVector2D::ZeroVector);
			}
			for (const double By : { -1.0, 1.0 })
			{
				for (const double Bz : { -1.0, 1.0 })
				{
					const FVector At(X, By * (FlatY - 4.0), Bz * (FlatZ - 4.6));
					B.Cylinder(TEXT("Trim"), At, At + FVector(Side * 1.4, 0.0, 0.0), 1.5, 8, FVector2D::ZeroVector);
				}
			}
			B.Box(TEXT("Light"), FVector(X, 0.0, FlatZ + 1.2), FVector(0.4, FlatY - 1.0, 0.45), FVector2D::ZeroVector);
		}
		return B.Build(Outer, TEXT("StackerBlock"));
	}

	/** What a drop right now would do (the same sums as DropBlock, read only): the kept span and whether it's perfect. */
	struct FDropRead
	{
		double Support = 0.0;
		double Left = 0.0;
		double Right = 0.0;
		bool bPerfect = false;
		bool bMiss = false;
	};

	FDropRead ReadDrop(const FTower& Tower)
	{
		FDropRead Out;
		const FBlock& Below = Tower.Top();
		const double W = Below.Width;
		Out.Support = Below.Center + BalanceDisplacement(Tower.Balance.LeanX);
		const double Offset = FMath::Abs(Tower.CursorCenter - Out.Support);
		const bool bSteady = FMath::Sqrt(Tower.Balance.LeanX * Tower.Balance.LeanX + Tower.Balance.LeanY * Tower.Balance.LeanY) <= BalanceLimit(Tower.Height(), W) * SteadyBalanceRatio;
		const double Tolerance = FMath::Min(bSteady ? SteadyPerfectTolerance : PerfectTolerance, W / 2.0);
		Out.Left = FMath::Max(Tower.CursorCenter - W / 2.0, Out.Support - W / 2.0);
		Out.Right = FMath::Min(Tower.CursorCenter + W / 2.0, Out.Support + W / 2.0);
		Out.bPerfect = Offset <= Tolerance;
		Out.bMiss = !Out.bPerfect && Out.Right - Out.Left < MinWidth - 1e-9;
		return Out;
	}

	/** A roll about the depth axis that tips the top toward +web x by Degrees. */
	FQuat Tip(double Degrees)
	{
		return FQuat(FVector::ForwardVector, -FMath::DegreesToRadians(Degrees));
	}
}

bool FStackerView::Project(const FVector& World, FVector2D& Out01) const
{
	FVector2D Pixels;
	if (!bValid || !FSceneView::ProjectWorldToScreen(World, Rect, ViewProjection, Pixels))
	{
		return false;
	}
	Out01 = Pixels / Size;
	return true;
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

UInstancedStaticMeshComponent* AStackerGame::MakeBatch(const TCHAR* Name, UStaticMesh* Mesh, const TArray<UMaterialInterface*>& Materials, bool bShadows)
{
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), Name));
	C->SetStaticMesh(Mesh);
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		C->SetMaterial(i, Materials[i]);
	}
	C->SetupAttachment(RootComponent);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(bShadows);
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
	TowerByLane.Init(INDEX_NONE, 4);
	for (int32 i = 0; i < N; ++i)
	{
		Lanes[i].LaneX = LaneXOf(Match.Towers[i].LaneIndex);
		if (TowerByLane.IsValidIndex(Match.Towers[i].LaneIndex))
		{
			TowerByLane[Match.Towers[i].LaneIndex] = i;
		}
		// S3: a phone that hasn't sent a frame yet is missing (frozen, can't drop), not balancing on roll 0.
		if (Lanes[i].Kind == EBuilderKind::Phone)
		{
			Match.Towers[i].Balance.LastInputMs = -BalanceStaleMs - 1.0;
		}
	}

	using namespace MovecadeKart::Art;
	BlockMesh = BuildBlockMesh(this);
	UMaterialInterface* TrimMat = Plain(this, Hex(TEXT("#59636e")), 0.45, 0.35);
	UMaterialInterface* LightMat = Plain(this, Hex(TEXT("#e6eaee")), 0.35, 0.1, 0.05);
	UMaterialInterface* Dim = Plain(this, Hex(TEXT("#4f5566")), 0.6, 0.2);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* LiftMat = Plain(this, Hex(TEXT("#d39a32")), 0.5, 0.3);
	UMaterialInterface* PistonMat = Plain(this, Hex(TEXT("#c9ccd2")), 0.25, 0.95);
	for (int32 i = 0; i < N; ++i)
	{
		const FLinearColor C = AStackerRooftop::LaneColor(Match.Towers[i].LaneIndex);
		UMaterialInterface* Paint[BlockKinds] = {
			Plain(this, C * 0.95f, 0.42, 0.25),
			Plain(this, C * 0.74f, 0.45, 0.25),
			// StackerScene.tsx: perfect blocks lerp toward white (.45 there; less here, where the lit panel already reads bright).
			Plain(this, FMath::Lerp(C, FLinearColor::White, 0.24f), 0.32, 0.25, 0.14),
			Dim,
			// The perfect block's flash as it lands.
			Plain(this, FMath::Lerp(C, FLinearColor::White, 0.55f), 0.3, 0.1, 0.9),
		};
		for (int32 b = 0; b < BlockKinds; ++b)
		{
			Batches.Add(MakeBatch(TEXT("Blocks"), BlockMesh, { Paint[b], TrimMat, LightMat }, true));
		}
		// Shards are short-lived and fast: no shadows.
		Batches.Add(MakeBatch(TEXT("Shards"), BlockMesh, { Paint[0], TrimMat, LightMat }, false));
		UMaterialInstanceDynamic* CursorMat = Plain(this, C, 0.3, 0.25, 0.3);
		CursorMids.Add(CursorMat);
		UStaticMeshComponent* Cursor = MakeMesh(TEXT("Cursor"), BlockMesh);
		Cursor->SetMaterial(0, CursorMat);
		Cursor->SetMaterial(1, TrimMat);
		Cursor->SetMaterial(2, LightMat);
		Cursors.Add(Cursor);
		UStaticMeshComponent* Held = MakeMesh(TEXT("Held"), BlockMesh);
		Held->SetMaterial(0, CursorMat);
		Held->SetMaterial(1, TrimMat);
		Held->SetMaterial(2, LightMat);
		HeldBlocks.Add(Held);
		// The drop guide: a soft shadow on the tower's top where the block will sit, a red ghost of the overhang.
		UMaterialInstanceDynamic* Keep = Glow(this, FLinearColor(0.01f, 0.01f, 0.02f));
		UMaterialInstanceDynamic* Cut = Glow(this, Hex(TEXT("#ff4f4f")));
		if (Keep) Keep->SetScalarParameterValue(TEXT("Opacity"), 0.38f);
		if (Cut) Cut->SetScalarParameterValue(TEXT("Opacity"), 0.32f);
		GuideMids.Add(Keep);
		GuideMids.Add(Cut);
		UStaticMeshComponent* KeepMesh = MakeMesh(TEXT("GuideKeep"), Cube, false);
		KeepMesh->SetMaterial(0, Keep);
		KeepMesh->SetTranslucentSortPriority(2);
		GuideKeep.Add(KeepMesh);
		UStaticMeshComponent* CutMesh = MakeMesh(TEXT("GuideCut"), Cube, false);
		CutMesh->SetMaterial(0, Cut);
		CutMesh->SetTranslucentSortPriority(1);
		GuideCut.Add(CutMesh);
		UStaticMeshComponent* Lift = MakeMesh(TEXT("Lift"), Cube);
		Lift->SetMaterial(0, LiftMat);
		Lift->SetVisibility(true);
		Lifts.Add(Lift);
		UStaticMeshComponent* Piston = MakeMesh(TEXT("Piston"), Cylinder);
		Piston->SetMaterial(0, PistonMat);
		Piston->SetVisibility(true);
		Pistons.Add(Piston);
		UArcadeAvatarComponent* A = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), TEXT("Builder")));
		A->AvatarScale = N >= 4 ? 0.78f : 0.84f;
		A->SetupAttachment(RootComponent);
		A->RegisterComponent();
		A->SetLook(Seats[i].Look);
		Avatars.Add(A);
	}
	BatchState.SetNum(Batches.Num());
	Birds = MakeBatch(TEXT("Birds"), Cube, { Plain(this, Hex(TEXT("#4c3973")), 0.7) }, false);

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

FStackerView AStackerGame::GetView() const
{
	FStackerView View;
	const ULocalPlayer* Player = Viewer ? Viewer->GetLocalPlayer() : nullptr;
	if (!Player || !Player->ViewportClient)
	{
		return View;
	}
	FSceneViewProjectionData Data;
	if (!Player->GetProjectionData(Player->ViewportClient->Viewport, Data))
	{
		return View;
	}
	int32 X = 0, Y = 0;
	Viewer->GetViewportSize(X, Y);
	View.ViewProjection = Data.ComputeViewProjectionMatrix();
	View.Rect = Data.GetConstrainedViewRect();
	View.Size = FVector2D(X, Y);
	View.bValid = X > 0 && Y > 0;
	return View;
}

double AStackerGame::TopY(int32 TowerIndex) const
{
	if (!Match.Towers.IsValidIndex(TowerIndex))
	{
		return 0.0;
	}
	return (Match.Towers[TowerIndex].Top().Index - ScrollOf(TowerIndex)) * RowHeight + RowHeight;
}

void AStackerGame::RankStandings()
{
	StandingOrder.Reset();
	for (int32 i = 0; i < Match.Towers.Num(); ++i)
	{
		StandingOrder.Add(i);
	}
	Algo::StableSort(StandingOrder, [this](int32 A, int32 B)
	{
		const FTower& Ta = Match.Towers[A];
		const FTower& Tb = Match.Towers[B];
		return Ta.Score != Tb.Score ? Ta.Score > Tb.Score : Ta.Height() > Tb.Height();
	});
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
		PresentNewEffects();
	}
}

void AStackerGame::PresentNewEffects()
{
	// Effect ids only grow (the rules' one counter), so "new" is "above the last one presented".
	int32 Newest = LastSeenEffectId;
	for (const FEffect& E : Match.Effects)
	{
		if (E.Id > LastSeenEffectId)
		{
			Newest = FMath::Max(Newest, E.Id);
			PresentEffect(E);
			if (OnEffect) OnEffect(E);
		}
	}
	LastSeenEffectId = Newest;
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
	PresentNewEffects();
}

void AStackerGame::PresentEffect(const FEffect& E)
{
	const int32 i = TowerOfLane(E.LaneIndex);
	if (i == INDEX_NONE)
	{
		return;
	}
	const FTower& Tower = Match.Towers[i];
	FLane& Lane = Lanes[i];
	Reactions[i] = TPair<EEffectKind, double>(E.Kind, Clock);
	if (Lane.Kind == EBuilderKind::Local && (E.Kind == EEffectKind::Land || E.Kind == EEffectKind::Perfect))
	{
		++CoachLandings;
	}
	const float P = static_cast<float>(E.Intensity);
	const FLinearColor Color = AStackerRooftop::LaneColor(Tower.LaneIndex);
	const double Y = (E.Row - Lane.Scroll) * RowHeight + RowHeight / 2.0;
	const FVector At = W(FVector(Lane.LaneX + Tower.Top().Center + BalanceDisplacement(Tower.Balance.LeanX), Y, 0.0));
	TArray<Fx::FKartFxEmit> Out;
	double Punch = 0.0;
	switch (E.Kind)
	{
	case EEffectKind::Land:
		Cues.Play(this, TEXT("stacker.drop"), 0.7f + 0.3f * P, 1.f + FMath::Min(24, E.Row) * 0.02f);
		Punch = 0.2 + 0.3 * P;
		{
			// A puff of grit squeezed out of the joint.
			Fx::FKartFxEmit Grit;
			Grit.Field = Fx::EField::Smoke;
			Grit.Position = At + FVector(-60.0, 0.0, -18.0);
			Grit.Count = 6;
			Grit.Direction = FVector(-0.4, 0.0, 1.0).GetSafeNormal();
			Grit.Spread = 1.4;
			Grit.Speed = FVector2D(0.4, 1.0);
			Grit.Life = FVector2D(0.35, 0.6);
			Grit.Size = FVector2D(0.06, 0.12);
			Grit.SizeEnd = 1.8;
			Grit.Color = MovecadeKart::Art::Hex(TEXT("#e8ddcc"));
			Grit.ColorEnd = MovecadeKart::Art::Hex(TEXT("#a89c8a"));
			Grit.Intensity = 0.8;
			Grit.Jitter = 0.35;
			Out.Add(Grit);
		}
		break;
	case EEffectKind::Perfect:
	case EEffectKind::Complete:
	{
		Lane.PerfectAt = Clock;
		Cues.Play(this, TEXT("stacker.drop"), 1.f, 1.f + FMath::Min(24, E.Row) * 0.02f);
		const int32 Step = FMath::Clamp(Tower.Combo - 1, 0, 12);
		Cues.Play(this, TEXT("stacker.perfect"), 0.7f + 0.3f * Step / 12.f, FMath::Pow(2.f, Step / 12.f));
		Punch = E.Kind == EEffectKind::Complete ? 1.2 : 0.45 + 0.75 * P;
		// StackerScene.tsx: round(8 + 16i) sparks, ten more in white at full intensity, a flash, a ring.
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
		// Twinkles along the seam: the sparkle that says "clean".
		Fx::FKartFxEmit Twinkle;
		Twinkle.Field = Fx::EField::Flare;
		Twinkle.Position = At + FVector(-80.0, 0.0, 22.0);
		Twinkle.Count = 5 + FMath::Min(Tower.Combo, 5);
		Twinkle.Spread = 0.6;
		Twinkle.Speed = FVector2D(0.2, 0.7);
		Twinkle.Life = FVector2D(0.35, 0.65);
		Twinkle.Size = FVector2D(0.14, 0.26);
		Twinkle.SizeEnd = 0.1;
		Twinkle.Color = FLinearColor::White;
		Twinkle.ColorEnd = FMath::Lerp(Color, FLinearColor::White, 0.5f);
		Twinkle.Intensity = 2.6;
		Twinkle.Jitter = FMath::Max(0.3, Tower.Top().Width * 0.45);
		Out.Add(Twinkle);
		if (P >= 0.99f)
		{
			Fx::FKartFxEmit White = Sparks;
			White.Count = 10;
			White.Color = White.ColorEnd = FLinearColor::White;
			White.Intensity = 2.8;
			Out.Add(White);
		}
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
		Fx::FKartFxEmit Ring;
		Ring.Field = Fx::EField::Ring;
		Ring.Position = At + FVector(0.0, 0.0, 22.0);
		Ring.Speed = FVector2D::ZeroVector;
		Ring.Life = FVector2D(0.45, 0.45);
		Ring.Size = FVector2D(1.0 + 0.3 * Tower.Top().Width, 1.0 + 0.3 * Tower.Top().Width);
		Ring.SizeEnd = 2.6;
		Ring.Color = FLinearColor::White;
		Ring.ColorEnd = Color;
		Ring.Intensity = P >= 0.99f || E.Kind == EEffectKind::Complete ? 2.2 : 1.4;
		Out.Add(Ring);
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
		// The tower shudders: hard for a topple, a jolt for a miss.
		Lane.Topple = FMath::Max(Lane.Topple, E.Kind == EEffectKind::Topout ? 1.0 : 0.35);
		Fx::FKartFxEmit Dust;
		Dust.Field = Fx::EField::Smoke;
		Dust.Position = At;
		Dust.Count = E.Kind == EEffectKind::Topout ? 34 : 22;
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
	UpdateBeats(Dt);
	UpdateCoach(Dt);
	UpdateCamera(Dt);
	for (int32 b = 0; b < Batches.Num(); ++b)
	{
		FlushBatch(b);
	}
}

void AStackerGame::FlushBatch(int32 Index)
{
	UInstancedStaticMeshComponent* Batch = Batches[Index];
	FBatch& State = BatchState[Index];
	bool bSame = State.Want.Num() == State.Have.Num();
	for (int32 k = 0; bSame && k < State.Want.Num(); ++k)
	{
		bSame = State.Want[k].Equals(State.Have[k], 0.01);
	}
	if (!bSame)
	{
		if (State.Want.Num() == Batch->GetInstanceCount())
		{
			// With last frame's transforms as the previous ones, so the renderer has real motion vectors (no TSR smear).
			Batch->BatchUpdateInstancesTransforms(0, State.Want, State.Have, false, true, false);
		}
		else
		{
			Batch->ClearInstances();
			if (State.Want.Num() > 0)
			{
				Batch->AddInstances(State.Want, false, false, false);
			}
		}
		Swap(State.Have, State.Want);
	}
	State.Want.Reset();
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
		// StackerScene.tsx: an exponential approach, never above the target; snapped once it is close, so the
		// tower and the city bands come to rest instead of creeping for ever.
		Lane.Scroll = FMath::Min(Want, Lane.Scroll + (Want - Lane.Scroll) * (1.0 - FMath::Exp(-8.0 * Dt)));
		if (FMath::Abs(Want - Lane.Scroll) < 1e-3)
		{
			Lane.Scroll = Want;
		}
		MaxScroll = FMath::Max(MaxScroll, Lane.Scroll);
		const double Scroll = FMath::Lerp(Lane.Scroll, 0.0, Compare);
		const int32 Len = Tower.Blocks.Num();
		const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
		const double Sway = BalanceDisplacement(Tower.Balance.LeanX);
		Lane.ShownLean += (Tower.Balance.LeanX - Lane.ShownLean) * (1.0 - FMath::Exp(-16.0 * Dt));
		Lane.Topple *= FMath::Exp(-1.6 * Dt);
		const double Live = 1.0 - Compare;
		// The tower bends with the lean (the top block sits exactly over the support the rules use), shivers as the
		// danger builds toward a topple, and shudders when it goes.
		const double LeanRatio = FMath::Clamp(Lane.ShownLean / Limit, -1.6, 1.6);
		const double Danger = FMath::Clamp(Tower.Balance.DangerMs / BalanceFailureMs, 0.0, 1.0);
		const double Shiver = Danger * Danger * FMath::Sin(Clock * 19.0 + i * 1.7);
		const double Shudder = Lane.Topple * FMath::Sin(Clock * 8.5 + i);
		const bool bDim = bDimLosers && !Winners.Contains(Tower.PlayerId);
		const int32 First = Compare > 0.0 ? 0 : FMath::Max(0, Len - VisibleRows);
		const bool bFlash = Clock - Lane.PerfectAt < 0.16;
		for (int32 k = First; k < Len; ++k)
		{
			const FBlock& B = Tower.Blocks[k];
			// presentation.ts: the landing squash over 280 ms, the lean carried up the tower.
			const double T = FMath::Min(1.0, B.AgeMs / 280.0);
			const double Sq = T < 1.0 ? FMath::Pow(1.0 - T, 1.5) * FMath::Cos(3.0 * PI * T) : 0.0;
			const double F = static_cast<double>(k + 1) / Len;
			const double X = Lane.LaneX + B.Center + Sway * F + (0.05 * Shiver + 0.4 * Shudder) * F * F * Live;
			const double Y = (k - Scroll) * RowH + RowH / 2.0 - FMath::Max(0.0, Sq) * 0.08;
			if (Y < -1.0)
			{
				continue;
			}
			const double TiltDeg = (LeanRatio * 4.5 + Shiver * 2.2 + Shudder * 9.0) * F * Live;
			const FVector Scale(1.0 + 0.2 * Sq, B.Width * (1.0 + 0.32 * Sq), (1.0 - 0.5 * Sq) * RowH / RowHeight);
			const int32 Kind = bDim ? Dimmed : (B.bPerfect ? (bFlash && k == Len - 1 ? Flashing : Bright) : (k % 2 ? Odd : Even));
			BatchState[i * BatchesPerLane + Kind].Want.Add(FTransform(Tip(TiltDeg), W(FVector(X, Y, 0.0)), Scale));
		}
		// The sweeping block: hidden while it reloads (the cooldown) and while the tower recovers.
		UStaticMeshComponent* Cursor = Cursors[i];
		const bool bCooling = Match.ElapsedMs - Tower.LastDropAtMs < DropCooldownMs;
		const bool bShow = Stage != EStackerStage::Finished && Match.Phase != EMatchPhase::Ended && !Tower.IsDone() && !bCooling && Tower.Balance.RecoveryMs <= 0.0;
		if (Cursor->IsVisible() != bShow)
		{
			Cursor->SetVisibility(bShow);
		}
		bool bGuide = false;
		if (bShow)
		{
			const FBlock& Top = Tower.Top();
			const double Y = (Top.Index + 1.35 - Scroll) * RowHeight + RowHeight / 2.0 + 0.06 * FMath::Sin(6.0 * Clock + Tower.LaneIndex);
			// The narrower the tower, the more the block you can't afford to miss shouts: faster, brighter.
			const double Urgency = 1.0 - FMath::Min(1.0, (Top.Width - MinWidth) / 1.6);
			const double Pulse = 1.0 + 0.05 * Urgency * FMath::Sin(Clock * 12.0);
			const double Glow = 0.45 + 0.3 * FMath::Sin(Clock * (7.0 + Urgency * 9.0)) + Urgency * 0.2;
			const double Roll = FMath::Clamp(Tower.Balance.LeanX / Limit, -1.5, 1.5) * 0.12;
			const double Pitch = FMath::Clamp(Tower.Balance.LeanY / Limit, -1.5, 1.5) * 0.1;
			Cursor->SetWorldTransform(FTransform(FRotator(FMath::RadiansToDegrees(Pitch), 0.0, FMath::RadiansToDegrees(Roll)), W(FVector(Lane.LaneX + Tower.CursorCenter, Y, 0.0)),
				FVector(Pulse, Top.Width * Pulse, Pulse)));
			if (UMaterialInstanceDynamic* M = CursorMids[i])
			{
				const FLinearColor C = AStackerRooftop::LaneColor(Tower.LaneIndex);
				M->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(C, FMath::Lerp(C, FLinearColor::White, 0.3f), static_cast<float>(FMath::Clamp(Glow, 0.0, 1.0))));
				M->SetScalarParameterValue(TEXT("Emissive"), static_cast<float>(0.12 + 0.3 * Glow * (0.5 + Urgency)));
			}
			// The guide on the tower's top: the span this drop keeps (bright when it would be perfect) and a red
			// ghost of the overhang that will be cut.
			const FDropRead Read = ReadDrop(Tower);
			const double TopSurface = (Top.Index - Scroll) * RowHeight + RowHeight / 2.0 + BlockSize.Y / 2.0 + 0.012;
			const double TopX = Lane.LaneX + Read.Support;
			const double CursorL = Tower.CursorCenter - Top.Width / 2.0, CursorR = Tower.CursorCenter + Top.Width / 2.0;
			UStaticMeshComponent* Keep = GuideKeep[i];
			UStaticMeshComponent* Cut = GuideCut[i];
			bGuide = true;
			if (UMaterialInstanceDynamic* KeepMat = GuideMids[i * 2])
			{
				KeepMat->SetVectorParameterValue(TEXT("Color"), Read.bPerfect ? FLinearColor(0.55f, 1.6f, 1.1f) : FLinearColor(0.01f, 0.01f, 0.02f));
				KeepMat->SetScalarParameterValue(TEXT("Opacity"), Read.bPerfect ? 0.55f + 0.2f * static_cast<float>(FMath::Sin(Clock * 14.0)) : 0.38f);
			}
			auto Slab = [this, &Lane](UStaticMeshComponent* M, double L, double R, double Y0, double Height)
			{
				const double Wd = FMath::Max(0.01, R - L);
				M->SetWorldTransform(FTransform(FQuat::Identity, W(FVector(Lane.LaneX + (L + R) / 2.0, Y0 + Height / 2.0, 0.0)), FVector(BlockSize.Z * 0.96, Wd, Height)));
				if (!M->IsVisible())
				{
					M->SetVisibility(true);
				}
			};
			if (Read.bMiss)
			{
				Keep->SetVisibility(false);
				Slab(Cut, CursorL, CursorR, TopSurface - 0.012, BlockSize.Y * 0.9);
			}
			else
			{
				const double L = Read.bPerfect ? Read.Support - Top.Width / 2.0 : Read.Left;
				const double R = Read.bPerfect ? Read.Support + Top.Width / 2.0 : Read.Right;
				Slab(Keep, L, R, TopSurface - 0.006, 0.012);
				if (!Read.bPerfect && Top.Width - (Read.Right - Read.Left) > 0.02)
				{
					// The overhang hangs on the side the block is off toward.
					const bool bRight = Tower.CursorCenter > Read.Support;
					Slab(Cut, bRight ? Read.Right : CursorL, bRight ? CursorR : Read.Left, TopSurface - 0.012, BlockSize.Y * 0.9);
				}
				else
				{
					Cut->SetVisibility(false);
				}
			}
			(void)TopX;
		}
		if (!bGuide)
		{
			GuideKeep[i]->SetVisibility(false);
			GuideCut[i]->SetVisibility(false);
		}
	}
	if (Rooftop)
	{
		Rooftop->SetScroll(MaxScroll);
	}
}

void AStackerGame::UpdateShards()
{
	// The rules stop stepping at the end: a shard still in the air would hang there through the showcase.
	if (Stage == EStackerStage::Finished)
	{
		return;
	}
	int32 Newest = LastSeenShardId;
	for (const FShard& Shard : Match.Shards)
	{
		const int32 i = TowerOfLane(Shard.LaneIndex);
		if (i == INDEX_NONE)
		{
			continue;
		}
		if (Shard.Id > LastSeenShardId)
		{
			Newest = FMath::Max(Newest, Shard.Id);
			// Debris for a real chunk: its pitch drops as it gets wider.
			if (Shard.Width >= 0.3)
			{
				Cues.Play(this, TEXT("stacker.debris"), 0.5f + FMath::Min(1.f, static_cast<float>(Shard.Width)) * 0.5f, 1.15f - FMath::Min(1.f, static_cast<float>(Shard.Width) / 2.f) * 0.35f);
			}
		}
		const double Life = FMath::Max(0.0, Shard.TtlMs / Shard.DurationMs);
		const double Tumble = 1.0 - Life;
		const FVector Pos(Lanes[i].LaneX + Shard.X, (Shard.Row - Lanes[i].Scroll) * RowHeight + RowHeight / 2.0 + Shard.Y, 0.0);
		BatchState[i * BatchesPerLane + BlockKinds].Want.Add(FTransform(FRotator(0.0, -FMath::RadiansToDegrees(Tumble * Shard.Spin * 0.45), -FMath::RadiansToDegrees(Tumble * Shard.Spin)), W(Pos),
			FVector(FMath::Max(0.01, Life), FMath::Max(0.01, Shard.Width * Life), FMath::Max(0.01, Life))));
	}
	LastSeenShardId = Newest;
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
		// The reactions (StackerAvatar.tsx): joy on a perfect, a wince on a miss, a stagger on a topple.
		const double ReactionAge = Clock - Reactions[i].Value;
		const EEffectKind Reaction = Reactions[i].Key;
		float Cheer = 0.f, Slump = 0.f;
		double Stagger = 0.0;
		switch (Reaction)
		{
		case EEffectKind::Perfect:
			Cheer = static_cast<float>((Tower.Combo >= 3 ? 0.75 : 0.45) * Smooth(0.0, 0.1, ReactionAge) * Smooth(0.7, 0.5, ReactionAge));
			break;
		case EEffectKind::Complete:
			Cheer = static_cast<float>(Smooth(0.0, 0.12, ReactionAge) * Smooth(1.6, 1.2, ReactionAge));
			break;
		case EEffectKind::Miss:
			Slump = static_cast<float>(0.6 * Smooth(0.0, 0.12, ReactionAge) * Smooth(0.95, 0.6, ReactionAge));
			break;
		case EEffectKind::Topout:
			Stagger = Smooth(0.0, 0.1, ReactionAge) * Smooth(1.4, 0.9, ReactionAge);
			Slump = static_cast<float>(0.9 * Stagger);
			break;
		default:
			break;
		}
		if (Tower.bEliminated)
		{
			Slump = FMath::Max(Slump, 0.7f);
		}
		if (Stage == EStackerStage::Finished)
		{
			// finishFirst / finishLast: the winner celebrates, the last place hangs its head.
			const bool bWin = Match.WinnerIds.Contains(Tower.PlayerId);
			const bool bLast = StandingOrder.Num() > 1 && StandingOrder.Last() == i && !bWin;
			Cheer = FMath::Max(Cheer, bWin ? 0.9f : 0.f);
			Slump = FMath::Max(Slump, bLast ? 0.6f : (bWin ? 0.f : 0.25f));
		}
		// The topple shoves the builder back off balance and twists them toward the tower.
		const FVector Shove = W(FVector(-FMath::Sign(Tower.Balance.LeanX + 1e-6) * 0.12 * Stagger * FMath::Sin(ReactionAge * 9.0), 0.0, 0.18 * Stagger));
		A->SetWorldLocationAndRotation(W(Base + FVector(0.0, LiftY, 0.0)) + Shove, FRotator(0.0, 35.0 + 12.0 * Stagger * FMath::Sin(ReactionAge * 7.0), 0.0));
		FArcadePoseInput& Pose = A->Pose();
		// The block held out in both hands, over the sweep: the hands follow the cursor, push on the drop.
		const double HandSide = FMath::Clamp(Tower.CursorCenter * 0.42, -0.6, 0.6);
		const double Fwd = 0.62 + 0.18 * Reach - 0.08 * Recoil;
		const double Up = -0.05 - 0.19 * Reach + 0.1 * Recoil - 0.08 * FMath::Abs(BalY);
		Pose.Hand[0] = FVector(Fwd, HandSide / ArmLength * 0.4 + 0.28, Up);
		Pose.Hand[1] = FVector(Fwd, HandSide / ArmLength * 0.4 - 0.28, Up);
		Pose.HandWeight[0] = Pose.HandWeight[1] = 1.f;
		Pose.Crouch = static_cast<float>(0.12 + 0.3 * Drive);
		Pose.Lean = static_cast<float>(0.1 + 0.2 * Reach + 0.08 * Brace + 0.1 * Strain - 0.35 * Stagger);
		Pose.Twist = static_cast<float>(-Lane.LateralSmooth * 0.6);
		const FVector CursorAt = W(FVector(Lane.LaneX + Tower.CursorCenter, (Tower.Top().Index + 1.35 - Lane.Scroll) * RowHeight, 0.0));
		Pose.Look = A->GetComponentTransform().InverseTransformVectorNoScale(CursorAt - A->GetComponentLocation() - FVector(0.0, 0.0, 95.0)).GetSafeNormal();
		Pose.LookWeight = 0.85f;
		Pose.Cheer = Cheer;
		Pose.Slump = Slump;
		Pose.Idle = 1.f;
		if (Cheer > 0.3f || Stagger > 0.3)
		{
			Pose.HandWeight[0] = Pose.HandWeight[1] = 0.f;
		}
		// The next block between the hands: small, thick, hidden while it reloads or the tower recovers.
		UStaticMeshComponent* Held = HeldBlocks[i];
		const bool bHold = Stage == EStackerStage::Match && !Tower.IsDone() && Age >= DropCooldownMs && !bRecovering && Cheer <= 0.3f;
		if (Held->IsVisible() != bHold)
		{
			Held->SetVisibility(bHold);
		}
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

void AStackerGame::UpdateBeats(float Dt)
{
	// StackerScene.tsx updateAltitudeBeats: a flight of birds each five rows, a camera peek from row ten.
	int32 Tallest = 0;
	double HighestScroll = 0.0;
	for (int32 i = 0; i < Match.Towers.Num(); ++i)
	{
		Tallest = FMath::Max(Tallest, Match.Towers[i].Height());
		HighestScroll = FMath::Max(HighestScroll, Lanes[i].Scroll);
	}
	const int32 Crossed = Tallest / HeightMilestone * HeightMilestone;
	if (Crossed < Beats.LastMilestone)
	{
		Beats.LastMilestone = Crossed;
	}
	if (Crossed > Beats.LastMilestone && Crossed > 0 && Stage == EStackerStage::Match)
	{
		Beats.LastMilestone = Crossed;
		Beats.BirdLife = BirdBeatSeconds;
		Beats.BirdRow = Tallest;
		if (Crossed >= PeekMilestone)
		{
			Beats.PeekRemaining = PeekSeconds;
		}
	}
	Beats.BirdLife = FMath::Max(0.0, Beats.BirdLife - Dt);
	Beats.PeekRemaining = FMath::Max(0.0, Beats.PeekRemaining - Dt);
	TArray<FTransform> Wings;
	if (Beats.BirdLife > 0.0 && Stage == EStackerStage::Match)
	{
		const double Life = Beats.BirdLife / BirdBeatSeconds;
		const double Entrance = 1.0 - Life;
		const double Range = FMath::Max(18.0, (Rooftop ? Rooftop->DeckWidth() : 18.0) * 0.9);
		for (int32 b = 0; b < BirdCount; ++b)
		{
			const double X = FMath::Fmod(Clock * 2.4 + b * Range / BirdCount + Entrance * 5.0, Range) - Range / 2.0;
			const double Y = (Beats.BirdRow - HighestScroll) * RowHeight + 1.2 + b * 0.18;
			const double Z = -3.8 - b * 0.18;
			const double Flap = FMath::RadiansToDegrees(0.22 + FMath::Sin(Clock * 12.0 + b) * 0.18) * 2.0;
			// web 0.58 x .08 x .08 wings, scaled (.85, .72 + life * .2, 1): UE (deep, across, up), cube 1 m.
			const FVector Scale(0.08, 0.58 * 0.85, 0.08 * (0.72 + Life * 0.2));
			Wings.Add(FTransform(Tip(-Flap), W(FVector(X - 0.24, Y, Z)), Scale));
			Wings.Add(FTransform(Tip(Flap), W(FVector(X + 0.24, Y, Z)), Scale));
		}
	}
	if (Wings.Num() != Birds->GetInstanceCount())
	{
		Birds->ClearInstances();
		if (Wings.Num() > 0)
		{
			Birds->AddInstances(Wings, false, false, false);
		}
	}
	else if (Wings.Num() > 0)
	{
		Birds->BatchUpdateInstancesTransforms(0, Wings, false, true, true);
	}
}

void AStackerGame::UpdateCoach(float Dt)
{
	// The local builder's coach marks: "drop when aligned" until a few blocks have landed, "lean against the wind"
	// when the wind starts to matter, until the tower has been held steady in it for a while.
	Coach.Tower = Lanes.IndexOfByPredicate([](const FLane& L) { return L.Kind == EBuilderKind::Local; });
	float WantDrop = 0.f, WantLean = 0.f;
	if (Coach.Tower != INDEX_NONE && Stage == EStackerStage::Match && Match.Phase == EMatchPhase::Playing)
	{
		const FTower& Tower = Match.Towers[Coach.Tower];
		const bool bLive = !Tower.IsDone() && Tower.Balance.RecoveryMs <= 0.0;
		const bool bCursor = bLive && Match.ElapsedMs - Tower.LastDropAtMs >= DropCooldownMs;
		Coach.bAligned = bCursor && ReadDrop(Tower).bPerfect;
		if (CoachLandings >= 3)
		{
			GCoachDropDone = true;
		}
		WantDrop = !GCoachDropDone && bCursor ? 1.f : 0.f;
		const FVector2D Wind = WindAt(Match.ElapsedMs, Tower.Height());
		const double Limit = BalanceLimit(Tower.Height(), Tower.Top().Width);
		const double Ratio = FMath::Abs(Tower.Balance.LeanX) / Limit;
		const bool bWindy = FMath::Abs(Wind.X) > 2.2;
		const bool bLeaning = Ratio > SteadyBalanceRatio || Tower.Balance.DangerMs > 0.0;
		if (bWindy && !bLeaning)
		{
			CoachSteadySeconds += Dt;
		}
		if (CoachSteadySeconds > 6.0 && CoachLeanShown > 2.5)
		{
			GCoachLeanDone = true;
		}
		WantLean = !GCoachLeanDone && bLive && bWindy && (bLeaning || CoachLeanShown < 2.5) ? 1.f : 0.f;
		// Push the palm against whatever is tipping the tower: the lean itself once it shows, else the wind.
		const double Push = Ratio > 0.25 ? -Tower.Balance.LeanX : -Wind.X;
		Coach.LeanSide = Push >= 0.0 ? 1.f : -1.f;
		if (Coach.Lean > 0.5f)
		{
			CoachLeanShown += Dt;
		}
	}
	const float K = 1.f - FMath::Exp(-8.f * Dt);
	Coach.Drop += (WantDrop - Coach.Drop) * K;
	Coach.Lean += (WantLean - Coach.Lean) * K;
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
	else if (Stage == EStackerStage::Match && Beats.PeekRemaining > 0.0)
	{
		// The peek: a breath back and up to take in how high it's got, then straight back to work.
		const double P = FMath::Sin(PI * (1.0 - Beats.PeekRemaining / PeekSeconds));
		const double Ease = P * P * (3.0 - 2.0 * P);
		Pos += FVector(0.0, 1.6, 3.2) * Ease;
		Look += FVector(0.0, -0.35, 0.0) * Ease;
		Fov += 4.0 * Ease;
	}
	else if (Stage == EStackerStage::Finished)
	{
		// The showcase: every compressed tower in frame, then after 2.5 s an orbit of the winner (score, then height).
		double TallestY = 1.0;
		for (const FTower& T : Match.Towers)
		{
			TallestY = FMath::Max(TallestY, T.Blocks.Num() * CompareRowHeight);
		}
		const double HalfWidth = (Span - 1) * 4.0 + 3.0;
		const double FitZ = HalfWidth / (FMath::Tan(FMath::DegreesToRadians(Fov / 2.0)) * Aspect * 0.9) + 3.0;
		Pos = FVector(0.0, 2.2 + TallestY * 0.6, FMath::Max(9.0, FitZ));
		Look = FVector(0.0, TallestY * 0.45, 0.0);
		if (GetStageAge() > 2.5 && StandingOrder.Num() > 0)
		{
			const FTower& Winner = Match.Towers[StandingOrder[0]];
			const double Top = Winner.Blocks.Num() * CompareRowHeight;
			// StackerScene.tsx: radius 5.2 + top * .9; a little wider here, where the results card shares the frame.
			const double Radius = 6.5 + Top * 1.15;
			// The orbit at .32 rad/s, swung back and forth across the front so the city stays behind the winner.
			const double OrbitAge = GetStageAge() - 2.5;
			const double A = 0.85 * FMath::Sin(OrbitAge * 0.32);
			const FVector Centre(LaneXOf(Winner.LaneIndex), Top * 0.5 + 0.2, 0.0);
			// The winner stands in the left of the frame (the standings fill the right): look past them to the right.
			const FVector Right(FMath::Cos(A), 0.0, -FMath::Sin(A));
			// A slow push in as the orbit starts, from the line-up into the winner's portrait.
			const double In = Smooth(0.0, 2.0, OrbitAge);
			Pos = FMath::Lerp(Pos, Centre + FVector(FMath::Sin(A) * Radius, 0.8 + Top * 0.15, FMath::Cos(A) * Radius), In);
			Look = FMath::Lerp(Look, Centre + Right * Radius * 0.24, In);
			Fov = FMath::Lerp(46.0, 44.0, In);
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
		Lane.Topple = 0.0;
		Lane.ShownLean = 0.0;
		Lane.PerfectAt = -10.0;
		if (Lane.Kind == EBuilderKind::Phone)
		{
			Match.Towers[i].Balance.LastInputMs = -BalanceStaleMs - 1.0;
		}
		Reactions[i] = TPair<EEffectKind, double>(EEffectKind::Land, -10.0);
	}
	LastSeenEffectId = 0;
	LastSeenShardId = 0;
	StandingOrder.Reset();
	Beats = FBeats();
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
