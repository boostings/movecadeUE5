#include "KartPawn.h"

#include "Audio/KartAudioComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "FX/KartFxComponent.h"
#include "KartDebugFlags.h"
#include "KartDriverAnimInstance.h"
#include "KartMovementComponent.h"
#include "KartPawnTuning.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY(LogKartPawn);

using namespace MovecadeKart;

namespace
{
	/** kartFxBus.ts: the shake envelope decays at 6.5/s. */
	constexpr double ShakeDecay = 6.5;
	/** GO: the FOV punch (degrees, a smaller one in split-screen), its seconds, and a little shake. */
	constexpr double RaceStartPunch = 7.0;
	constexpr double RaceStartPunchSplit = 4.0;
	constexpr double RaceStartPunchSeconds = 0.5;
	constexpr double RaceStartShake = 0.05;
	/** Name plate text height (cm). */
	constexpr float NamePlateTextSize = 30.f;

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
}

AKartPawn::AKartPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// Run after the movement component has stepped the sim this frame.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Root = CreateDefaultSubobject<USphereComponent>(TEXT("Root"));
	Root->InitSphereRadius(Pawn::KartRadiusCm);
	Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Root;

	SlipPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SlipPivot"));
	SlipPivot->SetupAttachment(Root);
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(SlipPivot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlateMaterial(TEXT("/Game/Karts/Materials/M_NamePlateBack.M_NamePlateBack"));

	VehicleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	VehicleRoot->SetupAttachment(BodyPivot);
	VehicleRoot->SetRelativeLocationAndRotation(FVector(0, 0, Pawn::RoadBelowOrigin), FRotator(0, -90, 0));

	// Everything but the wheels rides the chassis spring (about the hub line, the model origin). The meshes come
	// with the vehicle (ApplyVehicle).
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
	NamePlate->SetWorldSize(NamePlateTextSize);
	NamePlate->SetOwnerNoSee(true);
	NamePlate->SetCastShadow(false);
	NamePlate->SetUsingAbsoluteRotation(true);
	NamePlateBack = MakePart(this, NamePlate, TEXT("NamePlateBack"), PlaneMesh.Object, FVector(-1, 0, 0), FVector(0.4, 1.0, 1.0), FRotator(90, 0, 0));
	NamePlateBack->SetMaterial(0, PlateMaterial.Object);
	NamePlateBack->SetOwnerNoSee(true);
	NamePlateBack->SetCastShadow(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->bConstrainAspectRatio = false;

	Movement = CreateDefaultSubobject<UKartMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Root;

	Audio = CreateDefaultSubobject<UKartAudioComponent>(TEXT("Audio"));
	// The prism shimmer, the rocket shell and every driving effect; it reads FKartItemVisual and the sim.
	CreateDefaultSubobject<UKartFxComponent>(TEXT("Fx"));

	Feel.bEnabled = !FKartDebugFlags::Get().bWebFeel;
}

UPawnMovementComponent* AKartPawn::GetMovementComponent() const
{
	return Movement;
}

void AKartPawn::BeginPlay()
{
	Super::BeginPlay();
	if (!bLookSet)
	{
		FKartLook Default = FKartLook::MakeVaried(0);
		Default.Paint = PaintColor;
		SetLook(Default);
	}
	SnapCamera();
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
	LogLaps();
}

void AKartPawn::LogLaps()
{
	// The lap times in the log are what Tools/ue/play.ps1 reports after a run.
	const FKartState& S = Movement->SimState();
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
	const double Remaining = ShakePeak * FMath::Exp(-ShakeDecay * (Now - ShakeStart));
	ShakePeak = FMath::Max(PeakMetres, Remaining);
	ShakeStart = Now;
}

void AKartPawn::PlayCountdownSwoop(double Seconds)
{
	SwoopStart = GetWorld()->GetTimeSeconds();
	SwoopSeconds = FMath::Max(0.1, Seconds);
}

void AKartPawn::PlayRaceStart()
{
	Feel.Punch(bSplitScreenCamera ? RaceStartPunchSplit : RaceStartPunch, RaceStartPunchSeconds);
	AddShake(RaceStartShake);
}

void AKartPawn::SnapCamera()
{
	if (Movement && Movement->HasSim())
	{
		CameraTeleports = Movement->SimState().TeleportCount;
	}
	FinishCamStart = -1.0;
	CameraPos = FVector::ZeroVector;
	CameraLook = FVector::ZeroVector;
	if (Movement && Movement->HasSim())
	{
		UpdateCamera(0.f);
	}
}
