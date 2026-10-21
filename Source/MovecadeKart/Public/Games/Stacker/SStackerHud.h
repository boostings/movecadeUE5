#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AStackerGame;
class FKartPainter;

/**
 * Sky Stacker's HUD (StackerCanvas.tsx, restyled in the Kart UI's painter): the round clock; a panel per lane with
 * the builder's score, lives, height and combo and the balance gauge (the lean against its limit, the wind, the
 * danger building toward a topple); the engine's 3-2-1; the drop callouts over each tower (Perfect!, +N, Out!,
 * Lost balance); a phone that has gone quiet; the local builder's controls; the intro card and the final standings.
 */
class SStackerHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SStackerHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AStackerGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintClock(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintLanes(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintCallouts(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintCountdown(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const AStackerGame& Game, float S) const;

	TWeakObjectPtr<AStackerGame> Game;
};
