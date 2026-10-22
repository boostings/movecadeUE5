#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AFishingGame;
class FKartPainter;

/**
 * Fishing Frenzy's HUD, painted with the Kart UI's painter: the round clock and the round's record; a panel per
 * angler with the score (kg), catches, combo and what to do now, the cast meter over the distance bands while a cast
 * is held, and in a fight the tension meter (slack, green, strain, the red line) with the snap and slack warnings
 * and the fish's runs; pins over every bobber that jump on a bite; the callouts (Too early!, SNAP!, +2.34 kg);
 * the catch card over a fish held up (species, weight, NEW RECORD); the 3-2-1, the local controls, the intro card
 * and the final standings.
 */
class SFishingHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFishingHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AFishingGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintClock(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintPins(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintLanes(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintCallouts(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintCatchCards(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintCountdown(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const AFishingGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const AFishingGame& Game, float S) const;

	TWeakObjectPtr<AFishingGame> Game;
};
