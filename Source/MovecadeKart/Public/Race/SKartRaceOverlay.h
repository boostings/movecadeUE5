#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AKartRaceManager;

/**
 * The full-screen race overlay (spec 04 §8.2, §8.4): intro card, countdown and "Go!", the top bar
 * (race time, finish window, lead lap, racers), the standings rail, and the results table.
 * Per-viewport details (speed, place, lap) are drawn by each player's AKartHUD.
 */
class MOVECADEKART_API SKartRaceOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKartRaceOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AKartRaceManager>, Manager)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100.0, 100.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** m:ss.mmm */
	static FString FormatRaceTime(double Ms, bool bMillis = true);
	static FString Ordinal(int32 Place);

private:
	TWeakObjectPtr<AKartRaceManager> Manager;
};
