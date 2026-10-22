#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"
#include "Widgets/SLeafWidget.h"

class AStackerGame;
class FKartPainter;
namespace MovecadeKart::Stacker { struct FTower; }

/**
 * Sky Stacker's HUD (StackerCanvas.tsx, restyled in the Kart UI's painter), added to the viewport by AStackerGame
 * for its round: the round clock and the wind; a panel per lane with the builder's score, lives, height and combo and
 * the balance gauge (the lean against its steady band and limit, the danger building toward a topple); the 3-2-1;
 * the drop callouts over each tower (Perfect!, +N, Out!, Lost balance); a phone that has gone quiet; the local
 * builder's controls and coach marks ("drop when aligned", "lean against the wind"); the intro card and the final
 * standings.
 *
 * Paints from cached text (a string is rebuilt only when its number changes) and takes the camera's projection once
 * per paint.
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
	/** A lane panel's numbers, as text. */
	struct FLaneText
	{
		FCachedText Score;
		FCachedText Row;
		FCachedText Combo;
	};

	/** The fixed labels, made once (an NSLOCTEXT per paint would look its text up every frame). */
	struct FLabels
	{
		FText Wind, Done, Out, Quiet, Tipping, Steady, DropNow, DropAligned, DropKeys, LeanWind, LeanKeysRight, LeanKeysLeft,
			Go, Controls, Title, Rules, Final;
		FString Left, Right;
	};

	void PaintClock(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintWind(const FKartPainter& P, const AStackerGame& Game, const FVector2D& Top, float S) const;
	void PaintLanes(const FKartPainter& P, const AStackerGame& Game, const FArcadeHudView& View, float S) const;
	void PaintLanePanel(const FKartPainter& P, const AStackerGame& Game, int32 TowerIndex, const FVector2D& Pos, float Width, float S) const;
	void PaintGauge(const FKartPainter& P, const AStackerGame& Game, const MovecadeKart::Stacker::FTower& Tower, bool bHuman, const FVector2D& Pos,
		float Width, float S) const;
	void PaintBanner(const FKartPainter& P, const AStackerGame& Game, const MovecadeKart::Stacker::FTower& Tower, bool bPhone, const FVector2D& Pos,
		float Width, float S) const;
	void PaintCallouts(const FKartPainter& P, const AStackerGame& Game, const FArcadeHudView& View, float S) const;
	void PaintCoach(const FKartPainter& P, const AStackerGame& Game, const FArcadeHudView& View, float S) const;
	void PaintDropCoach(const FKartPainter& P, const AStackerGame& Game, const FArcadeHudView& View, float S) const;
	void PaintLeanCoach(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintCountdown(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const AStackerGame& Game, float S) const;
	void CacheStandingLines(const AStackerGame& Game) const;

	/** Where each lane's panel sits (its top centre, pixels), for the coach's lean mark. */
	mutable TArray<FVector2D> PanelAt;
	mutable TArray<FLaneText> LaneText;
	mutable FCachedText ClockText;
	/** The standings' detail lines, built once when the round ends. */
	mutable TArray<FString> StandingLines;
	mutable bool bStandingsCached = false;
	/** "Rosa · Mika · ...": the intro card's line-up. */
	FString Names;
	FLabels Labels;

	TWeakObjectPtr<AStackerGame> Game;
};
