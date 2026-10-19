#include "KartPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

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

	FEnhancedActionKeyMapping& Stick = Context->MapKey(SteerAxis, EKeys::Gamepad_LeftX);
	UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(this);
	DeadZone->LowerThreshold = 0.1f;
	DeadZone->Type = EDeadZoneType::Axial;
	Stick.Modifiers.Add(DeadZone);

	for (const FKey& Key : { EKeys::A, EKeys::Left, EKeys::Gamepad_DPad_Left }) { Context->MapKey(SteerLeft, Key); }
	for (const FKey& Key : { EKeys::D, EKeys::Right, EKeys::Gamepad_DPad_Right }) { Context->MapKey(SteerRight, Key); }
	for (const FKey& Key : { EKeys::W, EKeys::Up, EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_RightTriggerAxis }) { Context->MapKey(Gas, Key); }
	for (const FKey& Key : { EKeys::S, EKeys::Down, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_LeftTriggerAxis }) { Context->MapKey(Brake, Key); }
	for (const FKey& Key : { EKeys::SpaceBar, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftShoulder }) { Context->MapKey(Drift, Key); }
	for (const FKey& Key : { EKeys::E, EKeys::LeftShift, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top }) { Context->MapKey(UseItem, Key); }

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
}
