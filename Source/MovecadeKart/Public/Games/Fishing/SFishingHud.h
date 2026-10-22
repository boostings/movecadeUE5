#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"
#include "Widgets/SLeafWidget.h"

class AFishingGame;
class FKartPainter;

/**
 * Fishing Frenzy's HUD, painted with the Kart UI's painter; AFishingGame adds it to the viewport and removes it in
 * EndPlay. It shows the round clock and the round's record; a panel per angler with the score (kg), catches, combo
 * and what to do now, the cast meter over the distance bands while a cast is held, and in a fight the tension meter
 * (slack, green, strain, the red line) with the snap and slack warnings and the fish's runs; pins over every bobber
 * that jump on a bite; the callouts (Too early!, SNAP!, +2.34 kg); the catch card over a fish held up (species,
 * weight, NEW RECORD); the 3-2-1, the local controls, the intro card and the final standings.
 *
 * Every label is formatted only when its value changes (the caches below), so a repaint builds no strings.
 */
class SFishingHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SFishingHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AFishingGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** What every paint step needs: the painter, the game, the canvas scale and this paint's camera. */
	struct FFrame
	{
		const FKartPainter& P;
		const AFishingGame& Game;
		float S;
		FArcadeHudView View;
	};

	/** The record line's inputs (the holder can change only with the weight, but a rematch starts over). */
	struct FRecordKey
	{
		int32 Grams = 0;
		int32 HolderLane = INDEX_NONE;
		bool operator==(const FRecordKey&) const = default;
	};

	/** A standings row's detail line inputs. */
	struct FDetailKey
	{
		int32 Catches = 0;
		int32 BiggestSpecies = -1;
		int32 BiggestG = 0;
		int32 JunkCount = 0;
		bool operator==(const FDetailKey&) const = default;
	};

	/** One angler's labels. */
	struct FLaneTexts
	{
		FCachedText Score;
		TArcadeCachedText<int64> Catches;
		FCachedText Combo;
		FCachedText FightDistance;
		/** Band x 1000 + metres. */
		FCachedText CastAim;
		FCachedText CastOut;
		FCachedText Waiting;
		FCachedText LandedName;
		FCachedText CardName;
		FCachedText CardWeight;
		FCachedText CardCombo;
		TArcadeCachedText<FDetailKey> Detail;
	};

	void PaintClock(const FFrame& F) const;
	void PaintPins(const FFrame& F) const;
	void PaintLanes(const FFrame& F) const;
	void PaintLaneCard(const FFrame& F, int32 Lane, const FVector2D& Pos, const FVector2D& Size) const;
	void PaintTension(const FFrame& F, int32 Lane, const FVector2D& Bar, float BarW) const;
	void PaintCastMeter(const FFrame& F, int32 Lane, double Charge, const FVector2D& Bar, float BarW) const;
	void PaintLaneStatus(const FFrame& F, int32 Lane, const FVector2D& Pos, float Width) const;
	void PaintCallouts(const FFrame& F) const;
	void PaintCatchCards(const FFrame& F) const;
	void PaintCatchCard(const FFrame& F, int32 Lane, const FVector2D& Anchor) const;
	void PaintCountdown(const FFrame& F) const;
	void PaintControls(const FFrame& F) const;
	void PaintIntro(const FFrame& F) const;
	void PaintStandings(const FFrame& F) const;

	FLaneTexts& TextsOf(int32 Lane) const;

	TWeakObjectPtr<AFishingGame> Game;

	mutable FCachedText ClockText;
	mutable TArcadeCachedText<FRecordKey> RecordText;
	mutable FCachedText CountdownText;
	mutable FCachedText LastSecondsText;
	/** The intro's names, keyed by the seat count (a round's seats never change). */
	mutable FCachedText IntroNames;
	mutable TArray<FLaneTexts> LaneTexts;
	mutable FArcadeStandingsOrder Ranks;
};
