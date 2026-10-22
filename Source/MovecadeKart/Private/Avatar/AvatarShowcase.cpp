#include "Avatar/AvatarShowcase.h"

#include "Art/KartMaterials.h"
#include "Avatar/ArcadeAvatar.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "KartLook.h"

namespace
{
	constexpr int32 ShowcaseCount = 7;

	void SpawnShowcase(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->GetFirstPlayerController())
		{
			return;
		}
		// A stage of its own high above the map (the map's sun and sky light it); the line-up faces the camera at -X.
		const bool bClose = Args.Contains(TEXT("close"));
		const FVector Stage(0.0, 0.0, 40000.0);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AAvatarShowcase* Showcase = World->SpawnActor<AAvatarShowcase>(Stage, FRotator::ZeroRotator, Params))
		{
			const double Distance = bClose ? 260.0 : 520.0;
			Showcase->Setup(Stage - FVector::ForwardVector * Distance, FVector::ForwardVector, Distance);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs ShowcaseCommand(TEXT("Movecade.AvatarShowcase"),
		TEXT("Lines up party avatars in front of the view, each playing an avatar feature (dev). Arg: close."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnShowcase));
}

AAvatarShowcase::AAvatarShowcase()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	GetRootComponent()->SetMobility(EComponentMobility::Movable);
}

void AAvatarShowcase::Setup(const FVector& ViewLocation, const FVector& ViewForward, double Distance)
{
	UWorld* World = GetWorld();
	const FVector Centre = ViewLocation + ViewForward * Distance;
	// A floor and a backdrop, so the line-up reads against something.
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	auto Panel = [&](const FVector& At, const FRotator& Rotation, const FVector& Scale, const FLinearColor& Colour)
	{
		UStaticMeshComponent* M = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), TEXT("Stage")));
		M->SetupAttachment(GetRootComponent());
		M->SetStaticMesh(Plane);
		M->RegisterComponent();
		M->SetWorldLocationAndRotation(At, Rotation);
		M->SetWorldScale3D(Scale);
		M->SetMaterial(0, MovecadeKart::Art::Plain(this, Colour, 0.7));
	};
	Panel(Centre, FRotator::ZeroRotator, FVector(30.0, 30.0, 1.0), FLinearColor(0.32f, 0.3f, 0.42f));
	Panel(Centre + ViewForward * 300.0 + FVector(0, 0, 500.0), FRotator(90.0, 0.0, 0.0), FVector(10.0, 30.0, 1.0), FLinearColor(0.2f, 0.18f, 0.34f));
	const FVector Side = FVector::CrossProduct(FVector::UpVector, ViewForward).GetSafeNormal();
	const double Spacing = Distance < 300.0 ? 70.0 : 105.0;
	const FRotator Facing = (-ViewForward).Rotation();
	for (int32 i = 0; i < ShowcaseCount; ++i)
	{
		UArcadeAvatarComponent* A = NewObject<UArcadeAvatarComponent>(this, MakeUniqueObjectName(this, UArcadeAvatarComponent::StaticClass(), TEXT("ShowAvatar")));
		A->SetupAttachment(GetRootComponent());
		A->RegisterComponent();
		A->SetWorldLocationAndRotation(Centre + Side * (i - (ShowcaseCount - 1) * 0.5) * Spacing, Facing);
		FKartLook Look = FKartLook::MakeVaried(i + 5);
		Look.bJerseyMatchesPaint = false;
		static const TCHAR* const Jerseys[] = { TEXT("f0508e"), TEXT("5ec8e5"), TEXT("f7c948"), TEXT("a67cf0"), TEXT("7ed957"), TEXT("ff8a3d"), TEXT("e8394d") };
		Look.Jersey = FLinearColor::FromSRGBColor(FColor::FromHex(Jerseys[i % UE_ARRAY_COUNT(Jerseys)]));
		Look.Paint = Look.Jersey;
		A->SetLook(Look);
		Avatars.Add(A);
	}
	// A camera at eye height a little above the line-up, looking at its chests.
	Camera = World->SpawnActor<ACameraActor>(Centre - ViewForward * Distance + FVector(0, 0, Distance < 300.0 ? 185.0 : 150.0), FRotator::ZeroRotator);
	if (Camera)
	{
		const FVector LookAt = Centre + FVector(0, 0, Distance < 300.0 ? 165.0 : 95.0);
		Camera->SetActorRotation((LookAt - Camera->GetActorLocation()).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(Distance < 300.0 ? 52.f : 70.f);
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			PC->SetViewTarget(Camera);
		}
	}
}

void AAvatarShowcase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	// Keep the view (games re-target their own cameras).
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && Camera && PC->GetViewTarget() != Camera)
	{
		PC->SetViewTarget(Camera);
	}
	const int32 Beat = static_cast<int32>(Clock / 3.f);
	const float InBeat = FMath::Fmod(Clock, 3.f);
	const float Envelope = FMath::Clamp(InBeat / 0.3f, 0.f, 1.f) * FMath::Clamp((2.7f - InBeat) / 0.3f, 0.f, 1.f);
	for (int32 i = 0; i < Avatars.Num(); ++i)
	{
		FArcadePoseInput& P = Avatars[i]->Pose();
		P = FArcadePoseInput();
		P.Look = FVector(1.0, 0.0, 0.05);
		P.LookWeight = 0.3f;
		switch (i)
		{
		case 0: // idle: breath, weight shifts, glances
			break;
		case 1:
			P.Emote = EArcadeEmote::Wave;
			P.EmoteWeight = 1.f;
			break;
		case 2:
			P.Emote = Beat % 2 == 0 ? EArcadeEmote::Point : EArcadeEmote::FistPump;
			P.EmoteWeight = Envelope;
			P.PointAt = FVector(0.8, 0.5, 0.15);
			break;
		case 3:
			P.Emote = EArcadeEmote::Clap;
			P.EmoteWeight = 1.f;
			break;
		case 4: // every celebration in turn
			P.CheerStyle = static_cast<EArcadeCheer>(1 + Beat % (static_cast<int32>(EArcadeCheer::Count) - 1));
			P.Cheer = Envelope;
			break;
		case 5: // every disappointment in turn
			P.SlumpStyle = static_cast<EArcadeSlump>(1 + Beat % (static_cast<int32>(EArcadeSlump::Count) - 1));
			P.Slump = Envelope;
			break;
		default: // straining at a heavy lift, then shrugging it off
			if (Beat % 2 == 0)
			{
				P.Effort = Envelope;
				P.Crouch = 0.35f * Envelope;
				P.Lean = 0.2f * Envelope;
				P.Hand[0] = FVector(0.55, 0.15, -0.5);
				P.Hand[1] = FVector(0.55, -0.15, -0.5);
				P.HandWeight[0] = P.HandWeight[1] = Envelope;
			}
			else
			{
				P.Emote = EArcadeEmote::Shrug;
				P.EmoteWeight = Envelope;
			}
			break;
		}
	}
}
