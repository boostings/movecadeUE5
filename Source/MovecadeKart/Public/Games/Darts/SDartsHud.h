#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"
#include "UI/KartSafeArea.h"
#include "Widgets/SLeafWidget.h"

class ADartsGame;
class FKartPainter;
struct FDartsVisitSummary;
namespace MovecadeKart::Darts { struct FDartsEffect; struct FDartsPlayer; }

/**
 * Bullseye Bash's HUD (DartsCanvas.tsx and the scene's callouts, restyled in the Kart UI's painter): the
 * scoreboard with each thrower's visits, who is up with the round, the called shot and the darts left; the
 * engine's 3-2-1; the score callouts rising off the board where each dart lands; the visit card with its
 * checkout-style call; the local thrower's controls with the power meter (its sweet spot marked) and a gauge by
 * the reticle; the first-visit coach marks; the big hit's flash; the intro card and the final standings.
 *
 * ADartsGame creates it over the viewport and removes it in its EndPlay; it only reads the game. Text is formatted
 * and measured only when what it shows changes.
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
	void PaintBoardRow(const FKartPainter& P, const ADartsGame& Game, int32 Row, const FVector2D& Pos, float S) const;
	void PaintUp(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintCountdown(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintCallouts(const FKartPainter& P, const ADartsGame& Game, const FArcadeHudView& View, float S) const;
	void PaintFinishBanner(const FKartPainter& P, const ADartsGame& Game, const FArcadeHudView& View, int32 Slot,
		const MovecadeKart::Darts::FDartsEffect& Effect, float S) const;
	void PaintVisit(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintVisitChips(const FKartPainter& Card, const FDartsVisitSummary& Visit, const FVector2D& Pos, const FLinearColor& Lane, double Age, float S) const;
	void PaintControls(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintGauge(const FKartPainter& P, const ADartsGame& Game, const FArcadeHudView& View, float S) const;
	void PaintCoach(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ADartsGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ADartsGame& Game, float S) const;
	/** A thrower's name fitted to the scoreboard (measured once per name and scale). */
	const FString& FittedName(int32 Row, const FString& Name, float S, float& OutSize) const;
	/** A visit card's text, formatted and measured once per visit and scale. */
	void RefreshVisitText(const FDartsVisitSummary& Visit, float S) const;

	TWeakObjectPtr<ADartsGame> Game;
	/** This paint's safe area and scale (portrait stacks the panels top to bottom). */
	mutable KartSafeArea::FKartSafeFrame Safe;

	struct FFitted
	{
		FString Source;
		FString Shown;
		float Scale = -1.f;
		float Size = 0.f;
	};
	mutable TArray<FFitted> Fitted;

	/** One per scoreboard row: the score, and each of the nine darts' points. */
	struct FRowText
	{
		FCachedText Score;
		FCachedText Darts[9];
	};
	mutable TArray<FRowText> RowText;

	/** One per live effect slot (keyed by the effect's id): its callout label and, for the finish, the banner width. */
	struct FCalloutText
	{
		int32 EffectId = INDEX_NONE;
		FString Label;
		float Width = 0.f;
		float Scale = -1.f;
	};
	mutable TArray<FCalloutText> Callouts;

	/** The visit card (keyed by when it was shown, and the scale it was measured at). */
	struct FVisitText
	{
		double At = -1.0;
		float Scale = -1.f;
		FString Call;
		float CallWidth = 0.f;
		FString Total;
		FText Footer;
		TArray<FString, TInlineAllocator<3>> Points;
		TArray<float, TInlineAllocator<3>> LabelSizes;
	};
	mutable FVisitText VisitText;

	mutable TArcadeCachedText<int32> RoundText;
	mutable FCachedText BountyText;
	mutable FCachedText CountdownText;
	mutable FCachedText UpText;
	mutable FCachedText IntroNames;
	mutable FArcadeStandingsOrder Ranks;
	/** One per standings row: place, score and the stats line. */
	struct FStandingText
	{
		FCachedText Place;
		FCachedText Score;
		TArcadeCachedText<int64> Stats;
	};
	mutable TArray<FStandingText> StandingText;

	/** Scratch for the painter's polygons (no allocation per paint). */
	mutable TArray<FVector2D> Poly;
};
