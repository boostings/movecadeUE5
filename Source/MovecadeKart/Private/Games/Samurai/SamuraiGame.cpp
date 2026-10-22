// ASamuraiGame: setup, the round's flow (intro, match, finish) and its frame. The other jobs are in
// SamuraiGameBlades.cpp, SamuraiGamePresentation.cpp, SamuraiGameCamera.cpp and SamuraiGamePractice.cpp.

#include "Games/Samurai/SamuraiGame.h"

#include "Avatar/ArcadeAvatar.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Games/Samurai/SamuraiArt.h"
#include "Games/Samurai/SamuraiDojo.h"
#include "Games/Samurai/SamuraiLog.h"
#include "Games/Samurai/SamuraiTuning.h"
#include "Games/Samurai/SSamuraiHud.h"
#include "KartCoords.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Store/KartCosmetics.h"

DEFINE_LOG_CATEGORY(LogSamurai);

using namespace MovecadeKart::Samurai;
using namespace MovecadeKart::Samurai::Tuning;

namespace
{
	constexpr int32 VariantCount = 6;
	/** Whole, plus half, minus half (Art::EPart). */
	constexpr int32 PartCount = 3;
	/** Under SArcadeOverlay (30): pause and results draw over the round's HUD. */
	constexpr int32 HudZOrder = 10;
}

ASamuraiGame::ASamuraiGame()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	BotRng.Initialize(BotRngSeed);
}

FVector ASamuraiGame::ToWorld(const FVector& Web)
{
	return MovecadeKart::Coords::ToUE(Web);
}

// ---- Setup ----------------------------------------------------------------------------------------------------

void ASamuraiGame::Setup(ASamuraiDojo* InDojo, const TArray<FSamuraiSeat>& InSeats, APlayerController* InViewer, uint32 Seed,
	const FSamuraiDebugFlags& InDebugFlags)
{
	check(GetWorld());
	Dojo = InDojo;
	Seats = InSeats;
	Viewer = InViewer;
	MatchSeed = Seed;
	DebugFlags = InDebugFlags;
	if (Dojo)
	{
		Dojo->SetLanes(Seats.Num());
	}
	SetupLanes();
	Match = NewMatch(Seed);
	SetupProps();
	SetupTargetPools();
	TArray<int32> TrailTriangles;
	SetupTrailTopology(TrailTriangles);
	SetupSwordsmen(TrailTriangles);
	SetupCameraAndHud();
	SetStage(ESamuraiStage::Intro);
}

void ASamuraiGame::SetupLanes()
{
	const int32 Count = Seats.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		FLane& Lane = Lanes.AddDefaulted_GetRef();
		Lane.PlayerId = Seats[i].Id;
		Lane.Kind = Seats[i].Kind;
		Lane.LaneX = LaneX(i, Count);
		Lane.TipGoal = FVector(Lane.LaneX + TipStartOffsetX, TipStartY, TargetDepth);
		Lane.Blade = BladeFor(Lane.LaneX, KamaeDirection(Lane.LaneX));
		Lane.SweepSeconds = BotInitialSweepSeconds;
		Lane.NextThinkAt = BotFirstThinkSeconds + i * BotThinkStagger;
	}
}

FMatch ASamuraiGame::NewMatch(uint32 Seed) const
{
	TArray<FSeed> MatchSeeds;
	for (const FSamuraiSeat& Seat : Seats)
	{
		MatchSeeds.Add({ Seat.Id, Seat.Name });
	}
	return CreateMatch(MatchSeeds, Seed);
}

void ASamuraiGame::SetupProps()
{
	// Each target whole and halved, and one run of slot materials per variant (the halves share the whole's).
	for (int32 V = 0; V < VariantCount; ++V)
	{
		const EVariant Variant = static_cast<EVariant>(V);
		for (int32 Part = 0; Part < PartCount; ++Part)
		{
			TargetMeshes.Add(Art::TargetMesh(this, Variant, static_cast<Art::EPart>(Part)));
		}
		MaterialStart.Add(TargetMaterials.Num());
		for (UMaterialInterface* Material : Art::TargetMaterials(this, Variant))
		{
			TargetMaterials.Add(Material);
		}
	}
	MaterialStart.Add(TargetMaterials.Num());
}

UStaticMeshComponent* ASamuraiGame::MakePropComponent(const TCHAR* Name, UStaticMesh* Mesh, bool bShadows)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
	Component->SetStaticMesh(Mesh);
	Component->SetupAttachment(RootComponent);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(bShadows);
	Component->SetVisibility(false);
	Component->RegisterComponent();
	return Component;
}

void ASamuraiGame::SetupTargetPools()
{
	using namespace MovecadeKart::Art;
	for (int32 i = 0; i < TargetPoolSize; ++i)
	{
		TargetPool.Add(MakePropComponent(TEXT("Target"), nullptr));
	}
	Slots.SetNum(TargetPoolSize);
	// The halves live 0.65 s: no shadows (52 shadowed multi-section casters otherwise).
	for (int32 i = 0; i < PiecePoolSize; ++i)
	{
		PiecePool.Add(MakePropComponent(TEXT("Piece"), nullptr, false));
	}
	Pieces.SetNum(PiecePoolSize);

	UStaticMesh* Octagon = Art::RingMesh(this, TelegraphInnerCm, TelegraphOuterCm, 8, /*bBar*/ true);
	UStaticMesh* HaloRing = Art::RingMesh(this, HaloInnerCm, HaloOuterCm, HaloSegments);
	// Translucent on purpose: the halos fade (opacity below 1) and the telegraph reads over the bomb behind it.
	TelegraphMaterial = Glow(this, Palette::Telegraph * TelegraphGlow);
	HaloMaterial = Glow(this, Palette::HaloLight * HaloGlow);
	FocusHaloMaterial = Glow(this, Palette::FocusHalo * FocusHaloGlow);
	if (HaloMaterial)
	{
		HaloMaterial->SetScalarParameterValue(TEXT("Opacity"), HaloOpacity);
	}
	if (FocusHaloMaterial)
	{
		FocusHaloMaterial->SetScalarParameterValue(TEXT("Opacity"), FocusHaloOpacity);
	}
	for (int32 i = 0; i < MarkerPoolSize; ++i)
	{
		UStaticMeshComponent* Telegraph = MakePropComponent(TEXT("Telegraph"), Octagon, false);
		Telegraph->SetMaterial(0, TelegraphMaterial);
		TelegraphPool.Add(Telegraph);
		HaloPool.Add(MakePropComponent(TEXT("Halo"), HaloRing, false));
		HaloWorn.Add(nullptr);
	}
	// The practice apple: a whole apple with its halo, hung in the local lane during the intro.
	PracticeMesh = MakePropComponent(TEXT("Practice"), nullptr);
	Dress(PracticeMesh, EVariant::Apple, static_cast<int32>(Art::EPart::Whole));
	PracticeHalo = MakePropComponent(TEXT("PracticeHalo"), HaloRing, false);
	PracticeHalo->SetMaterial(0, FocusHaloMaterial);
}

void ASamuraiGame::SetupTrailTopology(TArray<int32>& OutTriangles)
{
	// TrailSamples pairs (inner edge, tip), both faces; created once, then only the vertices move.
	for (int32 k = 0; k < TrailSamples; ++k)
	{
		const double Along = static_cast<double>(k) / (TrailSamples - 1);
		TrailVerts.Add(FVector::ZeroVector);
		TrailVerts.Add(FVector::ZeroVector);
		TrailNormals.Add(FVector(-1, 0, 0));
		TrailNormals.Add(FVector(-1, 0, 0));
		TrailUVs.Add(FVector2D(Along, 0));
		TrailUVs.Add(FVector2D(Along, 1));
		if (k > 0)
		{
			const int32 a = (k - 1) * 2, b = a + 1, c = k * 2 + 1, d = k * 2;
			OutTriangles.Append({ a, b, c, a, c, d, a, c, b, a, d, c });
		}
	}
}

void ASamuraiGame::SetupSwordsmen(const TArray<int32>& TrailTriangles)
{
	UStaticMesh* Katana = Art::KatanaMesh(this);
	const TArray<UMaterialInterface*> KatanaMaterials = Art::KatanaMaterials(this);
	// The store's katana skin is the local player's; everyone else swings the plain blade.
	const TArray<UMaterialInterface*> LocalKatanaMaterials = FKartCosmetics::KatanaMaterials(this, true, KatanaMaterials);
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		UArcadeAvatarComponent* Avatar = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), TEXT("Swordsman")));
		Avatar->AvatarScale = SwordsmanScale;
		Avatar->SetupAttachment(RootComponent);
		Avatar->RegisterComponent();
		Avatar->SetLook(Seats[i].Look);
		Avatars.Add(Avatar);

		UStaticMeshComponent* Blade = MakePropComponent(TEXT("Katana"), Katana);
		const TArray<UMaterialInterface*>& Materials = Seats[i].Kind == ESwordKind::Local ? LocalKatanaMaterials : KatanaMaterials;
		for (int32 Slot = 0; Slot < Materials.Num(); ++Slot)
		{
			Blade->SetMaterial(Slot, Materials[Slot]);
		}
		Blade->SetVisibility(true);
		Katanas.Add(Blade);

		UProceduralMeshComponent* Trail = NewObject<UProceduralMeshComponent>(this, MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(), TEXT("Trail")));
		Trail->SetupAttachment(RootComponent);
		Trail->SetCastShadow(false);
		Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Trail->bUseAsyncCooking = true;
		Trail->RegisterComponent();
		Trail->CreateMeshSection_LinearColor(0, TrailVerts, TrailTriangles, TrailNormals, TrailUVs, TrailColors, TrailTangents, false);
		// Translucent (it fades): white-hot at heart, tinted toward the seat's colour so each lane's cuts read as its own.
		UMaterialInstanceDynamic* TrailMaterial = MovecadeKart::Art::Glow(this, FMath::Lerp(Palette::TrailCore, Seats[i].Color, TrailTint) * TrailGlow);
		if (TrailMaterial)
		{
			TrailMaterial->SetScalarParameterValue(TEXT("Opacity"), TrailOpacity);
		}
		TrailMaterials.Add(TrailMaterial);
		Trail->SetMaterial(0, TrailMaterial);
		Trail->SetMeshSectionVisible(0, false);
		Trails.Add(Trail);
	}
}

void ASamuraiGame::SetupCameraAndHud()
{
	Camera = GetWorld()->SpawnActor<ACameraActor>();
	if (!ensureMsgf(Camera, TEXT("Samurai: could not spawn the round's camera")))
	{
		return;
	}
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	if (Viewer)
	{
		Viewer->SetViewTarget(Camera);
	}
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Hud = SNew(SSamuraiHud).Game(this);
		Viewport->AddViewportWidgetContent(Hud.ToSharedRef(), HudZOrder);
	}
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

// ---- Queries ----------------------------------------------------------------------------------------------------

const FSamuraiSeat* ASamuraiGame::SeatOf(const FString& Id) const
{
	return Seats.FindByPredicate([&Id](const FSamuraiSeat& Seat) { return Seat.Id == Id; });
}

bool ASamuraiGame::HasLocal() const
{
	return Seats.ContainsByPredicate([](const FSamuraiSeat& Seat) { return Seat.Kind == ESwordKind::Local; });
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
	const bool bAnyHuman = Lanes.ContainsByPredicate([](const FLane& Lane) { return Lane.Kind != ESwordKind::Bot; });
	return !bAnyHuman && LaneIndex == 0;
}

// ---- Flow ---------------------------------------------------------------------------------------------------------

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

void ASamuraiGame::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bPaused)
	{
		UpdateCamera(DeltaSeconds);
		return;
	}
	float RealDt = 0.f;
	const float Dt = AdvanceClocks(DeltaSeconds, RealDt);
	const bool bSkip = ReadLocalInput(RealDt);
	switch (Stage)
	{
	case ESamuraiStage::Intro:
		TickIntro(Dt, bSkip);
		break;
	case ESamuraiStage::Match:
		TickMatch(Dt);
		break;
	case ESamuraiStage::Finished:
		SampleAllBlades(Dt);
		break;
	}
	WatchFocus();
	UpdatePresentation(Dt, RealDt);
}

float ASamuraiGame::AdvanceClocks(float DeltaSeconds, float& OutRealDt)
{
	OutRealDt = FMath::Min(DeltaSeconds, MaxFrameSeconds);
	RealClock += OutRealDt;
	// Slow motion: the whole round slows (rules included, so nothing desyncs), down to SlowMoScale at once and
	// easing back over the beat.
	const double SlowAge = RealClock - SlowAt;
	const double TimeScale = SlowAge < SlowSeconds ? FMath::Lerp(SlowMoScale, 1.0, Ease::Smooth(SlowSeconds * SlowMoEaseFrom, SlowSeconds, SlowAge)) : 1.0;
	const float Dt = static_cast<float>(OutRealDt * TimeScale);
	Clock += Dt;
	return Dt;
}

void ASamuraiGame::SampleAllBlades(double DtSeconds)
{
	for (int32 i = 0; i < Lanes.Num(); ++i)
	{
		SampleBlade(Lanes[i], i, DtSeconds);
	}
}

void ASamuraiGame::TickIntro(float Dt, bool bSkip)
{
	// The blades move (the swordsmen settle into their guard) but nothing scores until the match.
	SampleAllBlades(Dt);
	// The title card, then (the first round, with a local swordsman) the practice apple.
	const double Age = GetStageAge();
	if (Practice == ESamuraiPractice::None && !bPracticeDone && Age >= TitleSeconds - PracticeLeadSeconds)
	{
		BeginPractice();
	}
	if (Practice != ESamuraiPractice::None)
	{
		UpdatePractice(Dt);
		if (bSkip && Practice != ESamuraiPractice::Cut)
		{
			EndPractice();
		}
	}
	if (Practice == ESamuraiPractice::None && Age >= TitleSeconds)
	{
		PracticeMesh->SetVisibility(false);
		PracticeHalo->SetVisibility(false);
		SetStage(ESamuraiStage::Match);
	}
}

void ASamuraiGame::TickMatch(float Dt)
{
	Accumulator += Dt * 1000.0;
	while (Accumulator >= StepMs)
	{
		StepFixed(StepMs, StepMs / 1000.0);
		Accumulator -= StepMs;
	}
	if (Match.Phase == EMatchPhase::Ended)
	{
		FinishRound();
	}
}

void ASamuraiGame::FinishRound()
{
	SetStage(ESamuraiStage::Finished);
	UE_LOG(LogSamurai, Log, TEXT("Round over"));
	// Petals over the winners.
	for (const FPlayer& Winner : Match.Players)
	{
		if (Dojo && Match.WinnerIds.Contains(Winner.Id))
		{
			Dojo->PetalBurst(FVector(LaneX(Winner.LaneIndex, LaneCount()), WinnerPetalY, WinnerPetalZ), WinnerPetals);
		}
	}
	if (OnFinished)
	{
		OnFinished();
	}
}

void ASamuraiGame::WatchFocus()
{
	// The moment a featured swordsman's Focus lights: a banner, a flash and a gust of petals.
	for (int32 i = 0; i < Lanes.Num() && i < Match.Players.Num(); ++i)
	{
		const bool bFocus = Match.Players[i].FocusRemainingMs > 0.0;
		if (bFocus && !Lanes[i].bWasFocus && IsFeatured(i) && Stage == ESamuraiStage::Match)
		{
			Shout(NSLOCTEXT("Samurai", "FocusShout", "FOCUS!"), Palette::Gold);
			Flash(Palette::Gold, FocusFlashPower, FocusFlashFringe);
			if (Dojo)
			{
				Dojo->PetalBurst(FVector(Lanes[i].LaneX, FocusPetalY, FocusPetalZ), FocusPetals);
			}
		}
		Lanes[i].bWasFocus = bFocus;
	}
}

void ASamuraiGame::Rematch()
{
	Match = NewMatch(++MatchSeed);
	ResetPresentation();
	Accumulator = 0.0;
	for (FLane& Lane : Lanes)
	{
		// A rematch doesn't stagger the bots' first look (the first round's SetupLanes does).
		Lane.NextThinkAt = BotFirstThinkSeconds;
		Lane.SweepAt = -10.0;
		Lane.Intent.Reset();
	}
	SetStage(ESamuraiStage::Intro);
}

// ---- IArcadeGame --------------------------------------------------------------------------------------------------

void ASamuraiGame::ArcadeRestart()
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

void ASamuraiGame::GetArcadeStandings(TArray<FArcadeStanding>& Out) const
{
	for (const FPlayer& Player : Match.Players)
	{
		FArcadeStanding& Row = Out.AddDefaulted_GetRef();
		const FSamuraiSeat* Seat = SeatOf(Player.Id);
		Row.Name = Player.Name;
		Row.Color = Seat ? Seat->Color : FLinearColor::White;
		Row.bHuman = Seat && Seat->Kind != ESwordKind::Bot;
		Row.Score = Player.Score;
	}
	RankByScore(Out);
}
