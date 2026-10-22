#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AvatarShowcase.generated.h"

class UArcadeAvatarComponent;
class ACameraActor;

/**
 * A dev line-up of party avatars facing a camera, each playing one feature of the avatar on a loop: idle life, the
 * wave, point and clap emotes, the celebration and disappointment variants, effort and the shrug. Spawned in front of
 * the current view by the console command `Movecade.AvatarShowcase [close]` (or `-ExecCmds="Movecade.AvatarShowcase"`
 * with Tools/ue/play.ps1), for checking the avatars in any lit map. Not used by the game.
 */
UCLASS(NotBlueprintable, Transient)
class MOVECADEKART_API AAvatarShowcase : public AActor
{
	GENERATED_BODY()

public:
	AAvatarShowcase();

	/** Lines up the avatars Distance cm in front of the viewer, and takes over the view. */
	void Setup(const FVector& ViewLocation, const FVector& ViewForward, double Distance);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TArray<TObjectPtr<UArcadeAvatarComponent>> Avatars;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	float Clock = 0.f;
};
