#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KartHUD.generated.h"

/** A text debug HUD for the graybox: speed, lap, checkpoint, drift and boost state, and controls. */
UCLASS()
class MOVECADEKART_API AKartHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
