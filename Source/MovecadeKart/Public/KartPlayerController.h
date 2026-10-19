#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "KartPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Local keyboard and gamepad input for testing without a phone. Bindings follow the web host
 * (MultiplayerApp.tsx:43-50, MultiplayerKart.tsx:908-943): W/Up gas, S/Down brake, A/D/Left/Right
 * steer (added on top of the stick), Space drift, plus a gamepad layout. The actions are built in
 * code, so no input assets are needed. R resets the kart to the grid.
 */
UCLASS()
class MOVECADEKART_API AKartPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void PlayerTick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;

private:
	UInputAction* MakeAction(const TCHAR* Name, bool bAxis);

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
	TObjectPtr<UInputAction> Reset;

	bool bResetHeld = false;
	/** -KartAutoDrive: steer with the web's Bullet Bill autopilot and hold gas (for automated checks). */
	bool bAutoDrive = false;
	/** -KartPhoneSeat=N (test hook): drive with room seat N's phone (URoomHostSubsystem::GetSeatInput) instead of the keyboard. */
	int32 PhoneSeat = INDEX_NONE;
	double PhoneLogTime = 0.0;
};
