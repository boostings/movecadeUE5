#include "KartPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Input/DeviceControlsSettings.h"
#include "Input/KartDeviceControls.h"
#include "KartGameMode.h"

namespace
{
	/** The left stick ignores the first tenth of its travel on each axis (worn sticks drift). */
	constexpr float SteerDeadZone = 0.1f;
}

UInputAction* AKartPlayerController::MakeAction(const TCHAR* Name, bool bAxis)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = bAxis ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
	return Action;
}

void AKartPlayerController::BeginPlay()
{
	Super::BeginPlay();

	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Kart"));
	SteerAxis = MakeAction(TEXT("IA_SteerAxis"), true);
	SteerLeft = MakeAction(TEXT("IA_SteerLeft"), false);
	SteerRight = MakeAction(TEXT("IA_SteerRight"), false);
	Gas = MakeAction(TEXT("IA_Gas"), false);
	Brake = MakeAction(TEXT("IA_Brake"), false);
	Drift = MakeAction(TEXT("IA_Drift"), false);
	UseItem = MakeAction(TEXT("IA_UseItem"), false);
	LookBack = MakeAction(TEXT("IA_LookBack"), false);

	FEnhancedActionKeyMapping& Stick = Context->MapKey(SteerAxis, EKeys::Gamepad_LeftX);
	UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(this);
	DeadZone->LowerThreshold = SteerDeadZone;
	DeadZone->Type = EDeadZoneType::Axial;
	Stick.Modifiers.Add(DeadZone);

	for (const FKey& Key : { EKeys::A, EKeys::Left, EKeys::Gamepad_DPad_Left }) { Context->MapKey(SteerLeft, Key); }
	for (const FKey& Key : { EKeys::D, EKeys::Right, EKeys::Gamepad_DPad_Right }) { Context->MapKey(SteerRight, Key); }
	for (const FKey& Key : { EKeys::W, EKeys::Up, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_RightTriggerAxis }) { Context->MapKey(Gas, Key); }
	for (const FKey& Key : { EKeys::S, EKeys::Down, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_LeftTriggerAxis }) { Context->MapKey(Brake, Key); }
	for (const FKey& Key : { EKeys::SpaceBar, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftShoulder }) { Context->MapKey(Drift, Key); }
	for (const FKey& Key : { EKeys::E, EKeys::LeftShift, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top }) { Context->MapKey(UseItem, Key); }
	for (const FKey& Key : { EKeys::C, EKeys::Gamepad_RightThumbstick }) { Context->MapKey(LookBack, Key); }

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(Context, 0);
		}
	}
}

void AKartPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	UEnhancedPlayerInput* Enhanced = Cast<UEnhancedPlayerInput>(PlayerInput);
	if (!Enhanced || !Context)
	{
		return;
	}
	auto Held = [Enhanced](const UInputAction* Action) { return Enhanced->GetActionValue(Action).Get<bool>(); };

	// The sim steers left for positive input (the web convention); stick right is positive in UE.
	LocalInput = MovecadeKart::FKartInput();
	LocalInput.SteeringX = -Enhanced->GetActionValue(SteerAxis).Get<float>();
	LocalInput.bLeft = Held(SteerLeft);
	LocalInput.bRight = Held(SteerRight);
	LocalInput.bForward = Held(Gas);
	LocalInput.bBackward = Held(Brake);
	LocalInput.bJump = Held(Drift);
	LocalInput.bUseItem = Held(UseItem);
	bLookingBack = Held(LookBack);
	AddDeviceInput();
}

void AKartPlayerController::AddDeviceInput()
{
	// The first local player is the device itself on a phone build: its tilt steers (over the stick), its buttons add
	// to the keys. Same seat semantics as a room phone (FKartPhoneSeatInput).
	const AKartGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AKartGameMode>() : nullptr;
	MovecadeKart::Input::FKartDeviceSeat* Device = Mode && IsPrimaryPlayer() ? Mode->GetDeviceSeat() : nullptr;
	if (!Device)
	{
		return;
	}
	Device->SetSensitivity(UDeviceControlsSettings::Get()->GetSensitivity());
	const MovecadeKart::FKartInput Phone = Device->Read();
	if (Phone.SteeringX != 0.0)
	{
		LocalInput.SteeringX = Phone.SteeringX;
	}
	LocalInput.bForward |= Phone.bForward;
	LocalInput.bBackward |= Phone.bBackward;
	LocalInput.bJump |= Phone.bJump;
	// An ITEM press is held for a moment, as a key is: the race reads input on its own fixed steps and turns a held
	// item into one press, so a one-frame pulse could fall between two steps.
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (Phone.bUseItem)
	{
		DeviceItemHeldUntil = Now + DeviceItemHoldSeconds;
	}
	LocalInput.bUseItem |= Now < DeviceItemHeldUntil;
}
