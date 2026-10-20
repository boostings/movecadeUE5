#include "KartPawn.h"

#include "Audio/KartAudioComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFxComponent.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "KartDriverAnimInstance.h"
#include "KartMovementComponent.h"
#include "KartPlayerController.h"
#include "Misc/CommandLine.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartPawn, Log, All);

namespace
{
	// Chase rig, Docs/Port/01 §10.2 (HostCameraFollow, with the x/z mismatch fixed: chase on both axes).
	constexpr double EyeHeight = 4.105;
	constexpr double LookHeight = 2.105;
	constexpr double CameraLerp = 8.0;

	/**
	 * The vehicles' model frame (the imported meshes, Tools/ue/import_karts.py): glTF (x, y, z) -> (x, z, y) * 100,
	 * so the model's forward is +Y, its up +Z, and the wheels' contact patch is 49 cm below the origin on every
	 * vehicle (hub height = wheel radius, Tools/art/.cache/out/mounts.json).
	 */
	constexpr double RoadBelowOrigin = 49.0;
	/** The avatar rig stands 2.56 units tall; the web seats it at 0.9 scale (Kart.jsx:649-679). */
	constexpr double DriverScale = 0.9;
	constexpr double DriverHeight = 256.0;
	/** The wrist sits half a hand outside the rim, so the palm lands on the tube (drivingGrip.ts). */
	constexpr double WristClearance = 7.0;
	constexpr double HandlebarGripInset = 6.0;

	struct FVehicleMount
	{
		const TCHAR* Name;
		/** The rig root (the driver's feet), model frame: vehicles.ts KART_SEAT or the GLB `seat` node. */
		FVector Seat;
		const TCHAR* GripNode;
		bool bStraddle;
		bool bGripPaint;
		bool bBooster;
		bool bTrim;
		const TCHAR* WheelNodes[4];
	};

	// vehicles.ts VEHICLE_MOUNT_SPECS. Front slots 0/1 steer; the moto's empty slots have no mesh.
	const FVehicleMount Mounts[] = {
		{ TEXT("Kart"), FVector(0, -15, -92.8), TEXT("d_wheel"), false, true, true, false, { TEXT("wheel_0"), TEXT("wheel_1"), TEXT("wheel_2"), TEXT("wheel_3") } },
		{ TEXT("Moto"), FVector(0, -20, -64), TEXT("handlebar"), true, false, false, true, { TEXT("wheel_0"), nullptr, TEXT("wheel_2"), nullptr } },
		{ TEXT("Coupe"), FVector(0, -15, -92.8), TEXT("d_wheel"), false, false, false, true, { TEXT("wheel_0"), TEXT("wheel_1"), TEXT("wheel_2"), TEXT("wheel_3") } },
		{ TEXT("Truck"), FVector(0, -15, -3.8), TEXT("d_wheel"), false, false, false, true, { TEXT("wheel_0"), TEXT("wheel_1"), TEXT("wheel_2"), TEXT("wheel_3") } },
	};

	/** Every d_wheel rim is tilted -1.134 rad about the lateral axis (the GLB node rotation): its disc normal, toward the driver. */
	const FVector RimNormal = FVector(0.0, -FMath::Sin(1.134), FMath::Cos(1.134));

	UStaticMesh* LoadVehicleMesh(const TCHAR* Vehicle, const TCHAR* Node)
	{
		const FString Path = FString::Printf(TEXT("/Game/Karts/%s/SM_%s_%s.SM_%s_%s"), Vehicle, Vehicle, Node, Vehicle, Node);
		return LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UStaticMeshComponent* MakePart(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh, const FVector& Location, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(Mesh);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeRotation(Rotation);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		return Part;
	}

	FLinearColor Lerp3(const FLinearColor& A, const FLinearColor& B, float T)
	{
		return FLinearColor(FMath::Lerp(A.R, B.R, T), FMath::Lerp(A.G, B.G, T), FMath::Lerp(A.B, B.B, T), 1.f);
	}

	/** appearanceColors.ts lipHex: the skin pulled 42 % toward a warm rose, then 0.9 (linear light). */
	FLinearColor LipColour(const FLinearColor& Skin)
	{
		const FLinearColor Rose = FLinearColor::FromSRGBColor(FColor(0xc8, 0x50, 0x6a));
		return Lerp3(Skin, Rose, 0.42f) * 0.9f;
	}

	/** The jersey darkened: the waistband and headband (appearanceColors.ts detailHex). */
	FLinearColor DetailColour(const FLinearColor& Jersey)
	{
		FLinearColor C = Jersey * 0.32f;
		C.A = 1.f;
		return C;
	}

	/** Shows or hides every render section that draws material slot Slot. */
	void ShowSlot(USkeletalMeshComponent* Mesh, FName Slot, bool bShow)
	{
		USkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
		const int32 Material = Mesh ? Mesh->GetMaterialIndex(Slot) : INDEX_NONE;
		FSkeletalMeshRenderData* Render = Asset ? Asset->GetResourceForRendering() : nullptr;
		if (Material == INDEX_NONE || !Render)
		{
			return;
		}
		for (int32 Lod = 0; Lod < Render->LODRenderData.Num(); ++Lod)
		{
			const TArray<FSkelMeshRenderSection>& Sections = Render->LODRenderData[Lod].RenderSections;
			for (int32 Section = 0; Section < Sections.Num(); ++Section)
			{
				if (Sections[Section].MaterialIndex == Material)
				{
					Mesh->ShowMaterialSection(Material, Section, bShow, Lod);
				}
			}
		}
	}

	/** Where a rotation about Axis by +Angle carries Probe: used to pick signs without trusting handedness. */
	double SignToward(const FVector& Axis, const FVector& Probe, const FVector& Toward)
	{
		return FVector::DotProduct(FQuat(Axis, 0.1).RotateVector(Probe), Toward) >= FVector::DotProduct(Probe, Toward) ? 1.0 : -1.0;
	}
}

AKartPawn::AKartPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// Run after the movement component has stepped the sim this frame.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Root = CreateDefaultSubobject<USphereComponent>(TEXT("Root"));
	Root->InitSphereRadius(115.f); // MESH/HARBOR_KART_RADIUS 1.15 m
	Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Root;

	SlipPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SlipPivot"));
	SlipPivot->SetupAttachment(Root);
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(SlipPivot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlateMaterial(TEXT("/Game/Karts/Materials/M_NamePlateBack.M_NamePlateBack"));
	BaseMaterial = ShapeMaterial.Object;

	VehicleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	VehicleRoot->SetupAttachment(BodyPivot);
	VehicleRoot->SetRelativeLocationAndRotation(FVector(0, 0, RoadBelowOrigin), FRotator(0, -90, 0));

	// Everything but the wheels rides the chassis spring (about the hub line, the model origin).
	ChassisPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ChassisPivot"));
	ChassisPivot->SetupAttachment(VehicleRoot);
	Body = MakePart(this, ChassisPivot, TEXT("Body"), nullptr, FVector::ZeroVector, FVector::OneVector);
	Trim = MakePart(this, ChassisPivot, TEXT("Trim"), nullptr, FVector::ZeroVector, FVector::OneVector);
	Booster = MakePart(this, ChassisPivot, TEXT("Booster"), nullptr, FVector::ZeroVector, FVector::OneVector);
	GripPivot = CreateDefaultSubobject<USceneComponent>(TEXT("GripPivot"));
	GripPivot->SetupAttachment(ChassisPivot);
	Grip = MakePart(this, GripPivot, TEXT("Grip"), nullptr, FVector::ZeroVector, FVector::OneVector);
	for (int32 i = 0; i < 4; ++i)
	{
		USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("WheelPivot%d"), i));
		Pivot->SetupAttachment(VehicleRoot);
		WheelPivots.Add(Pivot);
		Wheels.Add(MakePart(this, Pivot, *FString::Printf(TEXT("Wheel%d"), i), nullptr, FVector::ZeroVector, FVector::OneVector));
	}

	Driver = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Driver"));
	Driver->SetupAttachment(ChassisPivot);
	Driver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Driver->SetGenerateOverlapEvents(false);
	Driver->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Driver->SetAnimInstanceClass(UKartDriverAnimInstance::StaticClass());
	Driver->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	// Pose after this pawn has written the grip targets.
	Driver->SetTickGroup(TG_PostPhysics);
	Driver->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);

	Outfit = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Outfit"));
	Outfit->SetupAttachment(Driver);
	Outfit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Outfit->SetGenerateOverlapEvents(false);
	Outfit->SetVisibility(false);

	// Name plate (MultiplayerKart.tsx:1769-1820): above the driver, in the paint colour, hidden in this
	// kart's own viewport (owner-no-see) and after the finish.
	NamePlate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NamePlate"));
	NamePlate->SetupAttachment(Root);
	NamePlate->SetHorizontalAlignment(EHTA_Center);
	NamePlate->SetVerticalAlignment(EVRTA_TextCenter);
	NamePlate->SetWorldSize(30.f);
	NamePlate->SetOwnerNoSee(true);
	NamePlate->SetCastShadow(false);
	NamePlate->SetUsingAbsoluteRotation(true);
	NamePlateBack = MakePart(this, NamePlate, TEXT("NamePlateBack"), PlaneMesh.Object, FVector(-1, 0, 0), FVector(0.4, 1.0, 1.0), FRotator(90, 0, 0));
	NamePlateBack->SetMaterial(0, PlateMaterial.Object);
	NamePlateBack->SetOwnerNoSee(true);
	NamePlateBack->SetCastShadow(false);

	// Prism: the body cycles through the rainbow instead of the web's translucent yellow sphere.
	Aura = MakePart(this, SlipPivot, TEXT("Aura"), SphereMesh.Object, FVector(0, 0, 8), FVector(2.6, 2.2, 0.06));
	Aura->SetVisibility(false);
	// Rocket: a capsule-ish shell along the kart (the web laid it across, spec 02 checklist 11).
	RocketShell = MakePart(this, SlipPivot, TEXT("RocketShell"), CylinderMesh.Object, FVector(0, 0, 70), FVector(1.3, 1.3, 2.9), FRotator(90, 0, 0));
	RocketShell->SetVisibility(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->bConstrainAspectRatio = false;

	Movement = CreateDefaultSubobject<UKartMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Root;

	Audio = CreateDefaultSubobject<UKartAudioComponent>(TEXT("Audio"));
	CreateDefaultSubobject<UKartFxComponent>(TEXT("Fx"));

	// Command-line switches for comparisons and art checks, read once per kart.
	const TCHAR* Cmd = FCommandLine::Get();
	Feel.bEnabled = !FParse::Param(Cmd, TEXT("KartWebFeel"));
	bDebugGrip = FParse::Param(Cmd, TEXT("KartDebugGrip"));
	bCinematic = FParse::Param(Cmd, TEXT("KartCinematic"));
	// -KartCloseCam=<yaw deg>,<distance m>,<height m>[,<deg/s>]: an orbit shot of this kart for art checks
	// (yaw 0 = straight ahead of the kart looking back at it; the optional rate orbits it).
	FString Value;
	TArray<FString> Parts;
	if (FParse::Value(Cmd, TEXT("KartCloseCam="), Value, false) && Value.ParseIntoArray(Parts, TEXT(",")) >= 3)
	{
		CloseCam = FVector4(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]), Parts.Num() > 3 ? FCString::Atod(*Parts[3]) : 0.0);
	}
}

UPawnMovementComponent* AKartPawn::GetMovementComponent() const
{
	return Movement;
}

void AKartPawn::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Base = BaseMaterial)
	{
		UMaterialInstanceDynamic* Shell = UMaterialInstanceDynamic::Create(Base, this);
		Shell->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.006f, 0.006f, 0.006f)); // #111111
		RocketShell->SetMaterial(0, Shell);
		AuraMaterial = UMaterialInstanceDynamic::Create(Base, this);
		Aura->SetMaterial(0, AuraMaterial);
	}
	if (!bLookSet)
	{
		FKartLook Default = FKartLook::MakeVaried(0);
		Default.Paint = PaintColor;
		SetLook(Default);
	}
	SnapCamera();
}

void AKartPawn::SetLook(const FKartLook& InLook)
{
	Look = InLook;
	// -KartVehicle=<kart|moto|coupe|truck> / -KartOutfit=<id>: every kart wears it (art checks).
	FString Forced;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartVehicle="), Forced))
	{
		FKartLook::ParseVehicle(Forced, Look.Vehicle);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("KartOutfit="), Forced))
	{
		FKartLook::ParseOutfit(Forced, Look.Outfit);
	}
	PaintColor = Look.Paint;
	bLookSet = true;
	ApplyVehicle();
	ApplyDriver();
	ApplyColours();

	const bool bPlate = !Look.Name.IsEmpty();
	NamePlate->SetText(FText::FromString(Look.Name));
	NamePlate->SetVisibility(bPlate, true);
	if (bPlate)
	{
		// Paint-coloured text, lifted toward white when the paint is too dark to read on the dark pill.
		FLinearColor Text = Look.Paint;
		const float Luma = Text.GetLuminance();
		if (Luma < 0.25f)
		{
			Text = Lerp3(Text, FLinearColor::White, FMath::Clamp((0.25f - Luma) / 0.25f, 0.f, 1.f) * 0.55f);
		}
		NamePlate->SetTextRenderColor(Text.ToFColorSRGB());
		const FVector Size = NamePlate->GetTextLocalSize();
		NamePlateBack->SetRelativeScale3D(FVector((Size.Z + 14.0) / 100.0, (Size.Y + 26.0) / 100.0, 1.0));
		const double HeadTop = RoadBelowOrigin + Mounts[static_cast<int32>(Look.Vehicle)].Seat.Z + DriverHeight * DriverScale;
		NamePlate->SetRelativeLocation(FVector(0, 0, HeadTop + 30.0));
	}
	UE_LOG(LogKartPawn, Log, TEXT("%s look: %s, %s-%s, %s, %s"), *GetName(), FKartLook::VehicleId(Look.Vehicle), FKartLook::FaceId(Look.Face),
		FKartLook::BodyId(Look.Body), FKartLook::OutfitId(Look.Outfit), *Look.Name);
}

void AKartPawn::ApplyVehicle()
{
	if (bVehicleBuilt && BuiltVehicle == Look.Vehicle)
	{
		return;
	}
	const FVehicleMount& Mount = Mounts[static_cast<int32>(Look.Vehicle)];
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
	GripNormal = bHandlebar ? FVector::UpVector : RimNormal;
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
		BuiltDriver.Reset(); // re-seat the driver for the new vehicle
	}
}

void AKartPawn::ApplyDriver()
{
	const FString Key = FString::Printf(TEXT("%s_%s"), FKartLook::FaceId(Look.Face), FKartLook::BodyId(Look.Body));
	if (BuiltDriver != Key)
	{
		const FString Path = FString::Printf(TEXT("/Game/Avatars/Drivers/%s/SK_Driver_%s.SK_Driver_%s"), *Key, *Key, *Key);
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Mesh)
		{
			UE_LOG(LogKartPawn, Warning, TEXT("No driver %s (run Tools/ue/import_karts.py)."), *Path);
			Driver->SetVisibility(false, true);
			return;
		}
		BuiltDriver = Key;
		bRebuildOutfit = true;
		Driver->EmptyOverrideMaterials();
		Driver->SetSkeletalMeshAsset(Mesh);
		Driver->SetAnimInstanceClass(UKartDriverAnimInstance::StaticClass());
		Driver->SetVisibility(true);

		// Turn the avatar's own axes (from its rest pose) onto the vehicle's: face forward (+Y), up +Z.
		FVector Forward, Right, Up;
		FQuat Facing = FQuat::Identity;
		if (MovecadeKart::DriverRig::ComputeBasis(Mesh->GetRefSkeleton(), Forward, Right, Up))
		{
			const FQuat Avatar = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
			const FQuat Vehicle = FRotationMatrix::MakeFromXZ(FVector::YAxisVector, FVector::ZAxisVector).ToQuat();
			Facing = Vehicle * Avatar.Inverse();
		}
		const FVehicleMount& Mount = Mounts[static_cast<int32>(Look.Vehicle)];
		Driver->SetRelativeTransform(FTransform(Facing, Mount.Seat, FVector(DriverScale)));

		DriverTints.Reset();
		static const FName Tinted[] = { TEXT("skin"), TEXT("skin_body"), TEXT("lips"), TEXT("hair"), TEXT("hair_short"), TEXT("hair_bangs"),
			TEXT("hair_puff"), TEXT("hair_tail"), TEXT("hair_crop"), TEXT("hair_curls"), TEXT("hair_bob"), TEXT("hair_bun"),
			TEXT("jersey"), TEXT("pants"), TEXT("waist"), TEXT("shoes"), TEXT("acc_cap"), TEXT("acc_headband") };
		for (const FName Slot : Tinted)
		{
			const int32 Index = Driver->GetMaterialIndex(Slot);
			if (Index != INDEX_NONE)
			{
				DriverTints.Add(Slot, Driver->CreateDynamicMaterialInstance(Index));
			}
		}
		// The face's resting expression (avatar4 appeal pass: a gentle smile and cheek lift).
		Driver->SetMorphTarget(TEXT("mouthSmile"), 0.22f);
		Driver->SetMorphTarget(TEXT("cheekSquint"), 0.12f);
	}
	else
	{
		const FVehicleMount& Mount = Mounts[static_cast<int32>(Look.Vehicle)];
		Driver->SetRelativeLocation(Mount.Seat);
	}

	// Outfit overlay: the paid garment replaces the stock clothes and bare arms (clothingAsset.ts).
	if (bRebuildOutfit || Look.Outfit != BuiltOutfit)
	{
		bRebuildOutfit = false;
		BuiltOutfit = Look.Outfit;
		Outfit->EmptyOverrideMaterials();
		USkeletalMesh* Garment = nullptr;
		if (Look.Outfit != EKartOutfit::Classic)
		{
			const FString Id = FString(FKartLook::OutfitId(Look.Outfit)).Replace(TEXT("-"), TEXT("_"));
			const FString Name = FString::Printf(TEXT("%s_%s"), *Id, FKartLook::BodyId(Look.Body));
			const FString Path = FString::Printf(TEXT("/Game/Avatars/Outfits/%s/SK_Outfit_%s.SK_Outfit_%s"), *Name, *Name, *Name);
			Garment = LoadObject<USkeletalMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		Outfit->SetSkeletalMeshAsset(Garment);
		Outfit->SetLeaderPoseComponent(Garment ? Driver.Get() : nullptr);
		Outfit->SetVisibility(Garment != nullptr);
		OutfitTints.Reset();
		if (Garment)
		{
			const int32 Index = Outfit->GetMaterialIndex(TEXT("skin"));
			if (Index != INDEX_NONE)
			{
				OutfitTints.Add(TEXT("skin"), Outfit->CreateDynamicMaterialInstance(Index));
			}
		}
	}

	// Which sections show: one hair style (none under a cap), the chosen accessory, and the stock
	// clothes only without an outfit.
	const bool bGarment = Outfit->GetSkeletalMeshAsset() != nullptr;
	for (uint8 Hair = 0; Hair <= static_cast<uint8>(EKartHair::Bun); ++Hair)
	{
		const bool bShow = Hair == static_cast<uint8>(Look.Hair) && Look.Accessory != EKartAccessory::Cap;
		ShowSlot(Driver, FName(FString(TEXT("hair_")) + FKartLook::HairId(static_cast<EKartHair>(Hair))), bShow);
	}
	ShowSlot(Driver, TEXT("acc_cap"), Look.Accessory == EKartAccessory::Cap);
	ShowSlot(Driver, TEXT("acc_glasses"), Look.Accessory == EKartAccessory::Glasses);
	ShowSlot(Driver, TEXT("acc_headband"), Look.Accessory == EKartAccessory::Headband);
	for (const TCHAR* Slot : { TEXT("skin_body"), TEXT("jersey"), TEXT("pants"), TEXT("waist"), TEXT("shoes"), TEXT("rubber"), TEXT("lace") })
	{
		ShowSlot(Driver, Slot, !bGarment);
	}
}

void AKartPawn::ApplyColours()
{
	if (PaintMaterial)
	{
		PaintMaterial->SetVectorParameterValue(TEXT("PaintColor"), Look.Paint);
	}
	const FLinearColor Jersey = Look.JerseyColor();
	auto Tint = [](const TMap<FName, TObjectPtr<UMaterialInstanceDynamic>>& Map, const TCHAR* Slot, const FLinearColor& Colour)
	{
		if (const TObjectPtr<UMaterialInstanceDynamic>* Found = Map.Find(Slot))
		{
			(*Found)->SetVectorParameterValue(TEXT("Tint"), Colour);
		}
	};
	Tint(DriverTints, TEXT("skin"), Look.Skin);
	Tint(DriverTints, TEXT("skin_body"), Look.Skin);
	Tint(DriverTints, TEXT("lips"), LipColour(Look.Skin));
	Tint(DriverTints, TEXT("hair"), Look.HairColor);
	for (uint8 Hair = 0; Hair <= static_cast<uint8>(EKartHair::Bun); ++Hair)
	{
		Tint(DriverTints, *(FString(TEXT("hair_")) + FKartLook::HairId(static_cast<EKartHair>(Hair))), Look.HairColor);
	}
	Tint(DriverTints, TEXT("jersey"), Jersey);
	Tint(DriverTints, TEXT("pants"), Look.Pants);
	Tint(DriverTints, TEXT("waist"), DetailColour(Jersey));
	Tint(DriverTints, TEXT("shoes"), Look.Shoes);
	Tint(DriverTints, TEXT("acc_cap"), Jersey);
	Tint(DriverTints, TEXT("acc_headband"), DetailColour(Jersey));
	Tint(OutfitTints, TEXT("skin"), Look.Skin);
}

void AKartPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Movement->HasSim())
	{
		return;
	}
	UpdateFeel(DeltaSeconds);
	UpdateVisual(DeltaSeconds);
	UpdateDriver(DeltaSeconds);
	UpdateNamePlate();
	UpdateCamera(DeltaSeconds);

	const MovecadeKart::FKartState& S = Movement->SimState();
	if (S.LapsCompleted < LoggedLaps)
	{
		// A restart put the kart back on the grid.
		LoggedLaps = 0;
		LastLapTime = 0.0;
	}
	if (S.LapsCompleted > LoggedLaps)
	{
		LoggedLaps = S.LapsCompleted;
		UE_LOG(LogKartPawn, Display, TEXT("Kart lap %d: %.2f s (race %.2f s)%s"), LoggedLaps, S.Time - LastLapTime, S.Time, S.bFinished ? TEXT(" FINISHED") : TEXT(""));
		LastLapTime = S.Time;
	}
}

void AKartPawn::UpdateFeel(float DeltaSeconds)
{
	const MovecadeKart::FKartState& S = Movement->SimState();
	MovecadeKart::FKartFeelInput In;
	In.DeltaSeconds = DeltaSeconds;
	In.Speed = S.Speed;
	In.Yaw = Movement->RenderYaw();
	In.HopOffset = S.HopOffset;
	In.DriftDirection = S.DriftDirection;
	In.DriftPower = S.DriftPower;
	In.BoostMultiplier = S.BoostMultiplier;
	In.BoostTimeRemaining = S.BoostTimeRemaining;
	In.bOnDirt = S.bOnDirt;
	// Impacts are one-step pulses: react once per event, however many frames (or steps) pass in between.
	const bool bNewWallHit = S.BarrierEventCount != SeenBarrierEvents;
	const bool bNewBump = S.BumpEventCount != SeenBumpEvents;
	SeenBarrierEvents = S.BarrierEventCount;
	SeenBumpEvents = S.BumpEventCount;
	In.WallImpact = bNewWallHit ? FMath::Max(S.BarrierEvent, 0.2) : 0.0;
	In.BumpImpact = bNewBump ? FMath::Max(S.BumpEvent, 0.2) : 0.0;
	if ((bNewWallHit || bNewBump) && !S.ImpactNormal.IsNearlyZero())
	{
		// The normal points away from what was hit: pointing to the kart's left means the hit was on its right.
		const double Yaw = Movement->RenderYaw();
		const FVector2D Right(FMath::Cos(Yaw), -FMath::Sin(Yaw));
		In.WallSide = FVector2D::DotProduct(S.ImpactNormal, Right) < 0.0 ? 1.0 : -1.0;
	}
	In.DriftBoostCount = S.DriftBoostCount;
	In.TeleportCount = S.TeleportCount;
	const MovecadeKart::FKartFeelPose& Pose = Feel.Update(In);

	// The model frame: +X is the kart's left, +Y forward, +Z up (see VehicleRoot). Signs come from the axes, so
	// "nose up" and "right side down" stay true whatever the handedness.
	static const double PitchSign = SignToward(FVector::XAxisVector, FVector::YAxisVector, FVector::UpVector);
	static const double RollSign = SignToward(FVector::YAxisVector, -FVector::XAxisVector, -FVector::UpVector);
	const FQuat Chassis = FQuat(FVector::YAxisVector, RollSign * FMath::DegreesToRadians(Pose.ChassisRoll))
		* FQuat(FVector::XAxisVector, PitchSign * FMath::DegreesToRadians(Pose.ChassisPitch));
	// Squash keeps the volume: what the height and length lose, the width gains.
	const double Width = 1.0 / FMath::Sqrt(FMath::Max(0.25, Pose.SquashUp * Pose.StretchForward));
	ChassisPivot->SetRelativeTransform(FTransform(Chassis, FVector(0, 0, Pose.ChassisHeave * 100.0), FVector(Width, Pose.StretchForward, Pose.SquashUp)));
}

void AKartPawn::UpdateVisual(float DeltaSeconds)
{
	using MovecadeKart::Coords::YawToUE;
	const MovecadeKart::FKartState& S = Movement->SimState();
	SlipPivot->SetRelativeRotation(FRotator(0, YawToUE(S.SlipYaw) + ItemVisual.SpinDegrees, 0));
	BodyPivot->SetRelativeLocationAndRotation(FVector(0, 0, S.HopOffset * 100.0 + ItemVisual.PopCm),
		FRotator(0, YawToUE(S.GroupYaw), FMath::RadiansToDegrees(-S.Lean) + ItemVisual.TumbleDegrees));
	// Lightning shrink eases in and out.
	const double Scale = FMath::FInterpTo(BodyPivot->GetRelativeScale3D().X, ItemVisual.Scale, DeltaSeconds, 8.0);
	BodyPivot->SetRelativeScale3D(FVector(Scale));

	const double Time = GetWorld()->GetTimeSeconds();
	Aura->SetVisibility(ItemVisual.bAura);
	const FLinearColor Rainbow = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Time * 600.0, 256.0)), 200, 255);
	if (PaintMaterial)
	{
		PaintMaterial->SetVectorParameterValue(TEXT("PaintColor"), ItemVisual.bAura ? Rainbow : PaintColor);
	}
	if (AuraMaterial)
	{
		AuraMaterial->SetVectorParameterValue(TEXT("Color"), Rainbow);
	}
	RocketShell->SetVisibility(ItemVisual.bRocket);
	if (VehicleRoot->IsVisible() == ItemVisual.bRocket)
	{
		VehicleRoot->SetVisibility(!ItemVisual.bRocket, true);
		if (!ItemVisual.bRocket)
		{
			Outfit->SetVisibility(Outfit->GetSkeletalMeshAsset() != nullptr);
		}
	}

	// Wheels (Kart.jsx:202-242): fronts steer with yRot * 30, every wheel spins speed * 0.6 rad/s.
	WheelSpin = FMath::Fmod(WheelSpin + S.Speed * 0.6 * DeltaSeconds, 2.0 * UE_DOUBLE_PI);
	static const double SpinSign = SignToward(FVector::XAxisVector, FVector::UpVector, FVector::YAxisVector); // top rolls forward (+Y)
	const FQuat Spin(FVector::XAxisVector, SpinSign * WheelSpin);
	const FQuat Steer = FRotator(0, YawToUE(S.SteerVisual * 30.0), 0).Quaternion();
	for (int32 i = 0; i < WheelPivots.Num(); ++i)
	{
		WheelPivots[i]->SetRelativeRotation(i < 2 ? Steer * Spin : Spin);
	}

	// Steering wheel: rim = damp(clamp(yRot * 64, ±1) * 1.1, 20) (drivingGrip.ts), clockwise to the right;
	// a handlebar turns with the front wheels.
	const double SteerRight = FMath::Clamp(-S.SteerVisual * 64.0, -1.0, 1.0);
	RimTurn = FMath::FInterpTo(RimTurn, SteerRight * 1.1, DeltaSeconds, 20.0);
	if (bHandlebar)
	{
		GripPivot->SetRelativeRotation(Steer);
	}
	else
	{
		// Turning right carries the rim's top toward the driver's right (-X in the model frame).
		const FVector Top = FVector::VectorPlaneProject(FVector::UpVector, GripNormal).GetSafeNormal();
		const double Sign = SignToward(GripNormal, Top, -FVector::XAxisVector);
		GripPivot->SetRelativeRotation(FQuat(GripNormal, Sign * RimTurn));
	}
}

void AKartPawn::UpdateDriver(float DeltaSeconds)
{
	UKartDriverAnimInstance* Anim = Cast<UKartDriverAnimInstance>(Driver->GetAnimInstance());
	if (!Anim || !Driver->GetSkeletalMeshAsset())
	{
		return;
	}
	const MovecadeKart::FKartState& S = Movement->SimState();
	FKartDriverPoseInput& In = Anim->Input;
	In.Steer = static_cast<float>(FMath::Clamp(-S.SteerVisual * 64.0, -1.0, 1.0));
	In.Moving = static_cast<float>(FMath::Clamp(FMath::Abs(S.Speed) / 45.0, 0.0, 1.0));
	In.LateralG = static_cast<float>(FMath::Clamp(-S.Lean * 4.0, -1.0, 1.0));
	In.Bounce = static_cast<float>(FMath::Clamp(S.HopOffset * 2.0, -1.0, 1.0));
	In.Time = GetWorld()->GetTimeSeconds();
	In.bStraddle = bStraddle;
	Cheer = FMath::FInterpTo(Cheer, S.bFinished ? 1.0 : 0.0, DeltaSeconds, 3.0);
	In.Cheer = static_cast<float>(Cheer);
	// Item throw: up in 0.12 s, held 0.2 s, back on the wheel over 0.3 s.
	const double SinceThrow = GetWorld()->GetTimeSeconds() - ItemThrowStart;
	const double Raise = SinceThrow < 0.12 ? SinceThrow / 0.12 : SinceThrow < 0.32 ? 1.0 : 1.0 - (SinceThrow - 0.32) / 0.3;
	In.ItemRaise = static_cast<float>(FMath::SmoothStep(0.0, 1.0, FMath::Clamp(Raise, 0.0, 1.0)));

	// Grip targets, from where the rim (or bar) actually is this frame, into the driver's component space.
	const FTransform GripFrame = GripPivot->GetComponentTransform();
	const FTransform Vehicle = ChassisPivot->GetComponentTransform();
	const FVector DriverRight = Vehicle.TransformVectorNoScale(-FVector::XAxisVector); // model right is -X
	const FVector DriverBack = Vehicle.TransformVectorNoScale(-FVector::YAxisVector);
	FVector Left, Right;
	if (bHandlebar)
	{
		const FVector Side = GripFrame.TransformVectorNoScale(-FVector::XAxisVector);
		const double Reach = GripRadius - HandlebarGripInset;
		Left = GripFrame.GetLocation() - Side * Reach + DriverBack * WristClearance;
		Right = GripFrame.GetLocation() + Side * Reach + DriverBack * WristClearance;
	}
	else
	{
		const FVector Normal = Vehicle.TransformVectorNoScale(GripNormal);
		const FVector Side = FVector::VectorPlaneProject(DriverRight, Normal).GetSafeNormal();
		const FQuat Turn = GripFrame.GetRotation() * Vehicle.GetRotation().Inverse();
		const double Reach = GripRadius + WristClearance;
		Left = GripFrame.GetLocation() + Turn.RotateVector(-Side) * Reach + Normal * 2.0;
		Right = GripFrame.GetLocation() + Turn.RotateVector(Side) * Reach + Normal * 2.0;
	}
	if (bDebugGrip)
	{
		DrawDebugSphere(GetWorld(), Left, 3.f, 8, FColor::Green);
		DrawDebugSphere(GetWorld(), Right, 3.f, 8, FColor::Red);
		DrawDebugSphere(GetWorld(), GripFrame.GetLocation(), 3.f, 8, FColor::Blue);
	}
	const FTransform& Component = Driver->GetComponentTransform();
	In.LeftHand = Component.InverseTransformPosition(Left);
	In.RightHand = Component.InverseTransformPosition(Right);
	In.bHasGrip = GripRadius > 0.0;

	// Natural blinks (blink.ts: 65 ms close, 20 ms hold, 135 ms reopen), every 2-5 s.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextBlink)
	{
		BlinkStart = Now;
		NextBlink = Now + FMath::FRandRange(2.0, 5.0);
	}
	const double T = (Now - BlinkStart) * 1000.0;
	const double Blink = T < 65.0 ? T / 65.0 : T < 85.0 ? 1.0 : T < 220.0 ? 1.0 - (T - 85.0) / 135.0 : 0.0;
	Driver->SetMorphTarget(TEXT("eyeBlink"), static_cast<float>(FMath::Clamp(Blink, 0.0, 1.0)));
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
	// Face the nearest other viewer's camera, upright (in split-screen each plate is mostly read by the
	// karts behind it; this kart's own viewport never shows its plate).
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

void AKartPawn::SetPaint(const FLinearColor& Color)
{
	PaintColor = Color;
	Look.Paint = Color;
	if (bLookSet)
	{
		ApplyColours();
	}
}

void AKartPawn::PlayItemThrow()
{
	ItemThrowStart = GetWorld()->GetTimeSeconds();
}

void AKartPawn::AddShake(double PeakMetres)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const double Remaining = ShakePeak * FMath::Exp(-6.5 * (Now - ShakeStart));
	ShakePeak = FMath::Max(PeakMetres, Remaining);
	ShakeStart = Now;
}

void AKartPawn::SnapCamera()
{
	CameraPos = FVector::ZeroVector;
	CameraLook = FVector::ZeroVector;
	if (Movement && Movement->HasSim())
	{
		UpdateCamera(0.f);
	}
}

void AKartPawn::UpdateCamera(float DeltaSeconds)
{
	using namespace MovecadeKart;
	const FKartState& S = Movement->SimState();
	const double Speed = FMath::Abs(S.Speed);
	const FKartFeelPose& Juice = Feel.Pose();
	const double Chase = (bSplitScreenCamera ? 6.0 : 5.4 + FMath::Min(1.35, Speed * 0.018)) + Juice.CameraPullBack;
	const double Ahead = bSplitScreenCamera ? 8.0 : 7.2 + FMath::Min(2.2, Speed * 0.045);
	const double Yaw = Movement->RenderYaw();
	const FVector2D Fwd(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
	const FVector2D Pos = Movement->RenderPosition();
	const FVector EyeTarget = Coords::ToUE(Pos - Fwd * Chase, EyeHeight);
	const FVector LookTarget = Coords::ToUE(Pos + Fwd * Ahead, LookHeight);

	if (CameraPos.IsZero() && CameraLook.IsZero())
	{
		CameraPos = EyeTarget;
		CameraLook = LookTarget;
	}
	else
	{
		// The web lerps with alpha = 8 * frame dt, frame dt capped at 1/30 s.
		const double Alpha = FMath::Min(1.0, CameraLerp * FMath::Min(static_cast<double>(DeltaSeconds), 1.0 / 30.0));
		CameraPos = FMath::Lerp(CameraPos, EyeTarget, Alpha);
		CameraLook = FMath::Lerp(CameraLook, LookTarget, Alpha);
	}
	// Feel offsets (drift swing, landing dip, corner bank) and the kartFxBus.ts shake ride on top of the
	// follow, never accumulated into it: the springs already smooth them.
	const FVector Right = Coords::DirToUE(FVector(FMath::Cos(Yaw), 0.0, -FMath::Sin(Yaw))); // the kart's right on the ground
	FVector Eye = CameraPos + Right * Juice.CameraSide * 100.0 + FVector(0, 0, Juice.CameraDip * 100.0);
	FRotator View = (CameraLook + FVector(0, 0, Juice.CameraDip * 40.0) - Eye).Rotation();
	View.Yaw += Juice.CameraYaw;
	View.Roll += Juice.CameraRoll;
	const double Now = GetWorld()->GetTimeSeconds();
	const double Amp = ShakePeak * FMath::Exp(-6.5 * (Now - ShakeStart)) + Juice.CameraRumble;
	if (Amp > 0.001)
	{
		const FRotationMatrix Axes(View);
		const double X = (FMath::Sin(Now * 47.3) * 0.6 + FMath::Sin(Now * 83.1 + 1.3) * 0.4) * Amp;
		const double Y = (FMath::Sin(Now * 53.7 + 2.1) * 0.6 + FMath::Sin(Now * 91.9 + 0.4) * 0.4) * Amp * 0.7;
		const double Roll = (FMath::Sin(Now * 39.1 + 0.7) * 0.6 + FMath::Sin(Now * 71.3 + 2.9) * 0.4) * Amp * 0.06;
		Eye += Axes.GetUnitAxis(EAxis::Y) * X * 100.0 + Axes.GetUnitAxis(EAxis::Z) * Y * 100.0;
		View.Roll += FMath::RadiansToDegrees(Roll);
	}
	// Look behind: from just ahead of the kart, back down the road (the kart stays in the foreground).
	if (const AKartPlayerController* Player = Cast<AKartPlayerController>(GetController()); Player && Player->IsLookingBack())
	{
		Eye = Coords::ToUE(Pos + Fwd * 3.6, EyeHeight * 0.6);
		View = (Coords::ToUE(Pos - Fwd * 9.0, LookHeight * 0.45) - Eye).Rotation();
	}
	// -KartCinematic: trailer coverage, a hard cut every 4.5 s through the chase view and tracking shots
	// around the kart (yaw from ahead, distance m, height m, orbit deg/s; distance <= 0 is the chase view).
	FVector4 Orbit = CloseCam;
	if (bCinematic)
	{
		static const FVector4 Shots[] = {
			FVector4(0, -1, 0, 0),       // chase
			FVector4(35, 5.0, 0.9, 0),   // low front three-quarter
			FVector4(95, 6.5, 1.4, 0),   // side tracking
			FVector4(0, 6.0, 2.2, 14),   // slow orbit
			FVector4(180, 13.0, 5.5, 0), // high and wide, behind
			FVector4(150, 3.4, 0.55, 0), // low over the rear wheel
		};
		Orbit = Shots[FMath::FloorToInt32(Now / 4.5) % UE_ARRAY_COUNT(Shots)];
	}
	if (Orbit.Y > 0.0)
	{
		// Rigid to the kart's heading (not the drifting body), so tracking shots stay smooth through drifts.
		const FVector Centre = VehicleRoot->GetComponentLocation() + FVector(0, 0, 50);
		const FVector Around = FRotator(0, GetActorRotation().Yaw + Orbit.X + Orbit.W * Now, 0).Vector();
		Eye = Centre + Around * Orbit.Y * 100.0 + FVector(0, 0, Orbit.Z * 100.0);
		Camera->SetWorldLocationAndRotation(Eye, (Centre - Eye).Rotation());
		Camera->SetFieldOfView(bCinematic ? 62.f : 50.f);
		return;
	}
	Camera->SetWorldLocationAndRotation(Eye, View);

	// Vertical FOV: 75 in split-screen, 72 + speed in a single view; converted to UE's horizontal FOV
	// for this player's part of the screen.
	const double VerticalFov = (bSplitScreenCamera ? 75.0 : 72.0 + FMath::Min(8.0, Speed * 0.05)) + Juice.CameraFovKick * (bSplitScreenCamera ? 0.6 : 1.0);
	double Aspect = 16.0 / 9.0;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		int32 X = 0, Y = 0;
		PC->GetViewportSize(X, Y);
		const ULocalPlayer* Local = PC->GetLocalPlayer();
		const FVector2D Part = Local ? FVector2D(Local->Size) : FVector2D(1.0, 1.0);
		if (X > 0 && Y > 0 && Part.X > 0.0 && Part.Y > 0.0)
		{
			Aspect = (X * Part.X) / (Y * Part.Y);
		}
	}
	const double HalfV = FMath::DegreesToRadians(VerticalFov * 0.5);
	Camera->SetFieldOfView(static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfV) * Aspect))));
}
