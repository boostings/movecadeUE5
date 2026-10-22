#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UHorizontalBoxSlot;
class UTextBlock;
class UVerticalBox;
class UVerticalBoxSlot;
class UWidget;
class UWidgetTree;

/**
 * The lobby's widget construction kit (private to KartLobbyWidget): the few UMG shapes the lobby is made of (text,
 * rounded cards, card buttons, fixed-size boxes) and the slot rules it lays them out with, so the layout code reads
 * as the layout. Everything is constructed in the widget's own UWidgetTree.
 */
namespace MovecadeKart::Lobby
{
	/** The lobby's measurements (1920x1080 canvas units), in one place. */
	namespace LobbyMetric
	{
		/** Padding of the whole layout inside the screen (left, top, right, bottom: clear of the hint bar). */
		inline const FMargin SafeArea(96.f, 54.f, 96.f, 120.f);
		constexpr float ColumnGap = 56.f;

		constexpr float JoinCardWidth = 500.f;
		constexpr float JoinCardRadius = 30.f;
		inline const FMargin JoinCardPadding(40.f, 32.f);
		constexpr float QrSize = 360.f;
		constexpr float QrFrameRadius = 22.f;
		constexpr float QrFramePadding = 14.f;
		constexpr float QrFrameOutline = 4.f;
		constexpr float StatusChipRadius = 22.f;
		inline const FMargin StatusChipPadding(16.f, 10.f);

		constexpr float PickerCardWidth = 760.f;
		constexpr float PickerCardRadius = 24.f;
		inline const FMargin PickerCardPadding(18.f, 14.f);
		constexpr float AccentBarWidth = 18.f;
		constexpr float AccentBarHeight = 76.f;
		constexpr float AccentBarRadius = 9.f;
		constexpr float ArrowRadius = 30.f;
		inline const FMargin ArrowPadding(20.f, 8.f);
		constexpr float TrackGap = 20.f;

		constexpr float SeatRadius = 26.f;
		inline const FMargin SeatPadding(16.f, 12.f, 24.f, 12.f);
		constexpr float SwatchSize = 56.f;
		constexpr float SwatchRadius = SwatchSize / 2.f;
		constexpr float SwatchOutline = 3.f;
		constexpr float SeatGap = 20.f;

		constexpr float StartRadius = 40.f;
		inline const FMargin StartPadding(40.f, 12.f);

		/** A card button's hover and press ring. */
		constexpr float FocusRingWidth = 4.f;
		/** A selected track card grows by this. */
		constexpr double SelectedScale = 1.03;
	}

	/** Builds lobby widgets into one UWidgetTree. */
	class FWidgetFactory
	{
	public:
		explicit FWidgetFactory(UWidgetTree& InTree) : Tree(InTree) {}

		UTextBlock* Text(const FString& Value, const FSlateFontInfo& Font, const FLinearColor& Color) const;
		/** Text that wraps at its box's width, centred. */
		UTextBlock* WrappedText(const FString& Value, const FSlateFontInfo& Font, const FLinearColor& Color) const;
		/** A rounded card filled with Color around Content (none: an empty bar or swatch). */
		UBorder* Card(const FLinearColor& Color, float Radius, const FMargin& Padding, UWidget* Content = nullptr) const;
		/** A rounded card with an outline. */
		UBorder* OutlinedCard(const FLinearColor& Color, float Radius, const FLinearColor& Outline, float OutlineWidth, const FMargin& Padding,
			UWidget* Content = nullptr) const;
		/** Content in a box of fixed Width and/or Height (<= 0 leaves that axis to the content). */
		UWidget* Sized(UWidget* Content, float Width, float Height) const;
		/**
		 * A rounded button, tinted Background, that rings in the sun colour on hover and press. Padding (when set) and
		 * bFillWidth go on the content's slot; unset keeps the button's default slot.
		 */
		UButton* CardButton(float Radius, const FLinearColor& Background, UWidget* Content, TOptional<FMargin> Padding = {}, bool bFillWidth = false) const;
		UVerticalBox* Column() const;
		UHorizontalBox* Row() const;

		/** Adds Child under the column with Bottom spacing; bFill takes the remaining height. */
		static UVerticalBoxSlot* Stack(UVerticalBox* Column, UWidget* Child, float Bottom, bool bFill = false);
		/** As Stack, centred horizontally. */
		static UVerticalBoxSlot* StackCentred(UVerticalBox* Column, UWidget* Child, float Bottom);
		/** Adds Child to the row (vertically centred) with Right spacing; bFill takes the remaining width. */
		static UHorizontalBoxSlot* Line(UHorizontalBox* Row, UWidget* Child, float Right, bool bFill = false);

	private:
		UWidgetTree& Tree;
	};
}
