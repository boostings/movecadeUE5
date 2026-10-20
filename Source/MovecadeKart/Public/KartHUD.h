#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KartHUD.generated.h"

/** Per-view canvas pass for the item screen effects (ink, flash, Prism tint). The HUD proper is SKartRaceHud (Slate). */
UCLASS()
class MOVECADEKART_API AKartHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
