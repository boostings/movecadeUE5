#include "Render/KartGradeSubsystem.h"

#include "EngineUtils.h"
#include "Render/KartGradeVolume.h"

bool UKartGradeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Played worlds only: editor viewports keep whatever the level itself shows.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UKartGradeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	for (TActorIterator<AKartGradeVolume> It(&InWorld); It; ++It)
	{
		GradeVolume = *It;
		return;
	}

	FActorSpawnParameters Params;
	Params.Name = TEXT("KartGrade");
	Params.ObjectFlags |= RF_Transient; // never saved into the level
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GradeVolume = InWorld.SpawnActor<AKartGradeVolume>(AKartGradeVolume::StaticClass(), FTransform::Identity, Params);
}
