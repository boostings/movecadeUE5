#include "KartPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Chase rig, Docs/Port/01 §10.2 (HostCameraFollow, with the x/z mismatch fixed: chase on both axes).
	constexpr double EyeHeight = 4.105;
	constexpr double LookHeight = 2.105;
	constexpr double CameraLerp = 8.0;

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
	Root->InitSphereRadius(115.f); // MESH/HARBOR_KART_RADIUS 1.15 m
	Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Root;

	SlipPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SlipPivot"));
	SlipPivot->SetupAttachment(Root);
	BodyPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyPivot"));
	BodyPivot->SetupAttachment(SlipPivot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = ShapeMaterial.Object;

	// Placeholder body inside the web kart's footprint (about 1.95 x 2.87 m), wheels at the corners.
	Body = MakePart(this, BodyPivot, TEXT("Body"), CubeMesh.Object, FVector(0, 0, 50), FVector(2.4, 1.5, 0.4));
	Driver = MakePart(this, BodyPivot, TEXT("Driver"), SphereMesh.Object, FVector(-15, 0, 105), FVector(0.6, 0.6, 0.7));
	const FVector WheelSlots[] = { FVector(95, -90, 29), FVector(95, 90, 29), FVector(-95, -92, 35), FVector(-95, 92, 35) };
	for (int32 i = 0; i < 4; ++i)
	{
		const float Radius = i < 2 ? 0.58f : 0.7f;
		Wheels.Add(MakePart(this, BodyPivot, *FString::Printf(TEXT("Wheel%d"), i), CylinderMesh.Object,
			WheelSlots[i], FVector(Radius, Radius, 0.3), FRotator(0, 0, 90)));
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->bConstrainAspectRatio = false;

	Movement = CreateDefaultSubobject<UKartMovementComponent>(TEXT("Movement"));
	Movement->UpdatedComponent = Root;
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
		UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(Base, this);
		Paint->SetVectorParameterValue(TEXT("Color"), PaintColor);
		Body->SetMaterial(0, Paint);
		UMaterialInstanceDynamic* Tyre = UMaterialInstanceDynamic::Create(Base, this);
		Tyre->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.011f, 0.011f, 0.012f)); // #1b1b1d
		for (UStaticMeshComponent* Wheel : Wheels)
		{
			Wheel->SetMaterial(0, Tyre);
		}
		UMaterialInstanceDynamic* Suit = UMaterialInstanceDynamic::Create(Base, this);
		Suit->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.9f, 0.9f, 0.92f));
		Driver->SetMaterial(0, Suit);
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
	UpdateVisual();
	UpdateCamera(DeltaSeconds);

	const MovecadeKart::FKartState& S = Movement->SimState();
	if (S.LapsCompleted > LoggedLaps)
	{
		LoggedLaps = S.LapsCompleted;
		UE_LOG(LogTemp, Display, TEXT("Kart lap %d: %.2f s (race %.2f s)%s"), LoggedLaps, S.Time - LastLapTime, S.Time, S.bFinished ? TEXT(" FINISHED") : TEXT(""));
		LastLapTime = S.Time;
	}
}

void AKartPawn::UpdateVisual()
{
	using MovecadeKart::Coords::YawToUE;
	const MovecadeKart::FKartState& S = Movement->SimState();
	SlipPivot->SetRelativeRotation(FRotator(0, YawToUE(S.SlipYaw), 0));
	BodyPivot->SetRelativeLocationAndRotation(FVector(0, 0, S.HopOffset * 100.0),
		FRotator(0, YawToUE(S.GroupYaw), FMath::RadiansToDegrees(-S.Lean)));
	// Front wheels steer with yRot * 30 (Kart.jsx:202-242).
	const double Steer = YawToUE(S.SteerVisual * 30.0);
	for (int32 i = 0; i < 2; ++i)
	{
		Wheels[i]->SetRelativeRotation(FRotator(0, Steer, 90));
	}
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
	const double Chase = 5.4 + FMath::Min(1.35, Speed * 0.018);
	const double Ahead = 7.2 + FMath::Min(2.2, Speed * 0.045);
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
	Camera->SetWorldLocationAndRotation(CameraPos, (CameraLook - CameraPos).Rotation());

	// Vertical FOV 72 + speed (one viewport), converted to UE's horizontal FOV for this viewport.
	const double VerticalFov = 72.0 + FMath::Min(8.0, Speed * 0.05);
	double Aspect = 16.0 / 9.0;
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		int32 X = 0, Y = 0;
		PC->GetViewportSize(X, Y);
		if (X > 0 && Y > 0)
		{
			Aspect = static_cast<double>(X) / Y;
		}
	}
	const double HalfV = FMath::DegreesToRadians(VerticalFov * 0.5);
	Camera->SetFieldOfView(static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfV) * Aspect))));
}
