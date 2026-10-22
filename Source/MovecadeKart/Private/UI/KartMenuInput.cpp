#include "UI/KartMenuInput.h"

#include "Input/Events.h"

EKartMenuAction FKartMenuInputProcessor::MapKey(const FKey& Key)
{
	struct FBinding
	{
		FKey Key;
		EKartMenuAction Action;
	};
	static const FBinding Bindings[] = {
		{ EKeys::Up, EKartMenuAction::Up }, { EKeys::W, EKartMenuAction::Up },
		{ EKeys::Gamepad_DPad_Up, EKartMenuAction::Up }, { EKeys::Gamepad_LeftStick_Up, EKartMenuAction::Up },
		{ EKeys::Down, EKartMenuAction::Down }, { EKeys::S, EKartMenuAction::Down },
		{ EKeys::Gamepad_DPad_Down, EKartMenuAction::Down }, { EKeys::Gamepad_LeftStick_Down, EKartMenuAction::Down },
		{ EKeys::Left, EKartMenuAction::Left }, { EKeys::A, EKartMenuAction::Left },
		{ EKeys::Gamepad_DPad_Left, EKartMenuAction::Left }, { EKeys::Gamepad_LeftStick_Left, EKartMenuAction::Left },
		{ EKeys::Right, EKartMenuAction::Right }, { EKeys::D, EKartMenuAction::Right },
		{ EKeys::Gamepad_DPad_Right, EKartMenuAction::Right }, { EKeys::Gamepad_LeftStick_Right, EKartMenuAction::Right },
		{ EKeys::Enter, EKartMenuAction::Accept }, { EKeys::SpaceBar, EKartMenuAction::Accept },
		{ EKeys::Gamepad_FaceButton_Bottom, EKartMenuAction::Accept },
		{ EKeys::Escape, EKartMenuAction::Back }, { EKeys::BackSpace, EKartMenuAction::Back },
		{ EKeys::Gamepad_FaceButton_Right, EKartMenuAction::Back },
		{ EKeys::Gamepad_Special_Right, EKartMenuAction::Start },
		{ EKeys::Q, EKartMenuAction::PrevTab }, { EKeys::Gamepad_LeftShoulder, EKartMenuAction::PrevTab },
		{ EKeys::E, EKartMenuAction::NextTab }, { EKeys::Gamepad_RightShoulder, EKartMenuAction::NextTab },
		{ EKeys::R, EKartMenuAction::Alt }, { EKeys::Gamepad_FaceButton_Top, EKartMenuAction::Alt },
		{ EKeys::P, EKartMenuAction::Pause }, { EKeys::Gamepad_Special_Left, EKartMenuAction::Pause },
	};
	for (const FBinding& Binding : Bindings)
	{
		if (Binding.Key == Key)
		{
			return Binding.Action;
		}
	}
	return EKartMenuAction::None;
}

bool FKartMenuInputProcessor::IsPauseKey(const FKey& Key)
{
	return Key == EKeys::Escape || Key == EKeys::P || Key == EKeys::Gamepad_Special_Left || Key == EKeys::Gamepad_Special_Right;
}

bool FKartMenuInputProcessor::IsDirectional(EKartMenuAction Action)
{
	return Action == EKartMenuAction::Up || Action == EKartMenuAction::Down || Action == EKartMenuAction::Left || Action == EKartMenuAction::Right;
}

void FKartMenuInputProcessor::SetDevice(EKartInputDevice NewDevice)
{
	if (NewDevice != Device && !bDeviceLocked)
	{
		Device = NewDevice;
		if (OnDeviceChanged)
		{
			OnDeviceChanged(Device);
		}
	}
}

bool FKartMenuInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	SetDevice(Key.IsGamepadKey() ? EKartInputDevice::Gamepad : EKartInputDevice::Keyboard);
	FKartMenuEvent Event;
	Event.Action = MapKey(Key);
	Event.Key = Key;
	Event.UserIndex = InKeyEvent.GetUserIndex();
	Event.Device = Device;
	Event.bRepeat = InKeyEvent.IsRepeat();
	// Held keys repeat for navigation only (a held Accept must not fire twice).
	if (Event.bRepeat && !IsDirectional(Event.Action))
	{
		Event.Action = EKartMenuAction::None;
	}
	return OnEvent ? OnEvent(Event) : false;
}

bool FKartMenuInputProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	// A stick or trigger counts as gamepad use once it is pushed past a dead zone (resting drift stays quiet).
	constexpr float DeviceSwitchDeadZone = 0.45f;
	if (FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > DeviceSwitchDeadZone)
	{
		SetDevice(EKartInputDevice::Gamepad);
	}
	return false;
}

bool FKartMenuInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetCursorDelta().SizeSquared() > 4.f)
	{
		SetDevice(EKartInputDevice::Mouse);
	}
	return false;
}

bool FKartMenuInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	SetDevice(EKartInputDevice::Mouse);
	return false;
}
