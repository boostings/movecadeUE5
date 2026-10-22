#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AStackerGame;
class FKartPainter;
struct FStackerView;

/**
 * Sky Stacker's HUD (StackerCanvas.tsx, restyled in the Kart UI's painter): the round clock and the wind; a panel per
 * lane with the builder's score, lives, height and combo and the balance gauge (the lean against its steady band and
 * limit, the wind's push, the danger building toward a topple); the engine's 3-2-1; the drop callouts over each tower
 * (Perfect!, +N, Out!, Lost balance); a phone that has gone quiet; the local builder's controls and coach marks
 * ("drop when aligned", "lean against the wind"); the intro card and the final standings.
 *
 * Paints from cached text: a string is rebuilt only when its number changes, and the camera's projection is taken
 * once per paint.
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
	/** A number and its text, rebuilt only when the number changes. */
	struct FCachedText
	{
		int32 Value = INT32_MIN;
		FString Text;
	};
	struct FLaneText
	{
		FCachedText Score;
		FCachedText Row;
		FCachedText Combo;
	};

	void PaintClock(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintLanes(const FKartPainter& P, const AStackerGame& Game, const FStackerView& View, float S) const;
	void PaintCallouts(const FKartPainter& P, const AStackerGame& Game, const FStackerView& View, float S) const;
	void PaintCoach(const FKartPainter& P, const AStackerGame& Game, const FStackerView& View, float S) const;
	void PaintCountdown(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const AStackerGame& Game, float S) const;

	/** Where each lane's panel sits (screen x, 0..1), for the coach's lean mark. */
	mutable TArray<FVector2D> PanelAt;
	mutable TArray<FLaneText> LaneText;
	mutable FCachedText ClockText;
	mutable TArray<FString> StandingLines;
	mutable bool bStandingsCached = false;
	FString Names;

	TWeakObjectPtr<AStackerGame> Game;
};
