#include "Input/DeviceControlsSettings.h"

void UDeviceControlsSettings::NudgeSensitivity(int32 Steps)
{
	const float Next = FMath::Clamp(GetSensitivity() + Steps * SensitivityStep, MinSensitivity, MaxSensitivity);
	Sensitivity = FMath::RoundToFloat(Next / SensitivityStep) * SensitivityStep;
	SaveConfig();
}
