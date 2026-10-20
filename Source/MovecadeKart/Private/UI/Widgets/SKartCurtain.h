#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/**
 * The level-travel wipe: four slanted colour bands sweep in from the right and cover the screen (closing),
 * then carry on off to the left (opening). OnClosed fires once the screen is fully covered.
 */
class SKartCurtain : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SKartCurtain) {}
		SLATE_ARGUMENT(bool, Closing)
	SLATE_END_ARGS()

	static constexpr float CloseSeconds = 0.5f;
	static constexpr float OpenSeconds = 0.6f;

	void Construct(const FArguments& InArgs);
	void Close(TFunction<void()> InOnClosed);
	void Open();
	bool IsFinishedOpening() const;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(100.0, 100.0); }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** 0: open (nothing covered), 1: closed, 2: opened past the left edge. */
	float Coverage() const;

	bool bClosing = true;
	double StartTime = 0.0;
	double Elapsed = 0.0;
	TFunction<void()> OnClosed;
};
