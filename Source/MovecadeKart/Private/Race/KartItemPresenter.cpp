#include "Race/KartItemPresenter.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "FX/KartFx.h"
#include "FX/KartFxComponent.h"
#include "FX/KartFxSubsystem.h"
#include "KartCoords.h"
#include "KartPawn.h"
#include "NiagaraComponent.h"
#include "Race/KartItemPresenterTuning.h"
#include "Race/KartRaceManager.h"
#include "TrackBuilder.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart;
using namespace MovecadeKart::ItemPresenter;

AKartItemPresenter::AKartItemPresenter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// The item "box" is an original prize orb (glass sphere, spinning gem), not the web's yellow "?" cube, which
	// read as a famous franchise's question block. The gem, labels and items get their code-built meshes in Bind.
	Boxes = MakeMeshes(TEXT("ItemBoxes"), Sphere.Object);
	BoxCores = MakeMeshes(TEXT("ItemBoxCores"), nullptr);
	BoxLabels = MakeMeshes(TEXT("ItemBoxLabels"), nullptr);
	BoxHalos = MakeMeshes(TEXT("ItemBoxHalos"), Sphere.Object);
	ItemMeshes.SetNum(static_cast<int32>(EKartItem::Count));
	for (int32 i = 1; i < static_cast<int32>(EKartItem::Count); ++i)
	{
		// The bomb's one custom float drives its glow pulse.
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

UInstancedStaticMeshComponent* AKartItemPresenter::MeshesFor(EKartItem Item) const
{
	const int32 Index = static_cast<int32>(Item);
	return ItemMeshes.IsValidIndex(Index) ? ItemMeshes[Index].Get() : nullptr;
}

AKartPawn* AKartItemPresenter::KartAt(int32 Seat) const
{
	const AKartRaceManager* Race = Manager.Get();
	return Race && Race->GetRacers().IsValidIndex(Seat) ? Race->GetRacers()[Seat].Kart.Get() : nullptr;
}

FVector AKartItemPresenter::KartGround(int32 Seat) const
{
	const AKartPawn* Kart = KartAt(Seat);
	return Kart ? Kart->GetActorLocation() : FVector::ZeroVector;
}

void AKartItemPresenter::Bind(AKartRaceManager* InManager)
{
	check(InManager);
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
	ResetBoxes();
	StartShowcase(*InManager);
}

void AKartItemPresenter::ResetBoxes()
{
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
	for (const FVector& Box : Manager->GetTrack()->ItemBoxes)
	{
		const FVector P = Coords::ToUE(Box);
		BoxPositions.Add(P);
		BoxWasVisible.Add(true);
		BoxShownAt.Add(-1000.0);
		Boxes->AddInstance(FTransform(P), true);
		BoxCores->AddInstance(FTransform(P), true);
		BoxLabels->AddInstance(FTransform(P), true);
		BoxHalos->AddInstance(FTransform(P), true);
		UNiagaraComponent* Twinkle = Fx ? Fx->MakeStream(Fx::EField::Flare, RootComponent, NAME_None) : nullptr;
		if (Twinkle)
		{
			Twinkle->SetWorldLocation(P);
		}
		BoxTwinkles.Add(Twinkle);
	}
}

void AKartItemPresenter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AKartRaceManager* Race = Manager.Get();
	if (!Race || !Race->GetTrack())
	{
		return;
	}
	// Item time drives what the sim decided (box respawns, slot timers); world time drives the animation.
	const double Now = Race->GetItemTime();
	const double Time = GetWorld()->GetTimeSeconds();

	HandleEvents(Race->ConsumeItemEvents(), Time);
	UpdateShowcase(Time);
	UpdateShowcaseCapture(Time, DeltaSeconds);
	UpdateBoxes(Time, Now);
	UpdateItems(Time);
	UpdateBlasts(Time);
	UpdateLights(Time);
	UpdateStrikes(Time);
	UpdateKartVisuals(Time, Now);
}

void AKartItemPresenter::UpdateKartVisuals(double Time, double Now)
{
	const AKartRaceManager* Race = Manager.Get();
	const FKartItemWorld& Items = Race->GetItems();
	for (const FKartRacer& Racer : Race->GetRacers())
	{
		if (!Items.Slots().IsValidIndex(Racer.Seat))
		{
			continue;
		}
		const FKartItemSlot& Slot = Items.Slots()[Racer.Seat];
		FKartItemVisual Visual;
		if (Now < Slot.SpinUntil)
		{
			Visual.SpinDegrees = SpinOutDegrees * EaseOut((Now - Slot.SpinStart) / SpinOutSeconds);
		}
		if (Slot.ExplodeStart >= 0.0 && Now - Slot.ExplodeStart < ExplodePopSeconds)
		{
			// Thrown up in a parabola, tumbling one turn.
			const double U = (Now - Slot.ExplodeStart) / ExplodePopSeconds;
			Visual.PopCm = ExplodePopCm * 4.0 * U * (1.0 - U);
			Visual.TumbleDegrees = 360.0 * U;
		}
		Visual.Scale = Now < Slot.ShrinkUntil ? ShrinkScale : 1.0;
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
