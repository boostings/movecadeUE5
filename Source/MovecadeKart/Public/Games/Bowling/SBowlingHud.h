#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ABowlingGame;
class FKartPainter;
struct FBowlerSeat;

/**
 * Bowling's HUD (BowlingCanvas.tsx:162-319, restyled in the Kart UI's painter): the score sheet with a row per
 * bowler and the ten frames marked X, / and –; who is up with the frame and ball; the local bowler's controls with
 * the power meter and the live line's read; first-turn coach marks that fade once you've bowled; the swing read-out
 * after each release; the beat banners (STRIKE, SPARE, TURKEY, SPLIT, GUTTER) with the screen flash; the intro card
 * and the final standings.
 *
 * The sheet's strings are cached per bowler and rebuilt only when their card gains a roll; names are fitted once per
 * scale; the standings are ordered once when the game ends.
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
	/** One bowler's sheet row, as strings, built when their card changes. */
	struct FRowCache
	{
		FString Id;
		int32 Rolls = -1;
		/** Per frame: the marks ball by ball, and the running total (empty while unset). */
		TArray<TArray<FString>> Marks;
		TArray<FString> Totals;
		FString Score;
		int32 Strikes = 0;
		int32 Spares = 0;
		/** The name fitted to the column, for the scale it was fitted at. */
		FString Name;
		FString FittedName;
		float FittedSize = 0.f;
		float FittedScale = -1.f;
	};

	const FRowCache& Row(const ABowlingGame& Game, int32 SeatIndex, float S) const;

	void PaintSheet(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintUp(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintCoach(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintSwing(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintBeat(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintFlash(const FKartPainter& P, const ABowlingGame& Game) const;
	void PaintIntro(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ABowlingGame& Game, float S) const;

	TWeakObjectPtr<ABowlingGame> Game;
	mutable TArray<FRowCache> Rows;
	/** Seat indices by final score, taken once when the game ends (cleared when it restarts). */
	mutable TArray<int32> FinalOrder;
};
