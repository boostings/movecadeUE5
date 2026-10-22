#pragma once

#include "CoreMinimal.h"
#include "UI/KartScreen.h"

class SArcadeOverlay;

/**
 * SArcadeOverlay's two screens (private to Games/): the local pause menu and the results bar with the party table.
 * They live in the overlay's SKartScreenStack and reach the game through the overlay (weakly: the game can end first).
 */
namespace MovecadeKart::Arcade::Overlay
{
	/** The pause screen's id, which the overlay checks to close a pause the game itself ended. */
	extern const FName PauseScreenId;

	TSharedRef<FKartScreen> MakePauseScreen(const TWeakPtr<SArcadeOverlay>& Overlay);
	/** bRoom: Party Mode's buttons (lobby, leave) and the automatic play-again. */
	TSharedRef<FKartScreen> MakeResultsScreen(const TWeakPtr<SArcadeOverlay>& Overlay, bool bRoom);
}
