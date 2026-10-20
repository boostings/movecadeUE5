#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "KartGradeSubsystem.generated.h"

class AKartGradeVolume;

/**
 * Gives every game world the project-wide grade: on BeginPlay it spawns an AKartGradeVolume unless the level
 * already has one (a level may place its own to tune the exposure reference or blur). Levels and game modes need
 * no setup; per-track post-process volumes keep working on top of it.
 */
UCLASS()
class MOVECADEKART_API UKartGradeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The world's grade volume (spawned or level-placed), or null before BeginPlay. */
	AKartGradeVolume* GetGradeVolume() const { return GradeVolume.Get(); }

	//~ UWorldSubsystem
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TWeakObjectPtr<AKartGradeVolume> GradeVolume;
};
