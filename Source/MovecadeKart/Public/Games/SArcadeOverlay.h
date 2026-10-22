#pragma once

#include "CoreMinimal.h"
#include "UI/KartMenuInput.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class IArcadeGame;
class SKartScreenStack;
class UWorld;

/**
 * The arcade games' menu layer, over each game's own HUD: the local pause (resume, restart, how to play, settings,
 * change game, main menu) and, once a game's standings are up, the "what next" bar (play again, next game, change
 * game, main menu; in Party Mode play again, next game, lobby, leave) with the party table across games. It owns
 * the pause and results keys through the UI input router, so the games never see them, and blocks game input while
 * a menu is up. In Party Mode the room owns pauses, and the results bar plays again on its own after a while unless
 * someone at the screen takes over (the phones have no say in it).
 *
 * Owned by the game mode (Attach in StartPlay, Detach in EndPlay) and the viewport; the screens themselves are in
 * ArcadeOverlayScreens.cpp.
 */
class MOVECADEKART_API SArcadeOverlay : public SCompoundWidget, public IKartMenuInputTarget
{
public:
	SLATE_BEGIN_ARGS(SArcadeOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AActor>, Game)
		/** The same actor, as the interface (no RTTI in this module). */
		SLATE_ARGUMENT(IArcadeGame*, Interface)
		SLATE_ARGUMENT(bool, Room)
	SLATE_END_ARGS()

	virtual ~SArcadeOverlay() override;
	void Construct(const FArguments& InArgs);

	/** Adds an overlay for Game (an actor implementing IArcadeGame) to the world's viewport. */
	template <class TGame>
	static TSharedPtr<SArcadeOverlay> Attach(TGame* Game, bool bRoom) { return AttachTo(Game, static_cast<IArcadeGame*>(Game), bRoom); }
	static TSharedPtr<SArcadeOverlay> AttachTo(AActor* Actor, IArcadeGame* Game, bool bRoom);
	/** Removes it from the viewport and the input router. */
	void Detach();

	/** True while a menu is up: games ignore their own input then. */
	static bool IsMenuOpen(const UWorld* World);

	void OpenPause();
	void CloseMenus(bool bResume = true);
	bool AreMenusOpen() const;
	bool IsRoom() const { return bRoom; }
	IArcadeGame* GetGame() const;
	AActor* GetGameActor() const { return Game.Get(); }

	// IKartMenuInputTarget
	virtual bool HandleMenuEvent(const FKartMenuEvent& Event) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void ShowResults();
	void UpdateCursor();
	/** Adds the finished game to the party table (UArcadeSession), once per finish. */
	void RecordFinishedGame(const IArcadeGame& G, double Now);

	TWeakObjectPtr<AActor> Game;
	IArcadeGame* Interface = nullptr;
	/** Seconds of the game's own results before the "what next" bar (the catalog's ResultsDelay). */
	double ResultsDelay = 2.0;
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<SKartScreenStack> Menus;
	bool bRoom = false;
	bool bRegistered = false;
	bool bCursorShown = false;
	bool bResultsShown = false;
	bool bRecorded = false;
};
