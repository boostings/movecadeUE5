#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ADerbyGame;
class FKartPainter;

/**
 * Home Run Derby's HUD, painted with the Kart UI's painter in a broadcast style: the score bug (batter, points,
 * home runs, the pitch count as ten balls and the strikes), the standings down the side, the pitch's type and speed,
 * the swing's timing (EARLY / PERFECT / LATE), the live distance as a hit carries and the call ("412 FT!",
 * HOME RUN, MOONSHOT, FOUL, STRIKE TWO), the "now batting" card, the local batter's aim and load, a phone's prompt,
 * the intro card and the final standings.
 */
class SDerbyHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDerbyHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ADerbyGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintBug(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintBoard(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintPitch(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintCall(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintStepIn(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintFinal(const FKartPainter& P, const ADerbyGame& Game, float S) const;

	TWeakObjectPtr<ADerbyGame> Game;
};
