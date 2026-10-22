// AKartPawn's look: the vehicle art and its mount, the dressed driver, the paint, and the name plate.
#include "KartPawn.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "KartDebugFlags.h"
#include "KartDriverAnimInstance.h"
#include "KartMovementComponent.h"
#include "KartPawnTuning.h"
#include "Materials/MaterialInstanceDynamic.h"



using namespace MovecadeKart;
using namespace MovecadeKart::Pawn;

namespace
{
	/** The paint's showroom finish: a glassier clear coat over a little more metal flake than M_KartPaint's defaults
	 *  (coat roughness 0.07, flakes 0.3), so it carries crisp highlights and sky reflections at race distance. */
	constexpr float PaintClearCoatRoughness = 0.035f;
	constexpr float PaintFlakeStrength = 0.42f;

	/** Name plate text darker than this luminance is lifted toward white (by up to MaxLift) to read on the dark pill. */
	constexpr float PlateMinLuma = 0.25f;
	constexpr float PlateMaxLift = 0.55f;
	/** The pill's margins round the text (cm). */
	constexpr double PlatePadHeight = 14.0;
	constexpr double PlatePadWidth = 26.0;

	UStaticMesh* LoadVehicleMesh(const TCHAR* Vehicle, const TCHAR* Node)
	{
		const FString Path = FString::Printf(TEXT("/Game/Karts/%s/SM_%s_%s.SM_%s_%s"), Vehicle, Vehicle, Node, Vehicle, Node);
		return LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

void AKartPawn::SetLook(const FKartLook& InLook)
{
	Look = InLook;
	const FKartDebugFlags& Flags = FKartDebugFlags::Get();
	if (Flags.Vehicle.IsSet())
	{
		FKartLook::ParseVehicle(Flags.Vehicle.GetValue(), Look.Vehicle);
	}
	if (Flags.Outfit.IsSet())
	{
		FKartLook::ParseOutfit(Flags.Outfit.GetValue(), Look.Outfit);
	}
	PaintColor = Look.Paint;
	bLookSet = true;
	ApplyVehicle();
	ApplyDriver();
	ApplyColours();
	ApplyNamePlate();
	UE_LOG(LogKartPawn, Log, TEXT("%s look: %s, %s-%s, %s, %s"), *GetName(), FKartLook::VehicleId(Look.Vehicle), FKartLook::FaceId(Look.Face),
		FKartLook::BodyId(Look.Body), FKartLook::OutfitId(Look.Outfit), *Look.Name);
}

void AKartPawn::ApplyVehicle()
{
	if (bVehicleBuilt && BuiltVehicle == Look.Vehicle)
	{
		return;
	}
	const FVehicleMount& Mount = MountFor(Look.Vehicle);
	UStaticMesh* BodyMesh = LoadVehicleMesh(Mount.Name, TEXT("body"));
	if (!BodyMesh)
	{
		UE_LOG(LogKartPawn, Warning, TEXT("No vehicle art for %s (run Tools/ue/import_karts.py)."), Mount.Name);
		return;
	}
	bVehicleBuilt = true;
	BuiltVehicle = Look.Vehicle;
	bStraddle = Mount.bStraddle;
	bHandlebar = FCString::Strcmp(Mount.GripNode, TEXT("handlebar")) == 0;

	for (UStaticMeshComponent* Part : { Body.Get(), Trim.Get(), Booster.Get(), Grip.Get() })
	{
		Part->EmptyOverrideMaterials();
	}
	Body->SetStaticMesh(BodyMesh);
	Trim->SetStaticMesh(Mount.bTrim ? LoadVehicleMesh(Mount.Name, TEXT("trim")) : nullptr);
	Booster->SetStaticMesh(Mount.bBooster ? LoadVehicleMesh(Mount.Name, TEXT("booster")) : nullptr);

	// The rim (or bar) turns about its own centre: pivot there, mesh offset back.
	UStaticMesh* GripMesh = LoadVehicleMesh(Mount.Name, Mount.GripNode);
	Grip->SetStaticMesh(GripMesh);
	const FBox GripBox = GripMesh ? GripMesh->GetBoundingBox() : FBox(FVector::ZeroVector, FVector::ZeroVector);
	GripCentre = GripBox.GetCenter();
	GripRadius = GripBox.GetExtent().X;
	GripNormal = bHandlebar ? FVector::UpVector : RimNormal();
	GripPivot->SetRelativeLocationAndRotation(GripCentre, FQuat::Identity);
	Grip->SetRelativeLocation(-GripCentre);

	// The meshes come out in model space; each wheel spins about its hub (bounds centre).
	WheelHubs.SetNum(4);
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMesh* WheelMesh = Mount.WheelNodes[i] ? LoadVehicleMesh(Mount.Name, Mount.WheelNodes[i]) : nullptr;
		Wheels[i]->SetStaticMesh(WheelMesh);
		WheelHubs[i] = WheelMesh ? WheelMesh->GetBoundingBox().GetCenter() : FVector::ZeroVector;
		WheelPivots[i]->SetRelativeLocationAndRotation(WheelHubs[i], FQuat::Identity);
		Wheels[i]->SetRelativeLocation(-WheelHubs[i]);
	}

	// One paint MID for every part that takes the body paint (body, booster, and the stock rim).
	PaintMaterial = nullptr;
	if (UMaterialInterface* Paint = Body->GetMaterial(0))
	{
		PaintMaterial = UMaterialInstanceDynamic::Create(Paint, this);
		PaintMaterial->SetScalarParameterValue(TEXT("ClearCoatRoughness"), PaintClearCoatRoughness);
		PaintMaterial->SetScalarParameterValue(TEXT("FlakeStrength"), PaintFlakeStrength);
		Body->SetMaterial(0, PaintMaterial);
		if (Mount.bBooster)
		{
			Booster->SetMaterial(0, PaintMaterial);
		}
		if (Mount.bGripPaint)
		{
			Grip->SetMaterial(0, PaintMaterial);
		}
	}

	if (Driver->GetSkeletalMeshAsset())
	{
		Dressing.Invalidate(); // re-seat the driver for the new vehicle
	}
}

void AKartPawn::ApplyDriver()
{
	const FVehicleMount& Mount = MountFor(Look.Vehicle);
	switch (Dressing.Dress(Driver, Outfit, Look))
	{
	case Avatar::EDressResult::NewMesh:
		Driver->SetAnimInstanceClass(UKartDriverAnimInstance::StaticClass());
		// Turn the avatar's own axes (from its rest pose) onto the vehicle's: face forward (+Y), up +Z.
		Driver->SetRelativeTransform(FTransform(Avatar::Facing(Driver->GetSkeletalMeshAsset(), FVector::YAxisVector, FVector::ZAxisVector),
			Mount.Seat, FVector(DriverScale)));
		break;
	case Avatar::EDressResult::Unchanged:
		Driver->SetRelativeLocation(Mount.Seat);
		break;
	case Avatar::EDressResult::Missing:
		break;
	}
}

void AKartPawn::ApplyColours()
{
	if (PaintMaterial)
	{
		PaintMaterial->SetVectorParameterValue(TEXT("PaintColor"), Look.Paint);
		SentPaintMaterial = nullptr; // UpdatePaint pushes its colour again
	}
	Dressing.Colour(Look);
}

void AKartPawn::ApplyNamePlate()
{
	const bool bPlate = !Look.Name.IsEmpty();
	NamePlate->SetText(FText::FromString(Look.Name));
	NamePlate->SetVisibility(bPlate, true);
	if (!bPlate)
	{
		return;
	}
	// Paint-coloured text, lifted toward white when the paint is too dark to read on the dark pill.
	FLinearColor Text = Look.Paint;
	const float Luma = Text.GetLuminance();
	if (Luma < PlateMinLuma)
	{
		Text = FMath::Lerp(Text, FLinearColor::White, FMath::Clamp((PlateMinLuma - Luma) / PlateMinLuma, 0.f, 1.f) * PlateMaxLift);
		Text.A = 1.f;
	}
	NamePlate->SetTextRenderColor(Text.ToFColorSRGB());
	const FVector Size = NamePlate->GetTextLocalSize();
	NamePlateBack->SetRelativeScale3D(FVector((Size.Z + PlatePadHeight) / 100.0, (Size.Y + PlatePadWidth) / 100.0, 1.0));
	const double HeadTop = RoadBelowOrigin + MountFor(Look.Vehicle).Seat.Z + DriverHeight * DriverScale;
	NamePlate->SetRelativeLocation(FVector(0, 0, HeadTop + NamePlateClearance));
}

void AKartPawn::UpdateNamePlate()
{
	if (!NamePlate->IsVisible() && Look.Name.IsEmpty())
	{
		return;
	}
	const bool bShow = !Look.Name.IsEmpty() && !Movement->SimState().bFinished;
	if (NamePlate->IsVisible() != bShow)
	{
		NamePlate->SetVisibility(bShow, true);
	}
	if (!bShow)
	{
		return;
	}
	// Face the nearest other viewer's camera, upright (in split-screen each plate is mostly read by the karts behind
	// it; this kart's own viewport never shows its plate).
	const FVector Plate = NamePlate->GetComponentLocation();
	double Best = TNumericLimits<double>::Max();
	FVector Facing = FVector::ZeroVector;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->PlayerCameraManager || (PC->GetPawn() == this && GetWorld()->GetNumPlayerControllers() > 1))
		{
			continue;
		}
		const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Plate;
		if (ToCamera.SizeSquared() < Best)
		{
			Best = ToCamera.SizeSquared();
			Facing = FVector(ToCamera.X, ToCamera.Y, 0.0);
		}
	}
	if (!Facing.IsNearlyZero())
	{
		NamePlate->SetWorldRotation(Facing.Rotation());
	}
}
