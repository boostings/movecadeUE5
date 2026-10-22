#include "UI/Screens/Store/KartStorePresenter.h"

// The store screens share one localization namespace (the keys predate the split into files).
#define LOCTEXT_NAMESPACE "KartStoreScreen"

using namespace MovecadeKart::Store;

namespace KartStoreUI
{
	FText TabName(ETab Tab)
	{
		switch (Tab)
		{
		case ETab::Karts: return LOCTEXT("TabKarts", "Karts");
		case ETab::Drivers: return LOCTEXT("TabDrivers", "Drivers");
		case ETab::Party: return LOCTEXT("TabParty", "Party Games");
		case ETab::Bundles: return LOCTEXT("TabBundles", "Bundles");
		default: return LOCTEXT("TabFree", "Free Unlocks");
		}
	}

	TArray<const FItem*> ItemsFor(ETab Tab)
	{
		TArray<const FItem*> Out;
		for (const FItem& I : Catalog())
		{
			bool bIn = false;
			switch (Tab)
			{
			case ETab::Karts: bIn = I.Slot == ESlot::KartPaint; break;
			case ETab::Drivers: bIn = I.Slot == ESlot::Colourway || I.Slot == ESlot::Headwear || I.Slot == ESlot::VictoryEmote; break;
			case ETab::Party: bIn = I.Slot == ESlot::BowlingBall || I.Slot == ESlot::Katana || I.Slot == ESlot::StackerBlock; break;
			case ETab::Bundles: bIn = I.Slot == ESlot::Bundle; break;
			default: bIn = I.IsFree(); break;
			}
			if (bIn)
			{
				Out.Add(&I);
			}
		}
		return Out;
	}

	FPrimaryAction PrimaryFor(const FKartStore* Store, const FItem& Item)
	{
		FPrimaryAction Out;
		if (!Store)
		{
			Out.Label = LOCTEXT("StoreUnavailable", "Store unavailable");
			Out.bEnabled = false;
			return Out;
		}
		switch (Store->GetState(Item.DefId))
		{
		case EItemState::Equipped:
			Out.Label = LOCTEXT("Unequip", "Take off");
			Out.Look = EPrimaryLook::Unequip;
			break;
		case EItemState::Owned:
			if (Item.Slot == ESlot::Bundle)
			{
				Out.Label = LOCTEXT("AllOwned", "All owned");
				Out.bEnabled = false;
			}
			else
			{
				Out.Label = LOCTEXT("Equip", "Equip");
				Out.Look = EPrimaryLook::Equip;
			}
			break;
		case EItemState::OwnedUnconfirmed:
			// Only Steam's answer unlocks a paid item; the local list could have been edited.
			Out.Label = LOCTEXT("NeedsSteam", "Start Steam to wear");
			Out.bEnabled = false;
			break;
		case EItemState::ForSale:
			Out.Label = FText::Format(LOCTEXT("BuyFor", "Buy  {0}"), Store->GetPriceText(Item.DefId));
			Out.Look = EPrimaryLook::Buy;
			Out.bEnabled = Store->CanPurchase(Item.DefId);
			if (!Out.bEnabled)
			{
				Out.Label = Store->IsPurchasing() ? LOCTEXT("OtherPending", "Checkout open") : !Store->IsOnline() ? LOCTEXT("Offline", "Store offline") : Store->GetPriceText(Item.DefId);
			}
			break;
		case EItemState::Pending:
			Out.Label = LOCTEXT("Pending", "Waiting for Steam...");
			Out.bEnabled = false;
			break;
		case EItemState::Locked:
			Out.Label = LOCTEXT("Earning", "Keep playing to unlock");
			Out.bEnabled = false;
			break;
		case EItemState::NotDropped:
			Out.Label = LOCTEXT("NotDropped", "Drops while you play");
			Out.bEnabled = false;
			break;
		default:
			Out.Label = LOCTEXT("ComingSoon", "Coming soon");
			Out.bEnabled = false;
			break;
		}
		return Out;
	}

	bool IsMockStore(const FKartStore* Store)
	{
		return Store && Store->GetBackend().GetName() == TEXT("mock");
	}

	FStatus StatusFor(const FKartStore* Store)
	{
		FStatus Out;
		if (!Store)
		{
			Out.Text = LOCTEXT("StatusNone", "Store unavailable");
			Out.Level = EStatusLevel::Offline;
		}
		else if (IsMockStore(Store))
		{
			Out.Text = Store->IsOnline() ? LOCTEXT("StatusMock", "Demo store (offline test mode)") : LOCTEXT("StatusOffline", "Offline: showing items you own");
			Out.Level = Store->IsOnline() ? EStatusLevel::Demo : EStatusLevel::Offline;
		}
		else
		{
			Out.Text = Store->IsOnline() ? LOCTEXT("StatusSteam", "Steam Inventory") : LOCTEXT("StatusSteamOff", "Steam offline: showing items you own");
			Out.Level = Store->IsOnline() ? EStatusLevel::Live : EStatusLevel::Offline;
		}
		return Out;
	}

	int32 MoveInGrid(int32 Focus, int32 Count, int32 Columns, int32 DX, int32 DY)
	{
		const int32 Col = Focus % Columns, Row = Focus / Columns;
		if (DX != 0)
		{
			const int32 NewCol = Col + DX;
			return NewCol >= 0 && NewCol < Columns && Row * Columns + NewCol < Count ? Row * Columns + NewCol : Focus;
		}
		const int32 NewRow = Row + DY;
		const int32 Rows = (Count + Columns - 1) / Columns;
		// A short last row: moving down onto it lands on its last tile.
		return NewRow >= 0 && NewRow < Rows ? FMath::Min(NewRow * Columns + Col, Count - 1) : Focus;
	}

	EDialogPhase PhaseAfter(EPurchaseResult Result)
	{
		return Result == EPurchaseResult::Success || Result == EPurchaseResult::AlreadyOwned ? EDialogPhase::Success
			: Result == EPurchaseResult::Cancelled ? EDialogPhase::Cancelled : EDialogPhase::Failed;
	}

	FName DialogId(EDialogPhase Phase)
	{
		return Phase == EDialogPhase::Confirm ? FName(TEXT("store-buy")) : Phase == EDialogPhase::Success ? FName(TEXT("store-done")) : FName(TEXT("store-purchase"));
	}

	int32 NumDialogButtons(EDialogPhase Phase, bool bCanEquip)
	{
		return Phase == EDialogPhase::Confirm || Phase == EDialogPhase::Failed || (Phase == EDialogPhase::Success && bCanEquip) ? 2 : 1;
	}

	FText DialogTitle(EDialogPhase Phase, const FItem& Item)
	{
		switch (Phase)
		{
		case EDialogPhase::Confirm: return FText::Format(LOCTEXT("BuyTitle", "Buy {0}?"), Item.Name);
		case EDialogPhase::Waiting: return LOCTEXT("WaitTitle", "Finish in Steam");
		case EDialogPhase::Success: return LOCTEXT("DoneTitle", "It's yours!");
		case EDialogPhase::Cancelled: return LOCTEXT("CancelTitle", "Purchase cancelled");
		default: return LOCTEXT("FailTitle", "That didn't go through");
		}
	}

	TArray<FText> DialogLines(EDialogPhase Phase, const FKartStore* Store, const FItem& Item, const FString& Detail)
	{
		switch (Phase)
		{
		case EDialogPhase::Confirm:
		{
			TArray<FText> Out = { LOCTEXT("CosmeticOnly", "Cosmetic only: it changes how things look, never how they play.") };
			if (Item.Slot == ESlot::Bundle && Store)
			{
				const int32 Have = Store->NumOwnedContents(Item.DefId);
				Out.Add(Have > 0 ? FText::Format(LOCTEXT("BundleHave", "You already own {0} of the {1} items. The bundle price stays the same."), Have, Item.Contents.Num())
					: FText::Format(LOCTEXT("BundleAll", "{0} items, all at once."), Item.Contents.Num()));
			}
			Out.Add(IsMockStore(Store) ? LOCTEXT("MockPay", "Demo store: no real money. In the Steam version, you pay in the Steam overlay.")
				: LOCTEXT("SteamPay", "You'll confirm and pay in the Steam overlay. Steam handles payment, receipts and refunds."));
			return Out;
		}
		case EDialogPhase::Waiting:
			return { FText::Format(LOCTEXT("WaitBody", "The Steam overlay has your checkout for {0}. Approve it there, or close it to cancel."), Item.Name),
				LOCTEXT("WaitNote", "You can close this; the item arrives as soon as Steam confirms.") };
		case EDialogPhase::Success:
			return { FText::Format(LOCTEXT("DoneBody", "{0} is in your collection."), Item.Name),
				Item.IsEquippable() ? LOCTEXT("DoneEquip", "Wear it now, or later from the store.") : LOCTEXT("DoneBundle", "Everything inside is ready to equip in its tab.") };
		case EDialogPhase::Cancelled:
			return { LOCTEXT("CancelBody", "No charge was made. You can buy it any time.") };
		default:
			return { LOCTEXT("FailBody", "You weren't charged. Check that Steam is online and the overlay is on, then try again."),
				Detail.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("FailDetail", "Steam said: {0}"), FText::FromString(Detail)) };
		}
	}

	TArray<FText> DialogButtons(EDialogPhase Phase, const FKartStore* Store, const FItem& Item)
	{
		switch (Phase)
		{
		case EDialogPhase::Confirm:
			return { FText::Format(LOCTEXT("BuyButton", "Buy  {0}"), Store ? Store->GetPriceText(Item.DefId) : FText::GetEmpty()), LOCTEXT("NotNow", "Not now") };
		case EDialogPhase::Waiting:
			return { LOCTEXT("Hide", "Close") };
		case EDialogPhase::Success:
			return Item.IsEquippable() ? TArray<FText>{ LOCTEXT("EquipNow", "Equip now"), LOCTEXT("Later", "Later") } : TArray<FText>{ LOCTEXT("Great", "Great!") };
		case EDialogPhase::Cancelled:
			return { LOCTEXT("Ok", "OK") };
		default:
			return { LOCTEXT("TryAgain", "Try again"), LOCTEXT("Close", "Close") };
		}
	}
}

#undef LOCTEXT_NAMESPACE
