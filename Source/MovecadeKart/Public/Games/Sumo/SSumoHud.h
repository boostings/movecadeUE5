#pragma once

#include "CoreMinimal.h"
#include "Games/ArcadeHud.h"
#include "Widgets/SLeafWidget.h"

class ASumoGame;
class FKartPainter;
struct FArcadeHudView;

/**
 * Sumo Bumpers' HUD, painted with the Kart UI's painter over ASumoGame (which adds it to the viewport and removes it
 * in EndPlay): the round and its winners so far, the ring's next shrink; a panel per player (round wins, knockouts,
 * score, the dash meter, a power-up's time); name tags over the pucks; the knockout feed; the callouts (KNOCKOUT!,
 * SHIELD!, a round's winner); the 3-2-1; the local controls; the intro card and the final standings.
 *
 * Numbers are formatted only when they change (FCachedText) and the camera is projected once per paint.
 */
class SSumoHud : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSumoHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ASumoGame>, Game)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** One player panel's numbers. */
	struct FPanelText
	{
		FCachedText Knockouts;
		FCachedText Score;
	};

	/** One row of the final standings. */
	struct FStandingText
	{
		FCachedText Place;
		FCachedText Score;
		/** Round wins, knockouts and falls packed into one key ("2 round wins · 3 KO · 1 fall"). */
		TArcadeCachedText<int64> Record;
	};

	void PaintTop(const FKartPainter& P, const ASumoGame& Game, float S) const;
	void PaintShrink(const FKartPainter& P, const ASumoGame& Game, const FVector2D& Centre, float S) const;
	void PaintPanels(const FKartPainter& P, const ASumoGame& Game, float S) const;
	void PaintPanel(const FKartPainter& P, const ASumoGame& Game, int32 Slot, const FVector2D& Pos, const FVector2D& Size, float S) const;
	void PaintTags(const FKartPainter& P, const ASumoGame& Game, const FArcadeHudView& View, float S) const;
	void PaintFeed(const FKartPainter& P, const ASumoGame& Game, float S) const;
	void PaintCallouts(const FKartPainter& P, const ASumoGame& Game, const FArcadeHudView& View, float S) const;
	void PaintCountdown(const FKartPainter& P, const ASumoGame& Game, float S) const;
	void PaintControls(const FKartPainter& P, const ASumoGame& Game, float S) const;
	void PaintIntro(const FKartPainter& P, const ASumoGame& Game, float S) const;
	void PaintStandings(const FKartPainter& P, const ASumoGame& Game, float S) const;

	TWeakObjectPtr<ASumoGame> Game;

	mutable FCachedText RoundText;
	mutable FCachedText ShrinkWarningText;
	mutable FCachedText ShrinkInText;
	mutable FCachedText CountdownText;
	mutable TArray<FPanelText> PanelText;
	mutable TArray<FStandingText> StandingText;
	mutable FArcadeStandingsOrder Ranks;
};
