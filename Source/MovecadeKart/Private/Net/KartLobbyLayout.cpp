// UKartLobbyWidget's widget tree, built in code (KartLobbyLayout.h has the kit and the measurements). The look follows
// the UI style (UI/KartUIStyle.h): white cards on the track backdrop, the game's type and palette, the shared glyphs.

#include "Net/KartLobbyLayout.h"
#include "Net/KartLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScaleBox.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Net/RoomHostSubsystem.h"
#include "UI/KartPaintWidget.h"
#include "UI/KartSafeArea.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartUIStyle.h"
#include "UI/KartWidgets.h"

using namespace KartUI;
using namespace MovecadeKart::Lobby;

namespace
{
	/** The lobby shows the first two tracks of the catalog, side by side. */
	constexpr int32 TrackCount = 2;
	constexpr int32 SeatCount = 4;
	constexpr int32 SeatsPerRow = 2;

	FSlateRoundedBoxBrush Rounded(float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
	{
		// White fill: UBorder's brush colour and UButton's background colour tint it.
		return FSlateRoundedBoxBrush(FLinearColor::White, Radius, Outline, OutlineWidth);
	}

	FButtonStyle CardButtonStyle(float Radius)
	{
		FButtonStyle Style;
		Style.SetNormal(Rounded(Radius));
		Style.SetHovered(Rounded(Radius, Color::Sun, LobbyMetric::FocusRingWidth));
		Style.SetPressed(Rounded(Radius, Color::SunDeep, LobbyMetric::FocusRingWidth));
		Style.SetDisabled(Rounded(Radius));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}

	/** The painted hint bar's actions: Kart adds the track switch. Built once; the backdrop paints every frame. */
	const TArray<FKartHint>& LobbyHints(bool bKart)
	{
		static const TArray<FKartHint> KartHints = {
			{ EKartMenuAction::Up, NSLOCTEXT("KartLobby", "Game", "Game") },
			{ EKartMenuAction::Left, NSLOCTEXT("KartLobby", "Track", "Track") },
			{ EKartMenuAction::Accept, NSLOCTEXT("KartLobby", "Start", "Start") },
			{ EKartMenuAction::Back, NSLOCTEXT("KartLobby", "Leave", "Leave") } };
		static const TArray<FKartHint> GameHints = {
			{ EKartMenuAction::Up, NSLOCTEXT("KartLobby", "Game", "Game") },
			{ EKartMenuAction::Accept, NSLOCTEXT("KartLobby", "Start", "Start") },
			{ EKartMenuAction::Back, NSLOCTEXT("KartLobby", "Leave", "Leave") } };
		return bKart ? KartHints : GameHints;
	}

	/** A section heading over the right column ("GAME", "TRACK", "PLAYERS"). */
	UTextBlock* Heading(const FWidgetFactory& Make, const TCHAR* Label)
	{
		return Make.Text(Label, Font(EFont::Label, 22.f, 2.f), Color::Sun);
	}
}

// ───────────────────────────── the kit ─────────────────────────────

namespace MovecadeKart::Lobby
{
	UTextBlock* FWidgetFactory::Text(const FString& Value, const FSlateFontInfo& InFont, const FLinearColor& Color) const
	{
		UTextBlock* Block = Tree.ConstructWidget<UTextBlock>();
		Block->SetText(FText::FromString(Value));
		Block->SetFont(InFont);
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	}

	UTextBlock* FWidgetFactory::WrappedText(const FString& Value, const FSlateFontInfo& InFont, const FLinearColor& Color) const
	{
		UTextBlock* Block = Text(Value, InFont, Color);
		Block->SetAutoWrapText(true);
		return Block;
	}

	UBorder* FWidgetFactory::Card(const FLinearColor& Color, float Radius, const FMargin& Padding, UWidget* Content) const
	{
		UBorder* Border = Tree.ConstructWidget<UBorder>();
		Border->SetBrush(Rounded(Radius));
		Border->SetBrushColor(Color);
		Border->SetPadding(Padding);
		if (Content)
		{
			Border->SetContent(Content);
		}
		return Border;
	}

	UBorder* FWidgetFactory::OutlinedCard(const FLinearColor& Color, float Radius, const FLinearColor& Outline, float OutlineWidth,
		const FMargin& Padding, UWidget* Content) const
	{
		UBorder* Border = Card(Color, Radius, Padding, Content);
		Border->SetBrush(Rounded(Radius, Outline, OutlineWidth));
		return Border;
	}

	UWidget* FWidgetFactory::Sized(UWidget* Content, float Width, float Height) const
	{
		USizeBox* Size = Tree.ConstructWidget<USizeBox>();
		if (Width > 0.f)
		{
			Size->SetWidthOverride(Width);
		}
		if (Height > 0.f)
		{
			Size->SetHeightOverride(Height);
		}
		Size->SetContent(Content);
		return Size;
	}

	UButton* FWidgetFactory::CardButton(float Radius, const FLinearColor& Background, UWidget* Content, TOptional<FMargin> Padding, bool bFillWidth) const
	{
		UButton* Button = Tree.ConstructWidget<UButton>();
		Button->SetStyle(CardButtonStyle(Radius));
		UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->SetContent(Content));
		if (ContentSlot && bFillWidth)
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		if (ContentSlot && Padding.IsSet())
		{
			ContentSlot->SetPadding(Padding.GetValue());
		}
		Button->SetBackgroundColor(Background);
		return Button;
	}

	UVerticalBox* FWidgetFactory::Column() const
	{
		return Tree.ConstructWidget<UVerticalBox>();
	}

	UHorizontalBox* FWidgetFactory::Row() const
	{
		return Tree.ConstructWidget<UHorizontalBox>();
	}

	UVerticalBoxSlot* FWidgetFactory::Stack(UVerticalBox* Column, UWidget* Child, float Bottom, bool bFill)
	{
		UVerticalBoxSlot* Slot = Column->AddChildToVerticalBox(Child);
		Slot->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
		if (bFill)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		return Slot;
	}

	UVerticalBoxSlot* FWidgetFactory::StackCentred(UVerticalBox* Column, UWidget* Child, float Bottom)
	{
		UVerticalBoxSlot* Slot = Stack(Column, Child, Bottom);
		Slot->SetHorizontalAlignment(HAlign_Center);
		return Slot;
	}

	UHorizontalBoxSlot* FWidgetFactory::Line(UHorizontalBox* Row, UWidget* Child, float Right, bool bFill)
	{
		UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Child);
		Slot->SetPadding(FMargin(0.f, 0.f, Right, 0.f));
		Slot->SetVerticalAlignment(VAlign_Center);
		if (bFill)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		return Slot;
	}
}

// ───────────────────────────── the lobby ─────────────────────────────

void UKartLobbyWidget::BuildTree()
{
	// The texts' fonts are fixed here, so a phone's bigger menu text applies while the tree is built.
	const KartSafeArea::FPhoneMenuTextScope PhoneText(KartSafeArea::FKartDisplay::Get(FVector2D(KartUI::Metric::CanvasW, KartUI::Metric::CanvasH)).bPhone);
	const FWidgetFactory Make(*WidgetTree);
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;
	BuildBackdrop(Make, *Root);

	SafeBorder = Make.Card(FLinearColor::Transparent, 0.f, LobbyMetric::SafeArea);
	if (UOverlaySlot* SafeSlot = Root->AddChildToOverlay(SafeBorder))
	{
		SafeSlot->SetHorizontalAlignment(HAlign_Fill);
		SafeSlot->SetVerticalAlignment(VAlign_Fill);
	}
	// The whole layout shrinks to fit the safe area (a short window, a phone's notch and home bar), never grows.
	UScaleBox* Fit = WidgetTree->ConstructWidget<UScaleBox>();
	Fit->SetStretch(EStretch::ScaleToFit);
	Fit->SetStretchDirection(EStretchDirection::DownOnly);
	SafeBorder->SetContent(Fit);
	UHorizontalBox* Columns = Make.Row();
	Fit->SetContent(Columns);

	// Left: how to join. Right: the title, the game, the track, the seats and Start.
	FWidgetFactory::Line(Columns, Make.Sized(BuildJoinCard(Make), LobbyMetric::JoinCardWidth, 0.f), LobbyMetric::ColumnGap);
	UVerticalBox* Right = Make.Column();
	FWidgetFactory::Stack(Right, Make.Text(TEXT("Party Mode"), Font(EFont::Display, 84.f, 5.f), Color::White), 0.f);
	SubtitleText = Make.Text(TEXT("Phones are the controllers: scan the code, tilt to steer."), Font(EFont::Body, 26.f, 2.f), Color::White);
	FWidgetFactory::Stack(Right, SubtitleText, 24.f);
	FWidgetFactory::Stack(Right, Heading(Make, TEXT("GAME")), 10.f);
	FWidgetFactory::Stack(Right, BuildGamePicker(Make), 26.f);
	FWidgetFactory::Stack(Right, BuildTrackPicker(Make), 0.f);
	AddSeats(Make, *Right);
	FWidgetFactory::Stack(Right, BuildFooter(Make), 0.f);
	FWidgetFactory::Line(Columns, Right, 0.f, /*bFill*/ true);
}

void UKartLobbyWidget::BuildBackdrop(const FWidgetFactory& Make, UOverlay& Root)
{
	// Painted under the cards: the dimming and the button hints.
	UKartPaintWidget* Backdrop = WidgetTree->ConstructWidget<UKartPaintWidget>();
	Backdrop->OnPaintKart = [WeakThis = TWeakObjectPtr<UKartLobbyWidget>(this)](const FKartPainter& P, const FKartScreenFrame& Frame)
	{
		const URoomHostSubsystem* Subsystem = WeakThis.IsValid() ? WeakThis->Room() : nullptr;
		const bool bKart = !Subsystem || Subsystem->GetGameId() == TEXT("kart");
		// Darker behind the join card, lighter towards the right where the track shows through.
		constexpr double Split = 0.62;
		P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X * Split, Frame.Size.Y), Color::WithAlpha(Color::Night, 0.82f), Color::WithAlpha(Color::Night, 0.45f), false);
		P.Gradient(FVector2D(Frame.Size.X * Split, 0.0), FVector2D(Frame.Size.X * (1.0 - Split), Frame.Size.Y), Color::WithAlpha(Color::Night, 0.45f), Color::WithAlpha(Color::Night, 0.3f), false);
		KartWidgets::Hints(P, Frame, LobbyHints(bKart));
	};
	if (UOverlaySlot* BackSlot = Root.AddChildToOverlay(Backdrop))
	{
		BackSlot->SetHorizontalAlignment(HAlign_Fill);
		BackSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

UWidget* UKartLobbyWidget::BuildJoinCard(const FWidgetFactory& Make)
{
	UVerticalBox* Column = Make.Column();
	FWidgetFactory::StackCentred(Column, Make.Text(TEXT("SCAN TO JOIN"), Font(EFont::Display, 46.f), Color::Navy), 18.f);

	QrImage = WidgetTree->ConstructWidget<UImage>();
	QrImage->SetColorAndOpacity(FLinearColor::White);
	UBorder* QrFrame = Make.OutlinedCard(FLinearColor::White, LobbyMetric::QrFrameRadius, Color::Navy, LobbyMetric::QrFrameOutline, FMargin(LobbyMetric::QrFramePadding), QrImage);
	FWidgetFactory::StackCentred(Column, Make.Sized(QrFrame, LobbyMetric::QrSize, LobbyMetric::QrSize), 22.f);

	FWidgetFactory::StackCentred(Column, Make.Text(TEXT("ROOM CODE"), Font(EFont::Label, 20.f), Color::NavyDim), 0.f);
	CodeText = Make.Text(TEXT("------"), Font(EFont::Display, 68.f), Color::Hot);
	FWidgetFactory::StackCentred(Column, CodeText, 4.f);
	UrlText = Make.Text(TEXT(""), Font(EFont::Body, 18.f), Color::NavyDim);
	// The link never runs past the card (a phone's bigger text): it shrinks to the card's inner width instead.
	UScaleBox* UrlFit = WidgetTree->ConstructWidget<UScaleBox>();
	UrlFit->SetStretch(EStretch::ScaleToFitX);
	UrlFit->SetStretchDirection(EStretchDirection::DownOnly);
	UrlFit->SetContent(UrlText);
	FWidgetFactory::StackCentred(Column, Make.Sized(UrlFit, LobbyMetric::JoinCardWidth - LobbyMetric::JoinCardPadding.GetTotalSpaceAlong<Orient_Horizontal>(), 0.f), 12.f);

	ConnectionText = Make.WrappedText(TEXT("Connecting to the room server..."), Font(EFont::Body, 18.f), Color::Navy);
	ConnectionText->SetJustification(ETextJustify::Center);
	UBorder* StatusChip = Make.Card(Color::PaperShade, LobbyMetric::StatusChipRadius, LobbyMetric::StatusChipPadding, Make.Sized(ConnectionText, LobbyMetric::QrSize, 0.f));
	FWidgetFactory::StackCentred(Column, StatusChip, 0.f);

	UBorder* Card = Make.Card(Color::Paper, LobbyMetric::JoinCardRadius, LobbyMetric::JoinCardPadding, Column);
	return Card;
}

UWidget* UKartLobbyWidget::BuildGamePicker(const FWidgetFactory& Make)
{
	// A carousel over Kart Racer and every party game in the catalog (arrows, Up/Down, or a click on the card).
	auto Arrow = [&Make](const TCHAR* Glyph)
	{
		UTextBlock* Label = Make.Text(Glyph, Font(EFont::Display, 34.f), Color::Navy);
		return Make.CardButton(LobbyMetric::ArrowRadius, Color::Paper, Label, LobbyMetric::ArrowPadding);
	};

	UHorizontalBox* GameCard = Make.Row();
	GameAccent = Make.Card(Color::Sun, LobbyMetric::AccentBarRadius, FMargin(0.f));
	FWidgetFactory::Line(GameCard, Make.Sized(GameAccent, LobbyMetric::AccentBarWidth, LobbyMetric::AccentBarHeight), 18.f);
	UVerticalBox* Words = Make.Column();
	GameName = Make.Text(TEXT("Kart Racer"), Font(EFont::Title, 34.f), Color::Navy);
	FWidgetFactory::Stack(Words, GameName, 0.f);
	GameTagline = Make.WrappedText(TEXT(""), Font(EFont::Body, 17.f), Color::NavyDim);
	FWidgetFactory::Stack(Words, GameTagline, 0.f);
	FWidgetFactory::Line(GameCard, Words, 12.f, /*bFill*/ true);
	GameCount = Make.Text(TEXT(""), Font(EFont::Label, 18.f), Color::NavyDim);
	FWidgetFactory::Line(GameCard, GameCount, 0.f);

	UButton* Previous = Arrow(TEXT("<"));
	UButton* CardButton = Make.CardButton(LobbyMetric::PickerCardRadius, Color::Sun, GameCard, LobbyMetric::PickerCardPadding, /*bFillWidth*/ true);
	UButton* Next = Arrow(TEXT(">"));
	Previous->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandlePreviousGameClicked);
	CardButton->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleNextGameClicked);
	Next->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleNextGameClicked);

	UHorizontalBox* Picker = Make.Row();
	FWidgetFactory::Line(Picker, Previous, 12.f);
	FWidgetFactory::Line(Picker, Make.Sized(CardButton, LobbyMetric::PickerCardWidth, 0.f), 12.f);
	FWidgetFactory::Line(Picker, Next, 0.f);
	return Picker;
}

UWidget* UKartLobbyWidget::BuildTrackPicker(const FWidgetFactory& Make)
{
	TrackSection = Make.Column();
	FWidgetFactory::Stack(TrackSection, Heading(Make, TEXT("TRACK")), 10.f);
	UHorizontalBox* Tracks = Make.Row();
	const TArray<FKartTrackInfo>& Catalog = FKartTrackCatalog::All();
	for (int32 i = 0; i < Catalog.Num() && i < TrackCount; ++i)
	{
		UHorizontalBox* TrackCard = Make.Row();
		UBorder* Bar = Make.Card(Catalog[i].Accent, LobbyMetric::AccentBarRadius, FMargin(0.f));
		FWidgetFactory::Line(TrackCard, Make.Sized(Bar, LobbyMetric::AccentBarWidth, LobbyMetric::AccentBarHeight), 18.f);
		UVerticalBox* Words = Make.Column();
		FWidgetFactory::Stack(Words, Make.Text(Catalog[i].Name.ToString(), Font(EFont::Title, 34.f), Color::Navy), 0.f);
		FWidgetFactory::Stack(Words, Make.WrappedText(Catalog[i].Tagline.ToString(), Font(EFont::Body, 17.f), Color::NavyDim), 0.f);
		FWidgetFactory::Line(TrackCard, Words, 0.f, /*bFill*/ true);

		UButton* Button = Make.CardButton(LobbyMetric::PickerCardRadius, Color::Paper, TrackCard, LobbyMetric::PickerCardPadding, /*bFillWidth*/ true);
		TrackButtons.Add(Button);
		FWidgetFactory::Line(Tracks, Button, i == 0 ? LobbyMetric::TrackGap : 0.f, /*bFill*/ true);
	}
	if (TrackButtons.IsValidIndex(0))
	{
		TrackButtons[0]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleTrack0Clicked);
	}
	if (TrackButtons.IsValidIndex(1))
	{
		TrackButtons[1]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleTrack1Clicked);
	}
	FWidgetFactory::Stack(TrackSection, Tracks, 30.f);
	return TrackSection;
}

void UKartLobbyWidget::AddSeats(const FWidgetFactory& Make, UVerticalBox& Column)
{
	FWidgetFactory::Stack(&Column, Heading(Make, TEXT("PLAYERS")), 10.f);
	UHorizontalBox* Rows[SeatCount / SeatsPerRow] = { Make.Row(), Make.Row() };
	for (int32 Seat = 1; Seat <= SeatCount; ++Seat)
	{
		UHorizontalBox* Row = Make.Row();
		UBorder* Swatch = Make.OutlinedCard(Color::PaperShade, LobbyMetric::SwatchRadius, Color::Navy, LobbyMetric::SwatchOutline, FMargin(0.f));
		FWidgetFactory::Line(Row, Make.Sized(Swatch, LobbyMetric::SwatchSize, LobbyMetric::SwatchSize), 16.f);
		UVerticalBox* Who = Make.Column();
		FWidgetFactory::Stack(Who, Make.Text(FString::Printf(TEXT("P%d"), Seat), Font(EFont::Label, 16.f), Color::NavyDim), 0.f);
		UTextBlock* Name = Make.Text(TEXT("Open seat"), Font(EFont::Title, 28.f), Color::NavyDim);
		FWidgetFactory::Stack(Who, Name, 0.f);
		FWidgetFactory::Line(Row, Who, 12.f, /*bFill*/ true);
		UTextBlock* State = Make.Text(TEXT(""), Font(EFont::Button, 22.f), Color::NavyDim);
		FWidgetFactory::Line(Row, State, 0.f);

		// Two seats a row: P1 P2 over P3 P4.
		UBorder* RowCard = Make.Card(Color::Paper, LobbyMetric::SeatRadius, LobbyMetric::SeatPadding, Row);
		UHorizontalBoxSlot* RowSlot = Rows[(Seat - 1) / SeatsPerRow]->AddChildToHorizontalBox(RowCard);
		RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RowSlot->SetPadding(FMargin(0.f, 0.f, Seat % SeatsPerRow == 1 ? LobbyMetric::SeatGap : 0.f, 0.f));
		SeatRows.Add(RowCard);
		SeatSwatches.Add(Swatch);
		SeatNames.Add(Name);
		SeatStates.Add(State);
	}
	FWidgetFactory::Stack(&Column, Rows[0], 16.f);
	FWidgetFactory::Stack(&Column, Rows[1], 30.f);
}

UWidget* UKartLobbyWidget::BuildFooter(const FWidgetFactory& Make)
{
	UHorizontalBox* Footer = Make.Row();
	StatusText = Make.WrappedText(TEXT(""), Font(EFont::Title, 30.f, 2.f), Color::White);
	FWidgetFactory::Line(Footer, StatusText, 24.f, /*bFill*/ true);
	StartLabel = Make.Text(TEXT("Start"), Font(EFont::Display, 40.f, 3.f), Color::White);
	UBorder* StartPad = Make.Card(FLinearColor::Transparent, 0.f, LobbyMetric::StartPadding, StartLabel);
	StartButton = Make.CardButton(LobbyMetric::StartRadius, Color::Hot, StartPad);
	StartButton->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleStartClicked);
	FWidgetFactory::Line(Footer, StartButton, 0.f);
	return Footer;
}
