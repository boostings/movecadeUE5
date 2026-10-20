#include "FX/KartFxComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FX/KartFxSubsystem.h"
#include "FX/KartItemMeshes.h"
#include "KartCoords.h"
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
	const TCHAR* GlowPath = TEXT("/Game/FX/Materials/M_FX_Glow.M_FX_Glow");
	const TCHAR* ItemPath = TEXT("/Game/FX/Materials/M_KartItem.M_KartItem");
	const TCHAR* ScreenPath = TEXT("/Game/FX/Materials/M_PP_KartScreen.M_PP_KartScreen");
	const TCHAR* PadPath = TEXT("/Game/FX/Materials/M_FX_PadFlash.M_FX_PadFlash");
	const TCHAR* SkidPath = TEXT("/Game/FX/Materials/M_FX_SkidMark.M_FX_SkidMark");

	constexpr double SkidSegment = 35.0;   // cm of travel per mark segment
	constexpr double SkidWidth = 22.0;     // cm
	constexpr int32 MaxSkidSegments = 240; // per kart, reused oldest first
	constexpr double PadFlashSeconds = 0.55;
	constexpr double ZapSeconds = 0.45;

	template <typename T = USceneComponent>
	T* FindPart(const AActor* Owner, const TCHAR* Name)
	{
		// Runs every tick (RefreshAnchors), so it walks the owned set in place and compares interned names.
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

	/** Material parameters written every frame, interned once. */
	namespace Param
	{
		const FName Color(TEXT("Color"));
		const FName Intensity(TEXT("Intensity"));
		const FName Now(TEXT("Now"));
		const FName Fade(TEXT("Fade"));
		const FName Age(TEXT("Age"));
		const FName Prism(TEXT("Prism"));
		const FName FlashT(TEXT("FlashT"));
		const FName InkAge(TEXT("InkAge"));
		const FName InkLeft(TEXT("InkLeft"));
		const FName Hit(TEXT("Hit"));
		const FName HitColor(TEXT("HitColor"));
		const FName Zap(TEXT("Zap"));
	}

	UStaticMeshComponent* MakeMesh(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, Name);
		C->SetStaticMesh(Mesh);
		C->SetupAttachment(Parent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetVisibility(false);
		C->RegisterComponent();
		return C;
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
	AuraFlares = Fx->MakeStream(EField::Flare, SlipPivot, TEXT("FxAuraFlares"), FVector(0.0, 0.0, 70.0));

	// Rocket: a modelled shell over the kart, its afterburner and smoke from the tail.
	Rocket = MakeMesh(Owner, SlipPivot, TEXT("FxRocket"), BuildRocketMesh(this));
	Rocket->SetRelativeLocation(FVector(0.0, 0.0, 72.0));
	Rocket->SetCastShadow(true);
	if (UMaterialInterface* Item = LoadObject<UMaterialInterface>(nullptr, ItemPath))
	{
		// Bullet Bill (MultiplayerKart.tsx:1677-1695): #111111, emissive #444444 0.3, metal 0.8, rough 0.3.
		auto Paint = [&](FName Slot, const FLinearColor& Base, const FLinearColor& Emissive, float Strength, float Metal, float Rough)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Item, this);
			M->SetVectorParameterValue(TEXT("BaseColor"), Base);
			M->SetVectorParameterValue(TEXT("Emissive"), Emissive);
			M->SetScalarParameterValue(TEXT("EmissiveStrength"), Strength);
			M->SetScalarParameterValue(TEXT("Metallic"), Metal);
			M->SetScalarParameterValue(TEXT("Roughness"), Rough);
			Rocket->SetMaterial(Rocket->GetMaterialIndex(Slot), M);
		};
		Paint(TEXT("Exhaust"), Hex(TEXT("2a2a2e")), Hex(TEXT("ff6a2a")), 2.5f, 0.6f, 0.4f);
		Paint(TEXT("Body"), Hex(TEXT("111111")), Hex(TEXT("444444")), 0.3f, 0.8f, 0.3f);
		Paint(TEXT("Band"), Hex(TEXT("e8e4dc")), Hex(TEXT("000000")), 0.f, 0.2f, 0.35f);
		Paint(TEXT("Fins"), Hex(TEXT("d8262c")), Hex(TEXT("441010")), 0.2f, 0.5f, 0.35f);
	}
	const FVector Tail = FVector(0.0, 0.0, 72.0) + RocketExhaust();
	RocketFire = Fx->MakeStream(EField::Fire, SlipPivot, TEXT("FxRocketFire"), Tail);
	RocketCore = Fx->MakeStream(EField::Fire, SlipPivot, TEXT("FxRocketCore"), Tail);
	RocketSmoke = Fx->MakeStream(EField::Smoke, SlipPivot, TEXT("FxRocketSmoke"), Tail);

	// Glows: one per rear wheel while drifting (Glow.jsx), and the prism shell (MultiplayerKart.tsx:1697-1713).
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, GlowPath);
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* G = MakeMesh(Owner, VehicleRoot, *FString::Printf(TEXT("FxWheelGlow%d"), i), Sphere);
		G->SetRelativeScale3D(FVector(0.34));
		if (Glow)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Glow, this);
			M->SetScalarParameterValue(TEXT("Power"), 1.4f);
			M->SetScalarParameterValue(TEXT("Core"), 0.9f);
			G->SetMaterial(0, M);
			GlowMaterials.Add(M);
		}
		WheelGlow.Add(G);
	}
	AuraShell = MakeMesh(Owner, SlipPivot, TEXT("FxAuraShell"), Sphere);
	AuraShell->SetRelativeLocation(FVector(0.0, 0.0, 70.0));
	AuraShell->SetRelativeScale3D(FVector(3.0, 2.3, 1.9));
	if (Glow)
	{
		AuraMaterial = UMaterialInstanceDynamic::Create(Glow, this);
		AuraMaterial->SetScalarParameterValue(TEXT("Power"), 2.4f);
		AuraMaterial->SetScalarParameterValue(TEXT("Core"), 0.03f);
		AuraMaterial->SetScalarParameterValue(TEXT("Intensity"), 2.2f);
		AuraMaterial->SetScalarParameterValue(TEXT("Shimmer"), 1.f);
		AuraMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.3f));
		AuraShell->SetMaterial(0, AuraMaterial);
	}

	// Tyre marks: one instanced quad per segment, in world space, faded by M_FX_SkidMark.
	SkidMarks = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("FxSkidMarks"));
	SkidMarks->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
	SkidMarks->SetupAttachment(Root);
	SkidMarks->SetUsingAbsoluteLocation(true);
	SkidMarks->SetUsingAbsoluteRotation(true);
	SkidMarks->SetUsingAbsoluteScale(true);
	SkidMarks->SetWorldTransform(FTransform::Identity);
	SkidMarks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkidMarks->SetCastShadow(false);
	SkidMarks->NumCustomDataFloats = 1;
	if (UMaterialInterface* Skid = LoadObject<UMaterialInterface>(nullptr, SkidPath))
	{
		SkidMaterial = UMaterialInstanceDynamic::Create(Skid, this);
		SkidMarks->SetMaterial(0, SkidMaterial);
	}
	SkidMarks->RegisterComponent();

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

void UKartFxComponent::RefreshAnchors()
{
	const AActor* Owner = GetOwner();
	const UStaticMeshComponent* Body = FindPart<UStaticMeshComponent>(Owner, TEXT("Body"));
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
		A.Exhaust[0] = FVector(28.0, -128.0, 46.0);
		A.Exhaust[1] = FVector(-28.0, -128.0, 46.0);
	}

	// Rear wheels: the rearmost hubs, contact patch one radius below the hub (VehicleRoot space).
	struct FWheel { FVector Hub; double Radius; };
	TArray<FWheel> Wheels;
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
		TArray<FWheel> Rear = Wheels.FilterByPredicate([&](const FWheel& W) { return W.Hub.Y < Wheels[0].Hub.Y + 20.0; });
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
		A.Wheel[0] = FVector(92.0, -95.0, -40.0);
		A.Wheel[1] = FVector(-92.0, -95.0, -40.0);
	}
	A.bValid = true;
	Anchors = A;

	for (int32 i = 0; i < 2; ++i)
	{
		for (UNiagaraComponent* C : { BoostFire[i].Get(), BoostCore[i].Get(), ExhaustPuff[i].Get() })
		{
			if (C)
			{
				C->SetRelativeLocation(A.Exhaust[i]);
			}
		}
		for (UNiagaraComponent* C : { WheelSmoke[i].Get(), WheelDust[i].Get(), WheelSparks[i].Get() })
		{
			if (C)
			{
				C->SetRelativeLocation(A.Wheel[i] + FVector(0.0, 0.0, 8.0));
			}
		}
		// Glows sit on the outside of the rear wheels, a little up from the contact patch.
		WheelGlow[i]->SetRelativeLocation(A.Wheel[i] + FVector((i == 0 ? 1.0 : -1.0) * 8.0, -4.0, A.WheelRadius * 0.55));
	}
	if (BoostSparks)
	{
		BoostSparks->SetRelativeLocation((A.Exhaust[0] + A.Exhaust[1]) * 0.5);
	}
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
	const double Dt = FMath::Min(static_cast<double>(DeltaTime), 0.05);
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
	UpdateDriveFx(S, Time, Dt, bBoosting, bRocket);
	UpdateSkids(S, Time);
	UpdateItemFx(Time, bPrism, bRocket);
	UpdateSpeedLines(S, bRocket);
	UpdatePadFlashes(Time);
	UpdateScreen(Time, Slot, ItemNow, bPrism);
}

void UKartFxComponent::UpdateEvents(const FKartState& S, double Time)
{
	AKartPawn* Kart = Cast<AKartPawn>(GetOwner());
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	if (!Fx)
	{
		return;
	}
	const FVector Location = Kart->GetActorLocation();
	const FVector Ground = VehicleRoot->GetComponentTransform().TransformPosition((Anchors.Wheel[0] + Anchors.Wheel[1]) * 0.5);
	TArray<FKartFxEmit> Out;

	// Wall scrapes and kart bumps: once per sim event, however many steps passed this frame (kartFxBus 'wall', 'bump').
	const FVector Normal = Coords::ToUE(S.ImpactNormal, 0.0).GetSafeNormal();
	if (S.BarrierEventCount != SeenBarrierEvents)
	{
		SeenBarrierEvents = S.BarrierEventCount;
		const double Scale = FMath::Clamp(FMath::Max(S.BarrierEvent, S.CollisionIntensity), 0.2, 1.0);
		const bool bScrape = S.bBarrierEventSustained;
		// The contact is a kart radius (1.15 m) toward the wall, which is opposite the normal.
		const FVector Contact = FVector(Location.X, Location.Y, Ground.Z) - Normal * 115.0;
		const int32 First = Out.Num();
		ImpactBurst(EImpact::Wall, Contact, bScrape ? Scale * 0.45 : Scale, Out);
		for (int32 i = First; i < Out.Num(); ++i)
		{
			Out[i].InheritCm = Velocity() * 0.6;
			if (Out[i].Field == EField::Sparks)
			{
				// Spray back along the wall and away from it.
				Out[i].Direction = (Normal + FVector::UpVector * 0.6 - Velocity().GetSafeNormal() * 0.5).GetSafeNormal();
				Out[i].Spread = 0.9;
			}
		}
		if (!bScrape)
		{
			Kart->AddShake(ImpactProfile(EImpact::Wall).Shake * Scale);
		}
	}
	if (S.BumpEventCount != SeenBumpEvents)
	{
		SeenBumpEvents = S.BumpEventCount;
		const double Scale = FMath::Clamp(S.BumpEvent, 0.2, 1.0);
		ImpactBurst(EImpact::Bump, FVector(Location.X, Location.Y, Ground.Z) - Normal * 90.0, Scale, Out);
		Kart->AddShake(ImpactProfile(EImpact::Bump).Shake * Scale);
	}

	// Respawn: a pop of smoke and twinkles where the kart reappears.
	if (S.TeleportCount != SeenTeleports)
	{
		SeenTeleports = S.TeleportCount;
		Poof(Location + FVector(0.0, 0.0, 60.0), Hex(TEXT("8fd8ff")), 1.6, Out);
	}

	// Landing after a hop: dust under the rear wheels, harder the faster the kart was going.
	const bool bHopping = S.HopTime >= 0.0;
	if (bWasHopping && !bHopping)
	{
		const double Strength = FMath::Clamp(FMath::Abs(S.Speed) / 40.0, 0.25, 1.0);
		for (int32 i = 0; i < 2; ++i)
		{
			LandingPuff(VehicleRoot->GetComponentTransform().TransformPosition(Anchors.Wheel[i]), Velocity(), Strength, S.bOnDirt, Out);
		}
	}
	bWasHopping = bHopping;

	// Boost pads: the sim stamps the pad's time when this kart triggers it.
	if (LastPadBoost.Num() != S.PadLastBoostTime.Num())
	{
		LastPadBoost = S.PadLastBoostTime;
	}
	const AKartRaceManager* R = Race.Get();
	const FTrack* Track = R ? R->GetTrack() : nullptr;
	for (int32 i = 0; i < S.PadLastBoostTime.Num(); ++i)
	{
		if (S.PadLastBoostTime[i] == LastPadBoost[i])
		{
			continue;
		}
		LastPadBoost[i] = S.PadLastBoostTime[i];
		if (!Track || !Track->BoostPads.IsValidIndex(i))
		{
			continue;
		}
		const FBoostPad& Pad = Track->BoostPads[i];
		const FVector Centre(Coords::ToUE(Pad.Position).X, Coords::ToUE(Pad.Position).Y, Ground.Z + 6.0);
		if (!PadFlashes.IsValidIndex(i))
		{
			PadFlashes.SetNum(Track->BoostPads.Num());
			PadMaterials.SetNum(Track->BoostPads.Num());
			PadFlashStart.Init(-1000.0, Track->BoostPads.Num());
		}
		if (!PadFlashes[i])
		{
			UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
			UStaticMeshComponent* C = MakeMesh(GetOwner(), nullptr, *FString::Printf(TEXT("FxPadFlash%d"), i), Plane);
			C->SetUsingAbsoluteLocation(true);
			C->SetUsingAbsoluteRotation(true);
			C->SetUsingAbsoluteScale(true);
			if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, PadPath))
			{
				PadMaterials[i] = UMaterialInstanceDynamic::Create(M, this);
				C->SetMaterial(0, PadMaterials[i]);
			}
			PadFlashes[i] = C;
		}
		// The Plane is 1 m square in X/Y; X along the pad, Y across (TrackBuilder). Chevrons run along +V, so turn it.
		PadFlashes[i]->SetWorldTransform(FTransform(FRotator(0.0, Coords::YawToUE(Pad.Yaw) + 90.0, 0.0), Centre, FVector(Pad.Width * 1.05, Pad.Length * 1.05, 1.0)));
		PadFlashStart[i] = Time;

		const FVector Forward = Kart->GetActorForwardVector();
		FKartFxEmit Burst;
		Burst.Field = EField::Sparks;
		Burst.Position = Location + FVector(0.0, 0.0, 30.0);
		Burst.Count = 26;
		Burst.Direction = (Forward + FVector::UpVector * 0.4).GetSafeNormal();
		Burst.Spread = 0.7;
		Burst.Speed = { 6.0, 14.0 };
		Burst.Life = { 0.25, 0.5 };
		Burst.Size = { 0.06, 0.11 };
		Burst.Color = Hex(TEXT("fff2b0"));
		Burst.ColorEnd = Hex(TEXT("ff8a1a"));
		Burst.Intensity = 2.8;
		Burst.InheritCm = Velocity();
		Out.Add(Burst);
		FKartFxEmit Ring;
		Ring.Field = EField::Ring;
		Ring.Position = FVector(Location.X, Location.Y, Ground.Z + 10.0);
		Ring.Count = 1;
		Ring.Speed = { 0.0, 0.0 };
		Ring.Life = { 0.35, 0.35 };
		Ring.Size = { 1.0, 1.0 };
		Ring.SizeEnd = 4.0;
		Ring.Color = Hex(TEXT("ffd36a"));
		Ring.ColorEnd = Hex(TEXT("ff7a1a"));
		Ring.Intensity = 2.2;
		Ring.InheritCm = Velocity() * 0.8;
		Out.Add(Ring);
	}
	Fx->Emit(Out);
}

void UKartFxComponent::UpdateDriveFx(const FKartState& S, double Time, double Dt, bool bBoosting, bool bRocket)
{
	AKartPawn* Kart = Cast<AKartPawn>(GetOwner());
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	const FVector Forward = Kart->GetActorForwardVector();
	const FVector Back = (-Forward + FVector(0.0, 0.0, 0.12)).GetSafeNormal();
	const FVector Inherit = Velocity() * 0.85;
	const double Speed = FMath::Abs(S.Speed);

	// Boost flames (KartFxLayer.tsx:159-194): 70/s per pipe. They inherit the kart's whole velocity so each pipe
	// grows a short flame cone instead of a trail of blobs, a hot blue-white core inside an orange flame.
	{
		// Over a flame's ~0.15 s life the fire field's drag (3/s) eats a fifth of the inherited speed: overshoot it.
		const FVector PipeInherit = Velocity() * 1.22;
		const bool bItemBoost = S.BoostMultiplier >= 2.0;
		FKartFxEmit Fire;
		Fire.Field = EField::Fire;
		Fire.Direction = Back;
		Fire.Spread = 0.2;
		Fire.Speed = { 2.5, 4.5 };
		Fire.Life = { 0.1, 0.2 };
		Fire.Size = { 0.32, 0.44 };
		Fire.SizeEnd = 0.3;
		Fire.Color = Hex(TEXT("ffc94a"));
		Fire.ColorEnd = Hex(TEXT("ff3a0a"));
		Fire.Intensity = bItemBoost ? 3.4 : 2.8;
		Fire.InheritCm = PipeInherit;
		const double Rate = bBoosting && !bRocket ? BoostFlameRate : 0.0;
		for (UNiagaraComponent* C : BoostFire)
		{
			UKartFxSubsystem::UpdateStream(C, Fire, Rate);
		}
		FKartFxEmit Core = Fire;
		Core.Speed = { 1.0, 2.0 };
		Core.Life = { 0.06, 0.1 };
		Core.Size = { 0.22, 0.28 };
		Core.SizeEnd = 0.6;
		Core.Color = Hex(TEXT("eaf4ff"));
		Core.ColorEnd = Hex(TEXT("5aa8ff"));
		Core.Intensity = 4.0;
		for (UNiagaraComponent* C : BoostCore)
		{
			UKartFxSubsystem::UpdateStream(C, Core, Rate > 0.0 ? 50.0 : 0.0);
		}
		FKartFxEmit Spark;
		Spark.Field = EField::Sparks;
		Spark.Direction = Back;
		Spark.Spread = 0.35;
		Spark.Speed = { 5.0, 8.0 };
		Spark.Life = { 0.15, 0.3 };
		Spark.Size = { 0.05, 0.08 };
		Spark.Color = Hex(TEXT("ffe8a8"));
		Spark.ColorEnd = Hex(TEXT("ff6a2a"));
		Spark.Intensity = 2.4;
		Spark.Jitter = 0.2;
		Spark.InheritCm = Velocity();
		UKartFxSubsystem::UpdateStream(BoostSparks, Spark, Rate > 0.0 ? 40.0 : 0.0);
	}

	// Exhaust puffs at cruise (Kart.jsx:485: speed > 15 and not boosting).
	{
		FKartFxEmit Puff;
		Puff.Field = EField::Smoke;
		Puff.Direction = Back;
		Puff.Spread = 0.4;
		Puff.Speed = { 0.28, 0.58 };
		Puff.Life = { 0.25, 0.45 };
		Puff.Size = { 0.12, 0.22 };
		Puff.SizeEnd = 2.4;
		Puff.Color = Hex(TEXT("f2f0ee"));
		Puff.ColorEnd = Hex(TEXT("d0ccc6"));
		Puff.InheritCm = Inherit * 0.6;
		const double Rate = Speed > 15.0 && !bBoosting && !bRocket ? 6.0 : 0.0;
		for (UNiagaraComponent* C : ExhaustPuff)
		{
			UKartFxSubsystem::UpdateStream(C, Puff, Rate);
		}
	}

	// Drift smoke, drift sparks and wheel glow (KartFxLayer.tsx:197-245, Glow.jsx).
	const bool bDrifting = S.DriftDirection != 0.0 && Speed > DriftSmokeMinSpeed && S.HopTime < 0.0;
	const FDriftLevel Level = DriftLevel(S.DriftPower);
	FKartFxEmit Spark;
	Spark.Field = EField::Sparks;
	Spark.Direction = FVector(Back.X, Back.Y, 0.7).GetSafeNormal();
	Spark.Spread = 0.7;
	Spark.Speed = { 3.0, 6.5 };
	Spark.Life = { 0.12, 0.3 };
	Spark.Size = { 0.045, 0.08 };
	Spark.Color = FLinearColor::LerpUsingHSV(Level.Color, FLinearColor::White, 0.35f);
	Spark.ColorEnd = Level.Color;
	Spark.Intensity = 3.0;
	Spark.InheritCm = Inherit * 0.6;
	{
		FKartFxEmit Smoke;
		Smoke.Field = EField::Smoke;
		Smoke.Direction = FVector(Back.X, Back.Y, 0.5).GetSafeNormal();
		Smoke.Spread = 0.6;
		Smoke.Speed = { 0.6, 1.6 };
		Smoke.Life = { 0.45, 0.8 };
		Smoke.Size = { 0.22, 0.34 };
		Smoke.SizeEnd = 2.8;
		Smoke.Color = Hex(TEXT("f4f3f6"));
		Smoke.ColorEnd = Hex(TEXT("cfcdd6"));
		Smoke.InheritCm = Inherit * 0.3;
		Smoke.Jitter = 0.1;
		for (UNiagaraComponent* C : WheelSmoke)
		{
			UKartFxSubsystem::UpdateStream(C, Smoke, bDrifting ? DriftSmokeRate * 0.7 : 0.0);
		}
		// More and faster sparks as the tier climbs.
		const double SparkRate = bDrifting && Level.Level > 0 ? 20.0 + Level.Level * 16.0 : 0.0;
		Spark.Speed *= 1.0 + 0.15 * Level.Level;
		for (UNiagaraComponent* C : WheelSparks)
		{
			UKartFxSubsystem::UpdateStream(C, Spark, SparkRate);
		}

		// Glow: white core, tier-coloured rim, on while drifting (Glow.jsx), pulsing faster at higher tiers.
		const bool bGlow = S.DriftDirection != 0.0;
		const double Pulse = 1.0 + 0.2 * FMath::Sin(Time * (22.0 + 6.0 * Level.Level));
		for (int32 i = 0; i < WheelGlow.Num(); ++i)
		{
			WheelGlow[i]->SetVisibility(bGlow);
			if (bGlow && GlowMaterials.IsValidIndex(i))
			{
				const FLinearColor C = Level.Level > 0 ? Level.Color : FLinearColor(0.9f, 0.9f, 1.f);
				GlowMaterials[i]->SetVectorParameterValue(Param::Color, C);
				GlowMaterials[i]->SetScalarParameterValue(Param::Intensity, static_cast<float>((Level.Level > 0 ? 6.0 : 1.2) * Pulse));
				WheelGlow[i]->SetRelativeScale3D(FVector((Level.Level > 0 ? 0.3 + 0.07 * Level.Level : 0.2) * Pulse));
			}
		}
	}

	if (Fx)
	{
		TArray<FKartFxEmit> Out;
		// A level-up flick of sparks and a twinkle in the new colour (Glow.jsx setColor -> Spark.emit).
		if (S.DriftDirection != 0.0 && Level.Level > LastDriftLevel.Level)
		{
			for (const UStaticMeshComponent* G : WheelGlow)
			{
				FKartFxEmit Flick = Spark;
				Flick.Position = G->GetComponentLocation();
				Flick.Count = 16;
				Flick.Spread = UE_DOUBLE_PI * 0.6;
				Flick.Direction = FVector::UpVector;
				Flick.Speed = { 2.0, 5.5 };
				Flick.Life = { 0.2, 0.4 };
				Flick.Size = { 0.05, 0.09 };
				Flick.InheritCm = Inherit;
				Out.Add(Flick);
				FKartFxEmit Star;
				Star.Field = EField::Flare;
				Star.Position = Flick.Position;
				Star.Count = 1;
				Star.Speed = { 0.0, 0.0 };
				Star.Life = { 0.25, 0.25 };
				Star.Size = { 0.9, 0.9 };
				Star.SizeEnd = 0.3;
				Star.Color = FLinearColor::White;
				Star.ColorEnd = Level.Color;
				Star.Intensity = 4.0;
				Star.InheritCm = Velocity();
				Out.Add(Star);
			}
		}
		if (S.DriftDirection != 0.0)
		{
			LastDriftLevel = Level;
		}
		else if (S.DriftBoostCount == LastDriftBoostCount)
		{
			LastDriftLevel = FDriftLevel();
		}

		// Mini-turbo release: a ring and sparks in the grade's colour, a gout of flame from both pipes.
		if (S.DriftBoostCount != LastDriftBoostCount)
		{
			const FLinearColor Grade = DriftGradeColor(S.LastDriftBoost);
			// The pipes' midpoint; the kart's own origin if the streams are missing (FX content not built or cooked).
			const FVector Rear = BoostFire.Num() == 2 && BoostFire[0] && BoostFire[1]
				? (BoostFire[0]->GetComponentLocation() + BoostFire[1]->GetComponentLocation()) * 0.5
				: VehicleRoot->GetComponentLocation();
			const FVector Ground = VehicleRoot->GetComponentTransform().TransformPosition((Anchors.Wheel[0] + Anchors.Wheel[1]) * 0.5);
			FKartFxEmit Ring;
			Ring.Field = EField::Ring;
			Ring.Position = FVector(Rear.X, Rear.Y, Ground.Z + 10.0);
			Ring.Count = 1;
			Ring.Speed = { 0.0, 0.0 };
			Ring.Life = { 0.35, 0.35 };
			Ring.Size = { 0.8, 0.8 };
			Ring.SizeEnd = 4.5;
			Ring.Color = Grade;
			Ring.ColorEnd = Grade * 0.6f;
			Ring.Intensity = 2.4;
			Ring.InheritCm = Velocity();
			Out.Add(Ring);
			FKartFxEmit Burst = Spark;
			Burst.Position = Rear;
			Burst.Count = 30;
			Burst.Spread = 1.1;
			Burst.Direction = FVector(Back.X, Back.Y, 0.6).GetSafeNormal();
			Burst.Speed = { 4.0, 10.0 };
			Burst.Life = { 0.2, 0.45 };
			Burst.Size = { 0.05, 0.1 };
			Burst.Color = FLinearColor::White;
			Burst.ColorEnd = Grade;
			Burst.Jitter = 0.4;
			Burst.InheritCm = Inherit;
			Out.Add(Burst);
			for (const UNiagaraComponent* Pipe : BoostFire)
			{
				if (!Pipe)
				{
					continue; // no FX content: MakeStream returned null
				}
				FKartFxEmit Gout;
				Gout.Field = EField::Fire;
				Gout.Position = Pipe->GetComponentLocation();
				Gout.Count = 12;
				Gout.Direction = Back;
				Gout.Spread = 0.35;
				Gout.Speed = { 3.0, 6.5 };
				Gout.Life = { 0.15, 0.3 };
				Gout.Size = { 0.4, 0.6 };
				Gout.SizeEnd = 0.4;
				Gout.Color = Hex(TEXT("fff0b8"));
				Gout.ColorEnd = Grade;
				Gout.Intensity = 3.0;
				Gout.InheritCm = Inherit;
				Out.Add(Gout);
			}
			LastDriftBoostCount = S.DriftBoostCount;
		}
		Fx->Emit(Out);
	}

	// Off-road dust (KartFxLayer.tsx:247-266): chunky tan puffs kicked up behind the rear wheels.
	{
		FKartFxEmit Dust;
		Dust.Field = EField::Smoke;
		Dust.Direction = FVector(Back.X, Back.Y, 0.8).GetSafeNormal();
		Dust.Spread = 0.7;
		Dust.Speed = { 0.8, 2.2 };
		Dust.Life = { 0.45, 0.8 };
		Dust.Size = { 0.24, 0.4 };
		Dust.SizeEnd = 2.6;
		Dust.Color = Hex(TEXT("e3c99c"));
		Dust.ColorEnd = Hex(TEXT("c9ab7c"));
		Dust.InheritCm = Inherit * 0.25;
		Dust.Jitter = 0.15;
		const double Rate = S.bOnDirt && Speed > DustMinSpeed ? DustRate * FMath::Clamp(Speed / 25.0, 0.4, 1.0) : 0.0;
		for (UNiagaraComponent* C : WheelDust)
		{
			UKartFxSubsystem::UpdateStream(C, Dust, Rate);
		}
	}
	(void)Dt;
}

void UKartFxComponent::UpdateSkids(const FKartState& S, double Time)
{
	if (!SkidMarks)
	{
		return;
	}
	const double Speed = FMath::Abs(S.Speed);
	// Marks while drifting, or cornering hard enough that the tail slips; never in the air or on dirt.
	const bool bSkid = S.HopTime < 0.0 && !S.bOnDirt && Speed > 10.0 &&
		(S.DriftDirection != 0.0 || (FMath::Abs(S.SlipYaw) > 0.16 && Speed > 22.0));
	if (SkidMaterial)
	{
		SkidMaterial->SetScalarParameterValue(Param::Now, static_cast<float>(Time));
	}
	const FTransform Frame = VehicleRoot->GetComponentTransform();
	for (int32 Wheel = 0; Wheel < 2; ++Wheel)
	{
		// Just above the road under the tyre.
		const FVector Contact = Frame.TransformPosition(Anchors.Wheel[Wheel]) + FVector(0.0, 0.0, 1.5);
		if (!bSkid)
		{
			bMarking[Wheel] = false;
			continue;
		}
		if (!bMarking[Wheel])
		{
			bMarking[Wheel] = true;
			LastMark[Wheel] = Contact;
			continue;
		}
		const FVector Step = Contact - LastMark[Wheel];
		if (Step.SizeSquared() < FMath::Square(SkidSegment))
		{
			continue;
		}
		if (Step.SizeSquared() > FMath::Square(SkidSegment * 8.0))
		{
			// A respawn or a frame hitch: start a fresh mark rather than streak across the track.
			LastMark[Wheel] = Contact;
			continue;
		}
		// The Plane is 1 m square in X/Y: stretch X along the step (a little overlap hides the joins).
		const FTransform Segment(Step.Rotation(), (Contact + LastMark[Wheel]) * 0.5, FVector(Step.Size() / 100.0 * 1.15, SkidWidth / 100.0, 1.0));
		int32 Index;
		if (SkidMarks->GetInstanceCount() < MaxSkidSegments)
		{
			Index = SkidMarks->AddInstance(Segment, /*bWorldSpace*/ true);
		}
		else
		{
			Index = NextSkid;
			SkidMarks->UpdateInstanceTransform(Index, Segment, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false, /*bTeleport*/ true);
		}
		NextSkid = (Index + 1) % MaxSkidSegments;
		// The material fades each segment out by its age: custom data 0 is when it was laid, "Now" the time. Adding,
		// moving and re-stamping an instance are all incremental instance updates; dirtying the render state here
		// rebuilt the whole skid-mark proxy for every new segment.
		SkidMarks->SetCustomDataValue(Index, 0, static_cast<float>(Time), /*bMarkRenderStateDirty*/ false);
		LastMark[Wheel] = Contact;
	}
}

void UKartFxComponent::UpdateItemFx(double Time, bool bPrism, bool bRocket)
{
	AKartPawn* Kart = Cast<AKartPawn>(GetOwner());
	const FVector Forward = Kart->GetActorForwardVector();
	const FVector Inherit = Velocity() * 0.85;

	// Prism: a rainbow-shimmer shell, rising rainbow twinkles and a sparkle trail.
	const FLinearColor Rainbow = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Time * 300.0, 256.0)), 190, 255);
	AuraShell->SetVisibility(bPrism && !bRocket);
	if (AuraMaterial && bPrism)
	{
		AuraMaterial->SetVectorParameterValue(Param::Color, Rainbow);
		const double Breathe = 1.0 + 0.04 * FMath::Sin(Time * 9.0);
		AuraShell->SetRelativeScale3D(FVector(3.0, 2.3, 1.9) * Breathe);
	}
	FKartFxEmit Twinkle;
	Twinkle.Field = EField::Flare;
	Twinkle.Direction = FVector::UpVector;
	Twinkle.Spread = 1.0;
	Twinkle.Speed = { 0.4, 1.8 };
	Twinkle.Life = { 0.35, 0.7 };
	Twinkle.Size = { 0.25, 0.5 };
	Twinkle.SizeEnd = 0.2;
	Twinkle.Color = FLinearColor::White;
	Twinkle.ColorEnd = Rainbow;
	Twinkle.Intensity = 3.2;
	Twinkle.Jitter = 1.1;
	Twinkle.InheritCm = Inherit * 0.7;
	UKartFxSubsystem::UpdateStream(AuraFlares, Twinkle, bPrism ? 40.0 : 0.0);

	// Rocket: the shell replaces the kart (the pawn hides the vehicle); afterburner, hot core and smoke from its tail.
	Rocket->SetVisibility(bRocket);
	FKartFxEmit Fire;
	Fire.Field = EField::Fire;
	Fire.Direction = -Forward;
	Fire.Spread = 0.16;
	Fire.Speed = { 5.0, 9.0 };
	Fire.Life = { 0.14, 0.3 };
	Fire.Size = { 0.7, 1.05 };
	Fire.SizeEnd = 0.25;
	Fire.Color = Hex(TEXT("ffd68a"));
	Fire.ColorEnd = Hex(TEXT("ff3d14"));
	Fire.Intensity = 3.0;
	Fire.Jitter = 0.12;
	Fire.InheritCm = Inherit;
	UKartFxSubsystem::UpdateStream(RocketFire, Fire, bRocket ? 110.0 : 0.0);
	FKartFxEmit Core = Fire;
	Core.Speed = { 3.0, 5.0 };
	Core.Life = { 0.08, 0.14 };
	Core.Size = { 0.55, 0.7 };
	Core.SizeEnd = 0.5;
	Core.Color = Hex(TEXT("f4f8ff"));
	Core.ColorEnd = Hex(TEXT("7ab8ff"));
	Core.Intensity = 5.0;
	Core.Jitter = 0.03;
	UKartFxSubsystem::UpdateStream(RocketCore, Core, bRocket ? 70.0 : 0.0);
	FKartFxEmit Smoke;
	Smoke.Field = EField::Smoke;
	Smoke.Direction = FVector(-Forward.X, -Forward.Y, 0.3).GetSafeNormal();
	Smoke.Spread = 0.4;
	Smoke.Speed = { 1.0, 2.5 };
	Smoke.Life = { 0.6, 1.0 };
	Smoke.Size = { 0.45, 0.7 };
	Smoke.SizeEnd = 2.6;
	Smoke.Color = Hex(TEXT("8e8882"));
	Smoke.ColorEnd = Hex(TEXT("5a5550"));
	Smoke.Jitter = 0.2;
	Smoke.InheritCm = Inherit * 0.4;
	UKartFxSubsystem::UpdateStream(RocketSmoke, Smoke, bRocket ? 26.0 : 0.0);
}

void UKartFxComponent::UpdateSpeedLines(const FKartState& S, bool bRocket)
{
	if (!SpeedLines)
	{
		return;
	}
	// Subtle: only above top speed (boosts, slipstream), stronger with the rocket.
	const double Speed = FMath::Abs(S.Speed);
	double Amount = FMath::Clamp((Speed - SpeedLinesFrom) / (SpeedLinesFull - SpeedLinesFrom), 0.0, 1.0);
	if (bRocket)
	{
		Amount = FMath::Max(Amount, 0.8);
	}
	Amount = Amount * Amount * (3.0 - 2.0 * Amount);
	FKartFxEmit Lines;
	Lines.Field = EField::SpeedLines;
	Lines.Size = { 2.2, 4.6 };     // tube radius around the view axis, m
	Lines.Speed = { 38.0, 55.0 };  // m/s rushing back
	Lines.Life = { 0.16, 0.26 };
	Lines.Jitter = 0.03;           // streak width, m
	Lines.Color = FLinearColor(1.f, 1.f, 1.f, static_cast<float>(0.32 * Amount));
	Lines.Intensity = 1.0;
	UKartFxSubsystem::UpdateStream(SpeedLines, Lines, IsViewed() && Amount > 0.02 ? 30.0 + 80.0 * Amount : 0.0);
}

void UKartFxComponent::UpdatePadFlashes(double Time)
{
	for (int32 i = 0; i < PadFlashes.Num(); ++i)
	{
		UStaticMeshComponent* C = PadFlashes[i];
		if (!C)
		{
			continue;
		}
		const double Age = Time - PadFlashStart[i];
		const bool bOn = Age >= 0.0 && Age < PadFlashSeconds;
		C->SetVisibility(bOn);
		if (bOn && PadMaterials[i])
		{
			const double T = Age / PadFlashSeconds;
			PadMaterials[i]->SetScalarParameterValue(Param::Fade, static_cast<float>(3.0 * FMath::Pow(1.0 - T, 1.5)));
			PadMaterials[i]->SetScalarParameterValue(Param::Age, static_cast<float>(Age));
		}
	}
}

void UKartFxComponent::UpdateScreen(double Time, const FKartItemSlot* Slot, double ItemNow, bool bPrism)
{
	// The item screen effects as a post-process on this kart's own camera, so split-screen views each get theirs.
	if (!ScreenMaterial)
	{
		UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, ScreenPath);
		if (!Camera || !Parent)
		{
			return;
		}
		ScreenMaterial = UMaterialInstanceDynamic::Create(Parent, this);
		Camera->PostProcessSettings.AddBlendable(ScreenMaterial, 1.f);
	}
	// RaceUI.tsx: the 0.5 s explosion flash and 4 s of ink (item clock), or the showcase's (world clock).
	float FlashT = -1.f, InkAge = -1.f, InkLeft = 0.f;
	if (Slot && ItemNow < Slot->FlashUntil)
	{
		FlashT = static_cast<float>(1.0 - (Slot->FlashUntil - ItemNow) / 0.5);
	}
	else if (Time - Showcase.FlashStart < 0.5)
	{
		FlashT = static_cast<float>((Time - Showcase.FlashStart) / 0.5);
	}
	if (Slot && ItemNow < Slot->InkUntil)
	{
		InkLeft = static_cast<float>(Slot->InkUntil - ItemNow);
		InkAge = static_cast<float>(FMath::Max(0.0, 4.0 - InkLeft));
	}
	else if (Time >= Showcase.InkStart && Time < Showcase.InkUntil)
	{
		InkLeft = static_cast<float>(Showcase.InkUntil - Time);
		InkAge = static_cast<float>(Time - Showcase.InkStart);
	}
	// Hit flash (kartFxBus): peak * (1 - t)^2 over 0.28 s.
	const double HitT = (Time - FlashStart) / 0.28;
	const double Hit = HitT < 0.0 || HitT >= 1.0 ? 0.0 : FlashPeak * (1.0 - HitT) * (1.0 - HitT);
	// Lightning: two strobes, the second softer.
	const double ZapT = (Time - ZapStart) / ZapSeconds;
	double Zap = 0.0;
	if (ZapT >= 0.0 && ZapT < 1.0)
	{
		Zap = FMath::Max(FMath::Exp(-ZapT * 18.0), 0.6 * FMath::Exp(-FMath::Square((ZapT - 0.35) * 9.0)));
	}
	ScreenMaterial->SetScalarParameterValue(Param::Prism, bPrism ? 1.f : 0.f);
	ScreenMaterial->SetScalarParameterValue(Param::FlashT, FlashT);
	ScreenMaterial->SetScalarParameterValue(Param::InkAge, InkAge);
	ScreenMaterial->SetScalarParameterValue(Param::InkLeft, InkLeft);
	ScreenMaterial->SetScalarParameterValue(Param::Hit, static_cast<float>(Hit));
	ScreenMaterial->SetVectorParameterValue(Param::HitColor, FlashTint);
	ScreenMaterial->SetScalarParameterValue(Param::Zap, static_cast<float>(Zap));
}
