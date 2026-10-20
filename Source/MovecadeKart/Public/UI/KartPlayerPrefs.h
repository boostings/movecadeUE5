#pragma once

#include "CoreMinimal.h"
#include "UI/KartSessionSettings.h"
#include "UObject/Object.h"
#include "KartPlayerPrefs.generated.h"

/**
 * The player's own settings that UGameUserSettings doesn't hold: volumes and the last race setup.
 * Saved to GameUserSettings.ini
 * ([/Script/MovecadeKart.KartPlayerPrefs]); read through GetDefault / GetMutableDefault.
 */
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class MOVECADEKART_API UKartPlayerPrefs : public UObject
{
	GENERATED_BODY()

public:
	/** 0..1 each. Master scales the other two. */
	UPROPERTY(Config)
	float MasterVolume = 1.f;

	UPROPERTY(Config)
	float MusicVolume = 0.6f;

	/** Race sounds and menu sounds. */
	UPROPERTY(Config)
	float SfxVolume = 1.f;

	UPROPERTY(Config)
	FKartRaceSetup LastRace;

	void Save() { SaveConfig(); }
};
