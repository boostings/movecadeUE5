#pragma once

#include "CoreMinimal.h"
#include "UI/KartScreen.h"

class AKartFrontEnd;
class AKartRaceManager;

/** Factories for every menu screen (front end, settings, race pause and results). */
namespace KartScreens
{
	// Front end (KartFrontEndScreens.cpp)
	TSharedRef<FKartScreen> MakeTitle(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeMainMenu(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeRacers(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeRules(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeTracks(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeCredits();

	/** A yes/no dialog over the current screen. No has focus first. */
	TSharedRef<FKartScreen> MakeConfirm(const FText& Title, const FText& Body, const FText& Yes, const FText& No, TFunction<void()> OnYes);

	// Settings (KartSettingsScreen.cpp): 0 Graphics, 1 Audio, 2 Controls.
	TSharedRef<FKartScreen> MakeSettings(int32 Tab = 0);

	// Race (KartRaceScreens.cpp)
	TSharedRef<FKartScreen> MakePause(AKartRaceManager* Race);
	TSharedRef<FKartScreen> MakeResults(AKartRaceManager* Race);
}
