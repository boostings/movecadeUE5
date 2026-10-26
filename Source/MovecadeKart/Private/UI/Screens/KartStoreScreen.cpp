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

	/**
	 * Where everything on the store screen sits, in layout units (1920 x 1080 reference) measured from the safe area,
	 * the tile or the card it belongs to. Font sizes are in the same units.
	 */
	struct FStoreLayout
	{
		/** The connection pill in the top right corner, and the Restore purchases chip under it. */
		struct FStatus
		{
			static constexpr double Top = 20.0;
			static constexpr double Height = 44.0;
			static constexpr float Radius = 22.f;
			static constexpr float FontSize = 20.f;
			/** Width beyond the text: the dot and the margins. */
			static constexpr float ExtraWidth = 64.f;
			static constexpr double DotX = 24.0;
			static constexpr float DotRadius = 8.f;
			static constexpr double TextX = 42.0;
			static constexpr double RestoreTop = 76.0;
			static constexpr double RestoreHeight = 50.0;
			static constexpr float RestoreRadius = 25.f;
			static constexpr float RestoreFontSize = 22.f;
			static constexpr float RestoreGlyphHeight = 30.f;
			static constexpr float RestoreExtraWidth = 60.f;
			static constexpr float RestorePadding = 18.f;
			static constexpr float RestoreGlyphGap = 12.f;
		};

		/** The tab row under the header, between the previous and next tab glyphs. */
		struct FTabs
		{
			static constexpr double Left = 56.0;
			static constexpr double Top = 166.0;
			static constexpr double Height = 58.0;
			static constexpr double Gap = 12.0;
			/** Width beyond the label. */
			static constexpr double ExtraWidth = 56.0;
			static constexpr float FontSize = 26.f;
			static constexpr float GlyphHeight = 36.f;
			static constexpr double NextGlyphGap = 8.0;
			/** How far the row drops in from on entrance. */
			static constexpr double EntranceDrop = 40.0;
		};

		/** One item tile in the grid. */
		struct FTile
		{
			static constexpr double ArtInset = 10.0;
			static constexpr double ArtHeight = 116.0;
			static constexpr float PreviewSize = 48.f;
			static constexpr double LockRight = 24.0;
			static constexpr double LockTop = 30.0;
			static constexpr float LockSize = 22.f;
			static constexpr double NameLeft = 14.0;
			static constexpr double NameTop = 134.0;
			/** The name shrinks from the largest size to the smallest to fit the tile, less this margin. */
			static constexpr float NameMaxSize = 21.f;
			static constexpr float NameMinSize = 15.f;
			static constexpr double NameMargin = 26.0;
			static constexpr double StateLeft = 12.0;
			static constexpr double StateFromBottom = 38.0;
			static constexpr float ChipHeight = 30.f;
			/** The unlock progress bar: it leaves room for the "n/m" count after it. */
			static constexpr double BarTop = 10.0;
			static constexpr double BarHeight = 12.0;
			static constexpr double BarShorter = 90.0;
			static constexpr double CountGap = 8.0;
			/** How far a tile drops in from on entrance, and the grid's clip margin. */
			static constexpr double EntranceDrop = 40.0;
			static constexpr double ClipMargin = 12.0;
		};

		/** The focused item's detail card on the right. */
		struct FDetail
		{
			static constexpr double ArtInset = 18.0;
			static constexpr double ArtHeight = 230.0;
			static constexpr float PreviewSize = 96.f;
			static constexpr double TextLeft = 30.0;
			static constexpr double BodyLeft = 32.0;
			static constexpr float ChipHeight = 32.f;
			static constexpr double ChipGap = 12.0;
			static constexpr float NameSize = 44.f;
			static constexpr float BodySize = 21.f;
			static constexpr double ButtonFromBottom = 58.0;
			static constexpr double ButtonHeight = 78.0;
			static constexpr double ButtonMargin = 80.0;
			/** The light stripes drifting over the preview: count, spacing, width, speed (units/s). */
			static constexpr int32 Stripes = 5;
			static constexpr double StripeSpacing = 110.0;
			static constexpr double StripeWidth = 46.0;
			static constexpr double StripeSpeed = 40.0;
		};

		/** The toast above the hint bar. */
		struct FToast
		{
			static constexpr double FromBottom = 110.0;
			static constexpr double Height = 64.0;
			static constexpr double ExtraWidth = 80.0;
			static constexpr float FontSize = 28.f;
		};
	};

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
			const FKartStore* Store = UKartStoreSubsystem::Active();
			// A dark wash over the orbit so the grid reads; the detail side a touch darker.
			P.Gradient(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.72f), Color::WithAlpha(Color::Night, 0.86f), false);
			KartWidgets::Header(P, Frame, LOCTEXT("StoreTitle", "Store"),
				LOCTEXT("StoreSub", "Cosmetics only. Every kart, ball and blade plays exactly the same."), Color::Hot);
			PaintStatus(P, Frame, Store);
			PaintTabs(P, Frame);

			const double Top = Frame.SafeMin.Y + ContentTop;
			PaintGrid(P, Frame, Store, FVector2D(Frame.SafeMin.X + KartWidgets::SlideX(Frame), Top));
			if (Items.IsValidIndex(Focus))
			{
				PaintDetail(P, Frame, Store, *Items[Focus], FVector2D(Frame.SafeMax.X - DetailW, Top));
			}
			PaintToast(P, Frame);
		}

	private:
		// ------------------------------------------------------------------------------------------ Store events

		void Bind()
		{
			FKartStore* Store = UKartStoreSubsystem::Active();
			if (!Store || BoundStore == Store)
			{
				return;
			}
			Unbind();
			BoundStore = Store;
			UnlockHandle = Store->OnUnlocked.AddSP(this, &FStoreScreen::OnUnlocked);
			RestoreHandle = Store->OnRestored.AddSP(this, &FStoreScreen::OnRestored);
			ApprovedHandle = Store->OnApproved.AddSP(this, &FStoreScreen::OnApproved);
		}

		void Unbind()
		{
			if (FKartStore* Store = UKartStoreSubsystem::Active(); Store && Store == BoundStore)
			{
				Store->OnUnlocked.Remove(UnlockHandle);
				Store->OnRestored.Remove(RestoreHandle);
				Store->OnApproved.Remove(ApprovedHandle);
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
				Toast(RestoreDoneText(UKartStoreSubsystem::Active(), bOk), bOk ? Color::Sky : Color::Hot);
			}
		}

		void OnApproved(int32 DefId)
		{
			if (const FItem* Item = FindItem(DefId))
			{
				Toast(ApprovedText(*Item), Color::Mint);
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
			FKartStore* Store = UKartStoreSubsystem::Active();
			if (!Store || !Items.IsValidIndex(Focus))
			{
				Host->PlaySound(EKartUISound::Error);
				return;
			}
			const FItem& Item = *Items[Focus];
			switch (Store->GetState(Item.DefId))
			{
			case EItemState::Owned:
				if (Item.IsEquippable() && Store->Equip(Item.DefId))
				{
					Host->PlaySound(EKartUISound::Go);
					Toast(FText::Format(LOCTEXT("EquippedToast", "Equipped: {0}"), Item.Name), Color::Mint);
					return;
				}
				break;
			case EItemState::Equipped:
				Store->Unequip(Item.Slot);
				Host->PlaySound(EKartUISound::Back);
				return;
			case EItemState::ForSale:
				if (Store->CanPurchase(Item.DefId))
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
			if (FKartStore* Store = UKartStoreSubsystem::Active(); Store && !Store->IsRestoring())
			{
				bRestoreRequested = true;
				Store->Restore();
				Host->PlaySound(EKartUISound::Accept);
				Toast(RestoreStartedText(Store), Color::Sky);
			}
		}

		// -------------------------------------------------------------------------------------------- Painting

		void PaintStatus(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* Store)
		{
			using L = FStoreLayout::FStatus;
			const FKartPainter F = P.Faded(KartWidgets::Entrance(Frame, 0.1f));
			const FStatus Status = StatusFor(Store);
			const FVector2D At(Frame.SafeMax.X, Frame.SafeMin.Y + L::Top);
			const FSlateFontInfo StatusFont = Font(EFont::Label, L::FontSize);
			const float W = static_cast<float>(FKartPainter::Measure(Status.Text.ToString(), StatusFont).X) + L::ExtraWidth;
			F.Box(At - FVector2D(W, 0.0), FVector2D(W, L::Height), Color::WithAlpha(Color::Night, 0.75f), L::Radius, Color::WithAlpha(Color::White, 0.3f), 2.f);
			F.Circle(At + FVector2D(-W + L::DotX, L::Height * 0.5), L::DotRadius, StatusDot(Status.Level));
			F.Text(Status.Text, StatusFont, At + FVector2D(-W + L::TextX, L::Height * 0.5), Color::White, FVector2D(0.0, 0.5));

			// Restore purchases: a clickable chip under the status.
			const FText RestoreLabel = Store && Store->IsRestoring() ? LOCTEXT("RestoringChip", "Restoring...") : LOCTEXT("RestoreChip", "Restore purchases");
			const FSlateFontInfo RF = Font(EFont::Button, L::RestoreFontSize);
			const float GH = L::RestoreGlyphHeight;
			const float RW = static_cast<float>(FKartPainter::Measure(RestoreLabel.ToString(), RF).X) + KartWidgets::GlyphWidth(EKartMenuAction::Alt, Frame.Device, GH) + L::RestoreExtraWidth;
			const FVector2D RPos(Frame.SafeMax.X - RW, Frame.SafeMin.Y + L::RestoreTop);
			F.Hit(RestoreHit, RPos, FVector2D(RW, L::RestoreHeight));
			F.Box(RPos, FVector2D(RW, L::RestoreHeight), Color::WithAlpha(Color::Paper, 0.92f), L::RestoreRadius);
			const double MidY = RPos.Y + L::RestoreHeight * 0.5;
			float X = static_cast<float>(RPos.X) + L::RestorePadding;
			X += KartWidgets::Glyph(F, EKartMenuAction::Alt, Frame.Device, FVector2D(X, MidY), GH) + L::RestoreGlyphGap;
			F.Text(RestoreLabel, RF, FVector2D(X, MidY), Color::Navy, FVector2D(0.0, 0.5));
		}

		void PaintTabs(const FKartPainter& P, const FKartScreenFrame& Frame)
		{
			using L = FStoreLayout::FTabs;
			const FSlateFontInfo TabFont = Font(EFont::Button, L::FontSize);
			const double H = L::Height;
			double X = Frame.SafeMin.X + L::Left;
			const double Y = Frame.SafeMin.Y + L::Top;
			const float In = KartWidgets::Entrance(Frame, 0.05f);
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::PrevTab, Frame.Device, FVector2D(Frame.SafeMin.X, Y + H * 0.5), L::GlyphHeight);
			for (int32 i = 0; i < static_cast<int32>(ETab::Num); ++i)
			{
				const ETab T = static_cast<ETab>(i);
				const bool bOn = T == Tab;
				const FText Name = TabName(T);
				const double W = FKartPainter::Measure(Name.ToString(), TabFont).X + L::ExtraWidth;
				const FVector2D Pos(X, Y + (1.0 - In) * -L::EntranceDrop);
				P.Hit(TabHit + i, Pos, FVector2D(W, H));
				const float Pop = bOn ? 1.f + 0.06f * Ease::OutBack(static_cast<float>((Frame.Now - TabAt) / 0.25), 3.f) : 1.f;
				const FKartPainter TP = P.Transformed(Pos + FVector2D(W, H) * 0.5, Pop).Faded(In);
				TP.Card(Pos, FVector2D(W, H), bOn ? TabAccent(T) : Color::WithAlpha(Color::Paper, 0.9f), static_cast<float>(H * 0.5), 6.f, bOn ? Color::White : FLinearColor::Transparent, 4.f);
				TP.Text(Name, TabFont, Pos + FVector2D(W, H) * 0.5, bOn ? Color::TextOn(TabAccent(T)) : Color::Navy, FVector2D(0.5, 0.5));
				X += W + L::Gap;
			}
			KartWidgets::Glyph(P.Faded(In), EKartMenuAction::NextTab, Frame.Device, FVector2D(X + L::NextGlyphGap, Y + H * 0.5), L::GlyphHeight);
		}

		void PaintGrid(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* Store, const FVector2D& Origin)
		{
			using L = FStoreLayout::FTile;
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
			P.PushClip(Origin - FVector2D(L::ClipMargin, L::ClipMargin), Area + FVector2D(2.0 * L::ClipMargin, 2.0 * L::ClipMargin));
			for (int32 i = 0; i < Items.Num(); ++i)
			{
				const int32 Row = i / Columns - FirstRow;
				if (Row < 0 || Row >= VisibleRows)
				{
					continue;
				}
				const int32 Col = i % Columns;
				const float In = FMath::Min(KartWidgets::Entrance(Frame, 0.08f + (Row * Columns + Col) * 0.025f), TabIn);
				const FVector2D Pos = Origin + FVector2D(Col * (TileW + Gap), Row * (TileH + Gap) + (1.0 - In) * L::EntranceDrop);
				PaintTile(P.Faded(In), Frame, Store, *Items[i], Pos, i, i == Focus);
			}
			P.PopClip();
			// Scroll hints when more rows hide above or below.
			const FVector2D Above = Origin + FVector2D(Area.X * 0.5, -18.0);
			const FVector2D Below = Origin + FVector2D(Area.X * 0.5, Area.Y + 22.0);
			if (FirstRow > 0)
			{
				KartWidgets::Arrow(P.Transformed(Above, 1.f, -90.f), Above, 14.f, true, Color::Sun);
			}
			if (FirstRow + VisibleRows < Rows)
			{
				KartWidgets::Arrow(P.Transformed(Below, 1.f, 90.f), Below, 14.f, true, Color::Sun);
			}
		}

		void PaintTile(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* Store, const FItem& Item, const FVector2D& Pos, int32 Index, bool bFocused) const
		{
			using L = FStoreLayout::FTile;
			const FVector2D Size(TileW, TileH);
			P.Hit(Index, Pos, Size);
			const EItemState State = Store ? Store->GetState(Item.DefId) : EItemState::ComingSoon;
			const float Pop = bFocused ? 1.f + 0.05f * Ease::OutBack(static_cast<float>((Frame.Now - FocusAt) / 0.3), 3.f) : 1.f;
			const FKartPainter T = P.Transformed(Pos + Size * 0.5, Pop);
			const bool bDim = State == EItemState::ComingSoon;
			const bool bUnearned = State == EItemState::Locked || State == EItemState::NotDropped;
			const FLinearColor Edge = bFocused ? Color::Sun : State == EItemState::Equipped ? Color::Mint : FLinearColor::Transparent;
			T.Card(Pos, Size, bDim ? Color::PaperShade : Color::Paper, Metric::Radius, bFocused ? 10.f : 6.f, Edge, bFocused ? 6.f : 4.f);
			const FVector2D Art(Pos.X + L::ArtInset, Pos.Y + L::ArtInset);
			const FVector2D ArtSize(Size.X - 2.0 * L::ArtInset, L::ArtHeight);
			T.Gradient(Art, ArtSize, Lighter(Item.Visual.Primary, 0.6f), Lighter(Item.Visual.Primary, 0.3f), true, Metric::Radius - 6.f);
			// Only the focused tile animates.
			PaintPreview(bDim || bUnearned ? T.Faded(0.55f) : T, Item, Art + ArtSize * 0.5, L::PreviewSize, bFocused ? Frame.Now : 0.0);
			if (bUnearned)
			{
				PaintLock(T, Art + FVector2D(ArtSize.X - L::LockRight, L::LockTop), L::LockSize, Color::Navy);
			}
			// The name shrinks to fit the tile rather than running off it.
			float NameSize = L::NameMaxSize;
			while (NameSize > L::NameMinSize && FKartPainter::Measure(Item.Name.ToString(), Font(EFont::Button, NameSize)).X > Size.X - L::NameMargin)
			{
				NameSize -= 1.f;
			}
			T.Text(Item.Name, Font(EFont::Button, NameSize), FVector2D(Pos.X + L::NameLeft, Pos.Y + L::NameTop), Color::Navy);
			PaintTileState(T, Frame, Store, Item, State, FVector2D(Pos.X + L::StateLeft, Pos.Y + Size.Y - L::StateFromBottom));
		}

		/** The tile's bottom line: a chip for its state, a spinner, or the unlock progress. */
		static void PaintTileState(const FKartPainter& T, const FKartScreenFrame& Frame, const FKartStore* Store, const FItem& Item, EItemState State, const FVector2D& ChipPos)
		{
			using L = FStoreLayout::FTile;
			switch (State)
			{
			case EItemState::Equipped:
				PaintChip(T, ChipPos, LOCTEXT("ChipEquipped", "EQUIPPED"), Color::Mint, L::ChipHeight);
				break;
			case EItemState::Owned:
				PaintChip(T, ChipPos, LOCTEXT("ChipOwned", "OWNED"), Color::Sky, L::ChipHeight);
				break;
			case EItemState::OwnedUnconfirmed:
				PaintChip(T, ChipPos, UnconfirmedChip(Store), Color::PaperShade, L::ChipHeight);
				break;
			case EItemState::AwaitingApproval:
				PaintChip(T, ChipPos, LOCTEXT("ChipAwaiting", "WAITING"), Color::Sky, L::ChipHeight);
				break;
			case EItemState::Pending:
				PaintSpinner(T, ChipPos + FVector2D(16.0, 15.0), 11.f, Frame.Now, Color::Sky);
				break;
			case EItemState::Locked:
			{
				int32 Cur = 0, Target = 1;
				Store->GetProgress(Item.DefId, Cur, Target);
				const double BarW = TileW - L::BarShorter;
				const FVector2D BarPos = ChipPos + FVector2D(0.0, L::BarTop);
				T.Box(BarPos, FVector2D(BarW, L::BarHeight), Color::PaperShade, 6.f);
				T.Box(BarPos, FVector2D(FMath::Max(L::BarHeight, BarW * Cur / FMath::Max(Target, 1)), L::BarHeight), Color::Mint, 6.f);
				T.Text(FString::Printf(TEXT("%d/%d"), Cur, Target), Font(EFont::Label, 17.f), ChipPos + FVector2D(BarW + L::CountGap, 16.0), Color::NavyDim, FVector2D(0.0, 0.5));
				break;
			}
			case EItemState::NotDropped:
				PaintChip(T, ChipPos, LOCTEXT("ChipGift", "GIFT DROP"), Color::Grape, L::ChipHeight);
				break;
			case EItemState::ComingSoon:
				PaintChip(T, ChipPos, LOCTEXT("ChipSoon", "SOON"), Color::NavyDim, L::ChipHeight);
				break;
			default:
			{
				const bool bPriced = Store && Store->HasPrice(Item.DefId);
				PaintChip(T, ChipPos, bPriced ? Store->GetPriceText(Item.DefId) : LOCTEXT("ChipNoPrice", "NO PRICE"), bPriced ? Color::Sun : Color::PaperShade, L::ChipHeight);
				break;
			}
			}
		}

		void PaintDetail(const FKartPainter& P, const FKartScreenFrame& Frame, const FKartStore* Store, const FItem& Item, const FVector2D& Pos) const
		{
			using L = FStoreLayout::FDetail;
			const FVector2D Size(DetailW, VisibleRows * TileH + (VisibleRows - 1) * Gap);
			const float Pop = Ease::OutBack(static_cast<float>((Frame.Now - FocusAt) / 0.35), 2.f);
			const FKartPainter Card = P.Transformed(Pos + Size * 0.5, 0.95f + 0.05f * Pop).Faded(KartWidgets::Entrance(Frame, 0.2f));
			Card.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 10.f, Color::White, 5.f);
			// The preview over light stripes drifting across.
			const FVector2D Art(Pos.X + L::ArtInset, Pos.Y + L::ArtInset);
			const FVector2D ArtSize(Size.X - 2.0 * L::ArtInset, L::ArtHeight);
			Card.Gradient(Art, ArtSize, Lighter(Item.Visual.Primary, 0.5f), FMath::Lerp(Item.Visual.Primary, Color::Night, 0.15f), true, Metric::Radius);
			for (int32 Stripe = 0; Stripe < L::Stripes; ++Stripe)
			{
				const double X = Art.X + FMath::Fmod(Frame.Now * L::StripeSpeed + Stripe * L::StripeSpacing, ArtSize.X + L::StripeSpacing) - L::StripeSpacing;
				const double X0 = FMath::Clamp(X, Art.X, Art.X + ArtSize.X);
				const double X1 = FMath::Clamp(X + L::StripeWidth, Art.X, Art.X + ArtSize.X);
				if (X1 > X0 + 1.0)
				{
					Card.Box(FVector2D(X0, Art.Y), FVector2D(X1 - X0, ArtSize.Y), Color::WithAlpha(Color::White, 0.12f));
				}
			}
			PaintPreview(Card, Item, Art + ArtSize * 0.5, L::PreviewSize, Frame.Now);
			const EItemState State = Store ? Store->GetState(Item.DefId) : EItemState::ComingSoon;

			// The slot and "cosmetic only" chips, the name, the description, then the state line.
			double Y = Art.Y + ArtSize.Y + L::ArtInset;
			const float ChipW = PaintChip(Card, FVector2D(Pos.X + L::TextLeft, Y), SlotName(Item.Slot), TabAccent(Tab), L::ChipHeight);
			PaintChip(Card, FVector2D(Pos.X + (L::TextLeft + L::ChipGap) + ChipW, Y), LOCTEXT("CosmeticChip", "COSMETIC ONLY"), Color::PaperShade, L::ChipHeight);
			Y += 46.0;
			Card.Text(Item.Name, Font(EFont::Display, L::NameSize), FVector2D(Pos.X + L::TextLeft, Y), Color::Navy);
			Y += 60.0;
			const double TextWidth = Size.X - 2.0 * L::BodyLeft;
			Y += Card.TextWrapped(Item.Description, Font(EFont::Body, L::BodySize), FVector2D(Pos.X + L::BodyLeft, Y), TextWidth, Color::NavyDim).Y + 12.0;
			PaintDetailState(Card, Store, Item, State, FVector2D(Pos.X + L::BodyLeft, Y), TextWidth);

			const FPrimaryAction Primary = PrimaryFor(Store, Item);
			FActionButton Button;
			Button.Centre = FVector2D(Pos.X + Size.X * 0.5, Pos.Y + Size.Y - L::ButtonFromBottom);
			Button.Size = FVector2D(Size.X - L::ButtonMargin, L::ButtonHeight);
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
		static void PaintDetailState(const FKartPainter& Card, const FKartStore* Store, const FItem& Item, EItemState State, const FVector2D& Line, double Width)
		{
			if (State == EItemState::Locked && Store)
			{
				int32 Cur = 0, Target = 1;
				Store->GetProgress(Item.DefId, Cur, Target);
				const double BarH = 26.0;
				const FVector2D BarPos = Line + FVector2D(0.0, 6.0);
				Card.Box(BarPos, FVector2D(Width, BarH), Color::PaperShade, 13.f);
				Card.Box(BarPos, FVector2D(FMath::Max(BarH, Width * Cur / FMath::Max(Target, 1)), BarH), Color::Mint, 13.f);
				Card.Text(FText::Format(LOCTEXT("Progress", "{0} of {1}"), Cur, Target), Font(EFont::Label, 18.f), Line + FVector2D(Width * 0.5, 19.0), Color::Navy, FVector2D(0.5, 0.5));
			}
			else if (State == EItemState::ForSale && Store)
			{
				Card.Text(Store->GetPriceText(Item.DefId), Font(EFont::Display, 40.f, 2.f), Line + FVector2D(0.0, -4.0), Color::Hot);
				if (Item.Slot == ESlot::Bundle && Store->NumOwnedContents(Item.DefId) > 0)
				{
					Card.Text(FText::Format(LOCTEXT("BundlePart", "You own {0} of {1}"), Store->NumOwnedContents(Item.DefId), Item.Contents.Num()),
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
			else if (State == EItemState::OwnedUnconfirmed)
			{
				Card.Text(UnconfirmedNote(Store), Font(EFont::Body, 20.f), Line, Color::NavyDim);
			}
			else if (State == EItemState::AwaitingApproval)
			{
				Card.Text(LOCTEXT("AwaitingNote", "Waiting for approval. Nothing charged yet."), Font(EFont::Body, 20.f), Line, Color::NavyDim);
			}
		}

		void PaintToast(const FKartPainter& P, const FKartScreenFrame& Frame) const
		{
			using L = FStoreLayout::FToast;
			const double Age = Frame.Now - ToastAt;
			if (ToastText.IsEmpty() || Age > ToastSeconds)
			{
				return;
			}
			// Pops in over 0.3 s, shrinks away over the last 0.4 s.
			const float In = Ease::OutBack(static_cast<float>(Age / 0.3), 2.f) * (1.f - Ease::InCubic(static_cast<float>((Age - 2.6) / 0.4)));
			const FSlateFontInfo F = Font(EFont::Button, L::FontSize);
			const double W = FKartPainter::Measure(ToastText.ToString(), F).X + L::ExtraWidth;
			const FVector2D C(Frame.Size.X * 0.5, Frame.SafeMax.Y - L::FromBottom);
			const FKartPainter T = P.Transformed(C, FMath::Max(In, 0.01f)).Faded(FMath::Clamp(In, 0.f, 1.f));
			T.Card(C - FVector2D(W * 0.5, L::Height * 0.5), FVector2D(W, L::Height), Color::Paper, static_cast<float>(L::Height * 0.5), 8.f, ToastAccent, 5.f);
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
		FDelegateHandle ApprovedHandle;
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
