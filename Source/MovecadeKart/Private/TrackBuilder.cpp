#include "TrackBuilder.h"

#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "KartCoords.h"
#include "KartDebugFlags.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Track/TrackBuildHelpers.h"
#include "Track/TrackLife.h"
#include "UI/KartTrackCatalog.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogTrackBuilder, Log, All);

namespace
{
	/** The engine Sphere is 1 m across: the sky dome is 10 km in radius. */
	constexpr double SkyDomeScale = 20000.0;
	/** The one-off sky capture is taken again at these times into play, once the dome's material and textures are in. */
	constexpr double SkyRecaptureFirst = 0.5;
	constexpr double SkyRecaptureSecond = 2.0;
	/** MultiplayerPowerups.tsx:159-164: preview boxes yaw at 1.5 rad/s and bob 0.1 m at 2.5 rad/s. */
	constexpr double PreviewBoxSpinRate = 1.5;
	constexpr double PreviewBoxBobCm = 10.0;
	constexpr double PreviewBoxBobRate = 2.5;
	constexpr double PreviewBoxScale = 2.0;
}

ATrackBuilder::ATrackBuilder()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Ground = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Ground"));
	Ground->SetupAttachment(RootComponent);
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ground->bUseAsyncCooking = true;
	// Flat ribbons and the 5 km sea gain nothing from casting shadows and overflow the VSM non-Nanite queue.
	Ground->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = ShapeMaterial.Object;

	ItemBoxes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ItemBoxes"));
	ItemBoxes->SetupAttachment(RootComponent);
	ItemBoxes->SetStaticMesh(CubeMesh.Object);
	ItemBoxes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ItemBoxes->SetMobility(EComponentMobility::Movable);
	ItemBoxes->SetCastShadow(false);

	BoostPads = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoostPads"));
	BoostPads->SetupAttachment(RootComponent);
	BoostPads->SetStaticMesh(PlaneMesh.Object);
	BoostPads->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(RootComponent);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetMobility(EComponentMobility::Movable);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->SetMobility(EComponentMobility::Movable);
	// The dome is static: one capture (RecaptureSky in ApplyLook, again once it has streamed in) instead of a
	// capture every frame. -KartSkyRealtime brings the per-frame capture back for comparisons.
	SkyLight->bRealTimeCapture = false;

	// Movable, so ApplyLook's setters take effect at BeginPlay too (static components ignore them).
	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(RootComponent);
	Atmosphere->SetMobility(EComponentMobility::Movable);

	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(RootComponent);
	Fog->SetMobility(EComponentMobility::Movable);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(RootComponent);
	PostProcess->bUnbound = true;

	SkySphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkySphere"));
	SkySphere->SetupAttachment(RootComponent);
	SkySphere->SetStaticMesh(SphereMesh.Object);
	SkySphere->SetRelativeScale3D(FVector(SkyDomeScale));
	SkySphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkySphere->SetCastShadow(false);
	SkySphere->SetReceivesDecals(false);
	SkySphere->bAffectDistanceFieldLighting = false;
	SkySphere->SetMobility(EComponentMobility::Movable);
}

FString ATrackBuilder::TrackPath(const FString& Id)
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Tracks"), Id + TEXT(".track.json"));
}

TSharedPtr<const MovecadeKart::FTrack> ATrackBuilder::GetTrack()
{
	if (!Track.IsValid() || LoadedId != TrackId)
	{
		Load();
	}
	return Track;
}

bool ATrackBuilder::Load()
{
	Track.Reset();
	Json.Reset();
	LoadedId = TrackId;

	FString Text;
	const FString Path = TrackPath(TrackId);
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTrackBuilder, Error, TEXT("Track file not found: %s. Run Tools/track-export (npm run export)."), *Path);
		return false;
	}
	// One DOM serves both the sim track and the presentation data (ribbons, scenery) read by Build().
	TSharedPtr<FJsonObject> Root;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root);
	TSharedPtr<MovecadeKart::FTrack> Parsed = MakeShared<MovecadeKart::FTrack>();
	FString Error;
	if (!MovecadeKart::FTrack::LoadFromJsonObject(Root, *Parsed, Error))
	{
		UE_LOG(LogTrackBuilder, Error, TEXT("%s: %s"), *Path, *Error);
		return false;
	}
	Json = MoveTemp(Root);
	Track = MoveTemp(Parsed);
	return true;
}

void ATrackBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The track is authored in world space.
	SetActorTransform(FTransform::Identity);
	if (GetTrack())
	{
		Build();
	}
}

void ATrackBuilder::Build()
{
	Ground->ClearAllMeshSections();
	SectionCount = 0;

	// Imported scenery first: an imported ribbon set replaces the procedural one, an imported sea the graybox one.
	ClearScenery();
	const bool bRibbonsImported = BuildImportedMeshes();
	BuildSea();
	const bool bSeaImported = GetComponentsByTag(UStaticMeshComponent::StaticClass(), TEXT("TrackSea")).Num() > 0;
	BuildPrefabInstances();
	BuildSigns();
	BuildLights();
	BuildMotion();

	if (!bRibbonsImported)
	{
		BuildRibbons();
	}
	if (!bSeaImported)
	{
		BuildGrayboxSea();
	}
	// Both tracks paint their own chequers, so only the graybox needs a start line.
	if (SceneryCount == 0)
	{
		BuildStartLine();
	}
	BuildBoostPads();
	BuildItemBoxes();
	ApplyGrayboxMaterials();
	ApplyLook();
}

void ATrackBuilder::BeginPlay()
{
	Super::BeginPlay();
	const FKartDebugFlags& Flags = FKartDebugFlags::Get();
	// -KartTrack=<id>: race another catalog track on this level (local play). The whole track, scenery and look
	// included, is rebuilt for it. Only catalog ids: the id becomes a file path.
	if (!Flags.Track.IsEmpty() && Flags.Track != TrackId && !FKartTrackCatalog::Find(Flags.Track))
	{
		UE_LOG(LogTrackBuilder, Error, TEXT("-KartTrack=%s: not a track in the catalog (UI/KartTrackCatalog.cpp)"), *Flags.Track);
	}
	else if (!Flags.Track.IsEmpty() && Flags.Track != TrackId)
	{
		if (FPaths::FileExists(TrackPath(Flags.Track)))
		{
			UE_LOG(LogTrackBuilder, Log, TEXT("-KartTrack: %s replaces %s"), *Flags.Track, *TrackId);
			TrackId = Flags.Track;
			if (GetTrack())
			{
				Build();
			}
		}
		else
		{
			UE_LOG(LogTrackBuilder, Error, TEXT("-KartTrack=%s: no %s"), *Flags.Track, *TrackPath(Flags.Track));
		}
	}
	if (GetTrack() && SceneryCount == 0 && GetComponentsByTag(UStaticMeshComponent::StaticClass(), MovecadeKart::TrackBuild::SceneryTag).Num() == 0)
	{
		Build(); // A level saved before the assets were imported.
	}
	else if (Json.IsValid())
	{
		ApplyLook(); // the look as the code has it now, even on a level saved earlier
	}
	GatherMotion();

	if (GetWorld()->IsGameWorld() && Track.IsValid() && ATrackLife::IsEnabled())
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		if (ATrackLife* Life = GetWorld()->SpawnActor<ATrackLife>(Params))
		{
			Life->Init(this);
		}
	}

	if (Flags.bTrackCam && Track.IsValid())
	{
		const double Rebase = Track->SuggestedRebaseY;
		const FVector From = MovecadeKart::Coords::ToUE(FVector(Flags.TrackCamFrom.X, Flags.TrackCamFrom.Y + Rebase, Flags.TrackCamFrom.Z));
		const FVector To = MovecadeKart::Coords::ToUE(FVector(Flags.TrackCamLookAt.X, Flags.TrackCamLookAt.Y + Rebase, Flags.TrackCamLookAt.Z));
		DebugCamera = GetWorld()->SpawnActor<ACameraActor>(From, (To - From).Rotation());
	}
}

void ATrackBuilder::SetPreviewItemBoxesVisible(bool bVisible)
{
	ItemBoxes->SetVisibility(bVisible);
}

void ATrackBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Time = GetWorld()->GetTimeSeconds();
	TickMotion(Time);
	TickSkyCapture(Time);
	TickTrackCamera();
	TickPreviewItemBoxes(Time);
	if (bShowCheckpoints)
	{
		DrawCheckpoints();
	}
}

void ATrackBuilder::TickSkyCapture(double Time)
{
	if (SkyRecapturesLeft > 0 && Time >= (SkyRecapturesLeft == 2 ? SkyRecaptureFirst : SkyRecaptureSecond))
	{
		--SkyRecapturesLeft;
		SkyLight->RecaptureSky();
	}
}

void ATrackBuilder::TickTrackCamera()
{
	// -KartTrackCam holds the first viewport on the fixed camera, whatever the game mode sets.
	AActor* Cam = DebugCamera.Get();
	if (!Cam)
	{
		return;
	}
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->GetViewTarget() != Cam)
	{
		PC->SetViewTarget(Cam);
	}
}

void ATrackBuilder::TickPreviewItemBoxes(double Time)
{
	// Only while the preview boxes show (a race hides them for the item presenter's live ones). Each update is an
	// incremental instance change, so the render state is never rebuilt for it.
	if (!ItemBoxes->IsVisible())
	{
		return;
	}
	const FRotator Spin(0.0, -FMath::RadiansToDegrees(PreviewBoxSpinRate * Time), 0.0);
	const FVector Bob(0.0, 0.0, PreviewBoxBobCm * FMath::Sin(PreviewBoxBobRate * Time));
	for (int32 i = 0; i < ItemBoxBase.Num(); ++i)
	{
		ItemBoxes->UpdateInstanceTransform(i, FTransform(Spin, ItemBoxBase[i] + Bob, FVector(PreviewBoxScale)), /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ false);
	}
}

void ATrackBuilder::DrawCheckpoints()
{
	if (!Track.IsValid())
	{
		return;
	}
	for (const MovecadeKart::FCheckpoint& Cp : Track->Checkpoints)
	{
		DrawDebugCircle(GetWorld(), MovecadeKart::Coords::ToUE(Cp.Position, 0.3 + Track->SuggestedRebaseY), Cp.Radius * 100.0, 48,
			Cp.bFinish ? FColor::Green : FColor::Cyan, false, -1.f, 0, 12.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
	}
}
