#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/KartMenuInput.h"
#include "KartUISubsystem.generated.h"

class SKartCurtain;
class UAudioComponent;
class USoundClass;
class USoundMix;
class USoundWave;

enum class EKartUISound : uint8
{
	Move,
	Tick,
	Accept,
	Back,
	Error,
	Start,
	Go,
	Pause,
	Resume,
	Whoosh,
};

/**
 * The UI's services that outlive a level: menu input routing (FKartMenuInputProcessor, to the topmost
 * IKartMenuInputTarget), the active input device for button hints, UI sounds and the menu music, the volume
 * mix, the curtain wipe across level travel, and the -KartUiShots screenshot tour.
 */
UCLASS()
class MOVECADEKART_API UKartUISubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UKartUISubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Input -------------------------------------------------------------------------------------------
	void PushInputTarget(const TSharedRef<IKartMenuInputTarget>& Target);
	void PopInputTarget(const TSharedRef<IKartMenuInputTarget>& Target);
	EKartInputDevice GetInputDevice() const { return Processor.IsValid() ? Processor->GetDevice() : EKartInputDevice::Keyboard; }
	bool IsUsingGamepad() const { return GetInputDevice() == EKartInputDevice::Gamepad; }

	// Sound -------------------------------------------------------------------------------------------
	void PlaySound(EKartUISound Sound);
	void SetMenuMusic(bool bPlaying);
	/** Master x music / master x SFX into the mix; call after the prefs change. */
	void ApplyVolumes();

	// Flow --------------------------------------------------------------------------------------------
	/** Wipes the curtain closed, then opens Level; the curtain opens again once it has loaded. */
	void TravelTo(const FString& Level);
	bool IsTravelling() const { return bTravelling; }
	/** Quits the game (after the curtain closes). */
	void QuitGame();

	// -KartUiShots=title,menu,... -----------------------------------------------------------------------
	bool IsTourActive() const { return TourSteps.Num() > 0 || TourIndex >= 0; }
	/** The step the tour is on, or NAME_None. */
	FName GetTourStep() const;
	/** Captures Saved/Screenshots/<platform>/KartUi_<Name>_<W>x<H>.png and moves the tour on. */
	void TourShot(FName Name);

private:
	void HandleMapLoaded(UWorld* World);
	bool HandleEvent(const FKartMenuEvent& Event);
	bool TickTour(float DeltaTime);
	UWorld* GetGameWorld() const;
	USoundWave* LoadSound(EKartUISound Sound);
	void ShowCurtain(bool bClosing, TFunction<void()> OnClosed = nullptr);

	TSharedPtr<FKartMenuInputProcessor> Processor;
	TArray<TWeakPtr<IKartMenuInputTarget>> Targets;
	TSharedPtr<SKartCurtain> Curtain;
	FTSTicker::FDelegateHandle TourTicker;
	FDelegateHandle MapLoadedHandle;

	UPROPERTY()
	TMap<uint8, TObjectPtr<USoundWave>> Sounds;

	UPROPERTY()
	TObjectPtr<USoundWave> MusicWave;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Music;

	UPROPERTY()
	TObjectPtr<USoundMix> VolumeMix;

	UPROPERTY()
	TObjectPtr<USoundClass> MusicClass;

	UPROPERTY()
	TObjectPtr<USoundClass> UIClass;

	bool bTravelling = false;
	bool bCurtainOnLoad = false;
	bool bWantMusic = false;
	TArray<FName> TourSteps;
	int32 TourIndex = -1;
	double TourStepStarted = 0.0;
};
