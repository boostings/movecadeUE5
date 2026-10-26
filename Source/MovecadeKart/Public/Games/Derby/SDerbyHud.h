#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"
#include "UI/KartSafeArea.h"
#include "Widgets/SLeafWidget.h"

class ADerbyGame;
class FKartPainter;
struct FDerbySeat;

/**
 * Home Run Derby's HUD, painted with the Kart UI's painter in a broadcast style: the score bug (batter, points,
 * home runs, the pitch count as ten balls and the strikes), the standings down the side, the pitch's type and speed,
 * the swing's timing (EARLY / PERFECT / LATE), the live distance as a hit carries and the call ("412 FT!",
 * HOME RUN, MOONSHOT, FOUL, STRIKE TWO), the "now batting" card, the local batter's aim and load, a phone's prompt,
 * the intro card and the final standings.
 *
 * ADerbyGame adds it to the game viewport in Setup and removes it in EndPlay. It reads the game every paint and keeps
 * only text caches of its own: labels are formatted when their numbers change, never per frame.
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
	/** A call's identity: the outcome (by when it came) and the count it left. */
	struct FCallKey
	{
		double OutcomeAt = 0.0;
		int32 Strikes = -1;
		bool operator!=(const FCallKey& Other) const { return OutcomeAt != Other.OutcomeAt || Strikes != Other.Strikes; }
	};

	/** One batter's labels: the name as shown (and upper-cased), and their numbers. */
	struct FBatterText
	{
		FString Name;
		FString NameUpper;
		FCachedText HomeRuns;
		FCachedText Score;
		/** The final standings' line: home runs, moonshots, the longest. */
		TArcadeCachedText<FIntVector> Summary;
	};

	void PaintBug(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintBoard(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintPitch(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintCall(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintTiming(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintTracker(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintLanding(const FKartPainter& P, const ADerbyGame& Game, float S, const FLinearColor& Ink) const;
	void PaintControls(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintPhonePrompt(const FKartPainter& P, const ADerbyGame& Game, const FDerbySeat& Up, const FVector2D& Base, float S) const;
	void PaintAim(const FKartPainter& P, const ADerbyGame& Game, const FVector2D& Base, float S) const;
	void PaintStepIn(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ADerbyGame& Game, float S) const;
	void PaintFinal(const FKartPainter& P, const ADerbyGame& Game, float S) const;

	/** The labels of every batter (and the places), built on the first paint: the seats never change during a derby. */
	FBatterText& BatterText(const ADerbyGame& Game, int32 BatterIndex) const;

	TWeakObjectPtr<ADerbyGame> Game;
	/** This paint's safe area and scale (a landscape phone moves the player panels to the top, clear of the touch controls). */
	mutable KartSafeArea::FKartSafeFrame Safe;

	mutable TArray<FBatterText> Batters;
	/** "1", "2", ...: the final standings' places. */
	mutable TArray<FString> PlaceLabels;
	/** "Dante · Rosa · ...". */
	mutable FString IntroNames;
	mutable TArcadeCachedText<FIntVector> BugLine;
	mutable FCachedText PitchType;
	mutable FCachedText PitchSpeed;
	mutable FCachedText TimingWord;
	mutable FCachedText LiveDistance;
	mutable TArcadeCachedText<FCallKey> CallTitle;
	mutable TArcadeCachedText<FCallKey> CallSubtitle;
	mutable FCachedText CallPoints;
	mutable FCachedText LandingDistance;
	mutable FCachedText StepInLine;
	mutable TArcadeCachedText<FIntPoint> StepInRound;
	mutable FArcadeStandingsOrder Ranks;
};
