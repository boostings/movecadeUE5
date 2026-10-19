#include "KartGameViewportClient.h"

UKartGameViewportClient::UKartGameViewportClient(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// FPerPlayerSplitscreenData(SizeX, SizeY, OriginX, OriginY), as fractions of the screen.
	FSplitscreenData& Three = SplitscreenInfo[ESplitScreenType::ThreePlayer_FavorBottom];
	Three.PlayerData.Reset();
	Three.PlayerData.Add(FPerPlayerSplitscreenData(0.5f, 0.5f, 0.0f, 0.0f));
	Three.PlayerData.Add(FPerPlayerSplitscreenData(0.5f, 0.5f, 0.5f, 0.0f));
	Three.PlayerData.Add(FPerPlayerSplitscreenData(0.5f, 0.5f, 0.25f, 0.5f));
}
