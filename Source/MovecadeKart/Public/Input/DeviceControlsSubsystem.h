#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceControlsFlags.h"
#include "Input/DeviceMotionSim.h"
#include "Input/DeviceTouchZones.h"
#include "Subsystems/WorldSubsystem.h"
#include "DeviceControlsSubsystem.generated.h"

class FDeviceMotionInputProcessor;
class SDeviceTouchControls;
class SWidget;

/**
 * The on-device controls for one world: the device player's FLocalDeviceController, fed by the motion sensors (a Slate
 * input preprocessor catching FMotionEvent), the touch overlay (SDeviceTouchControls on the game viewport) and, off
 * Shipping, the simulator or a replay. Idle until a game calls Activate with its scheme and sink; a level change ends
 * it with the world.
 *
 * Who activates: AArcadeGameModeBase for the party games (when the launch has a device seat, see
 * FArcadeLaunchOptions::bDeviceSeat), AKartGameMode for a local race. FDeviceControlsFlags says whether this run has
 * device controls at all.
 */
UCLASS()
class MOVECADEKART_API UDeviceControlsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDeviceControlsSubsystem* Get(const UObject* WorldContext);
	/** This run plays with the device's own controls (FDeviceControlsFlags). */
	static bool IsEnabled() { return MovecadeKart::Input::FDeviceControlsFlags::Get().bEnabled; }

	/**
	 * Starts the device player on Scheme, delivering its traffic to Sink. Replaces any scheme already running.
	 * IsMenuOpen hides the touch overlay while the game's menus are up; OpenPause is its pause chip.
	 */
	void Activate(TSharedRef<MovecadeKart::Input::IDeviceControlScheme> Scheme, TSharedRef<MovecadeKart::Input::IDeviceInputSink> Sink,
		TFunction<bool()> IsMenuOpen = nullptr, TFunction<void()> OpenPause = nullptr);
	void Deactivate();
	bool IsActive() const { return Controller.IsValid(); }

	/** The calibrate button: the device's next pose is neutral. */
	void Calibrate();
	void NudgeSensitivity(int32 Steps);
	double GetSensitivity() const;

	/** The running scheme, or null. */
	MovecadeKart::Input::IDeviceControlScheme* GetScheme() const { return Controller ? &Controller->GetScheme() : nullptr; }

	/**
	 * Where the running scheme's touch controls sit on a widget of Size whose unsafe border is Insets (same units):
	 * what SDeviceTouchControls draws and hit-tests, and what the HUDs keep clear of. Empty when inactive.
	 */
	MovecadeKart::Input::FDeviceTouchZones GetTouchZones(const FVector2D& Size, const FMargin& Insets, bool bSettingsOpen = false) const;

	/** The running scheme lets the player choose tilt or on-screen steering (the kart). */
	bool HasSteeringChoice() const;
	/** On-screen steering is up (the player's choice, or the device sends no motion). */
	bool IsTouchSteering() const;
	/** The device's motion sensors are reporting. */
	bool IsMotionAvailable() const;
	/** The player's steering choice: saved, and applied at once to a running scheme. */
	void SetTiltSteering(bool bTilt);

	// What the input preprocessor and the touch overlay call.
	void HandleMotion(const MovecadeKart::Input::FEngineMotionReading& Reading);
	void HandleButton(MovecadeKart::Input::EDeviceButton Button, bool bDown);
	void HandleTouch(const MovecadeKart::Input::FDeviceTouch& Touch);
	double Now() const;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void ShowOverlay();
	void HideOverlay();
	void StartMotionSource();
	void StepScript();
	void StepReplay();
	void WriteRecording();

	TUniquePtr<MovecadeKart::Input::FLocalDeviceController> Controller;
	TSharedPtr<FDeviceMotionInputProcessor> MotionProcessor;
	TSharedPtr<SDeviceTouchControls> Overlay;
	TSharedPtr<SWidget> OverlayRoot;
	TFunction<bool()> MenuOpenQuery;
	TFunction<void()> PauseAction;
	MovecadeKart::Input::FMotionConvention Convention;
	double ActivatedAt = 0.0;

	// The desktop test path (-DeviceMotion=sim, -DeviceMotionReplay=, -DeviceMotionRecord=).
	TSharedPtr<MovecadeKart::Input::IDeviceMotionScript> Script;
	MovecadeKart::Input::FDeviceRecording Replay;
	int32 ReplayNext = 0;
	bool bRecording = false;
	MovecadeKart::Input::FDeviceRecording Recording;
};
