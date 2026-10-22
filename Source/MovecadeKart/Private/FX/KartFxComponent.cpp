#include "FX/KartFxComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FX/KartFxMaterials.h"
#include "FX/KartFxSubsystem.h"
#include "FX/KartItemMeshes.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "Race/KartRaceManager.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

namespace
{
	const TCHAR* const SkidMaterialPath = TEXT("/Game/FX/Materials/M_FX_SkidMark.M_FX_SkidMark");

	/** The rocket shell rides this high over the kart's slip pivot; the prism shell is centred a little lower. */
	constexpr double RocketHeightCm = 72.0;
	constexpr double AuraHeightCm = 70.0;
	/** The wheel glow spheres (1 m across, scaled). */
	constexpr double WheelGlowScale = 0.34;
	/** A frame is clamped to this for the derived velocity (a hitch is not a jump in speed). */
	constexpr double MaxFrameSeconds = 0.05;

	/** Where the effects sit when a part is missing: a kart-sized guess, in the model frame (+X left, +Y forward). */
	const FVector FallbackExhaust(28.0, -128.0, 46.0);
	const FVector FallbackRearWheel(92.0, -95.0, -40.0);

	template <typename T = USceneComponent>
	T* FindPart(const AActor* Owner, const TCHAR* Name)
	{
		// Walks the owned set in place and compares interned names (no component array is built).
		const FName Wanted(Name, FNAME_Find);
		if (Wanted.IsNone())
		{
			return nullptr;
		}
		for (UActorComponent* C : Owner->GetComponents())
		{
			if (T* Part = Cast<T>(C); Part && Part->GetFName() == Wanted)
			{
				return Part;
			}
		}
		return nullptr;
	}

	/** The local bounding box of a part's mesh (in its parent's space: parts sit at identity under their pivot). */
	bool PartBox(const UStaticMeshComponent* Part, FBox& Out)
	{
		if (!Part || !Part->GetStaticMesh())
		{
			return false;
		}
		Out = Part->GetStaticMesh()->GetBoundingBox().TransformBy(Part->GetRelativeTransform());
		return Out.IsValid != 0;
	}
}

UKartFxComponent::UKartFxComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the pawn has posed its visual layers this frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UKartFxComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* Owner = GetOwner())
	{
		PrimaryComponentTick.AddPrerequisite(Owner, Owner->PrimaryActorTick);
	}
	Build();
}

UStaticMeshComponent* UKartFxComponent::MakeFxMesh(USceneComponent* Parent, FName Name, UStaticMesh* Mesh)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(GetOwner(), Name);
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(Parent);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetVisibility(false);
	C->RegisterComponent();
	return C;
}

void UKartFxComponent::Build()
{
	AActor* Owner = GetOwner();
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	if (bBuilt || !Owner || !Fx)
	{
		return;
	}
	bBuilt = true;
	USceneComponent* Root = Owner->GetRootComponent();
	// Body effects ride the sprung chassis, wheel effects the vehicle frame, item shells the slip yaw.
	ChassisPivot = FindPart(Owner, TEXT("ChassisPivot"));
	VehicleRoot = FindPart(Owner, TEXT("VehicleRoot"));
	SlipPivot = FindPart(Owner, TEXT("SlipPivot"));
	ChassisPivot = ChassisPivot ? ChassisPivot.Get() : Root;
	VehicleRoot = VehicleRoot ? VehicleRoot.Get() : Root;
	SlipPivot = SlipPivot ? SlipPivot.Get() : Root;

	for (int32 i = 0; i < 2; ++i)
	{
		BoostFire.Add(Fx->MakeStream(EField::Fire, ChassisPivot, *FString::Printf(TEXT("FxBoostFire%d"), i)));
		BoostCore.Add(Fx->MakeStream(EField::Fire, ChassisPivot, *FString::Printf(TEXT("FxBoostCore%d"), i)));
		ExhaustPuff.Add(Fx->MakeStream(EField::Smoke, ChassisPivot, *FString::Printf(TEXT("FxExhaustPuff%d"), i)));
		WheelSmoke.Add(Fx->MakeStream(EField::Smoke, VehicleRoot, *FString::Printf(TEXT("FxWheelSmoke%d"), i)));
		WheelDust.Add(Fx->MakeStream(EField::Smoke, VehicleRoot, *FString::Printf(TEXT("FxWheelDust%d"), i)));
		WheelSparks.Add(Fx->MakeStream(EField::Sparks, VehicleRoot, *FString::Printf(TEXT("FxWheelSparks%d"), i)));
	}
	BoostSparks = Fx->MakeStream(EField::Sparks, ChassisPivot, TEXT("FxBoostSparks"));
	AuraFlares = Fx->MakeStream(EField::Flare, SlipPivot, TEXT("FxAuraFlares"), FVector(0.0, 0.0, AuraHeightCm));
	BuildRocket(*Fx);
	BuildGlows();
	BuildSkidMarks();

	// Speed lines ride this kart's camera and only its own view sees them.
	if (UCameraComponent* Camera = Owner->FindComponentByClass<UCameraComponent>())
	{
		SpeedLines = Fx->MakeStream(EField::SpeedLines, Camera, TEXT("FxSpeedLines"));
		if (SpeedLines)
		{
			SpeedLines->SetOnlyOwnerSee(true);
		}
	}
}

void UKartFxComponent::BuildRocket(UKartFxSubsystem& Fx)
{
	// A modelled shell over the kart, its afterburner and smoke from the tail.
	Rocket = MakeFxMesh(SlipPivot, TEXT("FxRocket"), SharedRocketMesh());
	Rocket->SetRelativeLocation(FVector(0.0, 0.0, RocketHeightCm));
	Rocket->SetCastShadow(true);
	// Bullet Bill (MultiplayerKart.tsx:1677-1695): #111111, emissive #444444 0.3, metal 0.8, rough 0.3.
	auto Paint = [this](FName Slot, const FLinearColor& Base, const FLinearColor& Emissive, float Strength, float Metal, float Rough)
	{
		if (UMaterialInstanceDynamic* M = MakeItemMaterial(this, Base, Emissive, Strength, Metal, Rough))
		{
			Rocket->SetMaterial(Rocket->GetMaterialIndex(Slot), M);
		}
	};
	Paint(TEXT("Exhaust"), Hex(TEXT("2a2a2e")), Hex(TEXT("ff6a2a")), 2.5f, 0.6f, 0.4f);
	Paint(TEXT("Body"), Hex(TEXT("111111")), Hex(TEXT("444444")), 0.3f, 0.8f, 0.3f);
	Paint(TEXT("Band"), Hex(TEXT("e8e4dc")), Hex(TEXT("000000")), 0.f, 0.2f, 0.35f);
	Paint(TEXT("Fins"), Hex(TEXT("d8262c")), Hex(TEXT("441010")), 0.2f, 0.5f, 0.35f);
	const FVector Tail = FVector(0.0, 0.0, RocketHeightCm) + RocketExhaust();
	RocketFire = Fx.MakeStream(EField::Fire, SlipPivot, TEXT("FxRocketFire"), Tail);
	RocketCore = Fx.MakeStream(EField::Fire, SlipPivot, TEXT("FxRocketCore"), Tail);
	RocketSmoke = Fx.MakeStream(EField::Smoke, SlipPivot, TEXT("FxRocketSmoke"), Tail);
}

void UKartFxComponent::BuildGlows()
{
	// One per rear wheel while drifting (Glow.jsx), and the prism shell (MultiplayerKart.tsx:1697-1713).
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* G = MakeFxMesh(VehicleRoot, *FString::Printf(TEXT("FxWheelGlow%d"), i), Sphere);
		G->SetRelativeScale3D(FVector(WheelGlowScale));
		if (UMaterialInstanceDynamic* M = MakeGlowMaterial(this, 1.4f, 0.9f))
		{
			G->SetMaterial(0, M);
			GlowMaterials.Add(M);
		}
		WheelGlow.Add(G);
	}
	AuraShell = MakeFxMesh(SlipPivot, TEXT("FxAuraShell"), Sphere);
	AuraShell->SetRelativeLocation(FVector(0.0, 0.0, AuraHeightCm));
	AuraShell->SetRelativeScale3D(AuraShellScale);
	AuraMaterial = MakeGlowMaterial(this, 2.4f, 0.03f);
	if (AuraMaterial)
	{
		AuraMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.2f);
		AuraMaterial->SetScalarParameterValue(TEXT("Shimmer"), 1.f);
		AuraMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.3f));
		AuraShell->SetMaterial(0, AuraMaterial);
	}
}

void UKartFxComponent::BuildSkidMarks()
{
	// One instanced quad per segment, in world space, faded by M_FX_SkidMark.
	SkidMarks = NewObject<UInstancedStaticMeshComponent>(GetOwner(), TEXT("FxSkidMarks"));
	SkidMarks->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
	SkidMarks->SetupAttachment(GetOwner()->GetRootComponent());
	SkidMarks->SetUsingAbsoluteLocation(true);
	SkidMarks->SetUsingAbsoluteRotation(true);
	SkidMarks->SetUsingAbsoluteScale(true);
	SkidMarks->SetWorldTransform(FTransform::Identity);
	SkidMarks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkidMarks->SetCastShadow(false);
	SkidMarks->NumCustomDataFloats = 1;
	if (UMaterialInterface* Skid = LoadObject<UMaterialInterface>(nullptr, SkidMaterialPath))
	{
		SkidMaterial = UMaterialInstanceDynamic::Create(Skid, this);
		SkidMarks->SetMaterial(0, SkidMaterial);
	}
	SkidMarks->RegisterComponent();
}

void UKartFxComponent::RefreshAnchors()
{
	const AActor* Owner = GetOwner();
	// The Body part is found once; its mesh tells when the vehicle changed.
	if (!BodyPart.IsValid())
	{
		BodyPart = FindPart<UStaticMeshComponent>(Owner, TEXT("Body"));
	}
	const UStaticMeshComponent* Body = BodyPart.Get();
	UStaticMesh* BodyMesh = Body ? Body->GetStaticMesh() : nullptr;
	if (Anchors.bValid && AnchoredBody.Get() == BodyMesh)
	{
		return;
	}
	AnchoredBody = BodyMesh;
	FAnchors A;

	// Exhausts: the back of the booster (or of the body), in the model frame (+X left, +Y forward, Z up).
	FBox Box;
	const bool bBooster = PartBox(FindPart<UStaticMeshComponent>(Owner, TEXT("Booster")), Box);
	if (bBooster || PartBox(Body, Box))
	{
		const FVector C = Box.GetCenter(), E = Box.GetExtent();
		const double Z = bBooster ? C.Z : Box.Min.Z + E.Z * 0.6;
		for (int32 i = 0; i < 2; ++i)
		{
			A.Exhaust[i] = FVector(C.X + (i == 0 ? 1.0 : -1.0) * E.X * (bBooster ? 0.52 : 0.32), Box.Min.Y - 4.0, Z);
		}
	}
	else
	{
		A.Exhaust[0] = FallbackExhaust;
		A.Exhaust[1] = FVector(-FallbackExhaust.X, FallbackExhaust.Y, FallbackExhaust.Z);
	}

	// Rear wheels: the rearmost hubs, contact patch one radius below the hub (VehicleRoot space).
	struct FWheel { FVector Hub; double Radius; };
	TArray<FWheel, TInlineAllocator<4>> Wheels;
	for (int32 i = 0; i < 4; ++i)
	{
		const USceneComponent* Pivot = FindPart(Owner, *FString::Printf(TEXT("WheelPivot%d"), i));
		const UStaticMeshComponent* Wheel = FindPart<UStaticMeshComponent>(Owner, *FString::Printf(TEXT("Wheel%d"), i));
		if (Pivot && Wheel && Wheel->GetStaticMesh())
		{
			const FVector E = Wheel->GetStaticMesh()->GetBoundingBox().GetExtent();
			Wheels.Add({ Pivot->GetRelativeLocation(), FMath::Max(E.Y, E.Z) });
		}
	}
	if (Wheels.Num() > 0)
	{
		Wheels.Sort([](const FWheel& L, const FWheel& R) { return L.Hub.Y < R.Hub.Y; });
		TArray<FWheel, TInlineAllocator<4>> Rear;
		for (const FWheel& W : Wheels)
		{
			if (W.Hub.Y < Wheels[0].Hub.Y + 20.0)
			{
				Rear.Add(W);
			}
		}
		Rear.Sort([](const FWheel& L, const FWheel& R) { return L.Hub.X > R.Hub.X; });
		const FWheel& Left = Rear[0];
		const FWheel& Right = Rear.Last();
		const double Spread = Rear.Num() == 1 ? 12.0 : 0.0; // a single rear wheel (moto): split the effects a little
		A.WheelRadius = Left.Radius;
		A.Wheel[0] = Left.Hub + FVector(Spread, 0.0, -Left.Radius);
		A.Wheel[1] = Right.Hub + FVector(-Spread, 0.0, -Right.Radius);
	}
	else
	{
		A.Wheel[0] = FallbackRearWheel;
		A.Wheel[1] = FVector(-FallbackRearWheel.X, FallbackRearWheel.Y, FallbackRearWheel.Z);
	}
	A.bValid = true;
	Anchors = A;
	PlaceStreams();
}

void UKartFxComponent::PlaceStreams()
{
	for (int32 i = 0; i < 2; ++i)
	{
		for (UNiagaraComponent* C : { BoostFire[i].Get(), BoostCore[i].Get(), ExhaustPuff[i].Get() })
		{
			if (C)
			{
				C->SetRelativeLocation(Anchors.Exhaust[i]);
			}
		}
		for (UNiagaraComponent* C : { WheelSmoke[i].Get(), WheelDust[i].Get(), WheelSparks[i].Get() })
		{
			if (C)
			{
				C->SetRelativeLocation(Anchors.Wheel[i] + FVector(0.0, 0.0, 8.0));
			}
		}
		// Glows sit on the outside of the rear wheels, a little up from the contact patch.
		WheelGlow[i]->SetRelativeLocation(Anchors.Wheel[i] + FVector((i == 0 ? 1.0 : -1.0) * 8.0, -4.0, Anchors.WheelRadius * 0.55));
	}
	if (BoostSparks)
	{
		BoostSparks->SetRelativeLocation((Anchors.Exhaust[0] + Anchors.Exhaust[1]) * 0.5);
	}
}

FVector UKartFxComponent::RearGround() const
{
	return VehicleRoot->GetComponentTransform().TransformPosition((Anchors.Wheel[0] + Anchors.Wheel[1]) * 0.5);
}

void UKartFxComponent::Flash(double Peak, const FLinearColor& Color)
{
	if (const UWorld* World = GetWorld())
	{
		FlashStart = World->GetTimeSeconds();
		FlashPeak = Peak;
		FlashTint = Color;
	}
}

void UKartFxComponent::Zap()
{
	if (const UWorld* World = GetWorld())
	{
		ZapStart = World->GetTimeSeconds();
	}
}

bool UKartFxComponent::IsViewed() const
{
	const AKartRaceManager* R = Race.Get();
	const FKartRacer* Racer = R ? R->FindRacer(Cast<AKartPawn>(GetOwner())) : nullptr;
	return Racer && Racer->Viewer != nullptr;
}

void UKartFxComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AKartPawn* Kart = Cast<AKartPawn>(GetOwner());
	if (!Kart || !Kart->GetKartMovement() || !Kart->GetKartMovement()->HasSim())
	{
		return;
	}
	Build();
	if (!bBuilt)
	{
		return;
	}
	if (!Race.IsValid())
	{
		Race = AKartRaceManager::Get(this);
	}
	const double Dt = FMath::Min(static_cast<double>(DeltaTime), MaxFrameSeconds);
	if (Dt <= 0.0)
	{
		return;
	}
	RefreshAnchors();
	const FKartState& S = Kart->GetKartMovement()->SimState();
	const double Time = GetWorld()->GetTimeSeconds();
	const FVector Location = Kart->GetActorLocation();
	// Velocity from the rendered motion; a teleport (respawn) is not a velocity.
	LastVelocity = bSeen && S.TeleportCount == SeenTeleports ? (Location - LastLocation) / Dt : FVector::ZeroVector;
	LastLocation = Location;
	bSeen = true;

	// Item status for this kart (and any showcase override).
	const FKartItemSlot* Slot = nullptr;
	double ItemNow = 0.0;
	if (const AKartRaceManager* R = Race.Get())
	{
		const FKartRacer* Racer = R->FindRacer(Kart);
		if (Racer && R->GetItems().Slots().IsValidIndex(Racer->Seat))
		{
			Slot = &R->GetItems().Slots()[Racer->Seat];
			ItemNow = R->GetItemTime();
		}
	}
	const bool bRocket = (Slot && ItemNow < Slot->RocketUntil) || Time < Showcase.RocketUntil;
	const bool bPrism = (Slot && ItemNow < Slot->PrismUntil) || Time < Showcase.PrismUntil;
	const bool bBoosting = S.Turbo > 0.0 || (S.BoostMultiplier > 1.0 && S.BoostTimeRemaining > 0.0);

	UpdateEvents(S, Time);
	UpdateDriveFx(S, Time, bBoosting, bRocket);
	UpdateSkids(S, Time);
	UpdateItemFx(Time, bPrism, bRocket);
	UpdateSpeedLines(S, bRocket);
	UpdatePadFlashes(Time);
	UpdateScreen(Time, Slot, ItemNow, bPrism);
}
