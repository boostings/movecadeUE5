#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DeviceControlsSettings.generated.h"

/**
 * The player's on-device control settings, saved to GameUserSettings.ini ([/Script/MovecadeKart.DeviceControlsSettings]).
 * The touch overlay's sensitivity chips change them; FLocalDeviceController applies them.
 */
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class MOVECADEKART_API UDeviceControlsSettings : public UObject
{
	GENERATED_BODY()

public:
	static constexpr float MinSensitivity = 0.5f;
	static constexpr float MaxSensitivity = 2.f;
	static constexpr float SensitivityStep = 0.1f;

	/** Motion sensitivity: 1 plays exactly like a phone controller; kart steering uses it as the room's steering sensitivity. */
	UPROPERTY(Config)
	float Sensitivity = 1.f;

	/** Kart: steer by tilting the device (true) or with the on-screen arrows (false). Without motion sensors the
	 *  arrows show either way. */
	UPROPERTY(Config)
	bool bTiltSteering = true;

	static UDeviceControlsSettings* Get() { return GetMutableDefault<UDeviceControlsSettings>(); }
	/** Steps the sensitivity by Steps x SensitivityStep (clamped, rounded to the step) and saves. */
	void NudgeSensitivity(int32 Steps);
	/** Sets and saves the steering choice. */
	void SetTiltSteering(bool bTilt);
	float GetSensitivity() const { return FMath::Clamp(Sensitivity, MinSensitivity, MaxSensitivity); }
};
