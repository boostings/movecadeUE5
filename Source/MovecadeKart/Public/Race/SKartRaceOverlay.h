#pragma once

#include "CoreMinimal.h"
#include "UI/KartMenuInput.h"
#include "Widgets/SCompoundWidget.h"

class AKartRaceManager;
class SKartRaceHud;
class SKartScreenStack;

/**
 * The race's UI layer (spec 04 §8): the in-race HUD for every view (SKartRaceHud: place, lap, speed, item
 * roulette, minimap, countdown, banners, status), and the race menus in an SKartScreenStack (pause,
 * settings, results). It takes over the race manager's host keys (pause, skip intro, results) through the
 * UI input router; with no UI subsystem (headless tests) the race manager keeps handling them itself.
 */
class MOVECADEKART_API SKartRaceOverlay : public SCompoundWidget, public IKartMenuInputTarget
{
public:
	SLATE_BEGIN_ARGS(SKartRaceOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AKartRaceManager>, Manager)
	SLATE_END_ARGS()

	virtual ~SKartRaceOverlay() override;
	void Construct(const FArguments& InArgs);

	/** The overlay of the race in progress, if any. */
	static TSharedPtr<SKartRaceOverlay> GetActive();

	void OpenPauseMenu();
	/** Clears the menus; bResume also unpauses the race. */
	void ClosePauseMenu(bool bResume = true);
	void CloseMenus();
	bool AreMenusOpen() const;

	// IKartMenuInputTarget
	virtual bool HandleMenuEvent(const FKartMenuEvent& Event) override;

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** m:ss.mmm (or m:ss) */
	static FString FormatRaceTime(double Ms, bool bMillis = true);
	/** 1st, 2nd, 3rd, 4th, 11th, 21st... */
	static FString Ordinal(int32 Place);
	/** "st", "nd", "rd", "th". */
	static FString OrdinalSuffix(int32 Place);

private:
	/** The results board over the finished race. */
	void ShowResults(AKartRaceManager& Race);
	/** Shows the mouse cursor (and lets the UI take input) while a menu is open. */
	void UpdateCursor();

	TWeakObjectPtr<AKartRaceManager> Manager;
	TSharedPtr<SKartRaceHud> Hud;
	TSharedPtr<SKartScreenStack> Menus;
	bool bRegistered = false;
	bool bCursorShown = false;
};
