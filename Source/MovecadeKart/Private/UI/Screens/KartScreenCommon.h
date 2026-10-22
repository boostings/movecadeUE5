#pragma once

#include "CoreMinimal.h"
#include "UI/KartScreen.h"
#include "UI/KartWidgets.h"

class UKartSessionSettings;

/**
 * Building blocks the menu screens in UI/Screens share (front end, settings, race menus). Private to the
 * module: screens include it; nothing outside UI/Screens should.
 */
namespace KartScreens::Shared
{
	/** The Slate clock every screen animates from. */
	double NowSeconds();

	/** The session settings of the host's world, or null (headless, or no game instance). */
	UKartSessionSettings* SessionOf(const IKartScreenHost* Host);

	using KartWidgets::SeatTag;
	using KartWidgets::CpuTag;

	/**
	 * A list's focus: the cursor plus the timestamps its focus pop and value-change nudge animate from.
	 * Screens own one per list and turn it into each row's FItemState while painting.
	 */
	struct FListFocus
	{
		FKartMenuCursor Cursor;
		/** When the cursor last moved (the focused item's pop). */
		double FocusAt = 0.0;
		/** The last value change: when, which way (-1 / +1) and on which row. */
		double ChangeAt = -10.0;
		int32 ChangeDir = 0;
		int32 ChangedRow = INDEX_NONE;

		KartWidgets::FItemState State(int32 Row, double Now, bool bEnabled = true) const;

		/** Moves the cursor by Delta; true if it moved (and restarts the pop). */
		bool Move(int32 Delta, double Now);
		/** Sets the cursor (clamped); true if it moved. */
		bool SetFocus(int32 Row, double Now);
		/** Records a value change on Row for the nudge animation. */
		void Changed(int32 Row, int32 Dir, double Now);

		/** Move, with the move sound when the cursor moved. */
		void Step(int32 Delta, const IKartScreenHost& Host);
		/** SetFocus, with the move sound when the cursor moved (mouse hover). */
		void HoverTo(int32 Row, const IKartScreenHost& Host);
	};

	// Front-end art ----------------------------------------------------------------------------------

	/** A dark, slanted panel down the left side so menus read over any backdrop; slides in with the screen. */
	void LeftPanel(const FKartPainter& P, const FKartScreenFrame& Frame, float Width, float Alpha = 0.8f);

	/** The "MOVECADE / KART RACER" logo built from type. Age drives the letters' drop-in; Now their bob. */
	void Logo(const FKartPainter& P, const FVector2D& Centre, float Scale, double Age, double Now);

	/** The race setup's step names ("Racers", "Rules", "Track") for KartWidgets::Steps. */
	const TArray<FText>& RaceSetupSteps();
}
