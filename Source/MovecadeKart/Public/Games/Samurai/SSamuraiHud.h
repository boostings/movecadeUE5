#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ASamuraiGame;
class FKartPainter;

/**
 * Samurai Slice's HUD (SamuraiCanvas.tsx, restyled in the Kart UI's painter): the round clock and wave; a panel per
 * lane with the swordsman's score, combo, lives and Focus (its three-cut charge, then the six-second double-points
 * meter); the engine's 3-2-1; the cut callouts rising in each lane (Perfect!, 2 cut!, Focus!, Bomb!, 3x lost); the
 * local swordsman's controls; the intro card and the final standings.
 */
class SSamuraiHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSamuraiHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASamuraiGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintClock(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintLanes(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintCallouts(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintCountdown(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ASamuraiGame& Game, float S) const;

	TWeakObjectPtr<ASamuraiGame> Game;
};
