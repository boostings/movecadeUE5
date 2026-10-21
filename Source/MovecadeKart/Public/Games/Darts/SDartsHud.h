#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ADartsGame;
class FKartPainter;

/**
 * Bullseye Bash's HUD (DartsCanvas.tsx and the scene's callouts, restyled in the Kart UI's painter): the
 * scoreboard with each thrower's visits, who is up with the round, the called shot and the darts left; the
 * engine's 3-2-1; the score callouts rising off the board where each dart lands, and the round banners; the
 * local thrower's controls with the power meter (its sweet spot marked); the intro card and the final standings.
 */
class SDartsHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDartsHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ADartsGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintBoard(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintUp(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintCountdown(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintCallouts(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ADartsGame& Game, float S) const;

	TWeakObjectPtr<ADartsGame> Game;
};
