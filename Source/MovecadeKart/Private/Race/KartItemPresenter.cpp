#include "Race/KartItemPresenter.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Race/KartRaceManager.h"
#include "TrackBuilder.h"
#include "UObject/ConstructorHelpers.h"

using namespace MovecadeKart;

namespace
{
	FLinearColor PresenterHex(const TCHAR* Value)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Value));
	}

	/** kartFxBus.ts KART_FX_PROFILES shake peaks (metres). */
	constexpr double ShakeHit = 0.34;
	constexpr double ShakeExplosion = 0.55;
	constexpr double ShakeSpin = 0.14;
	constexpr double ShakeWall = 0.20;
	constexpr double ShakeBump = 0.12;

	double PresenterEaseOut(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return 1.0 - FMath::Pow(1.0 - X, 3.0);
	}
}

AKartItemPresenter::AKartItemPresenter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = Material.Object;

	Boxes = MakeMeshes(TEXT("ItemBoxes"), Cube.Object, PresenterHex(TEXT("ffcc00")));
	Projectiles.SetNum(static_cast<int32>(EKartItem::Count));
	Projectiles[static_cast<int32>(EKartItem::Shot)] = MakeMeshes(TEXT("Shot"), Sphere.Object, PresenterHex(TEXT("00ff00")));
	Projectiles[static_cast<int32>(EKartItem::Seeker)] = MakeMeshes(TEXT("Seeker"), Sphere.Object, PresenterHex(TEXT("ff0000")));
	Projectiles[static_cast<int32>(EKartItem::SkySeeker)] = MakeMeshes(TEXT("SkySeeker"), Sphere.Object, PresenterHex(TEXT("0066ff")));
	Projectiles[static_cast<int32>(EKartItem::Banana)] = MakeMeshes(TEXT("Banana"), Cylinder.Object, PresenterHex(TEXT("ffff00")));
	Projectiles[static_cast<int32>(EKartItem::Bomb)] = MakeMeshes(TEXT("Bomb"), Sphere.Object, PresenterHex(TEXT("111111")));
	Blasts = MakeMeshes(TEXT("Blasts"), Sphere.Object, PresenterHex(TEXT("ff7a1f")));
}

UInstancedStaticMeshComponent* AKartItemPresenter::MakeMeshes(const TCHAR* Name, UStaticMesh* Mesh, const FLinearColor& Color)
{
	UInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	Component->SetupAttachment(RootComponent);
	Component->SetStaticMesh(Mesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetMobility(EComponentMobility::Movable);
	Component->ComponentTags.Add(*Color.ToFColor(true).ToHex());
	return Component;
}

void AKartItemPresenter::Bind(AKartRaceManager* InManager)
{
	Manager = InManager;
	InManager->OnHaptic.AddUObject(this, &AKartItemPresenter::HandleHaptic);

	// Colour each instanced mesh from the hex in its tag.
	TArray<UInstancedStaticMeshComponent*> All = { Boxes, Blasts };
	for (UInstancedStaticMeshComponent* Component : Projectiles)
	{
		if (Component)
		{
			All.Add(Component);
		}
	}
	for (UInstancedStaticMeshComponent* Component : All)
	{
		if (BaseMaterial && Component->ComponentTags.Num() > 0)
		{
			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			Material->SetVectorParameterValue(TEXT("Color"), PresenterHex(*Component->ComponentTags[0].ToString()));
			Component->SetMaterial(0, Material);
		}
	}

	// This presenter draws the live boxes; hide the track builder's static preview ones.
	for (TActorIterator<ATrackBuilder> It(GetWorld()); It; ++It)
	{
		It->SetPreviewItemBoxesVisible(false);
	}
	BoxPositions.Reset();
	Boxes->ClearInstances();
	const FTrack* Track = InManager->GetTrack();
	for (const FVector& Box : Track->ItemBoxes)
	{
		const FVector P = Coords::ToUE(Box);
		BoxPositions.Add(P);
		Boxes->AddInstance(FTransform(FRotator::ZeroRotator, P, FVector(2.0)), true);
	}
}

void AKartItemPresenter::HandleHaptic(int32 Seat, float Intensity, FName Type, bool bSustained)
{
	AKartRaceManager* Race = Manager.Get();
	if (!Race || bSustained || !Race->GetRacers().IsValidIndex(Seat))
	{
		return;
	}
	const double Scale = FMath::Clamp(static_cast<double>(Intensity), 0.15, 1.0);
	if (Type == TEXT("barrier"))
	{
		Race->GetRacers()[Seat].Kart->AddShake(ShakeWall * Scale);
	}
	else if (Type == TEXT("kart"))
	{
		Race->GetRacers()[Seat].Kart->AddShake(ShakeBump * Scale);
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
	const FKartItemWorld& Items = Race->GetItems();
	const double Now = Race->GetItemTime();
	const double Time = GetWorld()->GetTimeSeconds();

	// Events: blasts and shakes.
	for (const FKartItemEvent& E : Race->ConsumeItemEvents())
	{
		switch (E.Type)
		{
		case EKartItemEventType::Explosion:
			ActiveBlasts.Add({ Coords::ToUE(E.Position, 0.6), E.Radius, Time });
			// Global events shake every view at half strength (kartFxBus.ts:91-125).
			for (const FKartRacer& Racer : Race->GetRacers())
			{
				Racer.Kart->AddShake(ShakeExplosion * 0.5);
			}
			break;
		case EKartItemEventType::Hit:
			Race->GetRacers()[E.Seat].Kart->AddShake(ShakeHit);
			break;
		case EKartItemEventType::SpinOut:
			Race->GetRacers()[E.Seat].Kart->AddShake(ShakeSpin);
			break;
		default:
			break;
		}
	}

	// Item boxes: spin 1.5 rad/s, bob 0.1 m at 2.5 rad/s; hidden once every kart has collected them.
	for (int32 i = 0; i < BoxPositions.Num(); ++i)
	{
		const bool bVisible = Items.IsBoxVisible(i, Now);
		const FVector P = BoxPositions[i] + FVector(0.0, 0.0, 10.0 * FMath::Sin(2.5 * Time));
		Boxes->UpdateInstanceTransform(i, FTransform(FRotator(0.0, -FMath::RadiansToDegrees(1.5 * Time), 0.0), P, FVector(bVisible ? 2.0 : 0.0)), true, i == BoxPositions.Num() - 1);
	}

	// Projectiles.
	for (UInstancedStaticMeshComponent* Component : Projectiles)
	{
		if (Component)
		{
			Component->ClearInstances();
		}
	}
	for (const FKartItemProjectile& P : Items.Projectiles())
	{
		UInstancedStaticMeshComponent* Component = Projectiles.IsValidIndex(static_cast<int32>(P.Type)) ? Projectiles[static_cast<int32>(P.Type)].Get() : nullptr;
		if (!Component)
		{
			continue;
		}
		const double Yaw = FMath::Atan2(-P.Vel.X, -P.Vel.Y);
		switch (P.Type)
		{
		case EKartItem::Banana:
			Component->AddInstance(FTransform(FRotator(0.0, FMath::RadiansToDegrees(2.0 * P.Age), 20.0), Coords::ToUE(P.Pos, 0.3), FVector(0.25, 0.25, 0.5)), true);
			break;
		case EKartItem::Bomb:
			Component->AddInstance(FTransform(FRotator(FMath::RadiansToDegrees(3.0 * P.Age), 0.0, FMath::RadiansToDegrees(5.0 * P.Age)), Coords::ToUE(P.Pos, P.Y), FVector(0.8)), true);
			break;
		case EKartItem::SkySeeker:
			Component->AddInstance(FTransform(FRotator(0.0, Coords::YawToUE(Yaw), 10.0 * FMath::Sin(Time * 10.0)), Coords::ToUE(P.Pos, P.Y + 0.5), FVector(0.6 * (1.0 + 0.2 * FMath::Sin(Time * 8.0)))), true);
			break;
		default:
			Component->AddInstance(FTransform(FRotator(0.0, Coords::YawToUE(Yaw) + FMath::RadiansToDegrees(8.0 * P.Age), 0.0), Coords::ToUE(P.Pos, 0.3), FVector(0.6)), true);
			break;
		}
	}

	// Explosion blasts: a quick fireball that swells and collapses over 0.4 s (opaque graybox; Niagara in the art pass).
	Blasts->ClearInstances();
	ActiveBlasts.RemoveAll([Time](const FBlast& B) { return Time - B.Start > 0.4; });
	for (const FBlast& B : ActiveBlasts)
	{
		const double T = (Time - B.Start) / 0.4;
		const double Size = B.Radius * (T < 0.3 ? PresenterEaseOut(T / 0.3) : 1.0 - (T - 0.3) / 0.7);
		Blasts->AddInstance(FTransform(FRotator::ZeroRotator, B.Center, FVector(FMath::Max(0.01, Size * 0.3), FMath::Max(0.01, Size * 0.3), FMath::Max(0.01, Size * 0.18))), true);
	}

	// Kart item visuals.
	for (const FKartRacer& Racer : Race->GetRacers())
	{
		const FKartItemSlot& Slot = Items.Slots()[Racer.Seat];
		FKartItemVisual Visual;
		if (Now < Slot.SpinUntil)
		{
			Visual.SpinDegrees = 720.0 * PresenterEaseOut((Now - Slot.SpinStart) / 1.0);
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
		Racer.Kart->SetItemVisual(Visual);
	}
}
