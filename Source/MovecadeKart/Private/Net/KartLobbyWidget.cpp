// UKartLobbyWidget: creation, the room's flow (start, back to the lobby, the party's next game), keys, and the
// orbiting track behind the lobby. The tree is in KartLobbyLayout.cpp and the text in KartLobbyView.cpp.

#include "Net/KartLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PanelWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Games/ArcadeCatalog.h"
#include "Games/ArcadeSession.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "KartCoords.h"
#include "Misc/PackageName.h"
#include "Net/RoomHostSubsystem.h"
#include "TrackBuilder.h"
#include "UI/KartSessionSettings.h"
#include "UI/KartTrackCatalog.h"
#include "UI/KartUISubsystem.h"

namespace
{
	const TCHAR* const LobbyAssetPackage = TEXT("/Game/UI/Lobby/WBP_Lobby");
	const TCHAR* const LobbyAssetClass = TEXT("/Game/UI/Lobby/WBP_Lobby.WBP_Lobby_C");
	/** Over the game's HUD, under the curtain. */
	constexpr int32 LobbyZOrder = 10;

	// The backdrop camera's slow orbit around the track (track units are the web's metres).
	constexpr float BackdropFieldOfView = 70.f;
	/** Radians per second. */
	constexpr double OrbitSpeed = 0.035;
	/** Orbit radius and eye height, as fractions of the track's larger extent. */
	constexpr double OrbitRadius = 0.62;
	constexpr double OrbitHeight = 0.32;
	/** Orbited when the level has no track yet. */
	const FBox2D FallbackBounds(FVector2D(-100.0, -100.0), FVector2D(100.0, 100.0));

	/** "Next game" auto start gives up after this long: the lobby's own flow takes over. */
	constexpr double ContinueTimeoutSeconds = 120.0;

	/** Kart Racer, then every party game in the catalog's order: the carousel. */
	int32 LobbyGameCount()
	{
		return MovecadeKart::Arcade::Games().Num() + 1;
	}

	int32 LobbyGameIndexOf(const FString& GameId)
	{
		const int32 Found = MovecadeKart::Arcade::Games().IndexOfByPredicate([&GameId](const MovecadeKart::Arcade::FGameInfo& G) { return G.Id == GameId; });
		return Found == INDEX_NONE ? 0 : Found + 1;
	}

	FString LobbyGameAt(int32 Index)
	{
		const TArray<MovecadeKart::Arcade::FGameInfo>& Games = MovecadeKart::Arcade::Games();
		return Games.IsValidIndex(Index - 1) ? Games[Index - 1].Id : FString(TEXT("kart"));
	}

	bool IsAny(const FKey& Key, std::initializer_list<FKey> Keys)
	{
		for (const FKey& Candidate : Keys)
		{
			if (Key == Candidate)
			{
				return true;
			}
		}
		return false;
	}
}

UKartLobbyWidget* UKartLobbyWidget::Show(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController())
	{
		return nullptr;
	}
	UClass* Class = nullptr;
	if (FPackageName::DoesPackageExist(LobbyAssetPackage))
	{
		Class = LoadClass<UKartLobbyWidget>(nullptr, LobbyAssetClass);
	}
	UKartLobbyWidget* Widget = CreateWidget<UKartLobbyWidget>(PC, Class ? Class : UKartLobbyWidget::StaticClass());
	if (!Widget)
	{
		UE_LOG(LogRoomHost, Error, TEXT("Could not create the lobby widget"));
		return nullptr;
	}
	Widget->AddToViewport(LobbyZOrder);
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
	TakeInputFocus();
	ShowBackdrop();
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

void UKartLobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		UpdateBackdrop(InDeltaTime);
	}
	UpdatePartyContinue();
	RefreshIfChanged();
}

FReply UKartLobbyWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (IsAny(Key, { EKeys::Enter, EKeys::Gamepad_FaceButton_Bottom }))
	{
		HandleStartClicked();
		return FReply::Handled();
	}
	if (IsAny(Key, { EKeys::Up, EKeys::Down, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down }))
	{
		if (Room())
		{
			StepGame(IsAny(Key, { EKeys::Up, EKeys::Gamepad_DPad_Up }) ? -1 : 1);
			PlayUiTick();
		}
		return FReply::Handled();
	}
	if (IsAny(Key, { EKeys::Left, EKeys::Right, EKeys::Tab, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right }))
	{
		// Two tracks: any direction switches to the other one.
		if (const URoomHostSubsystem* Subsystem = Room(); Subsystem && Subsystem->GetGameId() == TEXT("kart"))
		{
			SelectTrack(FKartTrackCatalog::IndexOf(Subsystem->GetRoom().SelectedMapId) == 0 ? 1 : 0);
			PlayUiTick();
		}
		return FReply::Handled();
	}
	if (IsAny(Key, { EKeys::Escape, EKeys::Gamepad_FaceButton_Right }))
	{
		LeaveParty();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UKartLobbyWidget::PlayUiTick() const
{
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
	{
		UI->PlaySound(EKartUISound::Tick);
	}
}

void UKartLobbyWidget::TakeInputFocus()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
}

// ───────────────────────────── selection ─────────────────────────────

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
		Subsystem->SelectGame(LobbyGameAt(Index));
		Refresh();
	}
}

void UKartLobbyWidget::StepGame(int32 Step)
{
	if (const URoomHostSubsystem* Subsystem = Room())
	{
		const int32 Count = LobbyGameCount();
		SelectGame((LobbyGameIndexOf(Subsystem->GetGameId()) + Step + Count) % Count);
	}
}

void UKartLobbyWidget::HandlePreviousGameClicked()
{
	StepGame(-1);
}

void UKartLobbyWidget::HandleNextGameClicked()
{
	StepGame(1);
}

void UKartLobbyWidget::HandleTrack0Clicked()
{
	SelectTrack(0);
}

void UKartLobbyWidget::HandleTrack1Clicked()
{
	SelectTrack(1);
}

// ───────────────────────────── the room's flow ─────────────────────────────

void UKartLobbyWidget::HandleStartClicked()
{
	if (URoomHostSubsystem* Subsystem = Room())
	{
		Subsystem->RequestStart();
	}
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
	if (UKartUISubsystem* UI = UKartUISubsystem::Get(this))
	{
		UI->SetMenuMusic(true);
	}
	if (!bAutoHide)
	{
		return;
	}
	SetVisibility(ESlateVisibility::Visible);
	ShowBackdrop();
	TakeInputFocus();
}

void UKartLobbyWidget::UpdatePartyContinue()
{
	// "Next game" on a party game's results queued a game: select it, and start as soon as the phones re-ready,
	// even with a single phone (the evening is already under way; nobody should have to reach for the keyboard).
	URoomHostSubsystem* Subsystem = Room();
	UArcadeSession* Session = UArcadeSession::Get(this);
	const UWorld* World = GetWorld();
	if (!Subsystem || !Session || !World || Subsystem->GetConnectionState() != EMcConnection::Open || Subsystem->GetRoom().Status != EMcRoomStatus::Lobby)
	{
		return;
	}
	if (Session->HasQueuedRoomGame())
	{
		Subsystem->SelectGame(Session->ConsumeQueuedRoomGame());
		bContinueParty = true;
		ContinueSince = World->GetRealTimeSeconds();
	}
	if (bContinueParty && Subsystem->GetRoom().GameId == Subsystem->GetGameId() && Subsystem->CanStart())
	{
		bContinueParty = false;
		Subsystem->RequestStart();
	}
	if (bContinueParty && World->GetRealTimeSeconds() - ContinueSince > ContinueTimeoutSeconds)
	{
		bContinueParty = false;
	}
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

// ───────────────────────────── the backdrop ─────────────────────────────

void UKartLobbyWidget::ShowBackdrop()
{
	// A slow orbit of the track behind the lobby, like the front end's menus. The race takes the view over itself.
	APlayerController* PC = GetOwningPlayer();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}
	if (!BackdropCamera)
	{
		BackdropCamera = World->SpawnActor<ACameraActor>();
		if (!BackdropCamera)
		{
			return;
		}
		BackdropCamera->GetCameraComponent()->SetFieldOfView(BackdropFieldOfView);
		BackdropCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	UpdateBackdrop(0.f);
	PC->SetViewTarget(BackdropCamera);
}

void UKartLobbyWidget::UpdateBackdrop(float DeltaSeconds)
{
	if (!BackdropCamera)
	{
		return;
	}
	BackdropClock += DeltaSeconds;
	if (!bBackdropBoundsFound)
	{
		// Looked up until the track exists (it usually does by the first frame), then kept.
		if (TActorIterator<ATrackBuilder> It{ GetWorld() })
		{
			if (const TSharedPtr<const MovecadeKart::FTrack> Track = It->GetTrack(); Track.IsValid() && Track->MinimapBounds.bIsValid)
			{
				BackdropBounds = Track->MinimapBounds;
				bBackdropBoundsFound = true;
			}
		}
	}
	const FBox2D& Bounds = bBackdropBoundsFound ? BackdropBounds : FallbackBounds;
	const double Extent = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y);
	const double Angle = BackdropClock * OrbitSpeed;
	const FVector2D Eye = Bounds.GetCenter() + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Extent * OrbitRadius;
	const FVector EyeUE = MovecadeKart::Coords::ToUE(Eye, Extent * OrbitHeight);
	const FVector Target = MovecadeKart::Coords::ToUE(Bounds.GetCenter(), 0.0);
	BackdropCamera->SetActorLocationAndRotation(EyeUE, (Target - EyeUE).Rotation());
}
