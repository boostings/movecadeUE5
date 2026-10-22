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
	/** The party-game picker (Arcade on the main menu). */
	TSharedRef<FKartScreen> MakeArcade(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeRacers(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeRules(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeTracks(AKartFrontEnd* FrontEnd);
	TSharedRef<FKartScreen> MakeCredits();

	// First launch (KartWelcomeScreen.cpp): the one-time "How Movecade works" card between the title and the menu.
	TSharedRef<FKartScreen> MakeWelcome(AKartFrontEnd* FrontEnd);
	/** True until the player has seen the welcome card (UKartPlayerPrefs::bSeenHowItWorks). */
	bool ShouldShowWelcome();

	// Store (KartStoreScreen.cpp): the cosmetics store. TourStep (-KartUiShots): store, storeparty, storefree, storebuy, storedone.
	TSharedRef<FKartScreen> MakeStore(AKartFrontEnd* FrontEnd, FName TourStep = NAME_None);
	/** The main menu card's icon for Store. */
	void PaintStoreIcon(const FKartPainter& P, const FVector2D& Centre, float Size, double Now);

	/** A yes/no dialog over the current screen. No has focus first; Back is No. Each callback runs after the dialog closes. */
	TSharedRef<FKartScreen> MakeConfirm(const FText& Title, const FText& Body, const FText& Yes, const FText& No, TFunction<void()> OnYes, TFunction<void()> OnNo = nullptr);

	// Settings (KartSettingsScreen.cpp): 0 Graphics, 1 Audio, 2 Controls, 3 Accessibility.
	TSharedRef<FKartScreen> MakeSettings(int32 Tab = 0);

	// Race (KartRaceScreens.cpp)
	TSharedRef<FKartScreen> MakePause(AKartRaceManager* Race);
	TSharedRef<FKartScreen> MakeResults(AKartRaceManager* Race);
}
