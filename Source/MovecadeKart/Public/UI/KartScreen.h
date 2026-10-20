#pragma once

#include "CoreMinimal.h"
#include "UI/KartMenuInput.h"
#include "UI/KartUIPainter.h"
#include "UI/KartUISubsystem.h"
#include "Widgets/SLeafWidget.h"

class FKartScreen;
class UWorld;

/** One entry in the bottom-right button hint bar. */
struct FKartHint
{
	EKartMenuAction Action = EKartMenuAction::Accept;
	FText Label;
};

/** A selection index over Count items that wraps (or clamps) and reports whether it moved. */
struct MOVECADEKART_API FKartMenuCursor
{
	int32 Index = 0;
	int32 Count = 0;
	bool bWrap = true;

	/** Moves by Delta; returns true if the index changed. */
	bool Move(int32 Delta);
	/** Sets the index (clamped); returns true if it changed. */
	bool Set(int32 NewIndex);
	/** Cycles a value in [0, Num) by Delta, wrapping. */
	static int32 Cycle(int32 Value, int32 Delta, int32 Num);
};

/** Per-frame state handed to a screen's Paint. */
struct FKartScreenFrame
{
	FVector2D Size = FVector2D::ZeroVector;
	/** The action-safe area (TV safe zone) inside Size. */
	FVector2D SafeMin = FVector2D::ZeroVector;
	FVector2D SafeMax = FVector2D::ZeroVector;
	/** Seconds since the screen was shown (or last came back to the top). */
	double Age = 0.0;
	/** 0..1 entering, 1 settled; 1..0 while leaving. */
	float Presence = 1.f;
	/** +1 when the motion runs forward (push), -1 backward (pop). */
	float Direction = 1.f;
	EKartInputDevice Device = EKartInputDevice::Keyboard;
	double Now = 0.0;

	FVector2D SafeSize() const { return SafeMax - SafeMin; }
	FVector2D Centre() const { return Size * 0.5; }
};

/** Where screens live: a stack with transitions, sounds and the world. */
class IKartScreenHost
{
public:
	virtual ~IKartScreenHost() = default;
	virtual void PushScreen(const TSharedRef<FKartScreen>& Screen) = 0;
	virtual void PopScreen() = 0;
	/** Pops back until Screen is on top. */
	virtual void PopTo(const FKartScreen* Screen) = 0;
	virtual void ReplaceScreen(const TSharedRef<FKartScreen>& Screen) = 0;
	virtual UWorld* GetWorld() const = 0;
	virtual void PlaySound(EKartUISound Sound) const = 0;
	virtual EKartInputDevice GetDevice() const = 0;
};

/**
 * A menu screen, painted immediate-mode by FKartPainter. Screens own their selection state and react to
 * menu actions (keyboard, any gamepad, by user index) and to the mouse through the hit regions they paint.
 */
class MOVECADEKART_API FKartScreen : public TSharedFromThis<FKartScreen>
{
public:
	virtual ~FKartScreen() = default;

	virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) = 0;
	/** Return true if handled. Unhandled Back pops the screen (unless CanGoBack is false). */
	virtual bool HandleEvent(const FKartMenuEvent& Event) { return false; }
	virtual void OnHover(int32 HitId) {}
	virtual void OnClick(int32 HitId) {}
	virtual void OnShown() {}
	virtual TArray<FKartHint> GetHints() const;
	virtual bool CanGoBack() const { return true; }
	/** A dialog keeps the screen under it painted. */
	virtual bool IsModal() const { return false; }
	virtual FName GetId() const = 0;

	IKartScreenHost* Host = nullptr;
};

/**
 * The widget that hosts a stack of FKartScreens: paints the top one (and the one leaving, during a
 * transition), the hint bar, routes menu input and mouse hover/clicks to the top screen. Used by the front
 * end and by the race overlay's pause, settings and results.
 */
class MOVECADEKART_API SKartScreenStack : public SLeafWidget, public IKartScreenHost, public IKartMenuInputTarget
{
public:
	SLATE_BEGIN_ARGS(SKartScreenStack) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UWorld>, World)
		/** Paints under the screens (backdrop tints, decorations). */
		SLATE_ARGUMENT(TFunction<void(const FKartPainter&, const FKartScreenFrame&)>, Background)
		SLATE_ARGUMENT(bool, ShowHints)
	SLATE_END_ARGS()

	SKartScreenStack();
	void Construct(const FArguments& InArgs);

	const TSharedPtr<FKartScreen>& Top() const;
	int32 Depth() const { return Stack.Num(); }
	void Clear();
	/** Called when Back is pressed on the bottom screen. */
	TFunction<void()> OnBackAtRoot;

	// IKartScreenHost
	virtual void PushScreen(const TSharedRef<FKartScreen>& Screen) override;
	virtual void PopScreen() override;
	virtual void PopTo(const FKartScreen* Screen) override;
	virtual void ReplaceScreen(const TSharedRef<FKartScreen>& Screen) override;
	virtual UWorld* GetWorld() const override { return World.Get(); }
	virtual void PlaySound(EKartUISound Sound) const override;
	virtual EKartInputDevice GetDevice() const override;

	// IKartMenuInputTarget
	virtual bool HandleMenuEvent(const FKartMenuEvent& Event) override;

	// SWidget
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.0, 1080.0); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }

	static constexpr double TransitionSeconds = 0.32;

private:
	void Show(const TSharedRef<FKartScreen>& Screen, float Direction);
	int32 HitAt(const FVector2D& Local) const;

	TWeakObjectPtr<UWorld> World;
	TFunction<void(const FKartPainter&, const FKartScreenFrame&)> Background;
	bool bShowHints = true;

	TArray<TSharedPtr<FKartScreen>> Stack;
	/** The screen sliding out, and when the current one came in. */
	TSharedPtr<FKartScreen> Leaving;
	double ShownAt = 0.0;
	float Direction = 1.f;
	mutable TArray<FKartHitRegion> Hits;
	int32 HoverId = INDEX_NONE;
	int32 PressedId = INDEX_NONE;
	mutable FVector2D LastSize = FVector2D(1920.0, 1080.0);
};
