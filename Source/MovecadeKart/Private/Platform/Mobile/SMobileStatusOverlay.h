#pragma once

#include "CoreMinimal.h"
#include "Platform/Mobile/MobileLifecycle.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class SProgressBar;
class STextBlock;

/**
 * The phone app's status layer, over every screen (viewport content, top Z order), never taking input:
 *   - the connection banner at the top (Party Mode: no network, reconnecting, back online, room lost);
 *   - the first launch's shader warm-up card at the bottom ("Getting the game ready", a progress bar), clear of
 *     the menus, which keep working underneath.
 * UMobileLifecycleSubsystem owns it and pushes changes (SetBanner / SetWarmup); the texts change only then.
 */
class SMobileStatusOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMobileStatusOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetBanner(MovecadeKart::Mobile::EConnectionBanner Banner);
	/** bShow with Progress 0..1. */
	void SetWarmup(bool bShow, float Progress);

private:
	MovecadeKart::Mobile::EConnectionBanner Shown = MovecadeKart::Mobile::EConnectionBanner::None;
	int32 ShownPercent = -1;

	/** The widgets point at these: they live as long as the overlay. */
	FSlateBrush PanelBrush;
	FSlateBrush DotBrush;
	FProgressBarStyle BarStyle;

	TSharedPtr<SBorder> BannerPanel;
	TSharedPtr<SBorder> BannerDot;
	TSharedPtr<STextBlock> BannerTitle;
	TSharedPtr<STextBlock> BannerDetail;
	TSharedPtr<SBorder> WarmupPanel;
	TSharedPtr<STextBlock> WarmupPercent;
	TSharedPtr<SProgressBar> WarmupBar;
};
