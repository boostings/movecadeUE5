#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ABowlingGame;
class FKartPainter;

/**
 * Bowling's HUD (BowlingCanvas.tsx:162-319, restyled in the Kart UI's painter): the score sheet with a row per
 * bowler and the ten frames marked X, / and –; who is up with the frame and ball; the local bowler's controls with
 * the power meter while the ball is gripped; the beat banners (STRIKE, SPARE, TURKEY, SPLIT, GUTTER); the intro
 * card and the final standings.
 */
class SBowlingHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SBowlingHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ABowlingGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintSheet(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintUp(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintBeat(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ABowlingGame& Game, float S) const;

	TWeakObjectPtr<ABowlingGame> Game;
};
