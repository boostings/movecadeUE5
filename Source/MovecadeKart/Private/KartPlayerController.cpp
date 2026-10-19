#include "KartPlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Misc/CommandLine.h"
#include "Sim/KartTrack.h"

UInputAction* AKartPlayerController::MakeAction(const TCHAR* Name, bool bAxis)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = bAxis ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
	return Action;
}

void AKartPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bAutoDrive = FParse::Param(FCommandLine::Get(), TEXT("KartAutoDrive"));

	Context =NewObject<UInputMappingContext>(this, TEXT("IMC_Kart"));
	SteerAxis = MakeAction(TEXT("IA_SteerAxis"), true);
	SteerLeft = MakeAction(TEXT("IA_SteerLeft"), false);
	SteerRight = MakeAction(TEXT("IA_SteerRight"), false);
	Gas = MakeAction(TEXT("IA_Gas"), false);
	Brake = MakeAction(TEXT("IA_Brake"), false);
	Drift = MakeAction(TEXT("IA_Drift"), false);
	Reset = MakeAction(TEXT("IA_Reset"), false);

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
	for (const FKey& Key : { EKeys::R, EKeys::Gamepad_Special_Left }) { Context->MapKey(Reset, Key); }

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(Context, 0);
		}
	}
}

void AKartPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// Read input before the kart steps its sim this frame.
	if (AKartPawn* Kart = Cast<AKartPawn>(InPawn))
	{
		Kart->GetKartMovement()->AddTickPrerequisiteActor(this);
	}
}

void AKartPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	AKartPawn* Kart = Cast<AKartPawn>(GetPawn());
	UEnhancedPlayerInput* Enhanced = Cast<UEnhancedPlayerInput>(PlayerInput);
	if (!Kart || !Enhanced || !Context)
	{
		return;
	}
	auto Held = [Enhanced](const UInputAction* Action) { return Enhanced->GetActionValue(Action).Get<bool>(); };

	MovecadeKart::FKartInput Input;
	// The sim steers left for positive input (the web convention); stick right is positive in UE.
	Input.SteeringX = -Enhanced->GetActionValue(SteerAxis).Get<float>();
	Input.bLeft = Held(SteerLeft);
	Input.bRight = Held(SteerRight);
	Input.bForward = Held(Gas);
	Input.bBackward = Held(Brake);
	Input.bJump = Held(Drift);

	if (bAutoDrive)
	{
		// MultiplayerKart.tsx:996-1042: aim at waypoint nearest + 3, steer = clamp(angle error * 2.5, +-1).
		const UKartMovementComponent* Movement = Kart->GetKartMovement();
		const MovecadeKart::FTrack* Track = Movement->GetTrack();
		const MovecadeKart::FKartState& S = Movement->SimState();
		const int32 Count = Track->Waypoints.Num();
		const FVector2D Target = Track->Waypoints[(Track->NearestWaypoint(S.Pos) + 3) % Count];
		const FVector2D D = Target - S.Pos;
		const double Error = FMath::FindDeltaAngleRadians(S.Yaw, FMath::Atan2(-D.X, -D.Y));
		Input = MovecadeKart::FKartInput();
		Input.SteeringX = FMath::Clamp(Error * 2.5, -1.0, 1.0);
		Input.bForward = true;
	}
	Kart->GetKartMovement()->SetKartInput(Input);

	const bool bReset = Held(Reset);
	if (bReset && !bResetHeld)
	{
		Kart->GetKartMovement()->ResetToGrid();
		Kart->SnapCamera();
	}
	bResetHeld = bReset;
}
