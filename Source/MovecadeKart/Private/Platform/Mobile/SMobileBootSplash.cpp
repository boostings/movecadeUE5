#include "Platform/Mobile/SMobileBootSplash.h"

#include "Styling/CoreStyle.h"
#include "UI/KartUIStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobileSplash"

namespace
{
	constexpr int32 TitleSize = 72;
	constexpr int32 SubtitleSize = 34;
	constexpr int32 LoadingSize = 20;
	constexpr float SpinnerRadius = 22.f;
	constexpr float SpinnerGap = 48.f;
}

void SMobileBootSplash::Construct(const FArguments& InArgs)
{
	// Colours are copied by value here, on the game thread; nothing below reads them again.
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(KartUI::Color::Night)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", TitleSize))
				.ColorAndOpacity(KartUI::Color::Sun)
				.Text(LOCTEXT("Title", "MOVECADE"))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", SubtitleSize))
				.ColorAndOpacity(KartUI::Color::White)
				.Text(LOCTEXT("Subtitle", "KART"))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, SpinnerGap, 0.f, 12.f)
			[
				SNew(SCircularThrobber).Radius(SpinnerRadius)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", LoadingSize))
				.ColorAndOpacity(KartUI::Color::Paper)
				.Text(LOCTEXT("Loading", "Loading"))
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
