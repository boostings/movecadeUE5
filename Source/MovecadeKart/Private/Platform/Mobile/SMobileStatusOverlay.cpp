#include "Platform/Mobile/SMobileStatusOverlay.h"

#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "UI/KartSafeArea.h"
#include "UI/KartUIStyle.h"
#include "UI/KartWidgets.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobileStatus"

using MovecadeKart::Mobile::EConnectionBanner;

namespace
{
	// Layout, in Slate units of the 1920 x 1080 design canvas (phones scale it with the DPI curve).
	constexpr float BannerTopMargin = 28.f;
	constexpr float BannerPadH = 26.f;
	constexpr float BannerPadV = 14.f;
	constexpr float DotSize = 14.f;
	constexpr float BannerTitleSize = 24.f;
	constexpr float BannerDetailSize = 18.f;
	/** The least room under the warm-up card; it also stays this far (menu canvas units) above the hint bar. */
	constexpr float WarmupBottomMargin = 56.f;
	constexpr float WarmupHintGap = 18.f;
	constexpr float WarmupWidth = 620.f;
	constexpr float WarmupPad = 32.f;
	constexpr float WarmupTitleSize = 30.f;
	constexpr float WarmupBodySize = 19.f;
	constexpr float WarmupBarHeight = 14.f;
	constexpr float PanelAlpha = 0.92f;
	constexpr float PanelRadius = 22.f;
	constexpr float PanelOutline = 3.f;

	FLinearColor AccentOf(EConnectionBanner Banner)
	{
		switch (Banner)
		{
		case EConnectionBanner::NoNetwork:
		case EConnectionBanner::Lost: return KartUI::Color::Hot;
		case EConnectionBanner::Reconnecting: return KartUI::Color::Sun;
		case EConnectionBanner::Restored: return KartUI::Color::Mint;
		case EConnectionBanner::None: break;
		}
		return KartUI::Color::White;
	}
}

void SMobileStatusOverlay::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	// The fonts are fixed here: on a phone they get the menus' bigger reading sizes (KartSafeArea::FPhoneMenuTextScope).
	const KartSafeArea::FPhoneMenuTextScope PhoneText(KartSafeArea::FKartDisplay::Get(FVector2D(KartUI::Metric::CanvasW, KartUI::Metric::CanvasH)).bPhone);
	// Rounded panels with the menus' light outline; colours are tints, so one white brush serves every accent.
	PanelBrush = FSlateRoundedBoxBrush(FLinearColor::White, PanelRadius, KartUI::Color::WithAlpha(KartUI::Color::Sky, 0.9f), PanelOutline);
	DotBrush = FSlateRoundedBoxBrush(FLinearColor::White, DotSize / 2.f);
	BarStyle = FProgressBarStyle()
		.SetBackgroundImage(FSlateRoundedBoxBrush(KartUI::Color::NavyDim, WarmupBarHeight / 2.f))
		.SetFillImage(FSlateRoundedBoxBrush(KartUI::Color::Sun, WarmupBarHeight / 2.f))
		.SetMarqueeImage(FSlateRoundedBoxBrush(KartUI::Color::Sun, WarmupBarHeight / 2.f));
	const FLinearColor Panel = KartUI::Color::WithAlpha(KartUI::Color::Night, PanelAlpha);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(TAttribute<FMargin>::CreateSP(this, &SMobileStatusOverlay::BannerPadding))
		[
			SAssignNew(BannerPanel, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(&PanelBrush)
			.BorderBackgroundColor(Panel)
			.Padding(FMargin(BannerPadH, BannerPadV))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, BannerPadV, 0.f)
				[
					SNew(SBox).WidthOverride(DotSize).HeightOverride(DotSize)
					[
						SAssignNew(BannerDot, SBorder).BorderImage(&DotBrush)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(BannerTitle, STextBlock)
						.Font(KartUI::Font(KartUI::EFont::Title, BannerTitleSize))
						.WrapTextAt(TAttribute<float>::CreateSP(this, &SMobileStatusOverlay::BannerTextWidth))
						.ColorAndOpacity(KartUI::Color::White)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(BannerDetail, STextBlock)
						.Font(KartUI::Font(KartUI::EFont::Body, BannerDetailSize))
						.WrapTextAt(TAttribute<float>::CreateSP(this, &SMobileStatusOverlay::BannerTextWidth))
						.ColorAndOpacity(KartUI::Color::Paper)
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(TAttribute<FMargin>::CreateSP(this, &SMobileStatusOverlay::WarmupPadding))
		[
			SAssignNew(WarmupPanel, SBorder)
			.Visibility(EVisibility::Collapsed)
			.BorderImage(&PanelBrush)
			.BorderBackgroundColor(Panel)
			.Padding(WarmupPad)
			[
				SNew(SBox).WidthOverride(TAttribute<FOptionalSize>::CreateSP(this, &SMobileStatusOverlay::WarmupCardWidth))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Font(KartUI::Font(KartUI::EFont::Display, WarmupTitleSize))
						.ColorAndOpacity(KartUI::Color::Sun)
						.Text(LOCTEXT("WarmupTitle", "Getting the game ready"))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f)
					[
						SNew(STextBlock)
						.Font(KartUI::Font(KartUI::EFont::Body, WarmupBodySize))
						.ColorAndOpacity(KartUI::Color::Paper)
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
						.Text(LOCTEXT("WarmupBody", "Preparing graphics for this phone. This only happens after an install or an update."))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 6.f)
					[
						SNew(SBox).HeightOverride(WarmupBarHeight)
						[
							SAssignNew(WarmupBar, SProgressBar)
							.Style(&BarStyle)
							.Percent(0.f)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SAssignNew(WarmupPercent, STextBlock)
						.Font(KartUI::Font(KartUI::EFont::Label, WarmupBodySize))
						.ColorAndOpacity(KartUI::Color::White)
					]
				]
			]
		]
	];
}

void SMobileStatusOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// The same safe frame the HUDs and menus use: the notch / island at the top, the home bar at the bottom.
	const KartSafeArea::FKartSafeFrame Safe = KartSafeArea::FKartSafeFrame::ForHud(AllottedGeometry);
	const FVector2D Size = Safe.Size;
	// The menus lay out on a canvas at least 1080 units on its short side, scaled down to fit (SKartScreenStack), so
	// their hint bar rises HintBarRise() canvas units, times that fit, above the safe bottom.
	const double Fit = FMath::Min(1.0, FMath::Min(Size.X, Size.Y) / KartUI::Metric::CanvasH);
	const double HintTop = Safe.SafeMax.Y - KartWidgets::HintBarRise() * Fit;
	BannerTop = Safe.bPhone ? static_cast<float>(FMath::Max<double>(BannerTopMargin, Safe.SafeMin.Y)) : BannerTopMargin;
	WarmupBottom = static_cast<float>(FMath::Max<double>(WarmupBottomMargin, Size.Y - HintTop + WarmupHintGap * Fit));
	SideRoom = static_cast<float>(FMath::Max(1.0, Safe.SafeSize().X));
}

FMargin SMobileStatusOverlay::BannerPadding() const
{
	return FMargin(0.f, BannerTop, 0.f, 0.f);
}

FMargin SMobileStatusOverlay::WarmupPadding() const
{
	return FMargin(0.f, 0.f, 0.f, WarmupBottom);
}

FOptionalSize SMobileStatusOverlay::WarmupCardWidth() const
{
	// Narrower than the design width on a phone held upright, inside the safe area with its padding.
	return FMath::Min(WarmupWidth, SideRoom - 2.f * WarmupPad);
}

float SMobileStatusOverlay::BannerTextWidth() const
{
	// The banner's text wraps before the panel would leave the safe area (dot and padding beside it).
	return FMath::Max(1.f, SideRoom - 2.f * BannerPadH - DotSize - BannerPadV);
}

void SMobileStatusOverlay::SetBanner(EConnectionBanner Banner)
{
	if (Banner == Shown)
	{
		return;
	}
	Shown = Banner;
	if (Banner == EConnectionBanner::None)
	{
		BannerPanel->SetVisibility(EVisibility::Collapsed);
		return;
	}
	FText Title;
	FText Detail;
	switch (Banner)
	{
	case EConnectionBanner::NoNetwork:
		Title = LOCTEXT("NoNetwork", "No connection");
		Detail = LOCTEXT("NoNetworkDetail", "Waiting for Wi-Fi or mobile data. The room stays open.");
		break;
	case EConnectionBanner::Reconnecting:
		Title = LOCTEXT("Reconnecting", "Reconnecting...");
		Detail = LOCTEXT("ReconnectingDetail", "Phones can stay in the room.");
		break;
	case EConnectionBanner::Restored:
		Title = LOCTEXT("Restored", "Back online");
		Detail = LOCTEXT("RestoredDetail", "The room is connected again.");
		break;
	case EConnectionBanner::Lost:
		Title = LOCTEXT("Lost", "Room connection lost");
		Detail = LOCTEXT("LostDetail", "Open Party Mode to start a new room.");
		break;
	case EConnectionBanner::None:
		break;
	}
	BannerTitle->SetText(Title);
	BannerDetail->SetText(Detail);
	BannerDot->SetBorderBackgroundColor(AccentOf(Banner));
	BannerPanel->SetVisibility(EVisibility::HitTestInvisible);
}

void SMobileStatusOverlay::SetWarmup(bool bShow, float Progress)
{
	WarmupPanel->SetVisibility(bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (!bShow)
	{
		return;
	}
	const int32 Percent = FMath::Clamp(FMath::FloorToInt32(Progress * 100.f), 0, 100);
	if (Percent != ShownPercent)
	{
		ShownPercent = Percent;
		WarmupBar->SetPercent(Progress);
		WarmupPercent->SetText(FText::Format(LOCTEXT("WarmupPercent", "{0}%"), FText::AsNumber(Percent)));
	}
}

#undef LOCTEXT_NAMESPACE
