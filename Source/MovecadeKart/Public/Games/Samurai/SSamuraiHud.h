#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"
#include "Widgets/SLeafWidget.h"

class ASamuraiGame;
class FKartPainter;
namespace MovecadeKart::Samurai { struct FPlayer; }

/**
 * Samurai Slice's HUD (SamuraiCanvas.tsx, restyled in the Kart UI's painter): the round clock and wave; a panel per
 * lane with the swordsman's score, combo, lives and Focus (its three-cut charge, then the six-second double-points
 * meter); the engine's 3-2-1; the cut callouts rising in each lane (Perfect!, 2 cut!, Focus!, Bomb!, 3x lost) and
 * the presentation's own notes (Swing faster!, Missed, Dropped); the centre banner (FOCUS!, 10x COMBO!) and the
 * Focus frame; the local swordsman's controls; the cinematic intro card, the practice apple's coach mark, and the
 * final standings.
 *
 * ASamuraiGame adds it to the viewport in Setup and removes it in EndPlay; it only reads the game. Every number it
 * paints is formatted when it changes, not per frame, and the camera projection is taken once per paint.
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
	// S is the canvas scale (the viewport's height over the 1080-line design canvas).
	void PaintClock(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintLanes(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintLanePanel(const FKartPainter& P, const ASamuraiGame& Game, const MovecadeKart::Samurai::FPlayer& Player, float Width, float S) const;
	void PaintFocusMeter(const FKartPainter& P, const MovecadeKart::Samurai::FPlayer& Player, const FVector2D& PanelPos, float PanelWidth, float S) const;
	void PaintCallouts(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintNotes(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintBanner(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintFocusFrame(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintCountdown(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintIntroSeats(const FKartPainter& P, const ASamuraiGame& Game, const FVector2D& Centre, double Age, float S) const;
	void PaintPractice(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintPracticeSwipe(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ASamuraiGame& Game, float S) const;
	const FLinearColor& CalloutColor(const FString& Hex) const;

	/** One lane panel's numbers. */
	struct FLaneTexts
	{
		FCachedText Score;
		FCachedText Combo;
	};

	/** Standings text: Place is per row (rank), Score and Detail (the stats line) per player. */
	struct FStandingTexts
	{
		FCachedText Place;
		FCachedText Score;
		TArcadeCachedText<FIntVector> Detail;
	};

	TWeakObjectPtr<ASamuraiGame> Game;
	mutable FCachedText SecondsText;
	mutable FCachedText CountText;
	mutable FCachedText WaveText;
	mutable TArray<FLaneTexts> LaneTexts;
	mutable FArcadeStandingsOrder Ranks;
	mutable TArray<FStandingTexts> StandingTexts;
	/** Effect colours by their hex, parsed once. */
	mutable TMap<FString, FLinearColor> CalloutColors;
	/** This paint's projection. */
	mutable FArcadeHudView View;
	/** Polygon and polyline points, reused. */
	mutable TArray<FVector2D> Scratch;
};
