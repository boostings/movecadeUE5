#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MobilePrefs.generated.h"

/**
 * The phone app's own settings, in GameUserSettings.ini ([/Script/MovecadeKart.MobilePrefs]) beside UKartPlayerPrefs.
 * Read with GetDefault, change with GetMutableDefault and Save(). The haptics switch is
 * MovecadeKart::Mobile::FHaptics::SetEnabled (which saves); the settings screen's row calls that.
 */
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class MOVECADEKART_API UMobilePrefs : public UObject
{
	GENERATED_BODY()

public:
	/** Phone vibration on hits, throws and landings. */
	UPROPERTY(Config)
	bool bHapticsEnabled = true;

	/** Set once the first launch reached the front end (UMobileLifecycleSubsystem::OnReadyForOnboarding). */
	UPROPERTY(Config)
	bool bLaunchedBefore = false;

	void Save() { SaveConfig(); }
};
