#pragma once

#include "CoreMinimal.h"
#include "Store/KartStore.h"

/**
 * The store screens' decisions, apart from their painting: which items a tab lists, what the main button says and
 * whether it works, the status line, the purchase dialog's phases and words, and the grid's focus moves. Pure
 * functions of the store's state (a null store means no store), so they are unit-tested (KartStoreTests). The
 * screens (KartStoreScreen.cpp, KartStorePurchaseDialog.cpp) only paint these and route input.
 */
namespace KartStoreUI
{
	using MovecadeKart::Store::FItem;
	using MovecadeKart::Store::FKartStore;

	// ------------------------------------------------------------------------------------------------- Tabs

	enum class ETab : uint8 { Karts, Drivers, Party, Bundles, Free, Num };

	FText TabName(ETab Tab);
	/** The catalog's items on Tab, in catalog order. */
	TArray<const FItem*> ItemsFor(ETab Tab);

	// ------------------------------------------------------------------------------------------------- Store

	/** How the primary button reads (its colour in the painting). */
	enum class EPrimaryLook : uint8 { Buy, Equip, Unequip, Unavailable };

	/** What the primary button offers for an item right now. */
	struct FPrimary
	{
		FText Label;
		EPrimaryLook Look = EPrimaryLook::Unavailable;
		bool bEnabled = true;
	};

	FPrimary PrimaryFor(const FKartStore* Store, const FItem& Item);

	/** The connection line in the corner: which backend, and whether it's reachable. */
	enum class EStatusLevel : uint8 { Live, Demo, Offline };

	struct FStatus
	{
		FText Text;
		EStatusLevel Level = EStatusLevel::Live;
	};

	FStatus StatusFor(const FKartStore* Store);
	bool IsMockStore(const FKartStore* Store);

	/** Where focus goes in a grid of Count tiles, Columns wide, moved by (DX, DY): it stays put at an edge. */
	int32 MoveInGrid(int32 Focus, int32 Count, int32 Columns, int32 DX, int32 DY);

	// -------------------------------------------------------------------------------------- Purchase dialog

	enum class EDialogPhase : uint8 { Confirm, Waiting, Success, Cancelled, Failed };

	/** The phase a finished checkout lands on. */
	EDialogPhase PhaseAfter(MovecadeKart::Store::EPurchaseResult Result);
	/** The dialog's screen id (the -KartUiShots tour waits for store-buy and store-done). */
	FName DialogId(EDialogPhase Phase);
	/** Confirm and Failed offer two buttons, and Success too when the item can be worn. */
	int32 NumDialogButtons(EDialogPhase Phase, bool bCanEquip);
	FText DialogTitle(EDialogPhase Phase, const FItem& Item);
	/** The dialog's body lines (an empty line is skipped). Detail is the backend's reason for a failure. */
	TArray<FText> DialogLines(EDialogPhase Phase, const FKartStore* Store, const FItem& Item, const FString& Detail);
	TArray<FText> DialogButtons(EDialogPhase Phase, const FKartStore* Store, const FItem& Item);
}
