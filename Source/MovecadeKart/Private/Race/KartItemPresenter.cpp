#include "Race/KartItemPresenter.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "FX/KartFx.h"
#include "FX/KartFxComponent.h"
#include "FX/KartFxSubsystem.h"
#include "FX/KartItemMeshes.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "NiagaraComponent.h"
#include "Race/KartRaceManager.h"
#include "TrackBuilder.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

namespace
{
	double EaseOut(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return 1.0 - FMath::Pow(1.0 - X, 3.0);
	}

	/** 0 -> 1 with a springy overshoot (easeOutBack): the "pop" of boxes and pop-ups. */
	double Pop(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		constexpr double C1 = 2.4;
		constexpr double C3 = C1 + 1.0;
		return 1.0 + C3 * FMath::Pow(X - 1.0, 3.0) + C1 * FMath::Pow(X - 1.0, 2.0);
	}

	constexpr double BlastSeconds = 0.8;
	constexpr double BoxSize = 200.0;       // MultiplayerPowerups.tsx BOX_SIZE 2 m
	constexpr double PopupSeconds = 0.9;
	constexpr double StrikeSeconds = 0.35;
	constexpr double StrikeHeight = 1100.0;
	constexpr int32 StrikePool = 4;

	/** Items that appear in the world as projectiles; the others pop up over the kart that uses them. */
	bool IsThrown(EKartItem Item)
	{
		return Item == EKartItem::Shot || Item == EKartItem::Seeker || Item == EKartItem::SkySeeker || Item == EKartItem::Banana || Item == EKartItem::Bomb;
	}

	const TCHAR* const ShowcaseBeats[] = { TEXT("explosion"), TEXT("lightning"), TEXT("prism"), TEXT("rocket"), TEXT("ink"), TEXT("hit"), TEXT("confetti"), TEXT("box") };
	constexpr int32 NumShowcaseBeats = UE_ARRAY_COUNT(ShowcaseBeats);
}

AKartItemPresenter::AKartItemPresenter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// The item "box" is an original prize orb (glass sphere, spinning gem), not the web's yellow "?" cube, which
	// read as a famous franchise's question block.
	Boxes = MakeMeshes(TEXT("ItemBoxes"), Sphere.Object);
	BoxCores = MakeMeshes(TEXT("ItemBoxCores"), nullptr);
	BoxLabels = MakeMeshes(TEXT("ItemBoxLabels"), nullptr);
	BoxHalos = MakeMeshes(TEXT("ItemBoxHalos"), Sphere.Object);
	ItemMeshes.SetNum(static_cast<int32>(EKartItem::Count));
	for (int32 i = 1; i < static_cast<int32>(EKartItem::Count); ++i)
	{
		ItemMeshes[i] = MakeMeshes(*FString::Printf(TEXT("Item_%s"), ItemName(static_cast<EKartItem>(i))), nullptr, i == static_cast<int32>(EKartItem::Bomb) ? 1 : 0);
	}
	Glows = MakeMeshes(TEXT("SkyGlows"), Sphere.Object);
	BombGlows = MakeMeshes(TEXT("BombGlows"), Sphere.Object);
	Blasts = MakeMeshes(TEXT("Blasts"), Sphere.Object, 1);
	// No shadows from any of it: small, fast, mostly emissive, and re-instanced every frame.
	for (UInstancedStaticMeshComponent* C : { BoxHalos.Get(), Glows.Get(), BombGlows.Get(), Blasts.Get(), Boxes.Get(), BoxLabels.Get(), BoxCores.Get() })
	{
		C->SetCastShadow(false);
	}
	for (UInstancedStaticMeshComponent* C : ItemMeshes)
	{
		if (C)
		{
			C->SetCastShadow(false);
		}
	}
	ItemBatches.SetNum(ItemMeshes.Num());
}

void AKartItemPresenter::Commit(UInstancedStaticMeshComponent* Component, FInstanceBatch& Batch)
{
	const int32 N = Batch.Transforms.Num();
	if (!Component || (N == 0 && Batch.Shown == 0))
	{
		Batch.Transforms.Reset();
		Batch.Custom.Reset();
		return;
	}
	// The render side follows instance changes on its own (FPrimitiveInstanceDataManager); no render state rebuild.
	const FTransform Folded(FQuat::Identity, Component->GetComponentLocation(), FVector::ZeroVector);
	const int32 Have = Component->GetInstanceCount();
	if (Have < N)
	{
		TArray<FTransform> Extra;
		Extra.Init(Folded, N - Have);
		Component->AddInstances(Extra, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true, /*bUpdateNavigation*/ false);
	}
	for (int32 i = N; i < Batch.Shown; ++i)
	{
		Batch.Transforms.Add(Folded);
	}
	Component->BatchUpdateInstancesTransforms(0, Batch.Transforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
	for (int32 i = 0; i < Batch.Custom.Num() && i < N; ++i)
	{
		Component->SetCustomDataValue(i, 0, Batch.Custom[i], false);
	}
	Batch.Shown = N;
	Batch.Transforms.Reset();
	Batch.Custom.Reset();
}

UInstancedStaticMeshComponent* AKartItemPresenter::MakeMeshes(const TCHAR* Name, UStaticMesh* Mesh, int32 CustomFloats)
{
	UInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	Component->SetupAttachment(RootComponent);
	if (Mesh)
	{
		Component->SetStaticMesh(Mesh);
	}
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetMobility(EComponentMobility::Movable);
	Component->NumCustomDataFloats = CustomFloats;
	return Component;
}

UInstancedStaticMeshComponent* AKartItemPresenter::MeshesFor(EKartItem Item) const
{
	const int32 Index = static_cast<int32>(Item);
	return ItemMeshes.IsValidIndex(Index) ? ItemMeshes[Index].Get() : nullptr;
}

UMaterialInstanceDynamic* AKartItemPresenter::ItemMaterial(const FLinearColor& Base, const FLinearColor& Emissive, float EmissiveStrength, float Metallic, float Roughness, float ClearCoat)
{
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/Materials/M_KartItem.M_KartItem"));
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, this);
	M->SetVectorParameterValue(TEXT("BaseColor"), Base);
	M->SetVectorParameterValue(TEXT("Emissive"), Emissive);
	M->SetScalarParameterValue(TEXT("EmissiveStrength"), EmissiveStrength);
	M->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	M->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	M->SetScalarParameterValue(TEXT("ClearCoat"), ClearCoat);
	return M;
}

UMaterialInstanceDynamic* AKartItemPresenter::GlowMaterial(const FLinearColor& Color, float Intensity, float Power, float Core)
{
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/Materials/M_FX_Glow.M_FX_Glow"));
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Parent, this);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	M->SetScalarParameterValue(TEXT("Power"), Power);
	M->SetScalarParameterValue(TEXT("Core"), Core);
	return M;
}

void AKartItemPresenter::SetupMaterials()
{
	if (bMaterialsBuilt)
	{
		return;
	}
	bMaterialsBuilt = true;
	auto Set = [](UInstancedStaticMeshComponent* C, FName Slot, UMaterialInterface* M)
	{
		const int32 Index = C->GetMaterialIndex(Slot);
		if (M && Index != INDEX_NONE)
		{
			C->SetMaterial(Index, M);
		}
	};
	auto Mesh = [this](EKartItem Item, UStaticMesh* Built)
	{
		UInstancedStaticMeshComponent* C = MeshesFor(Item);
		C->SetStaticMesh(Built);
		return C;
	};

	// Meshes modelled in code (FX/KartItemMeshes).
	BoxCores->SetStaticMesh(BuildChamferCubeMesh(this));
	BoxLabels->SetStaticMesh(BuildBoxLabelsMesh(this));
	UStaticMesh* Shell = BuildShellMesh(this);
	Mesh(EKartItem::Shot, Shell);
	Mesh(EKartItem::Seeker, Shell);
	Mesh(EKartItem::SkySeeker, BuildWingedShellMesh(this));
	Mesh(EKartItem::Banana, BuildBananaMesh(this));
	Mesh(EKartItem::Bomb, BuildBombMesh(this));
	Mesh(EKartItem::Nitro, BuildNitroMesh(this));
	Mesh(EKartItem::TripleNitro, BuildTripleNitroMesh(this));
	Mesh(EKartItem::Prism, BuildPrismMesh(this));
	Mesh(EKartItem::Lightning, BuildLightningMesh(this));
	Mesh(EKartItem::Ink, BuildInkMesh(this));
	Mesh(EKartItem::Rocket, SharedRocketMesh());

	// Shells: clear-coated dome in the item colour, cream rim, belly in the inner colour (emissive 0.32 on the web).
	const FLinearColor Cream = Hex(TEXT("f4efe2"));
	for (EKartItem Item : { EKartItem::Shot, EKartItem::Seeker, EKartItem::SkySeeker })
	{
		const FItemColors C = ItemColors(Item);
		UInstancedStaticMeshComponent* M = MeshesFor(Item);
		Set(M, TEXT("Dome"), ItemMaterial(C.Color, C.Emissive, 0.6f, 0.3f, 0.32f, 1.0f));
		Set(M, TEXT("Rim"), ItemMaterial(Cream, Cream, 0.05f, 0.1f, 0.35f, 0.6f));
		Set(M, TEXT("Belly"), ItemMaterial(C.Inner, C.Inner, 0.15f, 0.08f, 0.56f, 0.2f));
		Set(M, TEXT("Wings"), ItemMaterial(FLinearColor(0.95f, 0.96f, 1.f), FLinearColor(0.6f, 0.7f, 1.f), 0.25f, 0.1f, 0.3f, 0.8f));
	}
	{
		const FItemColors C = ItemColors(EKartItem::Banana);
		Set(MeshesFor(EKartItem::Banana), TEXT("Peel"), ItemMaterial(C.Color, C.Emissive, 0.32f, 0.05f, 0.45f, 0.5f));
		Set(MeshesFor(EKartItem::Banana), TEXT("Tips"), ItemMaterial(C.Inner, C.Inner * 0.3f, 0.1f, 0.0f, 0.7f, 0.0f));
	}
	{
		const FItemColors C = ItemColors(EKartItem::Bomb);
		UInstancedStaticMeshComponent* M = MeshesFor(EKartItem::Bomb);
		Set(M, TEXT("Body"), ItemMaterial(C.Color, C.Emissive, 0.32f, 0.3f, 0.3f, 1.0f));
		Set(M, TEXT("Cap"), ItemMaterial(Hex(TEXT("8c8f96")), C.Inner, 0.0f, 0.9f, 0.3f, 0.3f));
		Set(M, TEXT("Fuse"), ItemMaterial(Hex(TEXT("d8c8a0")), Hex(TEXT("ff8a3a")), 0.2f, 0.0f, 0.8f, 0.0f));
	}
	for (EKartItem Item : { EKartItem::Nitro, EKartItem::TripleNitro })
	{
		const FItemColors C = ItemColors(Item);
		UInstancedStaticMeshComponent* M = MeshesFor(Item);
		Set(M, TEXT("Body"), ItemMaterial(C.Color, C.Emissive, 0.25f, 0.15f, 0.25f, 1.0f));
		Set(M, TEXT("Band"), ItemMaterial(C.Inner, C.Inner, 0.08f, 0.0f, 0.4f, 0.5f));
		Set(M, TEXT("Metal"), ItemMaterial(Hex(TEXT("c9ccd2")), Hex(TEXT("000000")), 0.0f, 1.0f, 0.18f, 0.2f));
	}
	if (UMaterialInterface* Prism = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/Materials/M_ItemPrism.M_ItemPrism")))
	{
		MeshesFor(EKartItem::Prism)->SetMaterial(0, Prism);
	}
	{
		const FItemColors C = ItemColors(EKartItem::Lightning);
		Set(MeshesFor(EKartItem::Lightning), TEXT("Bolt"), ItemMaterial(C.Color, C.Color, 1.2f, 0.1f, 0.3f, 1.0f));
		Set(MeshesFor(EKartItem::Lightning), TEXT("Edge"), ItemMaterial(Hex(TEXT("f08a00")), Hex(TEXT("ff9a1a")), 0.6f, 0.2f, 0.35f, 0.6f));
	}
	{
		const FItemColors C = ItemColors(EKartItem::Ink);
		UInstancedStaticMeshComponent* M = MeshesFor(EKartItem::Ink);
		Set(M, TEXT("Ink"), ItemMaterial(C.Color, C.Inner, 0.35f, 0.0f, 0.12f, 1.0f));
		Set(M, TEXT("Eyes"), ItemMaterial(FLinearColor::White, FLinearColor::White, 0.35f, 0.0f, 0.3f, 0.5f));
		Set(M, TEXT("Pupils"), ItemMaterial(Hex(TEXT("0a0a10")), FLinearColor::Black, 0.0f, 0.0f, 0.1f, 1.0f));
	}
	{
		UInstancedStaticMeshComponent* M = MeshesFor(EKartItem::Rocket);
		Set(M, TEXT("Exhaust"), ItemMaterial(Hex(TEXT("2a2a2e")), Hex(TEXT("ff6a2a")), 2.5f, 0.6f, 0.4f));
		Set(M, TEXT("Body"), ItemMaterial(Hex(TEXT("f2f4f8")), Hex(TEXT("c8d4ff")), 0.15f, 0.2f, 0.3f));
		Set(M, TEXT("Band"), ItemMaterial(Hex(TEXT("ff6a13")), Hex(TEXT("ff4a00")), 0.3f, 0.2f, 0.35f));
		Set(M, TEXT("Fins"), ItemMaterial(Hex(TEXT("21c7c0")), Hex(TEXT("0a5a58")), 0.2f, 0.4f, 0.35f));
	}
	Glows->SetMaterial(0, GlowMaterial(ItemColors(EKartItem::SkySeeker).Emissive * 2.f + FLinearColor(0.f, 0.05f, 0.3f), 3.0f, 1.6f, 0.08f));
	BombGlows->SetMaterial(0, GlowMaterial(ItemColors(EKartItem::Bomb).Emissive, 2.5f, 1.4f, 0.12f));

	// Prize orb: violet glass with a cyan rim, an iridescent magenta gem inside, and a soft cyan halo.
	if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/Materials/M_ItemBoxGlass.M_ItemBoxGlass")))
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Glass, this);
		M->SetVectorParameterValue(TEXT("Color"), Hex(TEXT("7a5cff")) * 0.6f);
		M->SetVectorParameterValue(TEXT("EdgeColor"), Hex(TEXT("5ff0ff")));
		Boxes->SetMaterial(0, M);
	}
	BoxCores->SetMaterial(0, ItemMaterial(Hex(TEXT("ff5fd2")), Hex(TEXT("b45cff")), 0.9f, 0.8f, 0.15f, 1.0f));
	if (UMaterialInterface* Label = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/Materials/M_ItemBoxLabel.M_ItemBoxLabel")))
	{
		BoxLabels->SetMaterial(0, Label);
	}
	BoxHalos->SetMaterial(0, GlowMaterial(Hex(TEXT("5ff0ff")), 0.9f, 2.6f, 0.0f));
	BoxLabels->SetVisibility(false);
	if (UMaterialInterface* Fire = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/Materials/M_FX_Fireball.M_FX_Fireball")))
	{
		Blasts->SetMaterial(0, Fire);
	}

	// Lightning strikes: a few different bolts, reused round robin.
	if (StrikeMeshes.Num() == 0)
	{
		UMaterialInterface* Bolt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/FX/Materials/M_FX_Bolt.M_FX_Bolt"));
		for (int32 i = 0; i < StrikePool; ++i)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Strike%d"), i));
			C->SetStaticMesh(BuildStrikeMesh(this, 101 + i * 37, StrikeHeight));
			C->SetupAttachment(RootComponent);
			C->SetUsingAbsoluteLocation(true);
			C->SetUsingAbsoluteRotation(true);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			C->SetVisibility(false);
			C->RegisterComponent();
			UMaterialInstanceDynamic* M = Bolt ? UMaterialInstanceDynamic::Create(Bolt, this) : nullptr;
			if (M)
			{
				C->SetMaterial(0, M);
			}
			StrikeMeshes.Add(C);
			StrikeMaterials.Add(M);
			Strikes.Add(FStrike());
		}
	}
}

void AKartItemPresenter::Bind(AKartRaceManager* InManager)
{
	if (Manager.Get() != InManager || !CueHandle.IsValid())
	{
		if (AKartRaceManager* Previous = Manager.Get())
		{
			Previous->OnCue.Remove(CueHandle);
		}
		CueHandle = InManager->OnCue.AddUObject(this, &AKartItemPresenter::HandleCue);
	}
	Manager = InManager;
	SetupMaterials();

	// This presenter draws the live boxes; hide the track builder's static preview ones.
	for (TActorIterator<ATrackBuilder> It(GetWorld()); It; ++It)
	{
		It->SetPreviewItemBoxesVisible(false);
	}
	BoxPositions.Reset();
	BoxWasVisible.Reset();
	BoxShownAt.Reset();
	Boxes->ClearInstances();
	BoxCores->ClearInstances();
	BoxLabels->ClearInstances();
	BoxHalos->ClearInstances();
	for (FInstanceBatch& Batch : BoxBatches)
	{
		Batch = FInstanceBatch();
	}
	for (UNiagaraComponent* C : BoxTwinkles)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	BoxTwinkles.Reset();
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	for (const FVector& Box : InManager->GetTrack()->ItemBoxes)
	{
		const FVector P = Coords::ToUE(Box);
		BoxPositions.Add(P);
		BoxWasVisible.Add(true);
		BoxShownAt.Add(-1000.0);
		Boxes->AddInstance(FTransform(P), true);
		BoxCores->AddInstance(FTransform(P), true);
		BoxLabels->AddInstance(FTransform(P), true);
		BoxHalos->AddInstance(FTransform(P), true);
		UNiagaraComponent* Twinkle = Fx ? Fx->MakeStream(EField::Flare, RootComponent, NAME_None) : nullptr;
		if (Twinkle)
		{
			Twinkle->SetWorldLocation(P);
		}
		BoxTwinkles.Add(Twinkle);
	}

	FString Beat;
	bShowcase = FParse::Param(FCommandLine::Get(), TEXT("KartFxShowcase")) || FParse::Value(FCommandLine::Get(), TEXT("KartFxShowcase="), Beat);
	ShowcaseBeat = Beat.ToLower();
	ShowcaseNext = -1.0;
	ShowcaseIndex = 0;
	if (bShowcase && InManager->GetRacers().Num() > 0)
	{
		const AKartPawn* Kart = InManager->GetRacers()[0].Kart;
		ShowcaseOrigin = Kart->GetActorLocation();
		ShowcaseForward = Kart->GetActorForwardVector();
	}
}

FVector AKartItemPresenter::KartGround(int32 Seat) const
{
	const AKartRaceManager* Race = Manager.Get();
	if (!Race || !Race->GetRacers().IsValidIndex(Seat))
	{
		return FVector::ZeroVector;
	}
	return Race->GetRacers()[Seat].Kart->GetActorLocation();
}

void AKartItemPresenter::HandleCue(int32 Seat, FName Cue)
{
	// Finish line: a confetti cannon over the kart that crossed it.
	static const FName Finish(TEXT("kart.finish"));
	const AKartRaceManager* Race = Manager.Get();
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	if (Cue != Finish || !Fx || !Race || !Race->GetRacers().IsValidIndex(Seat))
	{
		return;
	}
	const AKartPawn* Kart = Race->GetRacers()[Seat].Kart;
	TArray<FKartFxEmit> Out;
	ConfettiBurst(Kart->GetActorLocation() + Kart->GetActorForwardVector() * 300.0 + FVector(0.0, 0.0, 120.0), Kart->GetActorForwardVector(), Out);
	Fx->Emit(Out);
}

void AKartItemPresenter::Strike(const FVector& Ground, double Time)
{
	if (Strikes.Num() == 0)
	{
		return;
	}
	const int32 i = NextStrike++ % Strikes.Num();
	Strikes[i] = { Time, Ground };
	StrikeMeshes[i]->SetWorldLocationAndRotation(Ground, FRotator(0.0, FMath::FRandRange(0.0, 360.0), 0.0));
}

void AKartItemPresenter::HandleEvents(const TArray<FKartItemEvent>& Events, double Time)
{
	AKartRaceManager* Race = Manager.Get();
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	TArray<FKartFxEmit> Out;
	auto KartFx = [Race](int32 Seat) -> UKartFxComponent*
	{
		return Race->GetRacers().IsValidIndex(Seat) ? Race->GetRacers()[Seat].Kart->FindComponentByClass<UKartFxComponent>() : nullptr;
	};
	auto Kart = [Race](int32 Seat) -> AKartPawn* { return Race->GetRacers().IsValidIndex(Seat) ? Race->GetRacers()[Seat].Kart.Get() : nullptr; };

	for (const FKartItemEvent& E : Events)
	{
		const FVector Ground = Coords::ToUE(E.Position, 0.0) + FVector(0.0, 0.0, KartGround(FMath::Max(0, E.Seat)).Z);
		switch (E.Type)
		{
		case EKartItemEventType::Explosion:
		{
			const FVector At(Ground.X, Ground.Y, KartGround(FMath::Max(0, E.Other)).Z);
			FBlast Blast{ At + FVector(0.0, 0.0, 60.0), E.Radius, Time, {} };
			for (int32 i = 0; i < 3; ++i)
			{
				Blast.Lobes[i] = FMath::VRand() * FVector(1.0, 1.0, 0.4) * E.Radius * 100.0 * 0.12;
			}
			ActiveBlasts.Add(Blast);
			ExplosionBurst(At, E.Radius, Out);
			RecentImpacts.Add({ At, Time });
			// Global events shake every view at half strength (kartFxBus.ts:91-125).
			for (const FKartRacer& Racer : Race->GetRacers())
			{
				Racer.Kart->AddShake(ImpactProfile(EImpact::Explosion).Shake * 0.5);
			}
			// The flash light: a free one from the pool (the oldest if all are lit), new only up to the pool size.
			constexpr int32 BlastLightPool = 3;
			int32 Slot = BlastLightStart.IndexOfByPredicate([](double Start) { return Start < 0.0; });
			if (Slot == INDEX_NONE && BlastLights.Num() < BlastLightPool)
			{
				UPointLightComponent* Light = NewObject<UPointLightComponent>(this, MakeUniqueObjectName(this, UPointLightComponent::StaticClass(), TEXT("BlastLight")));
				Light->SetupAttachment(RootComponent);
				Light->SetUsingAbsoluteLocation(true);
				Light->SetLightColor(Hex(TEXT("ff9a40")));
				Light->SetIntensityUnits(ELightUnits::Candelas);
				Light->SetIntensity(0.f);
				Light->SetCastShadows(false);
				Light->RegisterComponent();
				Slot = BlastLights.Add(Light);
				BlastLightStart.Add(-1.0);
			}
			if (Slot == INDEX_NONE)
			{
				Slot = 0;
				for (int32 i = 1; i < BlastLightStart.Num(); ++i)
				{
					Slot = BlastLightStart[i] < BlastLightStart[Slot] ? i : Slot;
				}
			}
			if (UPointLightComponent* Light = BlastLights[Slot])
			{
				Light->SetWorldLocation(Blast.Center + FVector(0.0, 0.0, 150.0));
				Light->SetAttenuationRadius(static_cast<float>(E.Radius * 100.0 * 2.5));
				Light->SetIntensity(0.f);
				Light->SetVisibility(true);
			}
			BlastLightStart[Slot] = Time;
			break;
		}
		case EKartItemEventType::Hit:
			if (AKartPawn* Victim = Kart(E.Seat))
			{
				Victim->AddShake(ImpactProfile(EImpact::ProjectileHit).Shake);
				ImpactBurst(EImpact::ProjectileHit, Victim->GetActorLocation(), 1.0, Out, -1.0, ItemColors(E.Item).Color);
				RecentImpacts.Add({ Victim->GetActorLocation(), Time });
				if (UKartFxComponent* VictimFx = KartFx(E.Seat))
				{
					VictimFx->Flash(ImpactProfile(EImpact::ProjectileHit).Flash, ImpactProfile(EImpact::ProjectileHit).FlashColor);
				}
			}
			break;
		case EKartItemEventType::SpinOut:
			if (AKartPawn* Victim = Kart(E.Seat))
			{
				Victim->AddShake(ImpactProfile(EImpact::SpinOut).Shake);
				ImpactBurst(EImpact::SpinOut, Victim->GetActorLocation(), 1.0, Out);
				// Dizzy stars circling up off the kart.
				FKartFxEmit Stars;
				Stars.Field = EField::Flare;
				Stars.Position = Victim->GetActorLocation() + FVector(0.0, 0.0, 140.0);
				Stars.Count = 10;
				Stars.Spread = 1.2;
				Stars.Speed = { 1.0, 2.5 };
				Stars.Life = { 0.6, 1.0 };
				Stars.Size = { 0.35, 0.55 };
				Stars.SizeEnd = 0.3;
				Stars.Color = Hex(TEXT("fff3a0"));
				Stars.ColorEnd = Hex(TEXT("ffb020"));
				Stars.Intensity = 3.0;
				Stars.Jitter = 0.6;
				Stars.InheritCm = Victim->GetKartMovement()->Velocity * 0.6;
				Out.Add(Stars);
			}
			break;
		case EKartItemEventType::Bump:
			ImpactBurst(EImpact::Bump, KartGround(E.Seat), 1.0, Out);
			break;
		case EKartItemEventType::Pickup:
		{
			// Collect burst: cyan twinkles, sparks, a ring and a puff of glass at the box.
			const FVector Box = Coords::ToUE(E.Position, 1.0);
			FKartFxEmit Twinkle;
			Twinkle.Field = EField::Flare;
			Twinkle.Position = Box;
			Twinkle.Count = 20;
			Twinkle.Spread = UE_DOUBLE_PI;
			Twinkle.Speed = { 2.0, 6.5 };
			Twinkle.Life = { 0.35, 0.7 };
			Twinkle.Size = { 0.35, 0.7 };
			Twinkle.SizeEnd = 0.2;
			Twinkle.Color = Hex(TEXT("c8fbff"));
			Twinkle.ColorEnd = Hex(TEXT("b45cff"));
			Twinkle.Intensity = 3.0;
			Twinkle.Jitter = 0.6;
			Out.Add(Twinkle);
			FKartFxEmit Sparks = Twinkle;
			Sparks.Field = EField::Sparks;
			Sparks.Count = 28;
			Sparks.Speed = { 5.0, 12.0 };
			Sparks.Life = { 0.25, 0.5 };
			Sparks.Size = { 0.06, 0.11 };
			Sparks.Color = Hex(TEXT("e8fdff"));
			Out.Add(Sparks);
			FKartFxEmit Ring;
			Ring.Field = EField::Ring;
			Ring.Position = Coords::ToUE(E.Position, 0.08);
			Ring.Count = 1;
			Ring.Speed = { 0.0, 0.0 };
			Ring.Life = { 0.4, 0.4 };
			Ring.Size = { 1.0, 1.0 };
			Ring.SizeEnd = 4.0;
			Ring.Color = Hex(TEXT("7ff4ff"));
			Ring.ColorEnd = Hex(TEXT("b45cff"));
			Ring.Intensity = 2.0;
			Out.Add(Ring);
			break;
		}
		case EKartItemEventType::Used:
			if (!IsThrown(E.Item) && E.Item != EKartItem::Rocket)
			{
				Popups.Add({ E.Item, E.Seat, Time });
			}
			if (E.Item == EKartItem::Lightning)
			{
				// Lightning lights up every view at once.
				for (const FKartRacer& Racer : Race->GetRacers())
				{
					if (UKartFxComponent* RacerFx = Racer.Kart->FindComponentByClass<UKartFxComponent>())
					{
						RacerFx->Zap();
					}
				}
			}
			break;
		case EKartItemEventType::Launch:
		case EKartItemEventType::Drop:
			if (AKartPawn* Thrower = Kart(E.Seat))
			{
				const FItemColors C = ItemColors(E.Item);
				const FVector At = Thrower->GetActorLocation() + FVector(0.0, 0.0, 50.0);
				Poof(At, E.Item == EKartItem::Bomb ? C.Emissive : C.Color, 0.9, Out);
				FKartFxEmit Sparks;
				Sparks.Field = EField::Sparks;
				Sparks.Position = At;
				Sparks.Count = 12;
				Sparks.Spread = UE_DOUBLE_PI * 0.7;
				Sparks.Speed = { 3.0, 8.0 };
				Sparks.Life = { 0.15, 0.35 };
				Sparks.Size = { 0.05, 0.09 };
				Sparks.Color = FLinearColor::White;
				Sparks.ColorEnd = E.Item == EKartItem::Bomb ? C.Emissive : C.Color;
				Sparks.Intensity = 2.4;
				Sparks.InheritCm = Thrower->GetKartMovement()->Velocity * 0.7;
				Out.Add(Sparks);
			}
			break;
		case EKartItemEventType::Bounce:
		{
			FKartFxEmit Sparks;
			Sparks.Field = EField::Sparks;
			Sparks.Position = Ground + FVector(0.0, 0.0, 35.0);
			Sparks.Count = 14;
			Sparks.Spread = UE_DOUBLE_PI * 0.8;
			Sparks.Speed = { 4.0, 10.0 };
			Sparks.Life = { 0.15, 0.35 };
			Sparks.Size = { 0.05, 0.09 };
			Sparks.Color = Hex(TEXT("fff4dc"));
			Sparks.ColorEnd = Hex(TEXT("ff5a1f"));
			Sparks.Intensity = 2.4;
			Out.Add(Sparks);
			break;
		}
		case EKartItemEventType::Lightning:
			if (AKartPawn* Victim = Kart(E.Seat))
			{
				const FVector At = Victim->GetActorLocation();
				Strike(At, Time);
				const FItemColors C = ItemColors(EKartItem::Lightning);
				FKartFxEmit Sparks;
				Sparks.Field = EField::Sparks;
				Sparks.Position = At + FVector(0.0, 0.0, 40.0);
				Sparks.Count = 34;
				Sparks.Spread = 1.3;
				Sparks.Speed = { 6.0, 14.0 };
				Sparks.Life = { 0.2, 0.45 };
				Sparks.Size = { 0.07, 0.12 };
				Sparks.Color = Hex(TEXT("f0faff"));
				Sparks.ColorEnd = C.Emissive;
				Sparks.Intensity = 4.0;
				Out.Add(Sparks);
				FKartFxEmit Ring;
				Ring.Field = EField::Ring;
				Ring.Position = At + FVector(0.0, 0.0, 8.0);
				Ring.Count = 1;
				Ring.Speed = { 0.0, 0.0 };
				Ring.Life = { 0.4, 0.4 };
				Ring.Size = { 1.2, 1.2 };
				Ring.SizeEnd = 4.5;
				Ring.Color = Hex(TEXT("e0f0ff"));
				Ring.ColorEnd = C.Emissive;
				Ring.Intensity = 2.6;
				Out.Add(Ring);
				FKartFxEmit Flash;
				Flash.Field = EField::Flare;
				Flash.Position = At + FVector(0.0, 0.0, 80.0);
				Flash.Count = 1;
				Flash.Speed = { 0.0, 0.0 };
				Flash.Life = { 0.2, 0.2 };
				Flash.Size = { 5.0, 5.0 };
				Flash.SizeEnd = 1.3;
				Flash.Color = FLinearColor::White;
				Flash.ColorEnd = C.Emissive;
				Flash.Intensity = 5.0;
				Out.Add(Flash);
				ImpactBurst(EImpact::SpinOut, At, 0.6, Out);
			}
			break;
		case EKartItemEventType::Inked:
			if (AKartPawn* Victim = Kart(E.Seat))
			{
				// A splash of ink bursting over the kart.
				const FItemColors C = ItemColors(EKartItem::Ink);
				FKartFxEmit Ink;
				Ink.Field = EField::Smoke;
				Ink.Position = Victim->GetActorLocation() + FVector(0.0, 0.0, 120.0);
				Ink.Count = 12;
				Ink.Spread = UE_DOUBLE_PI;
				Ink.Speed = { 2.0, 5.0 };
				Ink.Life = { 0.5, 0.9 };
				Ink.Size = { 0.35, 0.6 };
				Ink.SizeEnd = 1.8;
				Ink.Color = C.Color;
				Ink.ColorEnd = Hex(TEXT("120a1e"));
				Ink.Jitter = 0.4;
				Ink.InheritCm = Victim->GetKartMovement()->Velocity * 0.7;
				Out.Add(Ink);
				FKartFxEmit Drops = Ink;
				Drops.Field = EField::Sparks;
				Drops.Count = 24;
				Drops.Speed = { 4.0, 9.0 };
				Drops.Life = { 0.3, 0.6 };
				Drops.Size = { 0.09, 0.14 };
				Drops.Color = C.Inner;
				Drops.ColorEnd = C.Color;
				Drops.Intensity = 1.0;
				Out.Add(Drops);
			}
			break;
		case EKartItemEventType::Invincible:
		case EKartItemEventType::RocketStart:
			if (AKartPawn* User = Kart(E.Seat))
			{
				const bool bRocket = E.Type == EKartItemEventType::RocketStart;
				const FVector At = User->GetActorLocation();
				FKartFxEmit Burst;
				Burst.Field = bRocket ? EField::Fire : EField::Flare;
				Burst.Position = At + FVector(0.0, 0.0, 80.0);
				Burst.Count = 26;
				Burst.Spread = UE_DOUBLE_PI;
				Burst.Speed = { 2.0, 7.0 };
				Burst.Life = { 0.3, 0.6 };
				Burst.Size = { 0.35, 0.7 };
				Burst.SizeEnd = bRocket ? 1.6 : 0.3;
				Burst.Color = bRocket ? Hex(TEXT("ffd27a")) : FLinearColor::White;
				Burst.ColorEnd = bRocket ? Hex(TEXT("ff3d1f")) : Hex(TEXT("ff4fd8"));
				Burst.Intensity = 2.8;
				Burst.Jitter = 0.7;
				Burst.InheritCm = User->GetKartMovement()->Velocity * 0.8;
				Out.Add(Burst);
				FKartFxEmit Ring;
				Ring.Field = EField::Ring;
				Ring.Position = At + FVector(0.0, 0.0, 8.0);
				Ring.Count = 1;
				Ring.Speed = { 0.0, 0.0 };
				Ring.Life = { 0.45, 0.45 };
				Ring.Size = { 1.2, 1.2 };
				Ring.SizeEnd = 5.0;
				Ring.Color = Burst.Color;
				Ring.ColorEnd = bRocket ? Burst.ColorEnd : Hex(TEXT("7ad8ff"));
				Ring.Intensity = 2.2;
				Ring.InheritCm = User->GetKartMovement()->Velocity * 0.8;
				Out.Add(Ring);
			}
			break;
		default:
			break;
		}
	}
	if (Fx)
	{
		Fx->Emit(Out);
	}
}

void AKartItemPresenter::UpdateBoxes(double Time, double Now)
{
	const FKartItemWorld& Items = Manager->GetItems();
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	TArray<FKartFxEmit> Out;
	const int32 N = BoxPositions.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const bool bVisible = Items.IsBoxVisible(i, Now);
		if (bVisible && !BoxWasVisible[i])
		{
			// Respawn: the box pops back in with a ring and a few twinkles.
			BoxShownAt[i] = Time;
			FKartFxEmit Ring;
			Ring.Field = EField::Ring;
			Ring.Position = BoxPositions[i] - FVector(0.0, 0.0, 92.0);
			Ring.Count = 1;
			Ring.Speed = { 0.0, 0.0 };
			Ring.Life = { 0.45, 0.45 };
			Ring.Size = { 2.6, 2.6 };
			Ring.SizeEnd = 0.25;
			Ring.Color = Hex(TEXT("c8fbff"));
			Ring.ColorEnd = Hex(TEXT("b45cff"));
			Ring.Intensity = 2.0;
			Out.Add(Ring);
			Poof(BoxPositions[i], Hex(TEXT("5ff0ff")), 1.3, Out);
		}
		BoxWasVisible[i] = bVisible;
		// Pop back in with a springy overshoot.
		const double Scale = bVisible ? FMath::Max(0.0, Pop((Time - BoxShownAt[i]) / 0.5)) : 0.0;
		// MultiplayerPowerups.tsx: spin 1.5 rad/s, bob 0.1 m at 2.5 rad/s, inner icon +0.5 rad/s.
		const FVector P = BoxPositions[i] + FVector(0.0, 0.0, 10.0 * FMath::Sin(2.5 * Time + i * 0.7));
		const FQuat Spin(FVector::UpVector, -1.5 * Time - i);
		BoxBatches[0].Add(FTransform(Spin, P, FVector(BoxSize * 0.9 / 100.0 * Scale)));
		// The gem stands on a corner (a cube tipped onto its diagonal) and tumbles slowly.
		static const FQuat Corner(FVector(1.0, -1.0, 0.0).GetSafeNormal(), FMath::Acos(1.0 / FMath::Sqrt(3.0)));
		const FQuat Core = FQuat(FVector::UpVector, -2.0 * Time - i) * Corner * FQuat(FVector(1.0, 1.0, 1.0).GetSafeNormal(), 0.3 * FMath::Sin(Time * 1.3 + i));
		BoxBatches[1].Add(FTransform(Core, P, FVector(BoxSize * 0.42 / 100.0 * Scale)));
		BoxBatches[2].Add(FTransform(FQuat::Identity, P, FVector(BoxSize * 0.9 * 2.0 / 100.0 * Scale * (1.0 + 0.04 * FMath::Sin(Time * 3.0 + i)))));
		if (BoxLabels->IsVisible())
		{
			BoxBatches[3].Add(FTransform(Spin, P, FVector(BoxSize / 100.0 * Scale)));
		}
		if (BoxTwinkles.IsValidIndex(i) && BoxTwinkles[i])
		{
			static const FKartFxEmit Twinkle = []
			{
				FKartFxEmit T;
				T.Field = EField::Flare;
				T.Direction = FVector::UpVector;
				T.Spread = 0.8;
				T.Speed = { 0.3, 0.9 };
				T.Life = { 0.7, 1.3 };
				T.Size = { 0.22, 0.42 };
				T.SizeEnd = 0.3;
				T.Color = Hex(TEXT("d8f9ff"));
				T.ColorEnd = Hex(TEXT("b45cff"));
				T.Intensity = 2.2;
				T.Jitter = 1.25;
				return T;
			}();
			UKartFxSubsystem::UpdateStream(BoxTwinkles[i], Twinkle, bVisible ? 5.0 : 0.0);
		}
	}
	Commit(Boxes, BoxBatches[0]);
	Commit(BoxCores, BoxBatches[1]);
	Commit(BoxHalos, BoxBatches[2]);
	Commit(BoxLabels, BoxBatches[3]);
	if (Fx)
	{
		Fx->Emit(Out);
	}
}

void AKartItemPresenter::UpdateItems(double Time)
{
	const AKartRaceManager* Race = Manager.Get();
	const FKartItemWorld& Items = Race->GetItems();
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	auto BatchFor = [this](EKartItem Item) -> FInstanceBatch& { return ItemBatches[static_cast<int32>(Item)]; };
	for (TPair<int32, FTrail>& Pair : Trails)
	{
		Pair.Value.bSeen = false;
	}
	TArray<FKartFxEmit> Out;

	for (const FKartItemProjectile& P : Items.Projectiles())
	{
		UInstancedStaticMeshComponent* Component = MeshesFor(P.Type);
		if (!Component)
		{
			continue;
		}
		const double Yaw = FMath::Atan2(-P.Vel.X, -P.Vel.Y);
		const FItemColors Colors = ItemColors(P.Type);
		FVector At;
		FVector TrailAt;
		switch (P.Type)
		{
		case EKartItem::Banana:
			// Stationary, yaw += 2 rad/s, y + 0.3 (scaled up 1.5x so it reads at speed).
			At = Coords::ToUE(P.Pos, 0.3 + 0.12);
			BatchFor(P.Type).Add(FTransform(FRotator(0.0, FMath::RadiansToDegrees(2.0 * P.Age), 14.0), At, FVector(1.5)));
			TrailAt = At;
			break;
		case EKartItem::Bomb:
		{
			// Tumble x += 5, z += 3 rad/s; glow pulse 1 + sin(t * (3 + 2t)) * 0.3 (MultiplayerItems.tsx:528-537).
			At = Coords::ToUE(P.Pos, P.Y);
			const FRotator Tumble(FMath::RadiansToDegrees(3.0 * P.Age), 0.0, FMath::RadiansToDegrees(5.0 * P.Age));
			const double Pulse = 1.0 + FMath::Sin(P.Age * (3.0 + 2.0 * P.Age)) * 0.3;
			BatchFor(P.Type).Add(FTransform(Tumble, At, FVector(1.0)), static_cast<float>(1.0 + (Pulse - 0.7) * 6.0));
			BombGlowBatch.Add(FTransform(FQuat::Identity, At, FVector(1.2 * Pulse)));
			TrailAt = At + Tumble.RotateVector(BombFuseTip());
			break;
		}
		case EKartItem::SkySeeker:
			// Altitude y + 0.5, roll wobble sin(t*10)*0.1, glow 1.5x pulsing 1 + sin(t*8)*0.2.
			At = Coords::ToUE(P.Pos, P.Y + 0.5);
			BatchFor(P.Type).Add(FTransform(FRotator(0.0, Coords::YawToUE(Yaw), FMath::RadiansToDegrees(0.1 * FMath::Sin(Time * 10.0))), At, FVector(1.15)));
			GlowBatch.Add(FTransform(FQuat::Identity, At, FVector(0.9 * (1.0 + 0.2 * FMath::Sin(Time * 8.0)))));
			TrailAt = At;
			break;
		default:
			// Green spins yaw += 8 rad/s; red faces its velocity. y + 0.3.
			At = Coords::ToUE(P.Pos, 0.3);
			BatchFor(P.Type).Add(FTransform(FRotator(0.0, Coords::YawToUE(Yaw) + (P.Type == EKartItem::Shot ? FMath::RadiansToDegrees(8.0 * P.Age) : 0.0), 0.0), At, FVector(1.0)));
			TrailAt = At;
			break;
		}

		if (!Fx)
		{
			continue;
		}
		FTrail& T = Trails.FindOrAdd(P.Id);
		T.bSeen = true;
		T.DeadSince = -1.0;
		T.Item = P.Type;
		T.LastPosition = At;
		if (!T.Trail && P.Type != EKartItem::Banana)
		{
			T.Trail = Fx->MakeStream(P.Type == EKartItem::Bomb ? EField::Smoke : EField::Fire, RootComponent, NAME_None);
			TrailComponents.Add(T.Trail);
			if (P.Type == EKartItem::Bomb || P.Type == EKartItem::SkySeeker || P.Type == EKartItem::Seeker)
			{
				T.Sparks = Fx->MakeStream(EField::Sparks, RootComponent, NAME_None);
				TrailComponents.Add(T.Sparks);
			}
		}
		const FVector Vel = Coords::ToUE(P.Vel, 0.0) + FVector(0.0, 0.0, P.VY * 100.0);
		if (T.Trail)
		{
			T.Trail->SetWorldLocation(TrailAt);
			FKartFxEmit Trail;
			if (P.Type == EKartItem::Bomb)
			{
				// A thin fuse smoke.
				Trail.Field = EField::Smoke;
				Trail.Direction = FVector::UpVector;
				Trail.Spread = 0.5;
				Trail.Speed = { 0.3, 0.9 };
				Trail.Life = { 0.35, 0.7 };
				Trail.Size = { 0.1, 0.16 };
				Trail.SizeEnd = 2.6;
				Trail.Color = Hex(TEXT("bab4ae"));
				Trail.ColorEnd = Hex(TEXT("6a6462"));
				UKartFxSubsystem::UpdateStream(T.Trail, Trail, 16.0);
			}
			else
			{
				// A dense stream of soft glows in the item colour, hot white near the item, left behind as it flies.
				const bool bSky = P.Type == EKartItem::SkySeeker;
				Trail.Field = EField::Fire;
				Trail.Direction = -Vel.GetSafeNormal();
				Trail.Spread = 0.25;
				Trail.Speed = { 0.3, 1.0 };
				Trail.Life = bSky ? FVector2D(0.3, 0.5) : FVector2D(0.2, 0.32);
				Trail.Size = bSky ? FVector2D(0.5, 0.7) : FVector2D(0.34, 0.48);
				Trail.SizeEnd = 0.15;
				Trail.Color = FLinearColor::LerpUsingHSV(Colors.Color, FLinearColor::White, 0.5f);
				Trail.ColorEnd = Colors.Emissive;
				Trail.Intensity = bSky ? 2.6 : 2.0;
				Trail.Jitter = 0.04;
				UKartFxSubsystem::UpdateStream(T.Trail, Trail, 60.0);
			}
		}
		if (T.Sparks)
		{
			T.Sparks->SetWorldLocation(TrailAt);
			FKartFxEmit Sparks;
			Sparks.Field = EField::Sparks;
			Sparks.InheritCm = Vel * 0.6;
			if (P.Type == EKartItem::Bomb)
			{
				// The lit fuse.
				Sparks.Direction = FVector::UpVector;
				Sparks.Spread = 1.0;
				Sparks.Speed = { 1.5, 3.8 };
				Sparks.Life = { 0.12, 0.3 };
				Sparks.Size = { 0.03, 0.06 };
				Sparks.Color = Hex(TEXT("fff2b0"));
				Sparks.ColorEnd = Hex(TEXT("ff6a2a"));
				Sparks.Intensity = 3.2;
				UKartFxSubsystem::UpdateStream(T.Sparks, Sparks, 45.0);
			}
			else
			{
				Sparks.Direction = -Vel.GetSafeNormal();
				Sparks.Spread = 0.6;
				Sparks.Speed = { 2.0, 5.0 };
				Sparks.Life = { 0.2, 0.4 };
				Sparks.Size = { 0.04, 0.08 };
				Sparks.Color = Hex(TEXT("eaf3ff"));
				Sparks.ColorEnd = Colors.Color;
				Sparks.Intensity = 2.6;
				Sparks.Jitter = 0.2;
				UKartFxSubsystem::UpdateStream(T.Sparks, Sparks, P.Type == EKartItem::SkySeeker ? 40.0 : 18.0);
			}
		}
	}

	// Projectiles that are gone: stop the trails, puff out the ones that simply expired, free them once faded.
	RecentImpacts.RemoveAll([Time](const TPair<FVector, double>& I) { return Time - I.Value > 0.3; });
	for (auto It = Trails.CreateIterator(); It; ++It)
	{
		FTrail& T = It.Value();
		if (T.bSeen)
		{
			continue;
		}
		if (T.DeadSince < 0.0)
		{
			T.DeadSince = Time;
			UKartFxSubsystem::UpdateStream(T.Trail, FKartFxEmit(), 0.0);
			UKartFxSubsystem::UpdateStream(T.Sparks, FKartFxEmit(), 0.0);
			const bool bImpact = RecentImpacts.ContainsByPredicate([&T](const TPair<FVector, double>& I) { return FVector::DistSquared2D(I.Key, T.LastPosition) < FMath::Square(400.0); });
			if (!bImpact && T.Item != EKartItem::Bomb)
			{
				const FItemColors C = ItemColors(T.Item);
				Poof(T.LastPosition, C.Color, T.Item == EKartItem::Banana ? 1.2 : 0.9, Out);
			}
		}
		else if (Time - T.DeadSince > 1.5)
		{
			for (UNiagaraComponent* C : { T.Trail.Get(), T.Sparks.Get() })
			{
				if (C)
				{
					TrailComponents.Remove(C);
					C->DestroyComponent();
				}
			}
			It.RemoveCurrent();
		}
	}

	// Pop-ups: the used item springs up over its kart, spins, then bursts in its colours.
	for (int32 i = Popups.Num() - 1; i >= 0; --i)
	{
		const FPopup& Popup = Popups[i];
		const double Age = Time - Popup.Start;
		const AKartPawn* Kart = Race->GetRacers().IsValidIndex(Popup.Seat) ? Race->GetRacers()[Popup.Seat].Kart.Get() : nullptr;
		UInstancedStaticMeshComponent* Mesh = MeshesFor(Popup.Item);
		if (!Kart || !Mesh || Age > PopupSeconds)
		{
			if (Kart && Fx)
			{
				const FItemColors C = ItemColors(Popup.Item);
				const FVector At = Kart->GetActorLocation() + FVector(0.0, 0.0, 250.0);
				Poof(At, C.Emissive, 1.4, Out);
				FKartFxEmit Ring;
				Ring.Field = EField::Flare;
				Ring.Position = At;
				Ring.Count = 1;
				Ring.Speed = { 0.0, 0.0 };
				Ring.Life = { 0.25, 0.25 };
				Ring.Size = { 2.4, 2.4 };
				Ring.SizeEnd = 1.6;
				Ring.Color = FLinearColor::White;
				Ring.ColorEnd = C.Color;
				Ring.Intensity = 3.5;
				Ring.InheritCm = Kart->GetVelocity();
				Out.Add(Ring);
			}
			Popups.RemoveAtSwap(i);
			continue;
		}
		const double Scale = Pop(Age / 0.35) * (Age > PopupSeconds - 0.12 ? 1.0 + (Age - (PopupSeconds - 0.12)) * 3.0 : 1.0) * 2.0;
		const FVector At = Kart->GetActorLocation() + FVector(0.0, 0.0, 170.0 + 80.0 * EaseOut(Age / 0.4));
		BatchFor(Popup.Item).Add(FTransform(FRotator(0.0, Kart->GetActorRotation().Yaw + 90.0 + Age * 540.0, 0.0), At, FVector(FMath::Max(0.01, Scale))));
	}

	// Showcase: every item mesh floating in a row ahead of the grid, spinning.
	if (bShowcase && ShowcaseBeat == TEXT("items"))
	{
		const FVector Right = FVector::CrossProduct(FVector::UpVector, ShowcaseForward);
		const int32 Count = static_cast<int32>(EKartItem::Count) - 1;
		for (int32 i = 1; i <= Count; ++i)
		{
			const EKartItem Item = static_cast<EKartItem>(i);
			const FVector At = ShowcaseOrigin + ShowcaseForward * 650.0 + Right * ((i - 1 - (Count - 1) * 0.5) * 115.0) + FVector(0.0, 0.0, 130.0 + 12.0 * FMath::Sin(Time * 2.0 + i));
			const double Scale = Item == EKartItem::Rocket ? 0.3 : (Item == EKartItem::Banana ? 1.8 : (Item == EKartItem::Bomb ? 0.9 : 1.3));
			const FTransform Shown(FRotator(0.0, Time * 45.0 + i * 30.0, 0.0), At, FVector(Scale));
			if (Item == EKartItem::Bomb)
			{
				BatchFor(Item).Add(Shown, 0.f); // the bomb's custom float (its glow pulse) stays aligned
			}
			else
			{
				BatchFor(Item).Add(Shown);
			}
		}
	}

	for (int32 i = 0; i < ItemMeshes.Num(); ++i)
	{
		Commit(ItemMeshes[i], ItemBatches[i]);
	}
	Commit(Glows, GlowBatch);
	Commit(BombGlows, BombGlowBatch);
	if (Fx)
	{
		Fx->Emit(Out);
	}
}

void AKartItemPresenter::UpdateBlasts(double Time)
{
	// Explosion fireballs: three overlapping lobes that swell fast (0.2 s) to ~0.42 x the blast radius, keep
	// expanding slowly and burn out to soot (toon fire colours in M_FX_Fireball), under the puffs of ExplosionBurst.
	ActiveBlasts.RemoveAll([Time](const FBlast& B) { return Time - B.Start > BlastSeconds; });
	for (const FBlast& B : ActiveBlasts)
	{
		const double T = (Time - B.Start) / BlastSeconds;
		const double Peak = B.Radius * 100.0 * 0.38;
		for (int32 i = 0; i < 3; ++i)
		{
			const double Delay = i * 0.05;
			const double U = FMath::Clamp((T - Delay) / (1.0 - Delay), 0.0, 1.0);
			const double Grow = EaseOut(U / 0.27) * (1.0 + 0.18 * U);
			const double Radius = Peak * (i == 0 ? 1.0 : 0.68) * Grow;
			const FVector Center = B.Center + B.Lobes[i] * (i == 0 ? 0.2 : 1.0) * Grow + FVector(0.0, 0.0, Radius * 0.35 + U * 120.0);
			BlastBatch.Add(FTransform(FQuat(FVector::UpVector, i * 2.1), Center, FVector(FMath::Max(0.01, Radius / 50.0))), static_cast<float>(U));
		}
	}
	Commit(Blasts, BlastBatch);

	// Light flash: up in 0.05 s, gone by 0.6 s; the light goes back to the pool.
	for (int32 i = 0; i < BlastLights.Num(); ++i)
	{
		UPointLightComponent* Light = BlastLights[i];
		if (!Light || BlastLightStart[i] < 0.0)
		{
			continue;
		}
		const double Age = Time - BlastLightStart[i];
		if (Age > 0.6)
		{
			Light->SetIntensity(0.f);
			Light->SetVisibility(false);
			BlastLightStart[i] = -1.0;
			continue;
		}
		const double Env = Age < 0.05 ? Age / 0.05 : FMath::Pow(1.0 - (Age - 0.05) / 0.55, 2.0);
		Light->SetIntensity(static_cast<float>(60000.0 * Env));
	}
}

void AKartItemPresenter::UpdateStrikes(double Time)
{
	for (int32 i = 0; i < Strikes.Num(); ++i)
	{
		const double Age = Time - Strikes[i].Start;
		const bool bOn = Age >= 0.0 && Age < StrikeSeconds;
		StrikeMeshes[i]->SetVisibility(bOn);
		if (bOn && StrikeMaterials[i])
		{
			// Flicker: full, a dip, a second flash, then out.
			const double T = Age / StrikeSeconds;
			const double Flicker = T < 0.25 ? 1.0 : (T < 0.4 ? 0.25 : (T < 0.6 ? 0.9 : FMath::Pow(1.0 - (T - 0.6) / 0.4, 2.0)));
			StrikeMaterials[i]->SetScalarParameterValue(TEXT("Fade"), static_cast<float>(Flicker));
		}
	}
}

void AKartItemPresenter::UpdateShowcase(double Time)
{
	AKartRaceManager* Race = Manager.Get();
	if (!bShowcase || ShowcaseBeat == TEXT("items") || Race->GetPhase() != EKartRacePhase::Racing || Race->GetRacers().Num() == 0)
	{
		return;
	}
	if (ShowcaseNext < 0.0)
	{
		ShowcaseNext = Time + 1.5;
	}
	if (Time < ShowcaseNext)
	{
		return;
	}
	ShowcaseNext = Time + 3.0;
	int32 Beat = ShowcaseIndex++ % NumShowcaseBeats;
	for (int32 i = 0; i < NumShowcaseBeats; ++i)
	{
		if (ShowcaseBeat == ShowcaseBeats[i])
		{
			Beat = i;
		}
	}
	AKartPawn* Kart = Race->GetRacers()[0].Kart;
	UKartFxComponent* KartFx = Kart->FindComponentByClass<UKartFxComponent>();
	const FVector At = Kart->GetActorLocation();
	// Far enough ahead that the kart reaches the effect about half a second in.
	const FVector Ahead = At + Kart->GetActorForwardVector() * (500.0 + Kart->GetVelocity().Size() * 0.3);
	TArray<FKartItemEvent> Events;
	auto Event = [&](EKartItemEventType Type, EKartItem Item)
	{
		FKartItemEvent E;
		E.Type = Type;
		E.Seat = 0;
		E.Other = 0;
		E.Item = Item;
		E.Position = Coords::FromUE(Ahead);
		Events.Add(E);
	};
	const FString Name = ShowcaseBeats[Beat];
	// Capture the beat: just after it starts and once it has developed (Saved/Screenshots, in this order).
	UE_LOG(LogTemp, Display, TEXT("KartFxShowcase: %s (2 shots)"), *Name);
	ShowcaseShots.Append({ Time + 0.12, Time + 0.45 });
	if (Name == TEXT("explosion"))
	{
		Event(EKartItemEventType::Explosion, EKartItem::Bomb);
		Events.Last().Radius = 5.0;
		Event(EKartItemEventType::Used, EKartItem::Nitro);
	}
	else if (Name == TEXT("lightning"))
	{
		Event(EKartItemEventType::Used, EKartItem::Lightning);
		for (int32 Seat = 0; Seat < Race->GetRacers().Num(); ++Seat)
		{
			Event(EKartItemEventType::Lightning, EKartItem::Lightning);
			Events.Last().Seat = Seat;
		}
	}
	else if (Name == TEXT("prism") && KartFx)
	{
		Event(EKartItemEventType::Used, EKartItem::Prism);
		Event(EKartItemEventType::Invincible, EKartItem::Prism);
		KartFx->Overrides().PrismUntil = Time + 2.8;
	}
	else if (Name == TEXT("rocket") && KartFx)
	{
		Event(EKartItemEventType::RocketStart, EKartItem::Rocket);
		KartFx->Overrides().RocketUntil = Time + 2.8;
	}
	else if (Name == TEXT("ink") && KartFx)
	{
		Event(EKartItemEventType::Used, EKartItem::Ink);
		Event(EKartItemEventType::Inked, EKartItem::Ink);
		KartFx->Overrides().InkStart = Time;
		KartFx->Overrides().InkUntil = Time + 2.8;
	}
	else if (Name == TEXT("hit"))
	{
		Event(EKartItemEventType::Hit, EKartItem::Seeker);
		Event(EKartItemEventType::SpinOut, EKartItem::Seeker);
		Event(EKartItemEventType::Used, EKartItem::TripleNitro);
	}
	else if (Name == TEXT("confetti"))
	{
		HandleCue(0, TEXT("kart.finish"));
	}
	else if (Name == TEXT("box"))
	{
		Event(EKartItemEventType::Pickup, EKartItem::None);
		Events.Last().Position = Coords::FromUE(At + Kart->GetActorForwardVector() * 500.0);
	}
	HandleEvents(Events, Time);
}

void AKartItemPresenter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AKartRaceManager* Race = Manager.Get();
	if (!Race || !Race->GetTrack())
	{
		return;
	}
	const FKartItemWorld& Items = Race->GetItems();
	const double Now = Race->GetItemTime();
	const double Time = GetWorld()->GetTimeSeconds();

	HandleEvents(Race->ConsumeItemEvents(), Time);
	UpdateShowcase(Time);
	if (bShowcase)
	{
		// A rough frame-time readout for the effects-heavy showcase (the window's whole frame, render included).
		ShowcaseFrames.Add(DeltaSeconds);
		if (ShowcaseFrames.Num() >= 300)
		{
			ShowcaseFrames.Sort();
			double Sum = 0.0;
			for (const float F : ShowcaseFrames)
			{
				Sum += F;
			}
			UE_LOG(LogTemp, Display, TEXT("KartFxShowcase: last %d frames avg %.2f ms, p95 %.2f ms"), ShowcaseFrames.Num(),
				Sum / ShowcaseFrames.Num() * 1000.0, ShowcaseFrames[FMath::FloorToInt32(ShowcaseFrames.Num() * 0.95)] * 1000.0);
			ShowcaseFrames.Reset();
		}
	}
	if (ShowcaseShots.Num() > 0 && Time >= ShowcaseShots[0])
	{
		ShowcaseShots.RemoveAt(0);
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->ConsoleCommand(TEXT("Shot showui"));
		}
	}
	UpdateBoxes(Time, Now);
	UpdateItems(Time);
	UpdateBlasts(Time);
	UpdateStrikes(Time);

	// Kart item visuals.
	for (const FKartRacer& Racer : Race->GetRacers())
	{
		const FKartItemSlot& Slot = Items.Slots()[Racer.Seat];
		FKartItemVisual Visual;
		if (Now < Slot.SpinUntil)
		{
			Visual.SpinDegrees = 720.0 * EaseOut((Now - Slot.SpinStart) / 1.0);
		}
		if (Slot.ExplodeStart >= 0.0 && Now - Slot.ExplodeStart < 0.8)
		{
			const double U = (Now - Slot.ExplodeStart) / 0.8;
			Visual.PopCm = 200.0 * 4.0 * U * (1.0 - U);
			Visual.TumbleDegrees = 360.0 * U;
		}
		Visual.Scale = Now < Slot.ShrinkUntil ? 0.5 : 1.0;
		Visual.bAura = Now < Slot.PrismUntil;
		Visual.bRocket = Now < Slot.RocketUntil;
		if (UKartFxComponent* KartFx = bShowcase ? Racer.Kart->FindComponentByClass<UKartFxComponent>() : nullptr)
		{
			Visual.bAura |= Time < KartFx->Overrides().PrismUntil;
			Visual.bRocket |= Time < KartFx->Overrides().RocketUntil;
		}
		Racer.Kart->SetItemVisual(Visual);
	}
}
