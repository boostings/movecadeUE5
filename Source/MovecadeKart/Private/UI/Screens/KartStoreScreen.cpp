#include "UI/Screens/KartScreens.h"

#include "Framework/Application/SlateApplication.h"
#include "Store/KartStore.h"
#include "Store/KartStoreSubsystem.h"
#include "UI/KartFrontEnd.h"
#include "UI/KartWidgets.h"
#include "UI/Screens/Store/KartStoreArt.h"
#include "UI/Screens/Store/KartStorePresenter.h"
#include "UI/Screens/Store/KartStorePurchaseDialog.h"

// The store screens share one localization namespace (the keys predate the split into files).
#define LOCTEXT_NAMESPACE "KartStoreScreen"

using namespace KartUI;
using namespace KartStoreUI;
using namespace MovecadeKart::Store;

namespace
{
	/** The item grid and the detail card, in layout units (1920 x 1080 reference). */
	constexpr int32 Columns = 5;
	constexpr int32 VisibleRows = 3;
	constexpr double TileW = 206.0;
	constexpr double TileH = 200.0;
	constexpr double Gap = 14.0;
	constexpr double DetailW = 600.0;
	/** Where the grid and the detail card start below the safe area's top (under the header and the tabs). */
	constexpr double ContentTop = 248.0;
	/** Seconds a toast stays up. */
	constexpr double ToastSeconds = 3.0;

	/** Hit ids: tiles use their item index; the rest sit above any grid. */
	constexpr int32 TabHit = 1000;
	constexpr int32 PrimaryHit = 2000;
	constexpr int32 RestoreHit = 2001;

	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	FLinearColor TabAccent(ETab Tab)
	{
		switch (Tab)
		{
		case ETab::Karts: return Color::Hot;
		case ETab::Drivers: return Color::Sky;
		case ETab::Party: return Color::Sun;
		case ETab::Bundles: return Color::Grape;
		default: return Color::Mint;
		}
	}

	FLinearColor PrimaryFill(EPrimaryLook Look)
	{
		switch (Look)
		{
		case EPrimaryLook::Buy: return Color::Sun;
		case EPrimaryLook::Equip: return Color::Mint;
		case EPrimaryLook::Unequip: return Color::Paper;
		default: return Color::Hot;
		}
	}

	FLinearColor StatusDot(EStatusLevel Level)
	{
		switch (Level)
		{
		case EStatusLevel::Live: return Color::Mint;
		case EStatusLevel::Demo: return Color::Sun;
		default: return Color::Hot;
		}
	}

	/**
	 * The cosmetics store: tabs of item tiles on the left, the focused item's detail card and its one action (buy,
	 * equip, take off) on the right, the connection status and Restore purchases in the corner. The decisions live in
	 * KartStorePresenter; buying opens FKartStorePurchaseDialog.
	 */
	class FStoreScreen final : public FKartScreen
	{
	public:
		FStoreScreen(AKartFrontEnd* InFrontEnd, FName InTourStep) : FrontEnd(InFrontEnd), TourStep(InTourStep)
		{
			if (TourStep == TEXT("storeparty"))
			{
				Tab = ETab::Party;
			}
			else if (TourStep == TEXT("storefree"))
			{
				Tab = ETab::Free;
			}
			Items = ItemsFor(Tab);
		}

		virtual ~FStoreScreen() override { Unbind(); }

		virtual FName GetId() const override { return TourStep.IsNone() || TourStep == TEXT("store") ? FName(TEXT("store")) : TourStep; }

		virtual TArray<FKartHint> GetHints() const override
		{
			return { { EKartMenuAction::PrevTab, FText::GetEmpty() }, { EKartMenuAction::NextTab, LOCTEXT("HintTabs", "Tabs") },
				{ EKartMenuAction::Alt, LOCTEXT("HintRestore", "Restore purchases") }, { EKartMenuAction::Accept, LOCTEXT("HintSelect", "Select") },
				{ EKartMenuAction::Back, LOCTEXT("HintBack", "Back") } };
		}

		virtual void OnShown() override
		{
			FocusAt = NowSeconds();
			if (AKartFrontEnd* FE = FrontEnd.Get())
			{
				FE->SetShot(EKartFrontEndShot::Orbit);
				FE->SetShowroom({});
			}
			Bind();
			ShowTourStep();
		}

		virtual bool HandleEvent(const FKartMenuEvent& Event) override
		{
			switch (Event.Action)
			{
			case EKartMenuAction::Left: MoveFocus(-1, 0); return true;
			case EKartMenuAction::Right: MoveFocus(1, 0); return true;
			case EKartMenuAction::Up: MoveFocus(0, -1); return true;
			case EKartMenuAction::Down: MoveFocus(0, 1); return true;
			case EKartMenuAction::PrevTab:
			case EKartMenuAction::NextTab:
				SetTab(static_cast<ETab>(FKartMenuCursor::Cycle(static_cast<int32>(Tab), Event.Action == EKartMenuAction::PrevTab ? -1 : 1, static_cast<int32>(ETab::Num))));
				return true;
			case EKartMenuAction::Accept:
			case EKartMenuAction::Start:
				Activate();
				return true;
			case EKartMenuAction::Alt:
				Restore();
				return true;
			default:
				return false;
			}
		}

		virtual void OnHover(int32 HitId) override
		{
			if (HitId >= 0 && HitId < Items.Num() && HitId != Focus)
			{
				SetFocus(HitId);
			}
		}

		virtual void OnClick(int32 HitId) override
		{
			if (HitId >= TabHit && HitId < TabHit + static_cast<int32>(ETab::Num))
			{
				SetTab(static_cast<ETab>(HitId - TabHit));
			}
			else if (HitId == PrimaryHit)
			{
				Activate();
			}
			else if (HitId == RestoreHit)
			{
				Restore();
			}
			else if (HitId >= 0 && HitId < Items.Num())
			{
				// A click picks the tile; a second click on it acts, like Accept.
				if (HitId == Focus)
				{
					Activate();
				}
				else
				{
					SetFocus(HitId);
				}
			}
		}

		virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override
		{
			const FKartStore* S = UKartStoreSubsystem::Active();
			// A dark wash over the orbit so the grid reads; the detail side a touch darker.
			P.Gradient(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.72f), Color::WithAlpha(Color::Night, 0.86f), false);
			KartWidgets::Header(P, Frame, LOCTEXT("StoreTitle", "Store"),
				LOCTEXT("StoreSub", "Cosmetics only. Every kart, ball and blade plays exactly the same."), Color::Hot);
			PaintStatus(P, Frame, S);
			PaintTabs(P, Frame);

			const double Top = Frame.SafeMin.Y + ContentTop;
			PaintGrid(P, Frame, S, FVector2D(Frame.SafeMin.X + KartWidgets::SlideX(Frame), Top));
			if (Items.IsValidIndex(Focus))
			{
				PaintDetail(P, Frame, S, *Items[Focus], FVector2D(Frame.SafeMax.X - DetailW, Top));
			}
			PaintToast(P, Frame);
		}

	private:
		// ------------------------------------------------------------------------------------------ Store events

		void Bind()
		{
			FKartStore* S = UKartStoreSubsystem::Active();
			if (!S || BoundStore == S)
			{
				return;
			}
			Unbind();
			BoundStore = S;
			UnlockHandle = S->OnUnlocked.AddSP(this, &FStoreScreen::OnUnlocked);
			RestoreHandle = S->OnRestored.AddSP(this, &FStoreScreen::OnRestored);
		}

		void Unbind()
		{
			if (FKartStore* S = UKartStoreSubsystem::Active(); S && S == BoundStore)
			{
				S->OnUnlocked.Remove(UnlockHandle);
				S->OnRestored.Remove(RestoreHandle);
			}
			BoundStore = nullptr;
		}

		void OnUnlocked(int32 DefId)
		{
			if (const FItem* Item = FindItem(DefId))
			{
				Toast(FText::Format(LOCTEXT("UnlockedToast", "Unlocked: {0}"), Item->Name), Color::Mint);
			}
		}

		void OnRestored(bool bOk)
		{
			if (bRestoreRequested)
			{
				bRestoreRequested = false;
				Toast(bOk ? LOCTEXT("Restored", "Purchases restored from Steam") : LOCTEXT("RestoreFailed", "Couldn't reach Steam. Showing what we had."), bOk ? Color::Sky : Color::Hot);
			}
		}

		// --------------------------------------------------------------------------------------------- Actions

		/** The -KartUiShots store steps: the tab, the focused item and the dialog each one shows. */
		void ShowTourStep()
		{
			if (!bTourOpened && (TourStep == TEXT("storebuy") || TourStep == TEXT("storedone")))
			{
				bTourOpened = true;
				if (const FItem* Item = FindItem(FName(TEXT("paint.chrome"))))
				{
					Focus = Items.IndexOfByKey(Item);
					Host->PushScreen(MakeShared<FKartStorePurchaseDialog>(Item->DefId, TourStep == TEXT("storebuy") ? EDialogPhase::Confirm : EDialogPhase::Success));
				}
			}
			const FName FocusKey = TourStep == TEXT("storeparty") ? FName(TEXT("katana.neon")) : TourStep == TEXT("storefree") ? FName(TEXT("ball.alleycat")) : NAME_None;
			if (!FocusKey.IsNone())
			{
				const int32 Found = Items.IndexOfByPredicate([FocusKey](const FItem* I) { return I->Key == FocusKey; });
				Focus = Found == INDEX_NONE ? 0 : Found;
			}
		}

		void Toast(const FText& Text, const FLinearColor& Accent)
		{
			ToastText = Text;
			ToastAccent = Accent;
			ToastAt = NowSeconds();
		}

		void SetFocus(int32 Index)
		{
			Focus = Index;
			FocusAt = NowSeconds();
			Host->PlaySound(EKartUISound::Move);
		}

		void SetTab(ETab NewTab)
		{
			if (NewTab == Tab)
			{
				return;
			}
			Tab = NewTab;
			Items = ItemsFor(Tab);
			Focus = 0;
			FirstRow = 0;
			TabAt = FocusAt = NowSeconds();
			Host->PlaySound(EKartUISound::Tick);
		}

		void MoveFocus(int32 DX, int32 DY)
		{
			if (Items.Num() == 0)
			{
				return;
			}
			const int32 Next = MoveInGrid(Focus, Items.Num(), Columns, DX, DY);
			if (Next != Focus)
			{
				SetFocus(Next);
			}
			else
			{
				Host->PlaySound(EKartUISound::Error);
			}
		}

		/** The focused item's action: equip, take off, or open the purchase dialog. */
		void Activate()
		{
			FKartStore* S = UKartStoreSubsystem::Active();
			if (!S || !Items.IsValidIndex(Focus))
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			const FItem& Item = *Items[Focus];
			switch (S->GetState(Item.DefId))
			{
			case EItemState::Owned:
				if (Item.IsEquippable() && S->Equip(Item.DefId))
				{
					Host->PlaySound(EKartUISound::Go);
					Toast(FText::Format(LOCTEXT("EquippedToast", "Equipped: {0}"), Item.Name), Color::Mint);
					return;
				}
				break;
			case EItemState::Equipped:
				S->Unequip(Item.Slot);
				Host->PlaySound(EKartUISound::Back);
				return;
			case EItemState::ForSale:
				if (S->CanPurchase(Item.DefId))
				{
					Host->PlaySound(EKartUISound::Accept);
					Host->PushScreen(MakeShared<FKartStorePurchaseDialog>(Item.DefId, EDialogPhase::Confirm));
					return;
				}
				break;
			default:
				break;
			}
			Host->PlaySound(EKartUISound::Error);
		}

		void Restore()
		{
			if (FKartStore* S = UKartStoreSubsystem::Active(); S && !S->IsRestoring())
			{
				bRestoreRequested = true;
				S->Restore();
				Host->PlaySound(EKartUISound::Accept);
				Toast(LOCTEXT("Restoring", "Checking Steam for your items..."), Color::Sky);
			}
		}

		// -------------------------------------------------------------------------------------------- Painting

		void PaintStatus(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S)
		{
			const FKartPainter F = P.Faded(KartWidgets::Entrance(Frame, 0.1f));
			const FStatus Status = StatusFor(S);
			const FVector2D At(Frame.SafeMax.X, Frame.SafeMin.Y + 20.0);
			const FSlateFontInfo StatusFont = Font(EFont::Label, 20.f);
			const float W = static_cast<float>(FKartPainter::Measure(Status.Text.ToString(), StatusFont).X) + 64.f;
			F.Box(At - FVector2D(W, 0.0), FVector2D(W, 44.0), Color::WithAlpha(Color::Night, 0.75f), 22.f, Color::WithAlpha(Color::White, 0.3f), 2.f);
			F.Circle(At + FVector2D(-W + 24.0, 22.0), 8.f, StatusDot(Status.Level));
			F.Text(Status.Text, StatusFont, At + FVector2D(-W + 42.0, 22.0), Color::White, FVector2D(0.0, 0.5));

			// Restore purchases: a clickable chip under the status.
			const FText RestoreLabel = S && S->IsRestoring() ? LOCTEXT("RestoringChip", "Restoring...") : LOCTEXT("RestoreChip", "Restore purchases");
			const FSlateFontInfo RF = Font(EFont::Button, 22.f);
			const float GH = 30.f;
			const float RW = static_cast<float>(FKartPainter::Measure(RestoreLabel.ToString(), RF).X) + KartWidgets::GlyphWidth(EKartMenuAction::Alt, Frame.Device, GH) + 60.f;
			const FVector2D RPos(Frame.SafeMax.X - RW, Frame.SafeMin.Y + 76.0);
			F.Hit(RestoreHit, RPos, FVector2D(RW, 50.0));
			F.Box(RPos, FVector2D(RW, 50.0), Color::WithAlpha(Color::Paper, 0.92f), 25.f);
			float X = static_cast<float>(RPos.X) + 18.f;
			X += KartWidgets::Glyph(F, EKartMenuAction::Alt, Frame.Device, FVector2D(X, RPos.Y + 25.0), GH) + 12.f;
			F.Text(RestoreLabel, RF, FVector2D(X, RPos.Y + 25.0), Color::Navy, FVector2D(0.0, 0.5));
		}

		void PaintTabs(const FKartPainter& P, const FKartScreenFrame& Frame)
		{
			const FSlateFontInfo TabFont = Font(EFont::Button, 26.f);
			const double H = 58.0, TabGap = 12.0;
			double X = Frame.SafeMin.X + 56.0;
			const double Y = Frame.SafeMin.Y + 166.0;
			const float In = KartWidgets::Entrance(Frame, 0.05f);
			const float GH = 36.f;
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::PrevTab, Frame.Device, FVector2D(Frame.SafeMin.X, Y + H * 0.5), GH);
			for (int32 i = 0; i < static_cast<int32>(ETab::Num); ++i)
			{
				const ETab T = static_cast<ETab>(i);
				const bool bOn = T == Tab;
				const FText Name = TabName(T);
				const double W = FKartPainter::Measure(Name.ToString(), TabFont).X + 56.0;
				const FVector2D Pos(X, Y + (1.0 - In) * -40.0);
				P.Hit(TabHit + i, Pos, FVector2D(W, H));
				const float Pop = bOn ? 1.f + 0.06f * Ease::OutBack(static_cast<float>((Frame.Now - TabAt) / 0.25), 3.f) : 1.f;
				const FKartPainter TP = P.Transformed(Pos + FVector2D(W, H) * 0.5, Pop).Faded(In);
				TP.Card(Pos, FVector2D(W, H), bOn ? TabAccent(T) : Color::WithAlpha(Color::Paper, 0.9f), static_cast<float>(H * 0.5), 6.f, bOn ? Color::White : FLinearColor::Transparent, 4.f);
				TP.Text(Name, TabFont, Pos + FVector2D(W, H) * 0.5, bOn ? Color::TextOn(TabAccent(T)) : Color::Navy, FVector2D(0.5, 0.5));
				X += W + TabGap;
			}
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::NextTab, Frame.Device, FVector2D(X + 8.0, Y + H * 0.5), GH);
		}

		void PaintGrid(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S, const FVector2D& Origin)
		{
			// Scroll so the focused row is in view.
			const int32 FocusRow = Focus / Columns;
			if (FocusRow < FirstRow)
			{
				FirstRow = FocusRow;
			}
			else if (FocusRow >= FirstRow + VisibleRows)
			{
				FirstRow = FocusRow - VisibleRows + 1;
			}
			const int32 Rows = (Items.Num() + Columns - 1) / Columns;
			const FVector2D Area(Columns * TileW + (Columns - 1) * Gap, VisibleRows * TileH + (VisibleRows - 1) * Gap);
			const float TabIn = Ease::OutCubic(static_cast<float>((Frame.Now - TabAt) / 0.25));
			P.PushClip(Origin - FVector2D(12.0, 12.0), Area + FVector2D(24.0, 24.0));
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				const int32 Row = i / Columns - FirstRow;
				if (Row < 0 || Row >= VisibleRows)
				{
					continue;
				}
				const int32 Col = i % Columns;
				const float In = FMath::Min(KartWidgets::Entrance(Frame, 0.08f + (Row * Columns + Col) * 0.025f), TabIn);
				const FVector2D Pos = Origin + FVector2D(Col * (TileW + Gap), Row * (TileH + Gap) + (1.0 - In) * 40.0);
				PaintTile(P.Faded(In), Frame, S, *Items[i], Pos, i, i == Focus);
			}
			P.PopClip();
			// Scroll hints when more rows hide above or below.
			if (FirstRow > 0)
			{
				KartWidgets::Arrow(P.Transformed(Origin + FVector2D(Area.X * 0.5, -18.0), 1.f, -90.f), Origin + FVector2D(Area.X * 0.5, -18.0), 14.f, true, Color::Sun);
			}
			if (FirstRow + VisibleRows < Rows)
			{
				KartWidgets::Arrow(P.Transformed(Origin + FVector2D(Area.X * 0.5, Area.Y + 22.0), 1.f, 90.f), Origin + FVector2D(Area.X * 0.5, Area.Y + 22.0), 14.f, true, Color::Sun);
			}
		}

		void PaintTile(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S, const FItem& Item, const FVector2D& Pos, int32 Index, bool bFocused) const
		{
			const FVector2D Size(TileW, TileH);
			P.Hit(Index, Pos, Size);
			const EItemState State = S ? S->GetState(Item.DefId) : EItemState::ComingSoon;
			const float Pop = bFocused ? 1.f + 0.05f * Ease::OutBack(static_cast<float>((Frame.Now - FocusAt) / 0.3), 3.f) : 1.f;
			const FKartPainter T = P.Transformed(Pos + Size * 0.5, Pop);
			const bool bDim = State == EItemState::ComingSoon;
			const bool bUnearned = State == EItemState::Locked || State == EItemState::NotDropped;
			const FLinearColor Edge = bFocused ? Color::Sun : State == EItemState::Equipped ? Color::Mint : FLinearColor::Transparent;
			T.Card(Pos, Size, bDim ? Color::PaperShade : Color::Paper, Metric::Radius, bFocused ? 10.f : 6.f, Edge, bFocused ? 6.f : 4.f);
			const FVector2D Art(Pos.X + 10.0, Pos.Y + 10.0);
			const FVector2D ArtSize(Size.X - 20.0, 116.0);
			T.Gradient(Art, ArtSize, Lighter(Item.Visual.Primary, 0.6f), Lighter(Item.Visual.Primary, 0.3f), true, Metric::Radius - 6.f);
			// Only the focused tile animates.
			PaintPreview(bDim || bUnearned ? T.Faded(0.55f) : T, Item, Art + ArtSize * 0.5, 48.f, bFocused ? Frame.Now : 0.0);
			if (bUnearned)
			{
				PaintLock(T, Art + FVector2D(ArtSize.X - 24.0, 30.0), 22.f, Color::Navy);
			}
			// The name shrinks to fit the tile rather than running off it.
			float NameSize = 21.f;
			while (NameSize > 15.f && FKartPainter::Measure(Item.Name.ToString(), Font(EFont::Button, NameSize)).X > Size.X - 26.0)
			{
				NameSize -= 1.f;
			}
			T.Text(Item.Name, Font(EFont::Button, NameSize), FVector2D(Pos.X + 14.0, Pos.Y + 134.0), Color::Navy);
			PaintTileState(T, Frame, S, Item, State, FVector2D(Pos.X + 12.0, Pos.Y + Size.Y - 38.0));
		}

		/** The tile's bottom line: a chip for its state, a spinner, or the unlock progress. */
		static void PaintTileState(const FKartPainter& T, const FKartScreenFrame& Frame, const FKartStore* S, const FItem& Item, EItemState State, const FVector2D& ChipPos)
		{
			switch (State)
			{
			case EItemState::Equipped:
				PaintChip(T, ChipPos, LOCTEXT("ChipEquipped", "EQUIPPED"), Color::Mint, 30.f);
				break;
			case EItemState::Owned:
				PaintChip(T, ChipPos, LOCTEXT("ChipOwned", "OWNED"), Color::Sky, 30.f);
				break;
			case EItemState::Pending:
				PaintSpinner(T, ChipPos + FVector2D(16.0, 15.0), 11.f, Frame.Now, Color::Sky);
				break;
			case EItemState::Locked:
			{
				int32 Cur = 0, Target = 1;
				S->GetProgress(Item.DefId, Cur, Target);
				const double BarW = TileW - 90.0;
				T.Box(ChipPos + FVector2D(0.0, 10.0), FVector2D(BarW, 12.0), Color::PaperShade, 6.f);
				T.Box(ChipPos + FVector2D(0.0, 10.0), FVector2D(FMath::Max(12.0, BarW * Cur / FMath::Max(Target, 1)), 12.0), Color::Mint, 6.f);
				T.Text(FString::Printf(TEXT("%d/%d"), Cur, Target), Font(EFont::Label, 17.f), ChipPos + FVector2D(BarW + 8.0, 16.0), Color::NavyDim, FVector2D(0.0, 0.5));
				break;
			}
			case EItemState::NotDropped:
				PaintChip(T, ChipPos, LOCTEXT("ChipGift", "GIFT DROP"), Color::Grape, 30.f);
				break;
			case EItemState::ComingSoon:
				PaintChip(T, ChipPos, LOCTEXT("ChipSoon", "SOON"), Color::NavyDim, 30.f);
				break;
			default:
			{
				const bool bPriced = S && S->HasPrice(Item.DefId);
				PaintChip(T, ChipPos, bPriced ? S->GetPriceText(Item.DefId) : LOCTEXT("ChipNoPrice", "NO PRICE"), bPriced ? Color::Sun : Color::PaperShade, 30.f);
				break;
			}
			}
		}

		void PaintDetail(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* S, const FItem& Item, const FVector2D& Pos) const
		{
			const FVector2D Size(DetailW, VisibleRows * TileH + (VisibleRows - 1) * Gap);
			const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - FocusAt) / 0.35), 2.f);
			const FKartPainter Card = P.Transformed(Pos + Size * 0.5, 0.95f + 0.05f * Pop).Faded(KartWidgets::Entrance(Frame, 0.2f));
			Card.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 10.f, Color::White, 5.f);
			// The preview over light stripes drifting across.
			const FVector2D Art(Pos.X + 18.0, Pos.Y + 18.0);
			const FVector2D ArtSize(Size.X - 36.0, 230.0);
			Card.Gradient(Art, ArtSize, Lighter(Item.Visual.Primary, 0.5f), FMath::Lerp(Item.Visual.Primary, Color::Night, 0.15f), true, Metric::Radius);
			for (int32 Stripe = 0; Stripe < 5; ++Stripe)
			{
				const double X = Art.X + FMath::Fmod(Frame.Now * 40.0 + Stripe * 110.0, ArtSize.X + 110.0) - 110.0;
				const double X0 = FMath::Clamp(X, Art.X, Art.X + ArtSize.X);
				const double X1 = FMath::Clamp(X + 46.0, Art.X, Art.X + ArtSize.X);
				if (X1 > X0 + 1.0)
				{
					Card.Box(FVector2D(X0, Art.Y), FVector2D(X1 - X0, ArtSize.Y), Color::WithAlpha(Color::White, 0.12f));
				}
			}
			PaintPreview(Card, Item, Art + ArtSize * 0.5, 96.f, Frame.Now);
			const EItemState State = S ? S->GetState(Item.DefId) : EItemState::ComingSoon;

			double Y = Art.Y + ArtSize.Y + 18.0;
			const float ChipW = PaintChip(Card, FVector2D(Pos.X + 30.0, Y), SlotName(Item.Slot), TabAccent(Tab), 32.f);
			PaintChip(Card, FVector2D(Pos.X + 42.0 + ChipW, Y), LOCTEXT("CosmeticChip", "COSMETIC ONLY"), Color::PaperShade, 32.f);
			Y += 46.0;
			Card.Text(Item.Name, Font(EFont::Display, 44.f), FVector2D(Pos.X + 30.0, Y), Color::Navy);
			Y += 60.0;
			Y += Card.TextWrapped(Item.Description, Font(EFont::Body, 21.f), FVector2D(Pos.X + 32.0, Y), Size.X - 64.0, Color::NavyDim).Y + 12.0;
			PaintDetailState(Card, S, Item, State, FVector2D(Pos.X + 32.0, Y), Size.X - 64.0);

			const FPrimary Primary = PrimaryFor(S, Item);
			FActionButton Button;
			Button.Centre = FVector2D(Pos.X + Size.X * 0.5, Pos.Y + Size.Y - 58.0);
			Button.Size = FVector2D(Size.X - 80.0, 78.0);
			Button.Label = Primary.Label;
			Button.Fill = PrimaryFill(Primary.Look);
			Button.bEnabled = Primary.bEnabled;
			Button.bFocused = true;
			Button.FocusAge = Frame.Now - FocusAt;
			Button.Action = EKartMenuAction::Accept;
			Button.Device = Frame.Device;
			Button.HitId = PrimaryHit;
			PaintActionButton(P.Faded(KartWidgets::Entrance(Frame, 0.3f)), Button);
		}

		/** The detail card's state line: a price, the unlock progress, or how to get it. */
		static void PaintDetailState(const FKartPainter& Card, const FKartStore* S, const FItem& Item, EItemState State, const FVector2D& Line, double Width)
		{
			if (State == EItemState::Locked && S)
			{
				int32 Cur = 0, Target = 1;
				S->GetProgress(Item.DefId, Cur, Target);
				const double BarW = Width;
				Card.Box(Line + FVector2D(0.0, 6.0), FVector2D(BarW, 26.0), Color::PaperShade, 13.f);
				Card.Box(Line + FVector2D(0.0, 6.0), FVector2D(FMath::Max(26.0, BarW * Cur / FMath::Max(Target, 1)), 26.0), Color::Mint, 13.f);
				Card.Text(FText::Format(LOCTEXT("Progress", "{0} of {1}"), Cur, Target), Font(EFont::Label, 18.f), Line + FVector2D(BarW * 0.5, 19.0), Color::Navy, FVector2D(0.5, 0.5));
			}
			else if (State == EItemState::ForSale && S)
			{
				Card.Text(S->GetPriceText(Item.DefId), Font(EFont::Display, 40.f, 2.f), Line + FVector2D(0.0, -4.0), Color::Hot);
				if (Item.Slot == ESlot::Bundle && S->NumOwnedContents(Item.DefId) > 0)
				{
					Card.Text(FText::Format(LOCTEXT("BundlePart", "You own {0} of {1}"), S->NumOwnedContents(Item.DefId), Item.Contents.Num()),
						Font(EFont::Label, 18.f), Line + FVector2D(Width, 20.0), Color::NavyDim, FVector2D(1.0, 0.5));
				}
			}
			else if (State == EItemState::Equipped)
			{
				Card.Text(LOCTEXT("WearingNow", "Wearing it now"), Font(EFont::Button, 24.f), Line, Color::Mint);
			}
			else if (State == EItemState::NotDropped)
			{
				Card.Text(LOCTEXT("DropNote", "Arcade gifts drop now and then while you play. Free."), Font(EFont::Body, 20.f), Line, Color::Grape);
			}
		}

		void PaintToast(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			const double Age = Frame.Now - ToastAt;
			if (ToastText.IsEmpty() || Age > ToastSeconds)
			{
				return;
			}
			const float In = Ease::OutBack(static_cast<float>(Age / 0.3), 2.f) * (1.f - Ease::InCubic(static_cast<float>((Age - 2.6) / 0.4)));
			const FSlateFontInfo F = Font(EFont::Button, 28.f);
			const double W = FKartPainter::Measure(ToastText.ToString(), F).X + 80.0;
			const FVector2D C(Frame.Size.X * 0.5, Frame.SafeMax.Y - 110.0);
			const FKartPainter T = P.Transformed(C, FMath::Max(In, 0.01f)).Faded(FMath::Clamp(In, 0.f, 1.f));
			T.Card(C - FVector2D(W * 0.5, 32.0), FVector2D(W, 64.0), Color::Paper, 32.f, 8.f, ToastAccent, 5.f);
			T.Text(ToastText, F, C, Color::Navy, FVector2D(0.5, 0.5));
		}

		TWeakObjectPtr<AKartFrontEnd> FrontEnd;
		/** The -KartUiShots step this screen stands for (NAME_None in play). */
		FName TourStep;
		bool bTourOpened = false;
		ETab Tab = ETab::Karts;
		TArray<const FItem*> Items;
		int32 Focus = 0;
		/** The grid's first visible row (it scrolls to keep the focus in view). */
		int32 FirstRow = 0;
		double FocusAt = 0.0;
		double TabAt = -10.0;
		FText ToastText;
		FLinearColor ToastAccent = Color::Mint;
		double ToastAt = -10.0;
		/** Only a Restore the player asked for gets a toast (the store restores on its own at start). */
		bool bRestoreRequested = false;
		FKartStore* BoundStore = nullptr;
		FDelegateHandle UnlockHandle;
		FDelegateHandle RestoreHandle;
	};
}

namespace KartScreens
{
	TSharedRef<FKartScreen> MakeStore(AKartFrontEnd* FrontEnd, FName TourStep)
	{
		return MakeShared<FStoreScreen>(FrontEnd, TourStep);
	}
}

#undef LOCTEXT_NAMESPACE
