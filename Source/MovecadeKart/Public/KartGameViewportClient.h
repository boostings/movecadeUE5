#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "KartGameViewportClient.generated.h"

/**
 * Split-screen layouts from the web host (RaceUI.tsx:34-50): two karts top/bottom, three as two on
 * top and one centred below at half width, four as a 2x2 grid. Selected in DefaultEngine.ini
 * (GameViewportClientClassName, ThreePlayerSplitscreenLayout=FavorBottom).
 */
UCLASS()
class MOVECADEKART_API UKartGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	UKartGameViewportClient(const FObjectInitializer& ObjectInitializer);
};
