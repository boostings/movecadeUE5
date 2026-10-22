#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KartHUD.generated.h"

/**
 * The race game mode's HUD class. It draws nothing itself: the HUD proper (place, lap, speed, item, minimap,
 * banners) is Slate (SKartRaceHud, one layout per view), and the item screen effects (ink, explosion flash, Prism
 * rainbow, hit and lightning flashes) are a post-process on each kart's camera (FX/KartFxComponent). It stays as
 * the class AKartGameMode names, so canvas debug drawing (showdebug) keeps working.
 */
UCLASS()
class MOVECADEKART_API AKartHUD : public AHUD
{
	GENERATED_BODY()
};
