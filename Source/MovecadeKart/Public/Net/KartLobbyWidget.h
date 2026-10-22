#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Net/RoomTypes.h"
#include "KartLobbyWidget.generated.h"

class ACameraActor;
class APlayerController;
class UBorder;
class UButton;
class UImage;
class UOverlay;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UWidget;
class URoomHostSubsystem;
namespace MovecadeKart::Lobby { class FWidgetFactory; }

/**
 * The room lobby (web LobbyScreen.tsx, Docs/Port/04 §8.4): join code and QR, the four seats with
 * names and kart paint, the game carousel and track picker, the start-blocked line / countdown, and a Start button.
 * Keys: Enter (or gamepad A) starts, Up/Down switch the game, Left/Right/Tab the track, Esc (or B) leaves the party.
 *
 * The widget tree is built in C++ when the class (or a Blueprint child with an empty tree, such as
 * /Game/UI/Lobby/WBP_Lobby) has none, so it works with no assets (KartLobbyLayout.cpp). Its text follows the room
 * (URoomHostSubsystem::GetLobbyRevision) rather than a timer (KartLobbyView.cpp). It hides itself when a match
 * starts and comes back when the room returns to the lobby, unless bAutoHide is off. Created by Show(), owned by
 * the viewport.
 */
UCLASS()
class MOVECADEKART_API UKartLobbyWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Creates the lobby (WBP_Lobby if it exists, else this class), adds it to PC's viewport and opens the room. */
	static UKartLobbyWidget* Show(APlayerController* PC);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lobby")
	bool bAutoHide = true;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	// KartLobbyLayout.cpp: the widget tree.
	void BuildTree();
	void BuildBackdrop(const MovecadeKart::Lobby::FWidgetFactory& Make, UOverlay& Root);
	UWidget* BuildJoinCard(const MovecadeKart::Lobby::FWidgetFactory& Make);
	UWidget* BuildGamePicker(const MovecadeKart::Lobby::FWidgetFactory& Make);
	UWidget* BuildTrackPicker(const MovecadeKart::Lobby::FWidgetFactory& Make);
	void AddSeats(const MovecadeKart::Lobby::FWidgetFactory& Make, UVerticalBox& Column);
	UWidget* BuildFooter(const MovecadeKart::Lobby::FWidgetFactory& Make);

	// KartLobbyView.cpp: the text and colours, from the room.
	/** Refreshes when the room's lobby revision or the countdown's second moved since the last refresh. */
	void RefreshIfChanged();
	void Refresh();
	void RefreshJoinCard(const URoomHostSubsystem& Room);
	void RefreshGameCard(const URoomHostSubsystem& Room);
	void RefreshTracks(const FMcRoomState& State);
	void RefreshSeats(const FMcRoomState& State);
	void RefreshFooter(const URoomHostSubsystem& Room, const FMcRoomState& State);

	// KartLobbyWidget.cpp: flow, input and the backdrop.
	URoomHostSubsystem* Room() const;
	void SelectTrack(int32 Index);
	/** Kart Racer or one of the ported party games (the room's gameId), by carousel index. */
	void SelectGame(int32 Index);
	/** The carousel: the game before (-1) or after (+1) the room's. */
	void StepGame(int32 Step);
	/** "Next game" on a party game's results queued one: select it and start once the phones are ready. */
	void UpdatePartyContinue();
	/** Esc / B: close the room and return to the main menu. */
	void LeaveParty();
	/** Mouse and focus for the lobby: game and UI, cursor shown. */
	void TakeInputFocus();
	/** The look: a slow orbit of the track behind the lobby. */
	void ShowBackdrop();
	void UpdateBackdrop(float DeltaSeconds);
	void PlayUiTick() const;

	UFUNCTION()
	void HandleStartClicked();
	UFUNCTION()
	void HandleTrack0Clicked();
	UFUNCTION()
	void HandleTrack1Clicked();
	UFUNCTION()
	void HandlePreviousGameClicked();
	UFUNCTION()
	void HandleNextGameClicked();
	UFUNCTION()
	void HandleStartRequested(const FString& MapId);
	UFUNCTION()
	void HandleReturnedToLobby();

	// The join card.
	UPROPERTY()
	TObjectPtr<UImage> QrImage;
	UPROPERTY()
	TObjectPtr<UTextBlock> CodeText;
	UPROPERTY()
	TObjectPtr<UTextBlock> UrlText;
	UPROPERTY()
	TObjectPtr<UTextBlock> ConnectionText;
	UPROPERTY()
	TObjectPtr<UTexture2D> ShownQr;

	// The game carousel's card: the accent bar, name, tagline and position.
	UPROPERTY()
	TObjectPtr<UTextBlock> SubtitleText;
	UPROPERTY()
	TObjectPtr<UBorder> GameAccent;
	UPROPERTY()
	TObjectPtr<UTextBlock> GameName;
	UPROPERTY()
	TObjectPtr<UTextBlock> GameTagline;
	UPROPERTY()
	TObjectPtr<UTextBlock> GameCount;

	/** The track picker, collapsed for games without tracks. */
	UPROPERTY()
	TObjectPtr<UVerticalBox> TrackSection;
	UPROPERTY()
	TArray<TObjectPtr<UButton>> TrackButtons;

	// The seats, P1..P4.
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> SeatRows;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> SeatSwatches;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> SeatNames;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> SeatStates;

	// The footer.
	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY()
	TObjectPtr<UButton> StartButton;
	UPROPERTY()
	TObjectPtr<UTextBlock> StartLabel;

	UPROPERTY()
	TObjectPtr<ACameraActor> BackdropCamera;
	double BackdropClock = 0.0;
	/** The orbited track's extent, found once (the track never changes under a lobby). */
	FBox2D BackdropBounds = FBox2D(ForceInit);
	bool bBackdropBoundsFound = false;

	/** What the text shows: the room's lobby revision and the countdown's second when it last refreshed. */
	uint32 ShownRevision = 0;
	int32 ShownCountdownSecond = INDEX_NONE;
	bool bShownOnce = false;

	/** A queued next game is selected and should start once the phones are ready. */
	bool bContinueParty = false;
	double ContinueSince = 0.0;
	bool bBound = false;
};
