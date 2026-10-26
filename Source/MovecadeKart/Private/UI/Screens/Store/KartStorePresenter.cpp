#include "UI/Screens/Store/KartStorePresenter.h"

// The store screens share one localization namespace (the keys predate the split into files).
#define LOCTEXT_NAMESPACE "KartStoreScreen"

using namespace MovecadeKart::Store;

namespace KartStoreUI
{
	namespace
	{
		/** The store's name at the start of a sentence ("The App Store said: ..."). */
		FText StorefrontTitle(EStorefront Front)
		{
			switch (Front)
			{
			case EStorefront::AppStore: return LOCTEXT("FrontTitleApple", "The App Store");
			case EStorefront::GooglePlay: return LOCTEXT("FrontTitleGoogle", "Google Play");
			default: return LOCTEXT("FrontTitleSteam", "Steam");
			}
		}
	}

	EStorefront StorefrontOf(const FKartStore* Store)
	{
		return Store ? Store->GetStorefront() : NativeStorefront();
	}

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
			// Only the store's answer unlocks a paid item; the local list could have been edited.
			Out.Label = StorefrontOf(Store) == EStorefront::Steam ? LOCTEXT("NeedsSteam", "Start Steam to wear")
				: FText::Format(LOCTEXT("NeedsStore", "Connect to {0} to wear"), StorefrontName(StorefrontOf(Store)));
			Out.bEnabled = false;
			break;
		case EItemState::AwaitingApproval:
			Out.Label = LOCTEXT("AwaitingApproval", "Waiting for approval");
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
			Out.Label = FText::Format(LOCTEXT("PendingStore", "Waiting for {0}..."), StorefrontName(StorefrontOf(Store)));
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
			const EStorefront Front = Store->GetStorefront();
			Out.Text = !Store->IsOnline() ? LOCTEXT("StatusOffline", "Offline: showing items you own")
				: Front == EStorefront::AppStore ? LOCTEXT("StatusMockApple", "Demo App Store (test mode)")
				: Front == EStorefront::GooglePlay ? LOCTEXT("StatusMockGoogle", "Demo Google Play (test mode)")
				: LOCTEXT("StatusMock", "Demo store (offline test mode)");
			Out.Level = Store->IsOnline() ? EStatusLevel::Demo : EStatusLevel::Offline;
		}
		else
		{
			switch (Store->GetStorefront())
			{
			case EStorefront::AppStore:
				Out.Text = Store->IsOnline() ? LOCTEXT("StatusApple", "App Store") : LOCTEXT("StatusAppleOff", "App Store offline: showing items you own");
				break;
			case EStorefront::GooglePlay:
				Out.Text = Store->IsOnline() ? LOCTEXT("StatusGoogle", "Google Play") : LOCTEXT("StatusGoogleOff", "Google Play offline: showing items you own");
				break;
			default:
				Out.Text = Store->IsOnline() ? LOCTEXT("StatusSteam", "Steam Inventory") : LOCTEXT("StatusSteamOff", "Steam offline: showing items you own");
				break;
			}
			Out.Level = Store->IsOnline() ? EStatusLevel::Live : EStatusLevel::Offline;
		}
		return Out;
	}

	FText RestoreStartedText(const FKartStore* Store)
	{
		return FText::Format(LOCTEXT("RestoringStore", "Checking {0} for your items..."), StorefrontName(StorefrontOf(Store)));
	}

	FText RestoreDoneText(const FKartStore* Store, bool bOk)
	{
		const FText Name = StorefrontName(StorefrontOf(Store));
		return bOk ? FText::Format(LOCTEXT("RestoredStore", "Purchases restored from {0}"), Name)
			: FText::Format(LOCTEXT("RestoreFailedStore", "Couldn't reach {0}. Showing what we had."), Name);
	}

	FText UnconfirmedChip(const FKartStore* Store)
	{
		switch (StorefrontOf(Store))
		{
		case EStorefront::AppStore: return LOCTEXT("ChipUnconfirmedApple", "NEEDS APP STORE");
		case EStorefront::GooglePlay: return LOCTEXT("ChipUnconfirmedGoogle", "NEEDS GOOGLE PLAY");
		default: return LOCTEXT("ChipUnconfirmed", "NEEDS STEAM");
		}
	}

	FText UnconfirmedNote(const FKartStore* Store)
	{
		return FText::Format(LOCTEXT("UnconfirmedNoteStore", "Wear it once {0} confirms it."), StorefrontName(StorefrontOf(Store)));
	}

	FText ApprovedText(const FItem& Item)
	{
		return FText::Format(LOCTEXT("ApprovedToast", "Approved: {0} is yours"), Item.Name);
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
			: Result == EPurchaseResult::Cancelled ? EDialogPhase::Cancelled
			: Result == EPurchaseResult::Pending ? EDialogPhase::Pending : EDialogPhase::Failed;
	}

	FName DialogId(EDialogPhase Phase)
	{
		return Phase == EDialogPhase::Confirm ? FName(TEXT("store-buy")) : Phase == EDialogPhase::Success ? FName(TEXT("store-done")) : FName(TEXT("store-purchase"));
	}

	int32 NumDialogButtons(EDialogPhase Phase, bool bCanEquip)
	{
		return Phase == EDialogPhase::Confirm || Phase == EDialogPhase::Failed || (Phase == EDialogPhase::Success && bCanEquip) ? 2 : 1;
	}

	FText DialogTitle(EDialogPhase Phase, const FItem& Item, EStorefront Front)
	{
		switch (Phase)
		{
		case EDialogPhase::Confirm: return FText::Format(LOCTEXT("BuyTitle", "Buy {0}?"), Item.Name);
		case EDialogPhase::Waiting: return Front == EStorefront::Steam ? LOCTEXT("WaitTitle", "Finish in Steam") : LOCTEXT("WaitTitleMobile", "Confirm your purchase");
		case EDialogPhase::Success: return LOCTEXT("DoneTitle", "It's yours!");
		case EDialogPhase::Cancelled: return LOCTEXT("CancelTitle", "Purchase cancelled");
		case EDialogPhase::Pending: return LOCTEXT("PendingTitle", "Waiting for approval");
		default: return LOCTEXT("FailTitle", "That didn't go through");
		}
	}

	TArray<FText> DialogLines(EDialogPhase Phase, const FKartStore* Store, const FItem& Item, const FString& Detail)
	{
		const EStorefront Front = StorefrontOf(Store);
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
			if (IsMockStore(Store))
			{
				Out.Add(Front == EStorefront::AppStore ? LOCTEXT("MockPayApple", "Demo store: no real money. On iPhone and iPad you pay with your Apple Account.")
					: Front == EStorefront::GooglePlay ? LOCTEXT("MockPayGoogle", "Demo store: no real money. On Android you pay with Google Play.")
					: LOCTEXT("MockPay", "Demo store: no real money. In the Steam version, you pay in the Steam overlay."));
			}
			else
			{
				Out.Add(Front == EStorefront::AppStore ? LOCTEXT("ApplePay", "You'll confirm and pay with your Apple Account. Apple handles payment, receipts and refunds.")
					: Front == EStorefront::GooglePlay ? LOCTEXT("GooglePay", "You'll confirm and pay with Google Play. Google handles payment, receipts and refunds.")
					: LOCTEXT("SteamPay", "You'll confirm and pay in the Steam overlay. Steam handles payment, receipts and refunds."));
			}
			return Out;
		}
		case EDialogPhase::Waiting:
			return { Front == EStorefront::AppStore ? FText::Format(LOCTEXT("WaitBodyApple", "Apple's purchase sheet is open for {0}. Confirm it there, or cancel it."), Item.Name)
					: Front == EStorefront::GooglePlay ? FText::Format(LOCTEXT("WaitBodyGoogle", "Google Play's purchase sheet is open for {0}. Confirm it there, or cancel it."), Item.Name)
					: FText::Format(LOCTEXT("WaitBody", "The Steam overlay has your checkout for {0}. Approve it there, or close it to cancel."), Item.Name),
				FText::Format(LOCTEXT("WaitNoteStore", "You can close this; the item arrives as soon as {0} confirms."), StorefrontName(Front)) };
		case EDialogPhase::Success:
			return { FText::Format(LOCTEXT("DoneBody", "{0} is in your collection."), Item.Name),
				Item.IsEquippable() ? LOCTEXT("DoneEquip", "Wear it now, or later from the store.") : LOCTEXT("DoneBundle", "Everything inside is ready to equip in its tab.") };
		case EDialogPhase::Cancelled:
			return { LOCTEXT("CancelBody", "No charge was made. You can buy it any time.") };
		case EDialogPhase::Pending:
			return { FText::Format(LOCTEXT("PendingBody", "{0} needs approval first (Ask to Buy), or the payment is still clearing. Nothing has been charged yet."), Item.Name),
				LOCTEXT("PendingNote", "It arrives by itself once it goes through. Restore purchases checks again.") };
		default:
			return { Front == EStorefront::Steam ? LOCTEXT("FailBody", "You weren't charged. Check that Steam is online and the overlay is on, then try again.")
					: LOCTEXT("FailBodyMobile", "You weren't charged. Check your connection, then try again."),
				Detail.IsEmpty() ? FText::GetEmpty() : FText::Format(LOCTEXT("FailDetailStore", "{0} said: {1}"), StorefrontTitle(Front), FText::FromString(Detail)) };
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
		case EDialogPhase::Pending:
			return { LOCTEXT("Ok", "OK") };
		default:
			return { LOCTEXT("TryAgain", "Try again"), LOCTEXT("Close", "Close") };
		}
	}
}

#undef LOCTEXT_NAMESPACE
