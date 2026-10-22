#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Games/Samurai/SamuraiGame.h"

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

	/** A number's text, rebuilt only when the number changes. */
	struct FCachedNumber
	{
		int32 Value = TNumericLimits<int32>::Min();
		FString Text;
		const FString& Get(int32 V, const TCHAR* Format = nullptr);
	};

	struct FLaneTexts
	{
		FCachedNumber Score;
		FCachedNumber Combo;
	};

	TWeakObjectPtr<ASamuraiGame> Game;
	/** The intro's roll call, built once. */
	FString Names;
	mutable FCachedNumber SecondsText;
	mutable FCachedNumber Wave;
	mutable TArray<FLaneTexts> LaneTexts;
	/** The final order, sorted once when the round ends (keyed by that stage's start). */
	mutable TArray<int32> Order;
	mutable double OrderFor = -1.0;
	/** This paint's projection. */
	mutable FSamuraiView View;
	mutable TArray<FVector2D> Scratch;
};
