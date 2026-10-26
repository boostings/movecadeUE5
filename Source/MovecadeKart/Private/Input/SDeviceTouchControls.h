#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Input/DeviceTouchZones.h"
#include "Mobile/MovecadeOrientation.h"
#include "Widgets/SLeafWidget.h"

class UDeviceControlsSubsystem;

/**
 * The on-screen half of the device controls, over the game: the scheme's buttons (A, B, gas, brake, drift, item, the
 * kart's steering arrows), the touch surface for its swipe fallback, and the pause and settings chips (settings opens
 * calibrate, sensitivity - / + and, for the kart, tilt or on-screen steering).
 * Multi-touch: each finger keeps the control it landed on until it lifts. The mouse works the same way on the desktop.
 *
 * Where everything goes is MovecadeKart::Input::FDeviceTouchZones (public, so the HUDs can keep clear of it); every
 * target is at least 44 pt. While the game's own menus are up the overlay hides and lets every touch through to them.
 * A scheme whose layout changes while it runs (GetLayoutRevision) is picked up on the next tick. Owned by
 * UDeviceControlsSubsystem (added to the viewport on Activate).
 */
class SDeviceTouchControls : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SDeviceTouchControls) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UDeviceControlsSubsystem>, Owner)
		SLATE_ARGUMENT(MovecadeKart::Input::FDeviceLayout, Layout)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** What the game's menus need from the overlay: hide while one is up, and a pause chip that opens it. */
	TFunction<bool()> IsMenuOpen;
	TFunction<void()> OpenPause;

	// SWidget
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& Event) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	using ETarget = MovecadeKart::Input::ETouchTarget;

	struct FHit
	{
		ETarget Target = ETarget::None;
		int32 Button = INDEX_NONE;
	};

	/** Every control for a widget of Size, with this tick's safe area and settings row. */
	MovecadeKart::Input::FDeviceTouchZones Zones(const FVector2D& Size) const;
	/** The control at a local point. */
	FHit HitAt(const FVector2D& Local, const FVector2D& Size) const;
	FReply Down(int32 Pointer, const FVector2D& Local, const FVector2D& Size);
	FReply Move(int32 Pointer, const FVector2D& Local, const FVector2D& Size);
	FReply Up(int32 Pointer, const FVector2D& Local, const FVector2D& Size);
	void ReleaseAll(const FVector2D& Size);
	bool IsBlocked() const;
	bool HasSteeringChoice() const;

	TWeakObjectPtr<UDeviceControlsSubsystem> Owner;
	MovecadeKart::Input::FDeviceLayout Layout;
	/** The scheme's layout revision Layout was copied at. */
	uint32 LayoutRevision = 0;
	/** What each finger (pointer index; the mouse is MousePointer) is holding. */
	TMap<int32, FHit> Held;
	/** Buttons down, for the pressed look. */
	TArray<bool> ButtonDown;
	/** The screen's safe area (UMovecadeOrientationSubsystem), refreshed each tick. */
	MovecadeKart::Mobile::FMovecadeSafeArea SafeArea;
	FString SensitivityLabel;
	/** The settings chip opened the calibrate / sensitivity row. */
	bool bSettingsOpen = false;
	/** On-screen steering is up (refreshed each tick), for the steering chip's label. */
	bool bTouchSteering = false;
	bool bMotionAvailable = true;
	/** Seconds the controls have been up (frame time), for the hint. */
	double HintAge = 0.0;
	double ShownSensitivity = -1.0;
};
