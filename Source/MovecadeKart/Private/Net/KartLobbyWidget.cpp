#include "Net/KartLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "Net/RoomHostSubsystem.h"
#include "Styling/CoreStyle.h"

namespace
{
	struct FTrackCard
	{
		const TCHAR* Id;
		const TCHAR* Name;
		const TCHAR* Blurb;
		const TCHAR* Hex;
	};

	// app/src/games/kart/tracks/catalog.ts + TrackPicker.tsx
	const FTrackCard GTracks[] = {
		{ TEXT("sunset-harbor"), TEXT("Sunset Harbor"), TEXT("Lighthouse bends · coral tunnel · marina"), TEXT("f3ad74") },
		{ TEXT("violet-loop"), TEXT("Violet Loop"), TEXT("The original figure-eight circuit"), TEXT("b8a1ff") },
	};

	const FLinearColor Ink = FLinearColor(FColor(0x0d, 0x10, 0x20));
	const FLinearColor Panel = FLinearColor(FColor(0x1b, 0x20, 0x3a));
	const FLinearColor PanelHi = FLinearColor(FColor(0x2a, 0x31, 0x55));
	const FLinearColor TextMain = FLinearColor(FColor(0xf4, 0xf0, 0xea));
	const FLinearColor TextDim = FLinearColor(FColor(0x9a, 0xa3, 0xc7));
	const FLinearColor Accent = FLinearColor(FColor(0x3f, 0xc9, 0xc2));
	const FLinearColor Brand = FLinearColor(FColor(0x6d, 0x8b, 0xff));

	FSlateFontInfo Font(const char* Face, int32 Size, int32 Spacing = 0)
	{
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Face, Size);
		Info.LetterSpacing = Spacing;
		return Info;
	}

	const TCHAR* LobbyClassPath = TEXT("/Game/UI/Lobby/WBP_Lobby.WBP_Lobby_C");
}

UKartLobbyWidget* UKartLobbyWidget::Show(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController())
	{
		return nullptr;
	}
	UClass* Class = nullptr;
	if (FPackageName::DoesPackageExist(TEXT("/Game/UI/Lobby/WBP_Lobby")))
	{
		Class = LoadClass<UKartLobbyWidget>(nullptr, LobbyClassPath);
	}
	UKartLobbyWidget* Widget = CreateWidget<UKartLobbyWidget>(PC, Class ? Class : UKartLobbyWidget::StaticClass());
	if (!Widget)
	{
		return nullptr;
	}
	Widget->AddToViewport(10);
	UE_LOG(LogRoomHost, Display, TEXT("Lobby shown (%s)"), *Widget->GetClass()->GetPathName());
	if (URoomHostSubsystem* Subsystem = Widget->Room())
	{
		Subsystem->OpenRoom();
	}
	return Widget;
}

URoomHostSubsystem* UKartLobbyWidget::Room() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<URoomHostSubsystem>() : nullptr;
}

TSharedRef<SWidget> UKartLobbyWidget::RebuildWidget()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	// Build when there is no designer content: no root, or only an empty root panel (a fresh WBP_Lobby).
	const UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
	if (!WidgetTree->RootWidget || (RootPanel && RootPanel->GetChildrenCount() == 0))
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UKartLobbyWidget::BuildTree()
{
	auto Text = [this](const FString& Value, const FSlateFontInfo& InFont, const FLinearColor& Color)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>();
		Block->SetText(FText::FromString(Value));
		Block->SetFont(InFont);
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	};
	auto Box = [this](const FLinearColor& Color, const FMargin& InPadding, UWidget* Content)
	{
		UBorder* Border = WidgetTree->ConstructWidget<UBorder>();
		Border->SetBrushColor(Color);
		Border->SetPadding(InPadding);
		if (Content)
		{
			Border->SetContent(Content);
		}
		return Border;
	};
	auto Sized = [this](UWidget* Content, float Width, float Height)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
		if (Width > 0.f) Size->SetWidthOverride(Width);
		if (Height > 0.f) Size->SetHeightOverride(Height);
		Size->SetContent(Content);
		return Size;
	};
	auto AddV = [](UVerticalBox* Parent, UWidget* Child, float Bottom, bool bFill = false)
	{
		UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(Child);
		Slot->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
		if (bFill)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		return Slot;
	};
	auto AddH = [](UHorizontalBox* Parent, UWidget* Child, float Right, bool bFill = false)
	{
		UHorizontalBoxSlot* Slot = Parent->AddChildToHorizontalBox(Child);
		Slot->SetPadding(FMargin(0.f, 0.f, Right, 0.f));
		Slot->SetVerticalAlignment(VAlign_Center);
		if (bFill)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		return Slot;
	};

	UBorder* Root = Box(Ink.CopyWithNewOpacity(0.94f), FMargin(64.f, 48.f), nullptr);
	WidgetTree->RootWidget = Root;
	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
	Root->SetContent(Columns);

	// Left: how to join.
	UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Left, Text(TEXT("KART RACER"), Font("Bold", 18, 400), Accent), 4.f);
	AddV(Left, Text(TEXT("Scan to join"), Font("Bold", 40), TextMain), 18.f);
	QrImage = WidgetTree->ConstructWidget<UImage>();
	QrImage->SetColorAndOpacity(FLinearColor::White);
	AddV(Left, Sized(Box(FLinearColor::White, FMargin(0.f), QrImage), 320.f, 320.f), 18.f)->SetHorizontalAlignment(HAlign_Left);
	AddV(Left, Text(TEXT("ROOM CODE"), Font("Bold", 14, 300), TextDim), 0.f);
	CodeText = Text(TEXT("------"), Font("Bold", 64, 350), TextMain);
	AddV(Left, CodeText, 6.f);
	UrlText = Text(TEXT(""), Font("Regular", 13), TextDim);
	AddV(Left, UrlText, 6.f);
	ConnectionText = Text(TEXT("Connecting to the room server..."), Font("Regular", 13), TextDim);
	ConnectionText->SetAutoWrapText(true);
	AddV(Left, Sized(ConnectionText, 360.f, 0.f), 0.f);
	AddH(Columns, Left, 64.f)->SetVerticalAlignment(VAlign_Top);

	// Right: track, seats, start.
	UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Right, Text(TEXT("TRACK"), Font("Bold", 14, 300), TextDim), 8.f);
	UHorizontalBox* Tracks = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(GTracks)); ++i)
	{
		UHorizontalBox* Card = WidgetTree->ConstructWidget<UHorizontalBox>();
		AddH(Card, Sized(Box(FLinearColor(FColor::FromHex(GTracks[i].Hex)), FMargin(0.f), nullptr), 10.f, 56.f), 14.f);
		UVerticalBox* Words = WidgetTree->ConstructWidget<UVerticalBox>();
		AddV(Words, Text(GTracks[i].Name, Font("Bold", 20), TextMain), 2.f);
		AddV(Words, Text(GTracks[i].Blurb, Font("Regular", 12), TextDim), 0.f);
		AddH(Card, Words, 0.f, true);
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		if (UButtonSlot* CardSlot = Cast<UButtonSlot>(Button->SetContent(Card)))
		{
			CardSlot->SetHorizontalAlignment(HAlign_Fill);
			CardSlot->SetPadding(FMargin(8.f, 6.f));
		}
		Button->SetBackgroundColor(Panel);
		TrackButtons.Add(Button);
		AddH(Tracks, Button, i == 0 ? 16.f : 0.f, true);
	}
	TrackButtons[0]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleTrack0Clicked);
	TrackButtons[1]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleTrack1Clicked);
	AddV(Right, Tracks, 28.f);

	AddV(Right, Text(TEXT("PLAYERS"), Font("Bold", 14, 300), TextDim), 8.f);
	for (int32 Seat = 1; Seat <= 4; ++Seat)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UBorder* Swatch = Box(PanelHi, FMargin(0.f), nullptr);
		AddH(Row, Sized(Swatch, 34.f, 34.f), 16.f);
		AddH(Row, Text(FString::Printf(TEXT("P%d"), Seat), Font("Bold", 16), TextDim), 16.f);
		UTextBlock* Name = Text(TEXT("Open seat"), Font("Bold", 22), TextDim);
		AddH(Row, Name, 12.f, true);
		UTextBlock* State = Text(TEXT(""), Font("Regular", 16), TextDim);
		AddH(Row, State, 0.f);
		UBorder* RowBox = Box(Panel, FMargin(16.f, 10.f), Row);
		AddV(Right, RowBox, 8.f);
		SeatRows.Add(RowBox);
		SeatSwatches.Add(Swatch);
		SeatNames.Add(Name);
		SeatStates.Add(State);
	}

	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	StatusText = Text(TEXT(""), Font("Bold", 20), TextMain);
	StatusText->SetAutoWrapText(true);
	AddH(Footer, StatusText, 24.f, true);
	StartButton = WidgetTree->ConstructWidget<UButton>();
	StartLabel = Text(TEXT("Start  (Enter)"), Font("Bold", 20), Ink);
	StartButton->SetContent(Box(FLinearColor::Transparent, FMargin(24.f, 10.f), StartLabel));
	StartButton->SetBackgroundColor(Accent);
	StartButton->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleStartClicked);
	AddH(Footer, StartButton, 0.f);
	AddV(Right, Footer, 0.f)->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
	AddH(Columns, Right, 0.f, true)->SetVerticalAlignment(VAlign_Top);
}

void UKartLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (URoomHostSubsystem* Subsystem = Room(); Subsystem && !bBound)
	{
		bBound = true;
		Subsystem->OnStartRequested.AddDynamic(this, &UKartLobbyWidget::HandleStartRequested);
		Subsystem->OnReturnedToLobby.AddDynamic(this, &UKartLobbyWidget::HandleReturnedToLobby);
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
	Refresh();
}

void UKartLobbyWidget::NativeDestruct()
{
	if (URoomHostSubsystem* Subsystem = Room(); Subsystem && bBound)
	{
		Subsystem->OnStartRequested.RemoveDynamic(this, &UKartLobbyWidget::HandleStartRequested);
		Subsystem->OnReturnedToLobby.RemoveDynamic(this, &UKartLobbyWidget::HandleReturnedToLobby);
		bBound = false;
	}
	Super::NativeDestruct();
}

void UKartLobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshAccumulator += InDeltaTime;
	if (RefreshAccumulator >= 0.1f)
	{
		RefreshAccumulator = 0.f;
		Refresh();
	}
}

FReply UKartLobbyWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		HandleStartClicked();
		return FReply::Handled();
	}
	if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Tab || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right)
	{
		if (const URoomHostSubsystem* Subsystem = Room())
		{
			SelectTrack(Subsystem->GetRoom().SelectedMapId == GTracks[0].Id ? 1 : 0);
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UKartLobbyWidget::Refresh()
{
	const URoomHostSubsystem* Subsystem = Room();
	if (!Subsystem || !CodeText)
	{
		return;
	}
	const FMcRoomState& State = Subsystem->GetRoom();
	const FString Code = Subsystem->GetJoinCode();
	CodeText->SetText(FText::FromString(Code.IsEmpty() ? TEXT("------") : Code));

	FString Url = Subsystem->GetJoinUrl();
	Url.Split(TEXT("#"), &Url, nullptr); // the token stays in the QR, not on screen
	Url.RemoveFromStart(TEXT("https://"));
	Url.RemoveFromStart(TEXT("http://"));
	UrlText->SetText(FText::FromString(Url));

	UTexture2D* Qr = Subsystem->GetJoinQrTexture();
	if (Qr != ShownQr)
	{
		ShownQr = Qr;
		QrImage->SetBrushFromTexture(Qr, false);
		QrImage->SetOpacity(Qr ? 1.f : 0.f);
	}

	FString Connection;
	switch (Subsystem->GetConnectionState())
	{
	case EMcConnection::Idle: Connection = TEXT("Room closed."); break;
	case EMcConnection::Connecting: Connection = TEXT("Connecting to the room server..."); break;
	case EMcConnection::Reconnecting: Connection = TEXT("Reconnecting... everyone keeps their spot."); break;
	case EMcConnection::Open: Connection = Subsystem->IsHost() ? TEXT("Room open. Phones join from the QR code.") : TEXT("Another screen is hosting this room."); break;
	case EMcConnection::Failed: Connection = TEXT("Room server error: ") + Subsystem->GetLastError(); break;
	}
	ConnectionText->SetText(FText::FromString(Connection));

	for (int32 i = 0; i < TrackButtons.Num(); ++i)
	{
		const bool bSelected = State.SelectedMapId == GTracks[i].Id;
		TrackButtons[i]->SetBackgroundColor(bSelected ? FLinearColor(FColor::FromHex(GTracks[i].Hex)) * 0.55f + Panel * 0.45f : Panel);
	}

	for (int32 i = 0; i < SeatRows.Num(); ++i)
	{
		const FMcSeat* Seat = State.FindBySeat(i + 1);
		if (!Seat)
		{
			SeatSwatches[i]->SetBrushColor(PanelHi);
			SeatNames[i]->SetText(FText::FromString(TEXT("Open seat")));
			SeatNames[i]->SetColorAndOpacity(FSlateColor(TextDim));
			SeatStates[i]->SetText(FText::GetEmpty());
			continue;
		}
		SeatSwatches[i]->SetBrushColor(Seat->PaintColor);
		SeatNames[i]->SetText(FText::FromString(Seat->bBot ? Seat->Name + TEXT("  (bot)") : Seat->Name));
		SeatNames[i]->SetColorAndOpacity(FSlateColor(TextMain));
		FString Status;
		FLinearColor Color = TextDim;
		if (Seat->bEliminated)
		{
			Status = TEXT("Out");
		}
		else if (!Seat->bConnected)
		{
			Status = FString::Printf(TEXT("Reconnecting · %ds"), FMath::CeilToInt32(Seat->GraceRemainingMs / 1000.f));
			Color = FLinearColor(FColor::FromHex(TEXT("f2b23f")));
		}
		else if (Seat->bReady)
		{
			Status = TEXT("Ready");
			Color = Accent;
		}
		else
		{
			Status = Seat->bCalibrated ? TEXT("Not ready") : TEXT("Calibrating");
		}
		SeatStates[i]->SetText(FText::FromString(Status));
		SeatStates[i]->SetColorAndOpacity(FSlateColor(Color));
	}

	const bool bSolo = State.Players.Num() == 1 && !State.Players[0].bBot;
	const int32 CountdownMs = Subsystem->GetLobbyCountdownRemainingMs();
	FString Line = Subsystem->GetStartBlockedReason();
	if (CountdownMs > 0)
	{
		Line = FString::Printf(TEXT("Starting in %d..."), FMath::CeilToInt32(CountdownMs / 1000.f));
	}
	else if (CountdownMs == 0 || State.Status == EMcRoomStatus::InProgress)
	{
		Line = TEXT("Here we go!");
	}
	else if (Line.IsEmpty())
	{
		Line = bSolo ? TEXT("Everyone ready. Start solo when you are.") : TEXT("Everyone ready");
	}
	StatusText->SetText(FText::FromString(Line));
	StartLabel->SetText(FText::FromString(bSolo ? TEXT("Start solo  (Enter)") : TEXT("Start  (Enter)")));
	const bool bCanStart = Subsystem->CanStart();
	StartButton->SetIsEnabled(bCanStart);
	StartButton->SetBackgroundColor(bCanStart ? Accent : PanelHi);
}

void UKartLobbyWidget::SelectTrack(int32 Index)
{
	if (URoomHostSubsystem* Subsystem = Room(); Subsystem && Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(GTracks)))
	{
		Subsystem->SelectMap(GTracks[Index].Id);
	}
}

void UKartLobbyWidget::HandleStartClicked()
{
	if (URoomHostSubsystem* Subsystem = Room())
	{
		Subsystem->RequestStart();
	}
}

void UKartLobbyWidget::HandleTrack0Clicked()
{
	SelectTrack(0);
}

void UKartLobbyWidget::HandleTrack1Clicked()
{
	SelectTrack(1);
}

void UKartLobbyWidget::HandleStartRequested(const FString& MapId)
{
	if (!bAutoHide)
	{
		return;
	}
	SetVisibility(ESlateVisibility::Collapsed);
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
}

void UKartLobbyWidget::HandleReturnedToLobby()
{
	if (!bAutoHide)
	{
		return;
	}
	SetVisibility(ESlateVisibility::Visible);
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
}
