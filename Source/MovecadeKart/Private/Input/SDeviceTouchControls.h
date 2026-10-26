#pragma once

#include "CoreMinimal.h"
#include "Input/DeviceControls.h"
#include "Mobile/MovecadeOrientation.h"
#include "Widgets/SLeafWidget.h"

class UDeviceControlsSubsystem;

/**
 * The on-screen half of the device controls, over the game: the scheme's buttons (A, B, gas, brake, drift, item),
 * the touch surface for its swipe fallback, and the chips along the top (pause, calibrate, sensitivity - / +).
 * Multi-touch: each finger keeps the control it landed on until it lifts. The mouse works the same way on the desktop.
 *
 * Every target is at least 44 pt: sizes are fractions of the screen's short side, and the smallest is 44 / 320 of it
 * (a 320 pt phone is the narrowest iOS screen). While the game's own menus are up the overlay hides and lets every
 * touch through to them. Owned by UDeviceControlsSubsystem (added to the viewport on Activate).
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

	/** The smallest target, as a share of the screen's short side: 44 pt on a 320 pt screen. */
	static constexpr float MinTargetShare = 44.f / 320.f;

	enum class ETarget : uint8 { None, Button, Surface, Pause, Calibrate, SensitivityDown, SensitivityUp };

	struct FHit
	{
		ETarget Target = ETarget::None;
		int32 Button = INDEX_NONE;
	};

	/** The control at a local point (buttons are hit a little outside their circle, as fingers are wide). */
	FHit HitAt(const FVector2D& Local, const FVector2D& Size) const;
	/** Centre and radius of button Index for a widget of Size. */
	void ButtonCircle(int32 Index, const FVector2D& Size, FVector2D& OutCentre, float& OutRadius) const;

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
	struct FChip
	{
		ETarget Target = ETarget::None;
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
	};

	void Chips(const FVector2D& Size, TArray<FChip, TInlineAllocator<4>>& Out) const;
	/** The safe area of a widget of Size: where every control goes. */
	void SafeRect(const FVector2D& Size, FVector2D& OutOrigin, FVector2D& OutSize) const;
	FReply Down(int32 Pointer, const FVector2D& Local, const FVector2D& Size);
	FReply Move(int32 Pointer, const FVector2D& Local, const FVector2D& Size);
	FReply Up(int32 Pointer, const FVector2D& Local, const FVector2D& Size);
	bool IsBlocked() const;

	TWeakObjectPtr<UDeviceControlsSubsystem> Owner;
	MovecadeKart::Input::FDeviceLayout Layout;
	/** What each finger (pointer index; the mouse is MousePointer) is holding. */
	TMap<int32, FHit> Held;
	/** Buttons down, for the pressed look. */
	TArray<bool> ButtonDown;
	/** The screen's safe area (UMovecadeOrientationSubsystem), refreshed each tick. */
	MovecadeKart::Mobile::FMovecadeSafeArea SafeArea;
	FString SensitivityLabel;
	double ShownSensitivity = -1.0;
};
