#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"

enum class EKartMenuAction : uint8
{
	None,
	Up,
	Down,
	Left,
	Right,
	Accept,
	Back,
	/** Gamepad Start: "press start", players 2..4 join. */
	Start,
	/** Q / E, the shoulder buttons: switch tabs. */
	PrevTab,
	NextTab,
	/** Y / R: a screen's secondary action (randomise, reset). */
	Alt,
	/** P, the gamepad View button. */
	Pause,
};

enum class EKartInputDevice : uint8
{
	Keyboard,
	Mouse,
	Gamepad,
};

struct FKartMenuEvent
{
	EKartMenuAction Action = EKartMenuAction::None;
	FKey Key;
	/** Slate user: 0 is the keyboard and the first gamepad, 1..3 the other gamepads. */
	int32 UserIndex = 0;
	EKartInputDevice Device = EKartInputDevice::Keyboard;
	bool bRepeat = false;
};

/** Anything that reacts to menu actions (a front-end screen, the race overlay). */
class IKartMenuInputTarget
{
public:
	virtual ~IKartMenuInputTarget() = default;
	/** Return true to consume the key (the game never sees it). */
	virtual bool HandleMenuEvent(const FKartMenuEvent& Event) = 0;
};

/**
 * Turns raw keys from every device into menu actions before Slate routes them, so menus work the same for
 * the keyboard, any gamepad (with its user index) and alongside the mouse, without depending on Slate focus
 * inside a game viewport. It also tracks the last device used, which the input hints follow.
 */
class MOVECADEKART_API FKartMenuInputProcessor : public IInputProcessor
{
public:
	TFunction<bool(const FKartMenuEvent&)> OnEvent;
	TFunction<void(EKartInputDevice)> OnDeviceChanged;

	static EKartMenuAction MapKey(const FKey& Key);
	/** Escape, P and both gamepad menu buttons. */
	static bool IsPauseKey(const FKey& Key);
	static bool IsDirectional(EKartMenuAction Action);

	EKartInputDevice GetDevice() const { return Device; }

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("KartMenuInput"); }

private:
	void SetDevice(EKartInputDevice NewDevice);

	EKartInputDevice Device = EKartInputDevice::Keyboard;
};
