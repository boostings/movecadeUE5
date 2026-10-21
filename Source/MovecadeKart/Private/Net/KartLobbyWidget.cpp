#include "Net/KartLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
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
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "KartCoords.h"
#include "Misc/PackageName.h"
#include "Net/RoomHostSubsystem.h"
#include "TrackBuilder.h"
#include "UI/KartPaintWidget.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartUIStyle.h"
#include "UI/KartUISubsystem.h"
#include "UI/KartWidgets.h"

// The lobby's look follows the UI style (UI/KartUIStyle.h): white cards on the track backdrop, the game's type and
// palette, and the shared button glyphs. Its behaviour (room calls, keys, auto-hide) is the network workstream's.

namespace
{
	using namespace KartUI;

	const TCHAR* LobbyClassPath = TEXT("/Game/UI/Lobby/WBP_Lobby.WBP_Lobby_C");

	FSlateRoundedBoxBrush Rounded(float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
	{
		// White fill: UBorder's brush colour and UButton's background colour tint it.
		return FSlateRoundedBoxBrush(FLinearColor::White, Radius, Outline, OutlineWidth);
	}

	FButtonStyle CardButtonStyle(float Radius)
	{
		FButtonStyle Style;
		Style.SetNormal(Rounded(Radius));
		Style.SetHovered(Rounded(Radius, Color::Sun, 4.f));
		Style.SetPressed(Rounded(Radius, Color::SunDeep, 4.f));
		Style.SetDisabled(Rounded(Radius));
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}
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
	auto Card = [this](const FLinearColor& Color, float Radius, const FMargin& InPadding, UWidget* Content)
	{
		UBorder* Border = WidgetTree->ConstructWidget<UBorder>();
		Border->SetBrush(Rounded(Radius));
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

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	WidgetTree->RootWidget = Root;

	// Painted layer: the dimming behind the cards and the button hints.
	UKartPaintWidget* Backdrop = WidgetTree->ConstructWidget<UKartPaintWidget>();
	Backdrop->OnPaintKart = [WeakThis = TWeakObjectPtr<UKartLobbyWidget>(this)](const FKartPainter& P, const FKartScreenFrame& Frame)
	{
		const URoomHostSubsystem* Subsystem = WeakThis.IsValid() ? WeakThis->Room() : nullptr;
		const bool bKart = !Subsystem || Subsystem->GetGameId() == TEXT("kart");
		P.Gradient(FVector2D::ZeroVector, FVector2D(Frame.Size.X * 0.62, Frame.Size.Y), Color::WithAlpha(Color::Night, 0.82f), Color::WithAlpha(Color::Night, 0.45f), false);
		P.Gradient(FVector2D(Frame.Size.X * 0.62, 0.0), FVector2D(Frame.Size.X * 0.38, Frame.Size.Y), Color::WithAlpha(Color::Night, 0.45f), Color::WithAlpha(Color::Night, 0.3f), false);
		TArray<FKartHint> Hints = { { EKartMenuAction::Up, NSLOCTEXT("KartLobby", "Game", "Game") } };
		if (bKart)
		{
			Hints.Add({ EKartMenuAction::Left, NSLOCTEXT("KartLobby", "Track", "Track") });
		}
		Hints.Add({ EKartMenuAction::Accept, NSLOCTEXT("KartLobby", "Start", "Start") });
		Hints.Add({ EKartMenuAction::Back, NSLOCTEXT("KartLobby", "Leave", "Leave") });
		KartWidgets::Hints(P, Frame, Hints);
	};
	if (UOverlaySlot* BackSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackSlot->SetHorizontalAlignment(HAlign_Fill);
		BackSlot->SetVerticalAlignment(VAlign_Fill);
	}

	UBorder* Safe = WidgetTree->ConstructWidget<UBorder>();
	Safe->SetBrushColor(FLinearColor::Transparent);
	Safe->SetPadding(FMargin(96.f, 54.f, 96.f, 120.f));
	if (UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe))
	{
		SafeSlot->SetHorizontalAlignment(HAlign_Fill);
		SafeSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
	Safe->SetContent(Columns);

	// Left: how to join, on a white card.
	UVerticalBox* Left = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Left, Text(TEXT("SCAN TO JOIN"), Font(EFont::Display, 46.f), Color::Navy), 18.f)->SetHorizontalAlignment(HAlign_Center);
	QrImage = WidgetTree->ConstructWidget<UImage>();
	QrImage->SetColorAndOpacity(FLinearColor::White);
	UBorder* QrFrame = Card(FLinearColor::White, 22.f, FMargin(14.f), QrImage);
	QrFrame->SetBrush(Rounded(22.f, Color::Navy, 4.f));
	AddV(Left, Sized(QrFrame, 360.f, 360.f), 22.f)->SetHorizontalAlignment(HAlign_Center);
	AddV(Left, Text(TEXT("ROOM CODE"), Font(EFont::Label, 20.f), Color::NavyDim), 0.f)->SetHorizontalAlignment(HAlign_Center);
	CodeText = Text(TEXT("------"), Font(EFont::Display, 68.f), Color::Hot);
	AddV(Left, CodeText, 4.f)->SetHorizontalAlignment(HAlign_Center);
	UrlText = Text(TEXT(""), Font(EFont::Body, 18.f), Color::NavyDim);
	AddV(Left, UrlText, 12.f)->SetHorizontalAlignment(HAlign_Center);
	ConnectionText = Text(TEXT("Connecting to the room server..."), Font(EFont::Body, 18.f), Color::Navy);
	ConnectionText->SetAutoWrapText(true);
	ConnectionText->SetJustification(ETextJustify::Center);
	AddV(Left, Card(Color::PaperShade, 22.f, FMargin(16.f, 10.f), Sized(ConnectionText, 360.f, 0.f)), 0.f)->SetHorizontalAlignment(HAlign_Center);
	UBorder* LeftCard = Card(Color::Paper, 30.f, FMargin(40.f, 32.f), Left);
	AddH(Columns, Sized(LeftCard, 500.f, 0.f), 56.f)->SetVerticalAlignment(VAlign_Center);

	// Right: title, track, seats, start.
	UVerticalBox* Right = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(Right, Text(TEXT("Party Mode"), Font(EFont::Display, 84.f, 5.f), Color::White), 0.f);
	SubtitleText = Text(TEXT("Phones are the controllers: scan the code, tilt to steer."), Font(EFont::Body, 26.f, 2.f), Color::White);
	AddV(Right, SubtitleText, 24.f);

	// The game: Kart Racer or a ported party game, as chunky pills.
	AddV(Right, Text(TEXT("GAME"), Font(EFont::Label, 22.f, 2.f), Color::Sun), 10.f);
	UHorizontalBox* Games = WidgetTree->ConstructWidget<UHorizontalBox>();
	for (const TCHAR* Name : { TEXT("Kart Racer"), TEXT("Bowling") })
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetStyle(CardButtonStyle(30.f));
		UTextBlock* Label = Text(Name, Font(EFont::Title, 30.f), Color::Navy);
		if (UButtonSlot* PillSlot = Cast<UButtonSlot>(Button->SetContent(Label)))
		{
			PillSlot->SetPadding(FMargin(34.f, 10.f));
		}
		Button->SetBackgroundColor(Color::Paper);
		AddH(Games, Button, 16.f);
		GameButtons.Add(Button);
	}
	GameButtons[0]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleGame0Clicked);
	GameButtons[1]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleGame1Clicked);
	AddV(Right, Games, 26.f);

	TrackSection = WidgetTree->ConstructWidget<UVerticalBox>();
	AddV(TrackSection, Text(TEXT("TRACK"), Font(EFont::Label, 22.f, 2.f), Color::Sun), 10.f);
	UHorizontalBox* Tracks = WidgetTree->ConstructWidget<UHorizontalBox>();
	const TArray<FKartTrackInfo>& Catalog = FKartTrackCatalog::All();
	for (int32 i = 0; i < Catalog.Num() && i < 2; ++i)
	{
		UHorizontalBox* TrackCard = WidgetTree->ConstructWidget<UHorizontalBox>();
		UBorder* Bar = Card(Catalog[i].Accent, 9.f, FMargin(0.f), nullptr);
		AddH(TrackCard, Sized(Bar, 18.f, 76.f), 18.f);
		UVerticalBox* Words = WidgetTree->ConstructWidget<UVerticalBox>();
		AddV(Words, Text(Catalog[i].Name.ToString(), Font(EFont::Title, 34.f), Color::Navy), 0.f);
		UTextBlock* Tagline = Text(Catalog[i].Tagline.ToString(), Font(EFont::Body, 17.f), Color::NavyDim);
		Tagline->SetAutoWrapText(true);
		AddV(Words, Tagline, 0.f);
		AddH(TrackCard, Words, 0.f, true);
		UButton* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetStyle(CardButtonStyle(24.f));
		if (UButtonSlot* CardSlot = Cast<UButtonSlot>(Button->SetContent(TrackCard)))
		{
			CardSlot->SetHorizontalAlignment(HAlign_Fill);
			CardSlot->SetPadding(FMargin(18.f, 14.f));
		}
		Button->SetBackgroundColor(Color::Paper);
		TrackButtons.Add(Button);
		AddH(Tracks, Button, i == 0 ? 20.f : 0.f, true);
	}
	if (TrackButtons.Num() > 0) TrackButtons[0]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleTrack0Clicked);
	if (TrackButtons.Num() > 1) TrackButtons[1]->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleTrack1Clicked);
	AddV(TrackSection, Tracks, 30.f);
	AddV(Right, TrackSection, 0.f);

	AddV(Right, Text(TEXT("PLAYERS"), Font(EFont::Label, 22.f, 2.f), Color::Sun), 10.f);
	UHorizontalBox* SeatRowsBox[2] = { WidgetTree->ConstructWidget<UHorizontalBox>(), WidgetTree->ConstructWidget<UHorizontalBox>() };
	for (int32 Seat = 1; Seat <= 4; ++Seat)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		UBorder* Swatch = Card(Color::PaperShade, 28.f, FMargin(0.f), nullptr);
		Swatch->SetBrush(Rounded(28.f, Color::Navy, 3.f));
		Swatch->SetBrushColor(Color::PaperShade);
		AddH(Row, Sized(Swatch, 56.f, 56.f), 16.f);
		UVerticalBox* Who = WidgetTree->ConstructWidget<UVerticalBox>();
		AddV(Who, Text(FString::Printf(TEXT("P%d"), Seat), Font(EFont::Label, 16.f), Color::NavyDim), 0.f);
		UTextBlock* Name = Text(TEXT("Open seat"), Font(EFont::Title, 28.f), Color::NavyDim);
		AddV(Who, Name, 0.f);
		AddH(Row, Who, 12.f, true);
		UTextBlock* State = Text(TEXT(""), Font(EFont::Button, 22.f), Color::NavyDim);
		AddH(Row, State, 0.f);
		UBorder* RowBox = Card(Color::Paper, 26.f, FMargin(16.f, 12.f, 24.f, 12.f), Row);
		UHorizontalBoxSlot* RowSlot = SeatRowsBox[(Seat - 1) / 2]->AddChildToHorizontalBox(RowBox);
		RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		RowSlot->SetPadding(FMargin(0.f, 0.f, Seat % 2 == 1 ? 20.f : 0.f, 0.f));
		SeatRows.Add(RowBox);
		SeatSwatches.Add(Swatch);
		SeatNames.Add(Name);
		SeatStates.Add(State);
	}
	AddV(Right, SeatRowsBox[0], 16.f);
	AddV(Right, SeatRowsBox[1], 30.f);

	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>();
	StatusText = Text(TEXT(""), Font(EFont::Title, 30.f, 2.f), Color::White);
	StatusText->SetAutoWrapText(true);
	AddH(Footer, StatusText, 24.f, true);
	StartButton = WidgetTree->ConstructWidget<UButton>();
	StartButton->SetStyle(CardButtonStyle(40.f));
	StartLabel = Text(TEXT("Start"), Font(EFont::Display, 40.f, 3.f), Color::White);
	UBorder* StartPad = Card(FLinearColor::Transparent, 0.f, FMargin(40.f, 12.f), StartLabel);
	StartButton->SetContent(StartPad);
	StartButton->SetBackgroundColor(Color::Hot);
	StartButton->OnClicked.AddDynamic(this, &UKartLobbyWidget::HandleStartClicked);
	AddH(Footer, StartButton, 0.f);
	AddV(Right, Footer, 0.f);
	AddH(Columns, Right, 0.f, true)->SetVerticalAlignment(VAlign_Center);
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
	ShowBackdrop(true);
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
	{
		UI->ApplyVolumes();
		UI->SetMenuMusic(true);
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
	if (BackdropCamera)
	{
		BackdropCamera->Destroy();
		BackdropCamera = nullptr;
	}
	Super::NativeDestruct();
}

void UKartLobbyWidget::ShowBackdrop(bool bShow)
{
	// A slow orbit of the track behind the lobby, like the front end's menus.
	APlayerController* PC = GetOwningPlayer();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}
	if (!bShow)
	{
		return; // the race takes the view over itself
	}
	if (!BackdropCamera)
	{
		BackdropCamera = World->SpawnActor<ACameraActor>();
		if (BackdropCamera)
		{
			BackdropCamera->GetCameraComponent()->SetFieldOfView(70.f);
			BackdropCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
	}
	if (BackdropCamera)
	{
		UpdateBackdrop(0.f);
		PC->SetViewTarget(BackdropCamera);
	}
}

void UKartLobbyWidget::UpdateBackdrop(float DeltaSeconds)
{
	if (!BackdropCamera)
	{
		return;
	}
	BackdropClock += DeltaSeconds;
	FBox2D Bounds(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0));
	for (TActorIterator<ATrackBuilder> It(GetWorld()); It; ++It)
	{
		if (const TSharedPtr<const MovecadeKart::FTrack> Track = It->GetTrack(); Track.IsValid() && Track->MinimapBounds.bIsValid)
		{
			Bounds = Track->MinimapBounds;
		}
		break;
	}
	const double Extent = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y);
	const double Angle = BackdropClock * 0.035;
	const FVector2D Eye = Bounds.GetCenter() + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Extent * 0.62;
	const FVector EyeUE = MovecadeKart::Coords::ToUE(Eye, Extent * 0.32);
	const FVector Target = MovecadeKart::Coords::ToUE(Bounds.GetCenter(), 0.0);
	BackdropCamera->SetActorLocationAndRotation(EyeUE, (Target - EyeUE).Rotation());
}

void UKartLobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		UpdateBackdrop(InDeltaTime);
	}
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
	if (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down)
	{
		if (const URoomHostSubsystem* Subsystem = Room())
		{
			SelectGame(Subsystem->GetGameId() == TEXT("kart") ? 1 : 0);
			if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
			{
				UI->PlaySound(EKartUISound::Tick);
			}
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Tab || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::Gamepad_DPad_Right)
	{
		if (const URoomHostSubsystem* Subsystem = Room(); Subsystem && Subsystem->GetGameId() == TEXT("kart"))
		{
			const int32 Current = FKartTrackCatalog::IndexOf(Subsystem->GetRoom().SelectedMapId);
			SelectTrack(Current == 0 ? 1 : 0);
			if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
			{
				UI->PlaySound(EKartUISound::Tick);
			}
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		LeaveParty();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UKartLobbyWidget::LeaveParty()
{
	// Back to the main menu: close the room and travel with the curtain.
	UKartSessionSettings* Session = UKartSessionSettings::Get(this);
	UKartUISubsystem* UI = UKartUISubsystem::Get(this);
	if (!Session || !UI || UI->IsTravelling())
	{
		return;
	}
	UI->PlaySound(EKartUISound::Back);
	if (URoomHostSubsystem* Subsystem = Room())
	{
		Subsystem->CloseRoom(true);
	}
	Session->ReturnToMenu(EKartMenuEntry::MainMenu);
	UI->TravelTo(UGameplayStatics::GetCurrentLevelName(this));
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
	FLinearColor ConnectionColor = Color::Navy;
	switch (Subsystem->GetConnectionState())
	{
	case EMcConnection::Idle: Connection = TEXT("Room closed."); break;
	case EMcConnection::Connecting: Connection = TEXT("Connecting to the room server..."); break;
	case EMcConnection::Reconnecting: Connection = TEXT("Reconnecting... everyone keeps their spot."); ConnectionColor = Color::SunDeep; break;
	case EMcConnection::Open: Connection = Subsystem->IsHost() ? TEXT("Room open. Phones join from the QR code.") : TEXT("Another screen is hosting this room."); break;
	case EMcConnection::Failed:
		// The one refusal a player can't fix from here: the server hasn't admitted this host yet.
		Connection = Subsystem->GetLastError().StartsWith(TEXT("UNAUTHORIZED_ORIGIN"))
			? FString::Printf(TEXT("%s doesn't accept this game as a host yet (it needs the server update in Docs/Port/server)."), *Subsystem->PartyHost)
			: TEXT("Room server error: ") + Subsystem->GetLastError();
		ConnectionColor = Color::Hot;
		break;
	}
	ConnectionText->SetText(FText::FromString(Connection));
	ConnectionText->SetColorAndOpacity(FSlateColor(ConnectionColor));

	const bool bKart = Subsystem->GetGameId() == TEXT("kart");
	for (int32 i = 0; i < GameButtons.Num(); ++i)
	{
		const bool bSelected = (i == 0) == bKart;
		GameButtons[i]->SetBackgroundColor(bSelected ? Color::Sun : Color::WithAlpha(Color::Paper, 0.9f));
		GameButtons[i]->SetRenderScale(bSelected ? FVector2D(1.04, 1.04) : FVector2D(1.0, 1.0));
	}
	TrackSection->SetVisibility(bKart ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	SubtitleText->SetText(FText::FromString(bKart ? TEXT("Phones are the controllers: scan the code, tilt to steer.")
		: TEXT("Phones are the ball: scan the code, swing and let go to bowl.")));

	const TArray<FKartTrackInfo>& Catalog = FKartTrackCatalog::All();
	for (int32 i = 0; i < TrackButtons.Num() && i < Catalog.Num(); ++i)
	{
		const bool bSelected = State.SelectedMapId == Catalog[i].Id;
		TrackButtons[i]->SetBackgroundColor(bSelected ? Color::Sun : Color::WithAlpha(Color::Paper, 0.9f));
		TrackButtons[i]->SetRenderScale(bSelected ? FVector2D(1.03, 1.03) : FVector2D(1.0, 1.0));
	}

	for (int32 i = 0; i < SeatRows.Num(); ++i)
	{
		const FMcSeat* Seat = State.FindBySeat(i + 1);
		if (!Seat)
		{
			SeatRows[i]->SetBrushColor(Color::WithAlpha(Color::Paper, 0.82f));
			SeatSwatches[i]->SetBrushColor(Color::PaperShade);
			SeatNames[i]->SetText(FText::FromString(TEXT("Waiting for a phone")));
			SeatNames[i]->SetColorAndOpacity(FSlateColor(Color::NavyDim));
			SeatStates[i]->SetText(FText::GetEmpty());
			continue;
		}
		SeatRows[i]->SetBrushColor(Color::Paper);
		SeatSwatches[i]->SetBrushColor(Seat->PaintColor);
		SeatNames[i]->SetText(FText::FromString(Seat->bBot ? Seat->Name + TEXT("  (bot)") : Seat->Name));
		SeatNames[i]->SetColorAndOpacity(FSlateColor(Color::Navy));
		FString Status;
		FLinearColor StatusColor = Color::NavyDim;
		if (Seat->bEliminated)
		{
			Status = TEXT("Out");
		}
		else if (!Seat->bConnected)
		{
			Status = FString::Printf(TEXT("Reconnecting · %ds"), FMath::CeilToInt32(Seat->GraceRemainingMs / 1000.f));
			StatusColor = Color::SunDeep;
		}
		else if (Seat->bReady)
		{
			Status = TEXT("Ready!");
			StatusColor = FLinearColor::FromSRGBColor(FColor(0x1f, 0xa8, 0x72));
		}
		else
		{
			Status = Seat->bCalibrated ? TEXT("Not ready") : TEXT("Calibrating");
		}
		SeatStates[i]->SetText(FText::FromString(Status));
		SeatStates[i]->SetColorAndOpacity(FSlateColor(StatusColor));
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
		Line = bSolo ? TEXT("Everyone ready. Start solo when you are.") : TEXT("Everyone ready!");
	}
	StatusText->SetText(FText::FromString(Line));
	StartLabel->SetText(FText::FromString(bSolo ? TEXT("Start solo") : TEXT("Start")));
	const bool bCanStart = Subsystem->CanStart();
	StartButton->SetIsEnabled(bCanStart);
	StartButton->SetBackgroundColor(bCanStart ? Color::Hot : Color::NavyDim);
}

void UKartLobbyWidget::SelectTrack(int32 Index)
{
	if (URoomHostSubsystem* Subsystem = Room(); Subsystem && FKartTrackCatalog::All().IsValidIndex(Index))
	{
		Subsystem->SelectMap(FKartTrackCatalog::All()[Index].Id);
	}
}

void UKartLobbyWidget::SelectGame(int32 Index)
{
	if (URoomHostSubsystem* Subsystem = Room())
	{
		Subsystem->SelectGame(Index == 0 ? TEXT("kart") : TEXT("bowling"));
		Refresh();
	}
}

void UKartLobbyWidget::HandleGame0Clicked()
{
	SelectGame(0);
}

void UKartLobbyWidget::HandleGame1Clicked()
{
	SelectGame(1);
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
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
	{
		UI->SetMenuMusic(false);
		UI->PlaySound(EKartUISound::Go);
	}
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
	ShowBackdrop(true);
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
}
