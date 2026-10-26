#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class UMovecadeOrientationSubsystem;

/**
 * -MobilePreview's overlay (never in Shipping): shades the parts of the screen outside the simulated safe area and
 * draws the camera housing, so desktop screenshots show what a phone would cover. Hit-test invisible.
 * UMovecadeOrientationSubsystem adds it to the game viewport on each level.
 */
class SMovecadeSafeAreaMask : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMovecadeSafeAreaMask) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const UMovecadeOrientationSubsystem* InScreen);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<const UMovecadeOrientationSubsystem> Screen;
};
