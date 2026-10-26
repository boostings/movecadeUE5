#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Sim/KartSim.h"
#include "KartPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * One local viewport, and for a Local seat its keyboard/gamepad input. Bindings follow the web host
 * (MultiplayerApp.tsx:43-50, MultiplayerKart.tsx:908-943): W/Up gas, S/Down brake, A/D/Left/Right
 * steer (added on top of the stick), Space drift, E/Left Shift item, C look behind, plus a gamepad layout. The actions are built in
 * code, so no input assets are needed. AKartRaceManager reads GetLocalInput each sim step.
 */
UCLASS()
class MOVECADEKART_API AKartPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void PlayerTick(float DeltaTime) override;

	const MovecadeKart::FKartInput& GetLocalInput() const { return LocalInput; }
	/** Held look-behind (C, right stick click): the pawn's camera turns round. Presentation only. */
	bool IsLookingBack() const { return bLookingBack; }

protected:
	virtual void BeginPlay() override;

private:
	UInputAction* MakeAction(const TCHAR* Name, bool bAxis);
	/** On-device controls (AKartGameMode::GetDeviceSeat): merged into LocalInput for the first local player. */
	void AddDeviceInput();

	UPROPERTY()
	TObjectPtr<UInputMappingContext> Context;

	UPROPERTY()
	TObjectPtr<UInputAction> SteerAxis;

	UPROPERTY()
	TObjectPtr<UInputAction> SteerLeft;

	UPROPERTY()
	TObjectPtr<UInputAction> SteerRight;

	UPROPERTY()
	TObjectPtr<UInputAction> Gas;

	UPROPERTY()
	TObjectPtr<UInputAction> Brake;

	UPROPERTY()
	TObjectPtr<UInputAction> Drift;

	UPROPERTY()
	TObjectPtr<UInputAction> UseItem;

	UPROPERTY()
	TObjectPtr<UInputAction> LookBack;

	MovecadeKart::FKartInput LocalInput;
	bool bLookingBack = false;
	/** The device's ITEM press reads as held until then (real seconds). */
	static constexpr double DeviceItemHoldSeconds = 0.1;
	double DeviceItemHeldUntil = -1.0;
};
