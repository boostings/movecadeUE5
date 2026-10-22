#include "Track/TrackLife.h"

#include "EngineUtils.h"
#include "KartCoords.h"
#include "KartDebugFlags.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Race/KartRaceManager.h"
#include "Track/TrackLifeShared.h"
#include "TrackBuilder.h"

DEFINE_LOG_CATEGORY(LogTrackLife);

using namespace MovecadeKart;
using namespace MovecadeKart::LifeShared;

namespace
{
	/** The fixed seed of the shows and the shells' scatter: the same race looks the same. */
	constexpr int32 LifeSeed = 0x7ac4;
	/** Where an empty kart slot is parked: far outside every field and flock. */
	const FVector ParkedKart(1.0e7, 1.0e7, -1.0e7);
	/** Velocity is smoothed over frames, half the new each frame. */
	constexpr double VelocitySmoothing = 0.5;
	/** -KartLifeDemo: the phantom kart starts 2 s in and laps a 6 m circle at 12 m/s. */
	constexpr double DemoDelaySeconds = 2.0;
	constexpr double DemoRadiusCm = 600.0;
	constexpr double DemoSpeedCm = 1200.0;
	/** A steady breeze (x, y direction) that slowly breathes. */
	constexpr float WindX = 0.8f;
	constexpr float WindY = 0.6f;
	/** Cheers settle at this rate (per second) once their hold has run out. */
	constexpr double CheerFall = 0.5;
}

ATrackLife::ATrackLife()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Rng.Initialize(LifeSeed);
}

bool ATrackLife::IsEnabled()
{
	return !FKartDebugFlags::Get().bNoLife;
}

void ATrackLife::Init(ATrackBuilder* Builder)
{
	const TSharedPtr<const FTrack> Track = Builder ? Builder->GetTrack() : nullptr;
	if (!Track.IsValid())
	{
		return;
	}
	Collection = LoadLife<UMaterialParameterCollection>(TEXT("MPC_TrackLife"));
	if (!Collection)
	{
		UE_LOG(LogTrackLife, Warning, TEXT("No track life assets; run Tools/ue/make_track_life.py"));
		return;
	}
	Karts.SetNum(MaxKarts);
	FindFinishLine(*Track);

	const FKartDebugFlags& Flags = FKartDebugFlags::Get();
	if (Flags.bLifeDemo && Flags.bTrackCam)
	{
		bDemo = true;
		DemoCentre = Coords::ToUE(FVector(Flags.TrackCamLookAt.X, Flags.TrackCamLookAt.Y + Track->SuggestedRebaseY, Flags.TrackCamLookAt.Z));
	}

	BuildGrass(*Track);
	BuildCrowds(Builder);
	BuildButterflies();
	Builder->EnableFoliageWind();
	UE_LOG(LogTrackLife, Log, TEXT("%s: %d grass tufts, %d spectators, %d butterflies"), *Track->Id, GrassTufts, Spectators, Butterflies.Num());
}

void ATrackLife::FindFinishLine(const FTrack& Track)
{
	// The fireworks go up over the finish checkpoint, either side of the road there.
	for (const FCheckpoint& Cp : Track.Checkpoints)
	{
		if (!Cp.bFinish)
		{
			continue;
		}
		FVector2D Nearest;
		double Offset = 0.0;
		Track.NearestCentre(Cp.Position, Nearest, Offset);
		const int32 Index = Track.NearestWaypoint(Cp.Position);
		const FVector2D Next = Track.Waypoints.IsValidIndex(Index) ? Track.Waypoints[(Index + 1) % Track.Waypoints.Num()] : Cp.Position + FVector2D(0.0, -1.0);
		FinishCm = Coords::ToUE(Nearest, Track.GroundY + Track.SuggestedRebaseY);
		const FVector Forward = (Coords::ToUE(Next) - Coords::ToUE(Cp.Position)).GetSafeNormal2D();
		FinishRight = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
		bFinishFirework = true;
		return;
	}
}

void ATrackLife::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Collection)
	{
		return;
	}
	if (!Race.IsValid())
	{
		BindRace();
	}
	GatherKarts(DeltaSeconds);
	UpdateShared(DeltaSeconds);
	UpdateTrample(DeltaSeconds);
	UpdateButterflies(DeltaSeconds);
	UpdateFireworks();
}

void ATrackLife::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AKartRaceManager* Manager = Race.Get())
	{
		Manager->OnCue.Remove(CueHandle);
	}
	if (UMaterialParameterCollectionInstance* Shared = Collection ? GetWorld()->GetParameterCollectionInstance(Collection) : nullptr)
	{
		Shared->SetScalarParameterValue(TEXT("Cheer"), 0.f);
	}
	Super::EndPlay(Reason);
}

void ATrackLife::BindRace()
{
	if (AKartRaceManager* Manager = AKartRaceManager::Get(this))
	{
		Race = Manager;
		CueHandle = Manager->OnCue.AddUObject(this, &ATrackLife::OnRaceCue);
	}
}

void ATrackLife::GatherKarts(float DeltaSeconds)
{
	int32 Count = 0;
	for (TActorIterator<AKartPawn> It(GetWorld()); It && Count < MaxKarts; ++It)
	{
		FKartView& View = Karts[Count++];
		const FVector Position = It->GetActorLocation();
		const bool bFresh = View.Position.IsZero();
		View.Velocity = bFresh || DeltaSeconds <= 0.f ? FVector::ZeroVector : FMath::Lerp(View.Velocity, (Position - View.Position) / DeltaSeconds, VelocitySmoothing);
		View.Position = Position;
		const UKartMovementComponent* Move = It->GetKartMovement();
		View.Speed = Move && Move->HasSim() ? FMath::Abs(Move->SimState().Speed) : View.Velocity.Size() / 100.0;
		View.bOnDirt = Move && Move->HasSim() && Move->SimState().bOnDirt;
	}
	for (int32 i = Count; i < MaxKarts; ++i)
	{
		Karts[i] = FKartView();
		Karts[i].Position = ParkedKart;
	}
	// -KartLifeDemo: a phantom kart lapping a circle on the lawn where -KartTrackCam looks.
	if (bDemo && Count < MaxKarts)
	{
		const double T = GetWorld()->GetTimeSeconds() - DemoDelaySeconds;
		if (T > 0.0)
		{
			const double A = T * DemoSpeedCm / DemoRadiusCm;
			FKartView& View = Karts[Count];
			View.Position = DemoCentre + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * DemoRadiusCm;
			View.Velocity = FVector(-FMath::Sin(A), FMath::Cos(A), 0.0) * DemoSpeedCm;
			View.Speed = DemoSpeedCm / 100.0;
			View.bOnDirt = true;
		}
	}
}

void ATrackLife::UpdateShared(float DeltaSeconds)
{
	UMaterialParameterCollectionInstance* Shared = GetWorld()->GetParameterCollectionInstance(Collection);
	if (!Shared)
	{
		return;
	}
	static const FName Names[MaxKarts] = { TEXT("Kart0"), TEXT("Kart1"), TEXT("Kart2"), TEXT("Kart3"), TEXT("Kart4"), TEXT("Kart5"), TEXT("Kart6"), TEXT("Kart7") };
	for (int32 i = 0; i < MaxKarts; ++i)
	{
		const FKartView& K = Karts[i];
		Shared->SetVectorParameterValue(Names[i], FLinearColor(K.Position.X, K.Position.Y, K.Position.Z, K.Speed));
	}
	const double T = GetWorld()->GetTimeSeconds();
	const double Strength = 1.0 + 0.25 * FMath::Sin(T * 0.13) + 0.15 * FMath::Sin(T * 0.37 + 1.1);
	Shared->SetVectorParameterValue(TEXT("Wind"), FLinearColor(WindX, WindY, static_cast<float>(Strength), 0.f));
	if (bHasField)
	{
		Shared->SetVectorParameterValue(TEXT("Field"), FLinearColor(FieldMin.X, FieldMin.Y, FieldSize, FieldSize));
	}
	// Cheers hold for a moment, then settle.
	if (CheerHold > 0.0)
	{
		CheerHold -= DeltaSeconds;
	}
	else
	{
		Cheer = FMath::Max(0.0, Cheer - DeltaSeconds * CheerFall);
	}
	Shared->SetScalarParameterValue(TEXT("Cheer"), static_cast<float>(Cheer));
}

void ATrackLife::OnRaceCue(int32 Seat, FName Cue)
{
	// The crowd jumps (level) and holds it (seconds); the bigger moments get fireworks (shells over seconds).
	auto Raise = [this](double Level, double Hold)
	{
		Cheer = FMath::Max(Cheer, Level);
		CheerHold = FMath::Max(CheerHold, Hold);
	};
	static const FName Go(TEXT("race.go")), Lap(TEXT("kart.lap")), Final(TEXT("kart.final-lap")), Finish(TEXT("kart.finish")), Boom(TEXT("kart.explosion"));
	if (Cue == Go)
	{
		Raise(1.0, 2.5);
		FireworkShow(4, 1.2);
	}
	else if (Cue == Lap)
	{
		Raise(0.7, 1.0);
	}
	else if (Cue == Final)
	{
		Raise(0.9, 1.5);
		FireworkShow(2, 0.6);
	}
	else if (Cue == Finish)
	{
		// The winner gets the big show; later finishers a few shells each.
		const bool bFirst = !bFinishShowDone;
		bFinishShowDone = true;
		Raise(1.0, bFirst ? 5.0 : 2.0);
		FireworkShow(bFirst ? 12 : 3, bFirst ? 5.0 : 1.0);
	}
	else if (Cue == Boom)
	{
		Raise(0.8, 0.6);
	}
}
