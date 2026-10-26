#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * The phone build's loading screen (MoviePlayer's WidgetLoadingScreen): the game's name on the night colour and a
 * spinner, from the first map load on, so a cold boot never sits on a black screen. MoviePlayer ticks it on its own
 * thread while the game thread loads: it touches no UObject and uses only the engine's built-in fonts.
 */
class SMobileBootSplash : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMobileBootSplash) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};
