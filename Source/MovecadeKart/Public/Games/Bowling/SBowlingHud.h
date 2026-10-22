#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"
#include "Widgets/SLeafWidget.h"

class ABowlingGame;
class FKartPainter;
struct FBowlerSeat;
namespace MovecadeKart::Bowling { struct FCard; }

/**
 * Bowling's HUD (BowlingCanvas.tsx:162-319, restyled in the Kart UI's painter): the score sheet with a row per
 * bowler and the ten frames marked X, / and –; who is up with the frame and ball; the local bowler's controls with
 * the power meter and the live line's read; first-turn coach marks that fade once you've bowled; the swing read-out
 * after each release; the beat banners (STRIKE, SPARE, TURKEY, SPLIT, GUTTER) with the screen flash; the intro card
 * and the final standings. ABowlingGame creates it over the viewport and removes it in EndPlay.
 *
 * Paints without building strings: each sheet row is cached per bowler and rebuilt only when their card gains a roll,
 * names are fitted once per scale, every other label is formatted only when its value changes, and the standings
 * are re-ranked only when a score changes.
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
		/** The frame the card is in, 1..10. */
		int32 Frame = 1;
		int32 Strikes = 0;
		int32 Spares = 0;
		/** The results' "N STRIKES · N SPARES". */
		FString Tally;
		/** The "3X" streak chip, formatted when the streak changes. */
		mutable FCachedText StreakText;
		/** The name fitted to the column, for the scale it was fitted at. */
		FString Name;
		FString FittedName;
		float FittedSize = 0.f;
		float FittedScale = -1.f;
	};

	/** The last beat's banner words, formatted once per beat. */
	struct FBeatCache
	{
		double At = -1.0;
		FString Word;
		FString Sub;
		/** Half the banner word's width at FittedScale (the turkey's stars sit outside it). */
		float HalfWidth = 0.f;
		float FittedScale = -1.f;
	};

	/** The row for a seat, refreshed when its card or name (or the scale) changed. */
	const FRowCache& Row(const ABowlingGame& Game, int32 SeatIndex, float S) const;
	static void RebuildRow(FRowCache& Row, const FBowlerSeat& Seat, const MovecadeKart::Bowling::FCard* Card);
	static void FitName(FRowCache& Row, const FString& Name, float S);
	const FBeatCache& Beat(const ABowlingGame& Game, float S) const;

	void PaintSheet(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintUp(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	/** The meter, with the sweet band marked and a tick where you'd let go. */
	void PaintMeter(const FKartPainter& P, const FVector2D& Pos, const FVector2D& Size, float Power, float S) const;
	void PaintKeys(const FKartPainter& P, const ABowlingGame& Game, const FVector2D& Pos, float S) const;
	void PaintCoach(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintSwing(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintBeat(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintFlash(const FKartPainter& P, const ABowlingGame& Game) const;
	void PaintIntro(const FKartPainter& P, const ABowlingGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ABowlingGame& Game, float S) const;

	TWeakObjectPtr<ABowlingGame> Game;
	mutable TArray<FRowCache> Rows;
	/** Seat indices by score for the results (ties in turn order), re-ranked only when a score changes. */
	mutable FArcadeStandingsOrder FinalRanks;
	mutable FBeatCache BeatWords;
	mutable FCachedText FrameText;
	/** "BALL n · n PINS STANDING", keyed by ball * 100 + pins. */
	mutable FCachedText BallText;
	/** "{name} is up", keyed by the bowler's seat. */
	mutable FCachedText UpText;
	/** "{name} bowls first", keyed by the first name's hash. */
	mutable FCachedText FirstText;
	mutable FCachedFloatText SpeedText;
	/** The swing read-out's speed and hook, keyed by the throw's time. */
	mutable TArcadeCachedText<double> SwingSpeedText;
	mutable TArcadeCachedText<double> SwingHookText;
};
