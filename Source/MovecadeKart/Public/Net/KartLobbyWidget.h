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
class UTextBlock;
class UVerticalBox;
class URoomHostSubsystem;

/**
 * The room lobby (web LobbyScreen.tsx, Docs/Port/04 §8.4): join code and QR, the four seats with
 * names and kart paint, the track picker, the start-blocked line / countdown, and a Start button.
 * Keys: Enter (or gamepad A) starts, Left/Right/Tab switch the track, Esc (or B) leaves the party for the menu.
 *
 * The widget tree is built in C++ when the class (or a Blueprint child with an empty tree, such as
 * /Game/UI/Lobby/WBP_Lobby) has none, so it works with no assets. It hides itself when a match
 * starts and comes back when the room returns to the lobby, unless bAutoHide is off.
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
	void BuildTree();
	void Refresh();
	void SelectTrack(int32 Index);
	/** Kart Racer or one of the ported party games (the room's gameId). */
	void SelectGame(int32 Index);
	/** The look: a slow orbit of the track behind the lobby. */
	void ShowBackdrop(bool bShow);
	void UpdateBackdrop(float DeltaSeconds);
	/** Esc / B: close the room and return to the main menu. */
	void LeaveParty();
	URoomHostSubsystem* Room() const;

	UFUNCTION()
	void HandleStartClicked();
	UFUNCTION()
	void HandleTrack0Clicked();
	UFUNCTION()
	void HandleTrack1Clicked();
	UFUNCTION()
	void HandleGame0Clicked();
	UFUNCTION()
	void HandleGame1Clicked();
	UFUNCTION()
	void HandleStartRequested(const FString& MapId);
	UFUNCTION()
	void HandleReturnedToLobby();

	UPROPERTY()
	TObjectPtr<UImage> QrImage;
	UPROPERTY()
	TObjectPtr<UTextBlock> CodeText;
	UPROPERTY()
	TObjectPtr<UTextBlock> UrlText;
	UPROPERTY()
	TObjectPtr<UTextBlock> ConnectionText;
	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY()
	TObjectPtr<UButton> StartButton;
	UPROPERTY()
	TObjectPtr<UTextBlock> StartLabel;
	UPROPERTY()
	TArray<TObjectPtr<UButton>> TrackButtons;
	UPROPERTY()
	TArray<TObjectPtr<UButton>> GameButtons;
	/** The track picker, collapsed for games without tracks. */
	UPROPERTY()
	TObjectPtr<UVerticalBox> TrackSection;
	UPROPERTY()
	TObjectPtr<UTextBlock> SubtitleText;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> SeatRows;
	UPROPERTY()
	TArray<TObjectPtr<UBorder>> SeatSwatches;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> SeatNames;
	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> SeatStates;

	UPROPERTY()
	TObjectPtr<UTexture2D> ShownQr;

	UPROPERTY()
	TObjectPtr<ACameraActor> BackdropCamera;

	double BackdropClock = 0.0;

	float RefreshAccumulator = 0.f;
	bool bBound = false;
};
