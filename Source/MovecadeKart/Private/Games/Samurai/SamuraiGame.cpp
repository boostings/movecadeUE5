#include "Games/Samurai/SamuraiGame.h"

#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "FX/KartFx.h"
#include "FX/KartFxSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Games/Samurai/SamuraiArt.h"
#include "Games/Samurai/SamuraiDojo.h"
#include "Games/Samurai/SSamuraiHud.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/RoomProtocol.h"
#include "ProceduralMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Store/KartCosmetics.h" // store cosmetics: katana skin

DEFINE_LOG_CATEGORY_STATIC(LogSamurai, Log, All);

using namespace MovecadeKart::Samurai;
namespace Fx = MovecadeKart::Fx;

namespace
{
	constexpr double StepMs = 1000.0 / 60.0;
	constexpr int32 TargetPoolSize = 20;
	constexpr int32 PiecePoolSize = 32;
	constexpr double PieceSeconds = 0.65;
	constexpr int32 TrailSamples = 12;
	/** The swordsman's arm, shoulder to palm, at the rig's .64 (Docs/Port/05 §3 rig). */
	constexpr double ArmLength = 0.525;

	double Smooth(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	double EaseOutBack(double X)
	{
		constexpr double C1 = 1.70158, C3 = C1 + 1.0;
		return 1.0 + C3 * FMath::Pow(X - 1.0, 3.0) + C1 * FMath::Pow(X - 1.0, 2.0);
	}

	/** A web direction to UE. */
	FVector DirUE(const FVector& Web)
	{
		return MovecadeKart::Coords::DirToUE(Web);
	}
}

ASamuraiGame::ASamuraiGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(0x5a3c1);
}

FVector ASamuraiGame::W(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

FVector ASamuraiGame::KamaeDirection(double LaneX) const
{
	// The guard: blade raised before the face, tip forward and up.
	return BladeDirectionToward(LaneX, FVector(LaneX + 0.25, 2.55, 0.15));
}

void ASamuraiGame::Setup(ASamuraiDojo* InDojo, const TArray<FSamuraiSeat>& InSeats, APlayerController* InViewer, uint32 Seed)
{
	Dojo = InDojo;
	Seats = InSeats;
	Viewer = InViewer;
	MatchSeed = Seed;
	const int32 N = Seats.Num();
	if (Dojo)
	{
		Dojo->SetLanes(N);
	}
	TArray<FSeed> MatchSeeds;
	for (int32 i = 0; i < N; ++i)
	{
		MatchSeeds.Add({ Seats[i].Id, Seats[i].Name });
		FLane& L = Lanes.AddDefaulted_GetRef();
		L.PlayerId = Seats[i].Id;
		L.Kind = Seats[i].Kind;
		L.LaneX = LaneX(i, N);
		L.TipGoal = FVector(L.LaneX + 0.3, 2.2, TargetDepth);
		L.Blade = BladeFor(L.LaneX, KamaeDirection(L.LaneX));
		L.NextThinkAt = 0.6 + i * 0.13;
	}
	Match = CreateMatch(MatchSeeds, Seed);

	// The props: each target whole and halved, and its materials.
	for (int32 V = 0; V < 6; ++V)
	{
		const EVariant Variant = static_cast<EVariant>(V);
		for (int32 P = 0; P < 3; ++P)
		{
			TargetMeshes.Add(Art::TargetMesh(this, Variant, static_cast<Art::EPart>(P)));
		}
		MaterialStart.Add(TargetMaterials.Num());
		for (UMaterialInterface* M : Art::TargetMaterials(this, Variant))
		{
			TargetMaterials.Add(M);
		}
	}
	MaterialStart.Add(TargetMaterials.Num());
	auto MakeMesh = [this](const TCHAR* Name, UStaticMesh* Mesh, bool bShadows = true)
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
	};
	for (int32 i = 0; i < TargetPoolSize; ++i)
	{
		TargetPool.Add(MakeMesh(TEXT("Target"), nullptr));
	}
	Slots.SetNum(TargetPoolSize);
	bPracticeDemo = FParse::Param(FCommandLine::Get(), TEXT("SamuraiPracticeDemo"));
	// The halves live 0.65 s: no shadows (52 shadowed multi-section casters otherwise).
	for (int32 i = 0; i < PiecePoolSize; ++i)
	{
		PiecePool.Add(MakeMesh(TEXT("Piece"), nullptr, false));
	}
	Pieces.SetNum(PiecePoolSize);
	using namespace MovecadeKart::Art;
	UStaticMesh* Octagon = Art::RingMesh(this, 98.0, 116.0, 8, true);
	UStaticMesh* HaloRing = Art::RingMesh(this, 70.0, 128.0, 40);
	TelegraphMaterial = Glow(this, Hex(TEXT("#d66d50")) * 2.2f);
	HaloMaterial = Glow(this, Hex(TEXT("#fff5d9")) * 1.1f);
	FocusHaloMaterial = Glow(this, Hex(TEXT("#ffcf6a")) * 1.2f);
	if (HaloMaterial) HaloMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.2f);
	if (FocusHaloMaterial) FocusHaloMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.3f);
	for (int32 i = 0; i < 8; ++i)
	{
		UStaticMeshComponent* T = MakeMesh(TEXT("Telegraph"), Octagon, false);
		T->SetMaterial(0, TelegraphMaterial);
		TelegraphPool.Add(T);
		HaloPool.Add(MakeMesh(TEXT("Halo"), HaloRing, false));
		HaloWorn.Add(nullptr);
	}
	// The practice apple: a whole apple with its halo, hung in the local lane during the intro.
	PracticeMesh = MakeMesh(TEXT("Practice"), TargetMeshes[static_cast<int32>(EVariant::Apple) * 3]);
	for (int32 m = MaterialStart[0]; m < MaterialStart[1]; ++m)
	{
		PracticeMesh->SetMaterial(m - MaterialStart[0], TargetMaterials[m]);
	}
	PracticeHalo = MakeMesh(TEXT("PracticeHalo"), HaloRing, false);
	PracticeHalo->SetMaterial(0, FocusHaloMaterial);

	// The trail ribbon's topology: TrailSamples pairs, both faces; created once, then only its vertices move.
	TArray<int32> TrailTris;
	for (int32 k = 0; k < TrailSamples; ++k)
	{
		const double A = static_cast<double>(k) / (TrailSamples - 1);
		TrailVerts.Add(FVector::ZeroVector);
		TrailVerts.Add(FVector::ZeroVector);
		TrailNormals.Add(FVector(-1, 0, 0));
		TrailNormals.Add(FVector(-1, 0, 0));
		TrailUVs.Add(FVector2D(A, 0));
		TrailUVs.Add(FVector2D(A, 1));
		if (k > 0)
		{
			const int32 a = (k - 1) * 2, b = a + 1, c = k * 2 + 1, d = k * 2;
			TrailTris.Append({ a, b, c, a, c, d, a, c, b, a, d, c });
		}
	}
	UStaticMesh* Katana = Art::KatanaMesh(this);
	const TArray<UMaterialInterface*> KatanaMats = Art::KatanaMaterials(this);
	const TArray<UMaterialInterface*> LocalKatanaMats = FKartCosmetics::KatanaMaterials(this, true, KatanaMats); // store cosmetics
	for (int32 i = 0; i < N; ++i)
	{
		UArcadeAvatarComponent* A = NewObject<UArcadeAvatarComponent>(this, *FString::Printf(TEXT("Swordsman%d"), i));
		A->AvatarScale = 0.64f;
		A->SetupAttachment(RootComponent);
		A->RegisterComponent();
		A->SetLook(Seats[i].Look);
		Avatars.Add(A);
		UStaticMeshComponent* K = MakeMesh(TEXT("Katana"), Katana);
		const TArray<UMaterialInterface*>& Mats = Seats[i].Kind == ESwordKind::Local ? LocalKatanaMats : KatanaMats; // store cosmetics
		for (int32 Slot = 0; Slot < Mats.Num(); ++Slot)
		{
			K->SetMaterial(Slot, Mats[Slot]);
		}
		K->SetVisibility(true);
		Katanas.Add(K);
		UProceduralMeshComponent* Trail = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Trail%d"), i));
		Trail->SetupAttachment(RootComponent);
		Trail->SetCastShadow(false);
		Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Trail->bUseAsyncCooking = true;
		Trail->RegisterComponent();
		Trail->CreateMeshSection_LinearColor(0, TrailVerts, TrailTris, TrailNormals, TrailUVs, TrailColors, TrailTangents, false);
		// White-hot at heart, tinted toward the seat's colour so each lane's cuts read as its own.
		UMaterialInstanceDynamic* TrailMat = Glow(this, FMath::Lerp(FLinearColor(0.95f, 0.98f, 1.f), Seats[i].Color, 0.55f) * 2.4f);
		if (TrailMat) TrailMat->SetScalarParameterValue(TEXT("Opacity"), 0.42f);
		TrailMaterials.Add(TrailMat);
		Trail->SetMaterial(0, TrailMat);
		Trail->SetMeshSectionVisible(0, false);
		Trails.Add(Trail);
	}

	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SSamuraiHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), 10);
	}
	SetStage(ESamuraiStage::Intro);
}

void ASamuraiGame::EndPlay(const EEndPlayReason::Type Reason)
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

const FSamuraiSeat* ASamuraiGame::SeatOf(const FString& Id) const
{
	return Seats.FindByPredicate([&Id](const FSamuraiSeat& S) { return S.Id == Id; });
}

bool ASamuraiGame::HasLocal() const
{
	return Seats.ContainsByPredicate([](const FSamuraiSeat& S) { return S.Kind == ESwordKind::Local; });
}

bool ASamuraiGame::ScreenOf(const FVector& World, FVector2D& Out01) const
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

FSamuraiView ASamuraiGame::MakeView() const
{
	FSamuraiView View;
	const ULocalPlayer* LP = Viewer ? Viewer->GetLocalPlayer() : nullptr;
	if (!LP || !LP->ViewportClient || !LP->ViewportClient->Viewport)
	{
		return View;
	}
	FSceneViewProjectionData Projection;
	if (!LP->GetProjectionData(LP->ViewportClient->Viewport, Projection))
	{
		return View;
	}
	const FIntPoint Size = LP->ViewportClient->Viewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return View;
	}
	View.ViewProjection = Projection.ComputeViewProjectionMatrix();
	View.Rect = Projection.GetConstrainedViewRect();
	View.ViewportSize = FVector2D(Size.X, Size.Y);
	View.bValid = true;
	return View;
}

bool FSamuraiView::Project(const FVector& World, FVector2D& Out01) const
{
	FVector2D Pixels;
	if (!bValid || !FSceneView::ProjectWorldToScreen(World, Rect, ViewProjection, Pixels))
	{
		return false;
	}
	Out01 = Pixels / ViewportSize;
	return true;
}

void ASamuraiGame::SetStage(ESamuraiStage Next)
{
	Stage = Next;
	StageAt = Clock;
	if (Next == ESamuraiStage::Match && OnMatchStart)
	{
		OnMatchStart();
	}
	if (Next == ESamuraiStage::Finished)
	{
		Cues.Play(this, TEXT("match.over"));
	}
}

void ASamuraiGame::FeedPhone(const FString& PlayerId, const MovecadeKart::Net::FSensorFrame& Frame)
{
	FLane* Lane = Lanes.FindByPredicate([&PlayerId](const FLane& L) { return L.PlayerId == PlayerId; });
	if (!Lane)
	{
		return;
	}
	using namespace MovecadeKart::Net;
	// presence.ts / SamuraiSwordsman.tsx: the calibrated attitude is the katana; bladeIntent.ts gates the sweep.
	Lane->PhoneDir = Frame.bHasQuaternion
		? BladeDirectionFromQuaternion(FQuat(Frame.Quaternion.X, Frame.Quaternion.Y, Frame.Quaternion.Z, Frame.Quaternion.W))
		: BladeDirectionFromEuler(Frame.Alpha, Frame.Beta, Frame.Gamma);
	const double Rate = (Frame.Flags & SensorFlagHasGyro) ? Frame.Gyro.Size() : 0.0;
	const double Accel = (Frame.Flags & SensorFlagHasAccel) ? Frame.Accel.Size() : 0.0;
	Lane->bPhoneIntent = Lane->Intent.Feed(Rate, Accel, Frame.DtMs > 0 ? Frame.DtMs : 10.0);
	Lane->PhoneAt = Clock;
}

void ASamuraiGame::SampleBlade(FLane& Lane, int32 Index, double Dt)
{
	const double SimSeconds = Match.ElapsedMs / 1000.0 + (Match.Phase == EMatchPhase::Countdown ? -Match.CountdownRemainingMs / 1000.0 : 0.0);
	FVector Dir = Lane.Blade.Direction;
	bool bIntent = false;
	switch (Lane.Kind)
	{
	case ESwordKind::Local:
	{
		Dir = BladeDirectionToward(Lane.LaneX, Lane.TipGoal);
		// The tip's angular speed is the swing: the same gate as a phone's gyro (M9).
		const double Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Lane.Blade.Direction, Dir), -1.0, 1.0)));
		bIntent = Lane.Intent.Feed(Angle / FMath::Max(Dt, 1e-4), 0.0, Dt * 1000.0);
		break;
	}
	case ESwordKind::Phone:
		if (Clock - Lane.PhoneAt < 0.36)
		{
			Dir = Lane.PhoneDir;
			bIntent = Lane.bPhoneIntent;
		}
		else
		{
			Dir = FMath::Lerp(Lane.Blade.Direction, KamaeDirection(Lane.LaneX), 0.1).GetSafeNormal();
		}
		break;
	case ESwordKind::Bot:
	{
		const double T = (SimSeconds - Lane.SweepAt) / Lane.SweepSeconds;
		if (T >= 0.0 && T <= 1.0)
		{
			const double S = Smooth(0.0, 1.0, T);
			Dir = FQuat::Slerp(FQuat::FindBetweenNormals(FVector(0, 1, 0), Lane.SweepFrom), FQuat::FindBetweenNormals(FVector(0, 1, 0), Lane.SweepTo), S).RotateVector(FVector(0, 1, 0));
			bIntent = true;
		}
		else if (T < 0.0 && T > -1.5)
		{
			// The wind-up: draw the blade to where the cut starts.
			Dir = FMath::Lerp(Lane.Blade.Direction, Lane.SweepFrom, 0.35).GetSafeNormal();
		}
		else
		{
			Dir = FMath::Lerp(Lane.Blade.Direction, KamaeDirection(Lane.LaneX), 0.12).GetSafeNormal();
			if (SimSeconds >= Lane.NextThinkAt && Seats.IsValidIndex(Index))
			{
				PlanBot(Lane, Seats[Index]);
			}
		}
		break;
	}
	}
	Lane.Previous = Lane.Blade;
	Lane.Blade = BladeFor(Lane.LaneX, Dir);
	const double Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(Lane.Previous.Direction, Lane.Blade.Direction), -1.0, 1.0)) / FMath::Max(Dt, 1e-4);
	Lane.Energy = FMath::Lerp(Lane.Energy, FMath::Clamp(Turn / 12.0, 0.0, 1.0), 0.25);
	Lane.bIntent = bIntent;
}

void ASamuraiGame::PlanBot(FLane& Lane, const FSamuraiSeat& Seat)
{
	const double SimSeconds = Match.ElapsedMs / 1000.0;
	const double S = Seat.Skill;
	const double Window = 0.035 + 0.08 * (1.0 - S);
	const FTarget* Best = nullptr;
	for (const FTarget& T : Match.Targets)
	{
		if (T.PlayerId != Lane.PlayerId || T.Kind == EKind::Bomb || FMath::Abs(T.Progress - CutApex) > Window)
		{
			continue;
		}
		if (!Best || FMath::Abs(T.Progress - CutApex) < FMath::Abs(Best->Progress - CutApex))
		{
			Best = &T;
		}
	}
	if (!Best)
	{
		Lane.NextThinkAt = SimSeconds + 0.05;
		return;
	}
	const FVector Goal = TargetCentre(*Best, LaneCount());
	// A sharp bot waits out a bomb beside the fruit; a wild one sometimes doesn't look.
	for (const FTarget& T : Match.Targets)
	{
		if (T.PlayerId == Lane.PlayerId && T.Kind == EKind::Bomb && FVector::Dist(TargetCentre(T, LaneCount()), Goal) < 1.15 && BotRng.FRand() < 0.4 + 0.6 * S)
		{
			Lane.NextThinkAt = SimSeconds + 0.12;
			return;
		}
	}
	const double Side = BotRng.FRand() < 0.5 ? -1.0 : 1.0;
	Lane.SweepFrom = BladeDirectionToward(Lane.LaneX, Goal + FVector(Side * 0.4, 0.95, 0.0));
	Lane.SweepTo = BladeDirectionToward(Lane.LaneX, Goal + FVector(-Side * 0.4, -0.95, 0.0));
	Lane.SweepAt = SimSeconds + 0.12;
	Lane.SweepSeconds = 0.16;
	Lane.NextThinkAt = SimSeconds + 0.12 + 0.16 + 0.22 + (1.0 - S) * 0.35;
}

void ASamuraiGame::StepFixed(double DtMs, double Dt)
{
	StepMatch(Match, DtMs);
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		SampleBlade(Lane, i, Dt);
		if (Match.Phase != EMatchPhase::Playing)
		{
			continue;
		}
		if (!Lane.bIntent)
		{
			FeedbackForMisses(Lane, i);
			continue;
		}
		if (!Lane.bWasIntent)
		{
			Lane.bSweepHit = false;
		}
		Lane.bWasIntent = true;
		Lane.TrailAt = Clock;
		// bladeContact.ts on the sim clock: whatever the blade passed through since the last step.
		TSet<int32>& Hits = HitScratch;
		Hits.Reset();
		for (const FTarget& T : Match.Targets)
		{
			if (T.PlayerId == Lane.PlayerId && SweptBladeHits(Lane.Previous.Base, Lane.Previous.Tip, Lane.Blade.Base, Lane.Blade.Tip, TargetCentre(T, LaneCount()), ContactRadius(T)))
			{
				Hits.Add(T.Id);
			}
		}
		if (Hits.Num() > 0)
		{
			// The follow-through's side and strength, for the swordsman's cut pose (presentation only).
			const FVector Sweep = Lane.Blade.Tip - Lane.Previous.Tip;
			Lane.CutSide = Sweep.X >= 0.0 ? 1.0 : -1.0;
			Lane.CutPower = FMath::Clamp(Sweep.Size() / 0.12, 0.35, 1.0);
			ResolveSwing(Match, Lane.PlayerId, ContactSwing(), &Hits);
			Lane.LastCutAt = Clock;
			Lane.bSweepHit = true;
		}
	}
	for (const FEffect& E : Match.Effects)
	{
		if (E.Id > LastSeenEffectId)
		{
			LastSeenEffectId = E.Id;
			PresentEffect(E);
			if (OnEffect && E.Kind != EEffectKind::Slash)
			{
				OnEffect(E);
			}
		}
	}
}

void ASamuraiGame::FeedbackForMisses(FLane& Lane, int32 Index)
{
	// What the rules stay silent about, for the people holding a blade: a sweep that ended without touching any
	// fruit near it, and a blade dragged through fruit too slowly to count as a cut (the intent gate is closed).
	const bool bSweepEnded = Lane.bWasIntent;
	Lane.bWasIntent = false;
	if (!IsHuman(Index) || Clock - Lane.HintAt < 1.3)
	{
		return;
	}
	const FTarget* Touched = nullptr;
	const FTarget* Nearest = nullptr;
	double NearestDist = 1.25;
	for (const FTarget& T : Match.Targets)
	{
		if (T.LaneIndex != Index || T.Kind == EKind::Bomb)
		{
			continue;
		}
		const FVector Centre = TargetCentre(T, LaneCount());
		if (!Touched && SweptBladeHits(Lane.Previous.Base, Lane.Previous.Tip, Lane.Blade.Base, Lane.Blade.Tip, Centre, ContactRadius(T)))
		{
			Touched = &T;
		}
		const double Dist = FVector::Dist(FMath::ClosestPointOnSegment(Centre, Lane.Blade.Base, Lane.Blade.Tip), Centre);
		if (Dist < NearestDist)
		{
			NearestDist = Dist;
			Nearest = &T;
		}
	}
	const double Turn = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Lane.Previous.Direction, Lane.Blade.Direction), -1.0, 1.0))) * 60.0;
	if (Touched && Turn > 20.0)
	{
		Lane.HintAt = Clock;
		Note(W(TargetCentre(*Touched, LaneCount()) + FVector(0.0, 0.5, 0.0)), NSLOCTEXT("Samurai", "TooSlow", "Swing faster!"), MovecadeKart::Art::Hex(TEXT("#ffd477")), 1.1, 0.8f);
		Cues.Play(this, TEXT("samurai.miss"), 0.25f, 1.6f);
	}
	else if (bSweepEnded && !Lane.bSweepHit && Nearest)
	{
		Lane.HintAt = Clock;
		Note(W(TargetCentre(*Nearest, LaneCount()) + FVector(0.0, 0.5, 0.0)), NSLOCTEXT("Samurai", "Whiff", "Missed"), FLinearColor(0.78f, 0.8f, 0.86f), 0.8, 0.7f);
	}
}

bool ASamuraiGame::IsHuman(int32 LaneIndex) const
{
	return Lanes.IsValidIndex(LaneIndex) && Lanes[LaneIndex].Kind != ESwordKind::Bot;
}

bool ASamuraiGame::IsFeatured(int32 LaneIndex) const
{
	if (IsHuman(LaneIndex))
	{
		return true;
	}
	const bool bAnyHuman = Lanes.ContainsByPredicate([](const FLane& L) { return L.Kind != ESwordKind::Bot; });
	return !bAnyHuman && LaneIndex == 0;
}

void ASamuraiGame::Note(const FVector& World, const FText& Label, const FLinearColor& Color, double Life, float Scale)
{
	if (Notes.Num() >= 12)
	{
		Notes.RemoveAt(0);
	}
	FSamuraiNote& N = Notes.AddDefaulted_GetRef();
	N.World = World;
	N.Label = Label;
	N.Color = Color;
	N.BornAt = Clock;
	N.Life = Life;
	N.Scale = Scale;
}

void ASamuraiGame::Shout(const FText& Text, const FLinearColor& Color)
{
	Banner = Text;
	BannerColor = Color;
	BannerAt = Clock;
}

void ASamuraiGame::SlowMo(double Seconds)
{
	// A beat of slow motion (the whole round slows, rules included, so nothing desyncs); never back to back.
	if (RealClock - SlowAt > Seconds + 2.0)
	{
		SlowAt = RealClock;
		SlowSeconds = Seconds;
	}
}

void ASamuraiGame::Flash(const FLinearColor& Color, double Power, double Fringe)
{
	FlashAt = RealClock;
	FlashColor = Color;
	FlashPower = Power;
	FringePower = Fringe;
}

FVector ASamuraiGame::CutNormal(int32 LaneIndex) const
{
	if (!Lanes.IsValidIndex(LaneIndex))
	{
		return FVector::ZeroVector;
	}
	const FLane& Lane = Lanes[LaneIndex];
	const FVector Sweep = Lane.Blade.Tip - Lane.Previous.Tip;
	const FVector N = FVector::CrossProduct(Lane.Blade.Direction, Sweep);
	return N.Size() < 1e-4 ? FVector::ZeroVector : DirUE(N).GetSafeNormal();
}

void ASamuraiGame::CutFx(EVariant Variant, const FVector& At, const FVector& Normal, double Scale, bool bPerfect)
{
	UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this);
	if (!FxSys)
	{
		return;
	}
	const FLinearColor Juice = MovecadeKart::Art::Hex(Def(Variant).Color);
	TArray<Fx::FKartFxEmit> Out;
	// Two arcs of juice thrown off the cut faces, falling under gravity.
	for (const double Side : { -1.0, 1.0 })
	{
		Fx::FKartFxEmit Arc;
		Arc.Field = Fx::EField::Sparks;
		Arc.Position = At + Normal * Side * Scale * 30.0;
		Arc.Count = bPerfect ? 16 : 10;
		Arc.Direction = (Normal * Side + FVector(0.0, 0.0, 0.9)).GetSafeNormal();
		Arc.Spread = 0.55;
		Arc.Speed = FVector2D(2.2, 5.2);
		Arc.Life = FVector2D(0.35, 0.7);
		Arc.Size = FVector2D(0.025, 0.05);
		Arc.Color = Juice * 1.2f;
		Arc.ColorEnd = Juice * 0.6f;
		Arc.Intensity = 1.8;
		Arc.Jitter = 0.08;
		Out.Add(Arc);
	}
	// A spray of droplets all round, the pulp, and a soft splash of the fruit's colour.
	Fx::FKartFxEmit Spray;
	Spray.Field = Fx::EField::Sparks;
	Spray.Position = At;
	Spray.Count = 10;
	Spray.Spread = 1.5;
	Spray.Speed = FVector2D(1.2, 3.4);
	Spray.Life = FVector2D(0.3, 0.55);
	Spray.Size = FVector2D(0.018, 0.035);
	Spray.Color = Juice;
	Spray.ColorEnd = Juice * 0.7f;
	Spray.Intensity = 1.5;
	Out.Add(Spray);
	Fx::FKartFxEmit Pulp;
	Pulp.Field = Fx::EField::Smoke;
	Pulp.Position = At;
	Pulp.Count = 6;
	Pulp.Spread = 1.0;
	Pulp.Speed = FVector2D(0.4, 1.2);
	Pulp.Life = FVector2D(0.35, 0.6);
	Pulp.Size = FVector2D(0.08, 0.16);
	Pulp.SizeEnd = 1.6;
	Pulp.Color = Juice;
	Pulp.ColorEnd = Juice * 0.5f;
	Pulp.Intensity = 0.8;
	Out.Add(Pulp);
	Fx::FKartFxEmit Splash;
	Splash.Field = Fx::EField::Fire;
	Splash.Position = At;
	Splash.Count = 4;
	Splash.Spread = 1.2;
	Splash.Speed = FVector2D(0.3, 0.9);
	Splash.Life = FVector2D(0.18, 0.3);
	Splash.Size = FVector2D(0.18, 0.28);
	Splash.SizeEnd = 1.8;
	Splash.Color = Juice;
	Splash.ColorEnd = Juice * 0.3f;
	Splash.Intensity = 1.2;
	Out.Add(Splash);
	if (bPerfect)
	{
		// A star of light and a ring where the blade met the apex.
		static const FLinearColor Light = MovecadeKart::Art::Hex(TEXT("#fff5d9"));
		Fx::FKartFxEmit Star;
		Star.Field = Fx::EField::Flare;
		Star.Position = At;
		Star.Count = 1;
		Star.Speed = FVector2D::ZeroVector;
		Star.Life = FVector2D(0.24, 0.24);
		Star.Size = FVector2D(1.3, 1.3);
		Star.SizeEnd = 0.2;
		Star.Color = Light;
		Star.ColorEnd = Light;
		Star.Intensity = 4.0;
		Out.Add(Star);
		Fx::FKartFxEmit Glints;
		Glints.Field = Fx::EField::Flare;
		Glints.Position = At;
		Glints.Count = 6;
		Glints.Spread = 3.1;
		Glints.Speed = FVector2D(1.0, 2.4);
		Glints.Life = FVector2D(0.3, 0.5);
		Glints.Size = FVector2D(0.12, 0.2);
		Glints.SizeEnd = 0.1;
		Glints.Color = Light;
		Glints.ColorEnd = Juice;
		Glints.Intensity = 3.0;
		Out.Add(Glints);
	}
	FxSys->Emit(Out);
}

void ASamuraiGame::PresentEffect(const FEffect& E)
{
	const FPlayer* Player = Match.Players.IsValidIndex(E.LaneIndex) ? &Match.Players[E.LaneIndex] : Match.FindPlayer(E.PlayerId);
	const float P = static_cast<float>(E.Intensity);
	const bool bFeatured = IsFeatured(E.LaneIndex);
	switch (E.Kind)
	{
	case EEffectKind::Slash:
		Cues.Play(this, TEXT("samurai.slice"), 0.55f + 0.45f * P, 0.92f + 0.22f * P);
		break;
	case EEffectKind::Hit:
	case EEffectKind::Combo:
	{
		Cues.Play(this, E.Kind == EEffectKind::Combo ? TEXT("samurai.combo") : TEXT("samurai.hit"), 0.7f + 0.3f * P);
		const int32 Combo = Player ? Player->Combo : 0;
		if (Combo >= 2)
		{
			// The bell climbs a semitone a cut (sound.ts), up an octave.
			const int32 Step = FMath::Min(12, Combo - 2);
			Cues.Play(this, TEXT("samurai.combo"), 0.6f + 0.4f * Step / 12.f, FMath::Pow(2.f, Step / 12.f));
		}
		FrozenUntil = Clock + 0.078;
		Punch = 0.16 * (0.72 + 0.28 * P);
		Shake = FMath::Max(Shake, E.Kind == EEffectKind::Combo ? 0.32 : 0.16);
		const FVector Cut = CutNormal(E.LaneIndex);
		bool bAnyPerfect = false;
		for (const FTarget& T : E.Struck)
		{
			const FVector At = W(TargetCentre(T, LaneCount()));
			const int32* Slot = TargetSlots.Find(T.Id);
			FQuat Rot = Slot ? Slots[*Slot].Spin : FQuat::Identity;
			// The halves part along the plane the blade swept: turn the fruit so its split faces that way.
			if (!Cut.IsNearlyZero())
			{
				Rot = FQuat::FindBetweenNormals(Rot.RotateVector(FVector(0, 1, 0)), Cut) * Rot;
			}
			SpawnPiece(T, At, Rot, true);
			SpawnPiece(T, At, Rot, false);
			const bool bPerfect = CutQuality(T.Progress) >= 0.85 || E.Kind == EEffectKind::Combo;
			bAnyPerfect |= CutQuality(T.Progress) >= 0.92;
			CutFx(T.Variant, At, Rot.RotateVector(FVector(0, 1, 0)), T.Radius * 0.75, bPerfect);
		}
		if (bFeatured)
		{
			if (bAnyPerfect || E.Struck.Num() >= 2)
			{
				Flash(MovecadeKart::Art::Hex(TEXT("#fff2d6")), 0.55, 0.35);
				FovKick = -2.2;
				SlowMo(E.Struck.Num() >= 2 ? 0.6 : 0.42);
			}
			if (Combo >= 5 && Combo % 5 == 0)
			{
				Shout(FText::Format(NSLOCTEXT("Samurai", "ComboShout", "{0}x COMBO!"), FText::AsNumber(Combo)), Combo >= 10 ? MovecadeKart::Art::Hex(TEXT("#ffd477")) : MovecadeKart::Art::Hex(TEXT("#7cf0c4")));
				if (Dojo)
				{
					Dojo->PetalBurst(FVector(LaneX(E.LaneIndex, LaneCount()), 3.0, 0.8), 18 + Combo);
				}
			}
		}
		break;
	}
	case EEffectKind::Bomb:
		Cues.Play(this, TEXT("samurai.bomb"));
		FrozenUntil = Clock + 0.12;
		Punch = 0.3;
		Shake = FMath::Max(Shake, 0.8);
		Flash(MovecadeKart::Art::Hex(TEXT("#ff8a3d")), bFeatured ? 1.0 : 0.4, bFeatured ? 1.0 : 0.4);
		FovKick = 3.5;
		if (UKartFxSubsystem* FxSys = UKartFxSubsystem::Get(this))
		{
			for (const FTarget& T : E.Struck)
			{
				if (T.Kind == EKind::Bomb)
				{
					const FVector At = W(TargetCentre(T, LaneCount()));
					TArray<Fx::FKartFxEmit> Out;
					Fx::ExplosionBurst(At - FVector(0.0, 0.0, 30.0), 2.1, Out);
					// The shell's shards and embers flung at the camera.
					Fx::FKartFxEmit Shards;
					Shards.Field = Fx::EField::Sparks;
					Shards.Position = At;
					Shards.Count = 26;
					Shards.Direction = FVector(-1.0, 0.0, 0.6).GetSafeNormal();
					Shards.Spread = 1.3;
					Shards.Speed = FVector2D(4.0, 9.0);
					Shards.Life = FVector2D(0.4, 0.9);
					Shards.Size = FVector2D(0.03, 0.06);
					Shards.Color = MovecadeKart::Art::Hex(TEXT("#ffd08a"));
					Shards.ColorEnd = MovecadeKart::Art::Hex(TEXT("#ff4a1c"));
					Shards.Intensity = 3.0;
					Out.Add(Shards);
					FxSys->Emit(Out);
				}
			}
		}
		break;
	case EEffectKind::Miss:
		Cues.Play(this, TEXT("samurai.miss"), 0.85f);
		break;
	}
}

void ASamuraiGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	const float RealDt = FMath::Min(DeltaSeconds, 0.1f);
	RealClock += RealDt;
	// Slow motion: down to a third at once, easing back over the beat.
	const double SlowAge = RealClock - SlowAt;
	const double TimeScale = SlowAge < SlowSeconds ? FMath::Lerp(0.33, 1.0, Smooth(SlowSeconds * 0.3, SlowSeconds, SlowAge)) : 1.0;
	const float Dt = static_cast<float>(RealDt * TimeScale);
	Clock += Dt;
	// The local swordsman's tip: the mouse, the right (or left) stick, the keys.
	bool bSkip = false;
	if (Viewer)
	{
		float MouseX = 0.f, MouseY = 0.f;
		Viewer->GetInputMouseDelta(MouseX, MouseY);
		FVector2D Stick(Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightX) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftX),
			Viewer->GetInputAnalogKeyState(EKeys::Gamepad_RightY) + Viewer->GetInputAnalogKeyState(EKeys::Gamepad_LeftY));
		Stick.X += (Viewer->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (Viewer->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
		Stick.Y += (Viewer->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (Viewer->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
		for (FLane& Lane : Lanes)
		{
			if (Lane.Kind != ESwordKind::Local)
			{
				continue;
			}
			Lane.TipGoal.X = FMath::Clamp(Lane.TipGoal.X + MouseX * 0.0042 + Stick.X * 4.2 * RealDt, Lane.LaneX - 1.6, Lane.LaneX + 1.6);
			Lane.TipGoal.Y = FMath::Clamp(Lane.TipGoal.Y + MouseY * 0.0042 + Stick.Y * 4.2 * RealDt, 0.1, 3.1);
		}
		bSkip = Viewer->WasInputKeyJustPressed(EKeys::SpaceBar) || Viewer->WasInputKeyJustPressed(EKeys::Enter) || Viewer->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Bottom);
	}
	switch (Stage)
	{
	case ESamuraiStage::Intro:
	{
		for (int32 i = 0; i < Lanes.Num(); ++i)
		{
			SampleBlade(Lanes[i], i, Dt);
		}
		// The title card, then (the first round, with a local swordsman) the practice apple.
		const double Age = GetStageAge();
		if (Practice == ESamuraiPractice::None && !bPracticeDone && Age >= TitleSeconds - 0.6)
		{
			PracticeLane = Lanes.IndexOfByPredicate([](const FLane& L) { return L.Kind == ESwordKind::Local; });
			if (PracticeLane != INDEX_NONE)
			{
				Practice = ESamuraiPractice::Waiting;
				PracticeAt = Clock;
				PracticeWeb = FVector(Lanes[PracticeLane].LaneX + 0.15, TargetHeight(CutApex), TargetDepth);
				Lanes[PracticeLane].TipGoal = FVector(Lanes[PracticeLane].LaneX - 1.1, 2.9, TargetDepth);
			}
			else
			{
				bPracticeDone = true;
			}
		}
		if (Practice != ESamuraiPractice::None)
		{
			UpdatePractice(Dt);
			if (bSkip && Practice != ESamuraiPractice::Cut)
			{
				Practice = ESamuraiPractice::None;
				bPracticeDone = true;
			}
		}
		const bool bHolding = Practice != ESamuraiPractice::None;
		if (!bHolding && Age >= TitleSeconds)
		{
			PracticeMesh->SetVisibility(false);
			PracticeHalo->SetVisibility(false);
			SetStage(ESamuraiStage::Match);
		}
		break;
	}
	case ESamuraiStage::Match:
		Accumulator += Dt * 1000.0;
		while (Accumulator >= StepMs)
		{
			StepFixed(StepMs, StepMs / 1000.0);
			Accumulator -= StepMs;
		}
		if (Match.Phase == EMatchPhase::Ended)
		{
			SetStage(ESamuraiStage::Finished);
			UE_LOG(LogSamurai, Log, TEXT("Round over"));
			// Petals over the winners.
			for (const FPlayer& Winner : Match.Players)
			{
				if (Dojo && Match.WinnerIds.Contains(Winner.Id))
				{
					Dojo->PetalBurst(FVector(LaneX(Winner.LaneIndex, LaneCount()), 3.4, 1.0), 60);
				}
			}
			if (OnFinished)
			{
				OnFinished();
			}
		}
		break;
	case ESamuraiStage::Finished:
		for (int32 i = 0; i < Lanes.Num(); ++i)
		{
			SampleBlade(Lanes[i], i, Dt);
		}
		break;
	}
	// Focus: the moment a featured swordsman's Focus lights, a banner and a gust of petals.
	for (int32 i = 0; i < Lanes.Num() && i < Match.Players.Num(); ++i)
	{
		const bool bFocus = Match.Players[i].FocusRemainingMs > 0.0;
		if (bFocus && !Lanes[i].bWasFocus && IsFeatured(i) && Stage == ESamuraiStage::Match)
		{
			Shout(NSLOCTEXT("Samurai", "FocusShout", "FOCUS!"), MovecadeKart::Art::Hex(TEXT("#ffd477")));
			Flash(MovecadeKart::Art::Hex(TEXT("#ffd477")), 0.7, 0.2);
			if (Dojo)
			{
				Dojo->PetalBurst(FVector(Lanes[i].LaneX, 3.2, 0.8), 40);
			}
		}
		Lanes[i].bWasFocus = bFocus;
	}
	Notes.RemoveAll([this](const FSamuraiNote& N) { return Clock - N.BornAt > N.Life; });
	const bool bFrozen = Clock < FrozenUntil;
	if (!bFrozen)
	{
		UpdateTargets(Dt);
		UpdatePieces();
		UpdateSwordsmen(Dt);
	}
	UpdateTrails();
	UpdateMood(RealDt);
	UpdateCamera(Dt);
}

void ASamuraiGame::UpdatePractice(float Dt)
{
	// "Slice the practice fruit": the apple hangs at the cut's apex in the local lane; a sweep through it with
	// intent cuts it (the same contact test as a round, judged here: the rules never see it); a blade dragged
	// through too slowly says so. Space, Enter or the pad's A skips it; it gives way after a while regardless.
	FLane& Lane = Lanes[PracticeLane];
	const double Age = GetPracticeAge();
	PracticeSpin = FQuat(FVector::UpVector, 0.9 * Dt) * PracticeSpin;
	const FVector Bob(0.0, 0.05 * FMath::Sin(Clock * 2.2), 0.0);
	const double Scale = Def(EVariant::Apple).Radius * 0.75;
	if (Practice == ESamuraiPractice::Cut)
	{
		PracticeMesh->SetVisibility(false);
		PracticeHalo->SetVisibility(false);
		if (Age > 1.3)
		{
			Practice = ESamuraiPractice::None;
			bPracticeDone = true;
		}
		return;
	}
	const FVector At = W(PracticeWeb + Bob);
	const double Pop = EaseOutBack(FMath::Clamp(Age / 0.35, 0.0, 1.0));
	PracticeMesh->SetWorldTransform(FTransform(PracticeSpin, At, FVector(Scale * FMath::Max(0.01, Pop))));
	PracticeMesh->SetVisibility(true);
	PracticeHalo->SetWorldTransform(FTransform(FRotator::ZeroRotator, At + FVector(Scale * 60.0, 0.0, 0.0), FVector(Scale * (1.0 + 0.06 * FMath::Sin(Clock * 6.0)) * Pop)));
	PracticeHalo->SetVisibility(true);
	if (bPracticeDemo && Age > 1.6 && Age < 2.2)
	{
		// -SamuraiPracticeDemo (screenshots, tests): the local blade sweeps through the apple by itself.
		Lane.TipGoal = FVector(Lane.LaneX - 1.1 + FMath::Clamp((Age - 1.6) / 0.22, 0.0, 1.0) * 2.6, 2.9 - FMath::Clamp((Age - 1.6) / 0.22, 0.0, 1.0) * 1.6, TargetDepth);
	}
	if (Lane.bIntent)
	{
		Lane.TrailAt = Clock;
	}
	const bool bTouch = Age > 0.35 && SweptBladeHits(Lane.Previous.Base, Lane.Previous.Tip, Lane.Blade.Base, Lane.Blade.Tip, PracticeWeb + Bob, Def(EVariant::Apple).Radius * 0.75);
	if (bTouch && Lane.bIntent)
	{
		FTarget Fake;
		Fake.Variant = EVariant::Apple;
		Fake.Radius = Def(EVariant::Apple).Radius;
		const FVector Cut = CutNormal(PracticeLane);
		FQuat Rot = PracticeSpin;
		if (!Cut.IsNearlyZero())
		{
			Rot = FQuat::FindBetweenNormals(Rot.RotateVector(FVector(0, 1, 0)), Cut) * Rot;
		}
		SpawnPiece(Fake, At, Rot, true);
		SpawnPiece(Fake, At, Rot, false);
		CutFx(EVariant::Apple, At, Rot.RotateVector(FVector(0, 1, 0)), Scale, true);
		Cues.Play(this, TEXT("samurai.slice"), 1.f, 1.f);
		Cues.Play(this, TEXT("samurai.hit"), 1.f);
		Flash(MovecadeKart::Art::Hex(TEXT("#fff2d6")), 0.6, 0.3);
		FrozenUntil = Clock + 0.07;
		Punch = 0.16;
		SlowMo(0.5);
		Note(W(PracticeWeb + FVector(0.0, 0.55, 0.0)), NSLOCTEXT("Samurai", "PracticeCut", "Clean cut!"), MovecadeKart::Art::Hex(TEXT("#7cf0c4")), 1.2, 1.1f);
		UE_LOG(LogSamurai, Log, TEXT("Practice apple cut after %.2f s"), Age);
		Practice = ESamuraiPractice::Cut;
		PracticeAt = Clock;
		Lane.LastCutAt = Clock;
		Lane.CutPower = 1.0;
		return;
	}
	if (bTouch && Clock - Lane.HintAt > 1.0)
	{
		Lane.HintAt = Clock;
		Practice = ESamuraiPractice::TooSlow;
		Note(W(PracticeWeb + FVector(0.0, 0.55, 0.0)), NSLOCTEXT("Samurai", "TooSlow", "Swing faster!"), MovecadeKart::Art::Hex(TEXT("#ffd477")), 1.1, 0.9f);
	}
	if (Age > 12.0)
	{
		UE_LOG(LogSamurai, Log, TEXT("Practice timed out"));
		Practice = ESamuraiPractice::None;
		bPracticeDone = true;
		PracticeMesh->SetVisibility(false);
		PracticeHalo->SetVisibility(false);
	}
}

void ASamuraiGame::UpdateMood(float RealDt)
{
	// The screen treatment: Focus warms and saturates while a featured swordsman's Focus burns; perfect cuts and
	// bombs flash and fringe for a moment.
	bool bFocus = false;
	for (int32 i = 0; i < Match.Players.Num(); ++i)
	{
		bFocus |= IsFeatured(i) && Match.Players[i].FocusRemainingMs > 0.0 && Stage == ESamuraiStage::Match;
	}
	FocusMood = FMath::FInterpTo(FocusMood, bFocus ? 1.f : 0.f, RealDt, 4.f);
	const double FlashAge = RealClock - FlashAt;
	const float FlashNow = static_cast<float>(FlashPower * FMath::Exp(-9.0 * FMath::Max(0.0, FlashAge)));
	const float FringeNow = static_cast<float>(FringePower * FMath::Exp(-7.0 * FMath::Max(0.0, FlashAge)));
	FovKick *= FMath::Exp(-6.0 * RealDt);
	if (Dojo)
	{
		const float Pulse = FocusMood * (0.85f + 0.15f * FMath::Sin(static_cast<float>(RealClock) * 5.f));
		Dojo->SetMood(Pulse, FlashNow, FlashColor, FringeNow);
	}
}

void ASamuraiGame::UpdateTargets(float Dt)
{
	int32 Telegraphs = 0, Halos = 0;
	++SlotStamp;
	for (const FTarget& T : Match.Targets)
	{
		// Stable slots: a target keeps its component from launch to removal, dressed once.
		int32 SlotIndex = INDEX_NONE;
		if (const int32* Found = TargetSlots.Find(T.Id))
		{
			SlotIndex = *Found;
		}
		else
		{
			SlotIndex = Slots.IndexOfByPredicate([](const FTargetSlot& S) { return S.TargetId == INDEX_NONE; });
			if (SlotIndex == INDEX_NONE)
			{
				continue;
			}
			FTargetSlot& Fresh = Slots[SlotIndex];
			Fresh.TargetId = T.Id;
			Fresh.Spin = FQuat(FVector::UpVector, T.Id * 0.7);
			Fresh.Kind = T.Kind;
			Fresh.Lane = T.LaneIndex;
			TargetSlots.Add(T.Id, SlotIndex);
			const int32 V = static_cast<int32>(T.Variant);
			UStaticMeshComponent* Dress = TargetPool[SlotIndex];
			Dress->SetStaticMesh(TargetMeshes[V * 3]);
			for (int32 m = MaterialStart[V]; m < MaterialStart[V + 1]; ++m)
			{
				Dress->SetMaterial(m - MaterialStart[V], TargetMaterials[m]);
			}
			Dress->SetVisibility(true);
		}
		FTargetSlot& Slot = Slots[SlotIndex];
		Slot.Stamp = SlotStamp;
		Slot.LastProgress = T.Progress;
		// Spin on two axes (SamuraiScene.tsx): about up at 1.8x, about across at 1.08x the target's spin.
		Slot.Spin = FQuat(FVector::UpVector, T.Spin * 1.8 * Dt) * FQuat(FVector::RightVector, T.Spin * 1.08 * Dt) * Slot.Spin;
		const FVector At = W(TargetCentre(T, LaneCount()));
		Slot.LastAt = At;
		const double Scale = T.Radius * 0.75;
		TargetPool[SlotIndex]->SetWorldTransform(FTransform(Slot.Spin, At, FVector(Scale)));
		const FPlayer* Thrower = Match.Players.IsValidIndex(T.LaneIndex) ? &Match.Players[T.LaneIndex] : nullptr;
		const bool bFocus = Thrower && Thrower->FocusRemainingMs > 0.0;
		if (T.Kind == EKind::Bomb && Telegraphs < TelegraphPool.Num())
		{
			// The bomb's barred octagon, pulsing, between it and the camera.
			UStaticMeshComponent* R = TelegraphPool[Telegraphs++];
			const double Pulse = 1.16 + 0.035 * FMath::Sin(Clock * 7.0 + T.Id);
			R->SetWorldTransform(FTransform(FRotator::ZeroRotator, At - FVector(Scale * 110.0, 0.0, 0.0), FVector(Scale * Pulse / 1.16)));
			R->SetVisibility(true);
		}
		else if (T.Kind != EKind::Bomb && Halos < HaloPool.Num() && (T.Kind == EKind::Bonus || bFocus || CutQuality(T.Progress) >= 0.85))
		{
			// The halo: a scroll, a Focus lane, or the precise moment to cut.
			const int32 h = Halos++;
			UStaticMeshComponent* H = HaloPool[h];
			UMaterialInterface* Want = T.Kind == EKind::Bonus || bFocus ? FocusHaloMaterial : HaloMaterial;
			if (HaloWorn[h] != Want)
			{
				HaloWorn[h] = Want;
				H->SetMaterial(0, Want);
			}
			H->SetWorldTransform(FTransform(FRotator::ZeroRotator, At + FVector(Scale * 60.0, 0.0, 0.0), FVector(Scale)));
			H->SetVisibility(true);
		}
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].TargetId != INDEX_NONE && Slots[i].Stamp != SlotStamp)
		{
			ReleaseSlot(i);
		}
	}
	for (int32 i = Telegraphs; i < TelegraphPool.Num(); ++i) TelegraphPool[i]->SetVisibility(false);
	for (int32 i = Halos; i < HaloPool.Num(); ++i) HaloPool[i]->SetVisibility(false);
}

void ASamuraiGame::ReleaseSlot(int32 Index)
{
	FTargetSlot& Slot = Slots[Index];
	// A fruit that fell out of a human's lane uncut: the rules only speak up when it breaks a combo.
	if (Slot.LastProgress > 0.97 && Slot.Kind != EKind::Bomb && IsHuman(Slot.Lane) && Stage == ESamuraiStage::Match && Match.Phase == EMatchPhase::Playing)
	{
		Note(Slot.LastAt + FVector(0.0, 0.0, 40.0), NSLOCTEXT("Samurai", "Dropped", "Dropped"), MovecadeKart::Art::Hex(TEXT("#ff8a8a")), 0.8, 0.7f);
	}
	TargetSlots.Remove(Slot.TargetId);
	Slot.TargetId = INDEX_NONE;
	TargetPool[Index]->SetVisibility(false);
}

void ASamuraiGame::SpawnPiece(const FTarget& T, const FVector& At, const FQuat& Rotation, bool bPlus)
{
	// A ring over the pool: the oldest half gives way; each half is dressed once, at birth.
	const int32 Index = NextPiece;
	NextPiece = (NextPiece + 1) % Pieces.Num();
	FPiece& Piece = Pieces[Index];
	Piece.Variant = T.Variant;
	Piece.bPlus = bPlus;
	Piece.bLive = true;
	Piece.Origin = At;
	Piece.Rotation = Rotation;
	const FVector Normal = Rotation.RotateVector(FVector(0, 1, 0));
	Piece.Normal = bPlus ? Normal : -Normal;
	Piece.Scale = T.Radius * 0.75;
	Piece.BornAt = Clock;
	Piece.SpinSign = bPlus ? 1.0 : -1.0;
	const int32 V = static_cast<int32>(T.Variant);
	UStaticMeshComponent* C = PiecePool[Index];
	C->SetStaticMesh(TargetMeshes[V * 3 + (bPlus ? 1 : 2)]);
	for (int32 m = MaterialStart[V]; m < MaterialStart[V + 1]; ++m)
	{
		C->SetMaterial(m - MaterialStart[V], TargetMaterials[m]);
	}
	C->SetWorldTransform(FTransform(Rotation, At, FVector(Piece.Scale)));
	C->SetVisibility(true);
}

void ASamuraiGame::UpdatePieces()
{
	// cutEffects.ts: the halves part along the cut's normal, arc and spin, and shrink away at the end.
	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		FPiece& P = Pieces[i];
		if (!P.bLive)
		{
			continue;
		}
		UStaticMeshComponent* C = PiecePool[i];
		const double T = Clock - P.BornAt;
		if (T > PieceSeconds)
		{
			P.bLive = false;
			C->SetVisibility(false);
			continue;
		}
		const FVector Pos = P.Origin + P.Normal * (1.4 * T * 100.0) + FVector(0.0, 0.0, (0.7 * T - 1.6 * T * T) * 100.0);
		const FQuat Spin(P.Rotation.GetForwardVector(), P.SpinSign * 2.0 * T);
		const double Fade = 1.0 - Smooth(PieceSeconds - 0.18, PieceSeconds, T);
		C->SetWorldTransform(FTransform(Spin * P.Rotation, Pos, FVector(P.Scale * FMath::Max(0.01, Fade))));
	}
}

void ASamuraiGame::UpdateSwordsmen(float Dt)
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		const FLane& Lane = Lanes[i];
		UArcadeAvatarComponent* A = Avatars[i];
		// Standing behind the lane's targets, facing them (web -z is UE +X).
		A->SetWorldLocationAndRotation(W(FVector(Lane.LaneX, 0.0, FBladeRig::StandZ)), FRotator::ZeroRotator);
		FArcadePoseInput& Pose = A->Pose();
		auto Frame = [](const FVector& D) { return FVector(-D.Z, D.X, D.Y) / ArmLength; };
		const FBlade& B = Lane.Blade;
		Pose.Hand[1] = Frame(B.Hand - B.Shoulder);
		Pose.HandWeight[1] = 1.f;
		// The off hand low on the grip.
		const FVector LeftShoulder = B.Shoulder - FVector(2.0 * FBladeRig::ShoulderSide, 0.0, 0.0);
		Pose.Hand[0] = Frame(B.Hand - B.Direction * 0.13 - LeftShoulder);
		Pose.HandWeight[0] = 1.f;
		// The cut: a step into the stroke and a follow-through, snapping in and settling over a third of a second.
		const double CutAge = Clock - Lane.LastCutAt;
		const double Strike = CutAge < 0.0 || CutAge > 0.6 ? 0.0 : Lane.CutPower * Smooth(0.0, 0.05, CutAge) * FMath::Exp(-5.5 * CutAge);
		Pose.Crouch = static_cast<float>(0.22 + 0.15 * Lane.Energy + 0.22 * Strike);
		Pose.Lean = static_cast<float>(0.06 + 0.16 * Lane.Energy + FMath::Max(0.0, -B.Direction.Y) * 0.18 + 0.2 * Strike);
		Pose.Twist = static_cast<float>(-B.Direction.X * 0.35 - Lane.CutSide * 0.3 * Strike);
		Pose.Lunge = static_cast<float>(0.7 * Strike);
		// Eyes on the target nearest its apex in this lane.
		FVector LookAt = W(FVector(Lane.LaneX, 1.55, -1.2));
		double Nearest = TNumericLimits<double>::Max();
		for (const FTarget& T : Match.Targets)
		{
			if (T.PlayerId == Lane.PlayerId && FMath::Abs(T.Progress - CutApex) < Nearest)
			{
				Nearest = FMath::Abs(T.Progress - CutApex);
				LookAt = W(TargetCentre(T, LaneCount()));
			}
		}
		Pose.Look = (LookAt - A->GetComponentLocation() - FVector(0.0, 0.0, 105.0)).GetSafeNormal();
		Pose.LookWeight = 0.8f;
		// Reactions: the latest effect in this lane.
		float Cheer = 0.f, Slump = 0.f;
		for (const FEffect& E : Match.Effects)
		{
			if (E.PlayerId != Lane.PlayerId)
			{
				continue;
			}
			const double Age = (E.DurationMs - E.TtlMs) / 1000.0;
			const float In = static_cast<float>(Smooth(0.0, 0.12, Age) * Smooth(E.DurationMs / 1000.0, E.DurationMs / 1000.0 - 0.25, Age));
			if (E.Kind == EEffectKind::Bomb) Slump = FMath::Max(Slump, In);
			if (E.Kind == EEffectKind::Combo) Cheer = FMath::Max(Cheer, 0.5f * In);
		}
		if (Stage == ESamuraiStage::Finished)
		{
			Cheer = FMath::Max(Cheer, Match.WinnerIds.Contains(Lane.PlayerId) ? 0.85f : 0.f);
			Slump = FMath::Max(Slump, Match.WinnerIds.Contains(Lane.PlayerId) ? 0.f : 0.5f);
		}
		Pose.Cheer = Cheer;
		Pose.Slump = Slump;
		Pose.Idle = 1.f;
		// The katana in the hand, along the blade, its edge leading the cut.
		const FVector DirW = DirUE(B.Direction).GetSafeNormal();
		const FVector EdgeHint = (DirUE(FVector(0.0, -0.4, -1.0)) - DirW * FVector::DotProduct(DirUE(FVector(0.0, -0.4, -1.0)), DirW)).GetSafeNormal();
		Katanas[i]->SetWorldLocationAndRotation(W(B.Hand), FRotationMatrix::MakeFromXZ(DirW, EdgeHint.IsNearlyZero() ? FVector::UpVector : EdgeHint).Rotator());
	}
}

void ASamuraiGame::UpdateTrails()
{
	// bladeTrail.tsx: a ribbon over the last dozen blade samples while the sweep is meant, fading 100 ms after.
	// The section's topology is fixed (TrailSamples pairs): only the vertices move, and a short trail repeats its
	// oldest sample (degenerate quads) rather than rebuilding the section.
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		FLane& Lane = Lanes[i];
		const bool bLive = Clock - Lane.TrailAt < 0.1;
		if (bLive)
		{
			Lane.Trail.Add(TPair<FVector, FVector>(W(FMath::Lerp(Lane.Blade.Base, Lane.Blade.Tip, 0.55)), W(Lane.Blade.Tip)));
			if (Lane.Trail.Num() > TrailSamples)
			{
				Lane.Trail.RemoveAt(0, Lane.Trail.Num() - TrailSamples, EAllowShrinking::No);
			}
		}
		else if (Lane.Trail.Num() > 0 && Clock - Lane.TrailAt > 0.12)
		{
			Lane.Trail.Reset();
		}
		UProceduralMeshComponent* Mesh = Trails[i];
		const int32 Num = Lane.Trail.Num();
		if (Num < 2)
		{
			if (Lane.bTrailShown)
			{
				Mesh->SetMeshSectionVisible(0, false);
				Lane.bTrailShown = false;
			}
			continue;
		}
		const int32 Pad = TrailSamples - Num;
		for (int32 k = 0; k < TrailSamples; ++k)
		{
			// Thinner toward the old end.
			const int32 j = FMath::Max(0, k - Pad);
			const double A = static_cast<double>(j) / (Num - 1);
			TrailVerts[k * 2] = FMath::Lerp(Lane.Trail[j].Value, Lane.Trail[j].Key, A);
			TrailVerts[k * 2 + 1] = Lane.Trail[j].Value;
		}
		Mesh->UpdateMeshSection_LinearColor(0, TrailVerts, TrailNormals, TrailUVs, TrailColors, TrailTangents, false);
		if (!Lane.bTrailShown)
		{
			Mesh->SetMeshSectionVisible(0, true);
			Lane.bTrailShown = true;
		}
	}
}

void ASamuraiGame::UpdateCamera(float Dt)
{
	// cameraDirector.ts arena, fitted to the lanes; the round-start sweep and the finale push-in.
	const double Aspect = [this]()
	{
		FVector2D Size(16.0, 9.0);
		if (const UGameViewportClient* V = GetWorld()->GetGameViewport())
		{
			V->GetViewportSize(Size);
		}
		return Size.Y > 0.0 ? Size.X / Size.Y : 16.0 / 9.0;
	}();
	const double HalfW = (LaneCount() - 1) * 1.8 + 1.35;
	const double Fit = HalfW / (FMath::Tan(FMath::DegreesToRadians(45.0 / 2.0)) * Aspect * 0.98) + 0.85;
	const double Z = FMath::Max(9.2, Fit);
	FVector Pos(FMath::Sin(Clock * 0.24) * 0.22, 3.2 + 0.1 * FMath::Sin(Clock * 0.31), Z), Look(0.0, 1.9, 0.0);
	double Fov = 45.0;
	if (Stage == ESamuraiStage::Intro)
	{
		const double T = Smooth(0.0, 1.0, GetStageAge() / 2.6);
		Pos = FMath::Lerp(FVector(5.5, 6.2, Z + 6.0), Pos, T);
		Look = FMath::Lerp(FVector(0.0, 2.6, -3.0), Look, T);
		Fov = FMath::Lerp(52.0, 45.0, T);
	}
	else if (Stage == ESamuraiStage::Finished)
	{
		const double T = Smooth(0.0, 1.0, GetStageAge() / 3.2);
		Pos = FMath::Lerp(Pos, FVector(0.0, 2.4, Z * 0.72), T);
		Look = FMath::Lerp(Look, FVector(0.0, 1.5, 0.6), T);
	}
	// Focus leans in a touch; perfect cuts and bombs kick the lens.
	Fov += FovKick - 1.6 * FocusMood;
	if (!bCamPlaced)
	{
		CamPos = Pos;
		CamLook = Look;
		CamFov = Fov;
		bCamPlaced = true;
	}
	const double K = 1.0 - FMath::Exp(-5.5 * FMath::Min<double>(Dt, 1.0 / 20.0));
	CamPos += (Pos - CamPos) * K;
	CamLook += (Look - CamLook) * K;
	CamFov += (Fov - CamFov) * (1.0 - FMath::Exp(-6.0 * Dt));
	// impact.ts: shake decays e^-9t, the punch e^-14t (toward the scene).
	Shake *= FMath::Exp(-9.0 * Dt);
	Punch *= FMath::Exp(-14.0 * Dt);
	const FVector Jolt(FMath::Sin(Clock * 61.0) * Shake * 0.06, FMath::Sin(Clock * 53.0 + 1.1) * Shake * 0.05, -Punch);
	const FVector From = W(CamPos + Jolt);
	const FVector To = W(CamLook);
	Camera->SetActorLocationAndRotation(From, (To - From).Rotation());
	const double HFov = 2.0 * FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CamFov) / 2.0) * Aspect));
	Camera->GetCameraComponent()->SetFieldOfView(static_cast<float>(HFov));
}

void ASamuraiGame::ResetPresentation()
{
	LastSeenEffectId = 0;
	Notes.Reset();
	BannerAt = -10.0;
	FocusMood = 0.f;
	for (FLane& Lane : Lanes)
	{
		Lane.bWasFocus = false;
		Lane.bWasIntent = false;
		Lane.LastCutAt = -10.0;
	}
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].TargetId != INDEX_NONE)
		{
			ReleaseSlot(i);
		}
	}
	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		Pieces[i].bLive = false;
		PiecePool[i]->SetVisibility(false);
	}
}

void ASamuraiGame::Rematch()
{
	TArray<FSeed> MatchSeeds;
	for (const FSamuraiSeat& Seat : Seats)
	{
		MatchSeeds.Add({ Seat.Id, Seat.Name });
	}
	Match = CreateMatch(MatchSeeds, ++MatchSeed);
	ResetPresentation();
	Accumulator = 0.0;
	for (FLane& Lane : Lanes)
	{
		Lane.NextThinkAt = 0.6;
		Lane.SweepAt = -10.0;
		Lane.Intent.Reset();
	}
	SetStage(ESamuraiStage::Intro);
}

void ASamuraiGame::ArcadeRestart()
{
	if (OnRematch) OnRematch(); else Rematch();
}

void ASamuraiGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (const MovecadeKart::Samurai::FPlayer& P : Match.Players)
	{
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		const FSamuraiSeat* Seat = SeatOf(P.Id);
		Row.Name = P.Name;
		Row.Color = Seat ? Seat->Color : FLinearColor::White;
		Row.bHuman = Seat && Seat->Kind != ESwordKind::Bot;
		Row.Score = P.Score;
	}
	RankByScore(Out);
}
