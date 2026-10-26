#include "UI/Screens/Store/KartStorePurchaseDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "Store/KartStoreSubsystem.h"
#include "UI/Screens/Store/KartStoreArt.h"

using namespace KartUI;
using namespace KartStoreUI;
using namespace MovecadeKart::Store;

namespace
{
	/** The dialog card and its parts, in layout units (1920 x 1080 reference). */
	const FVector2D DialogSize(1000.0, 600.0);
	const FVector2D ArtSize(340.0, 340.0);
	constexpr double DialogPadding = 40.0;
	constexpr double DialogTop = 56.0;
	constexpr float PreviewSize = 130.f;
	constexpr double ButtonHeight = 84.0;
	constexpr double TwoButtonWidth = 420.0;
	constexpr double OneButtonWidth = 520.0;
	/** How far above the card's bottom edge the buttons' centres sit. */
	constexpr double ButtonRise = 82.0;
	/** The text column, top to bottom: the slot chip, the title, then the price (Confirm) or the spinner (Waiting). */
	constexpr float SlotChipHeight = 34.f;
	constexpr double SlotChipAdvance = 52.0;
	constexpr float TitleSize = 46.f;
	constexpr double TitleAdvance = 64.0;
	constexpr float PriceSize = 52.f;
	constexpr double PriceAdvance = 70.0;
	constexpr float SpinnerRadius = 22.f;
	constexpr double SpinnerAdvance = 64.0;
	constexpr float BodySize = 21.f;
	constexpr double LineGap = 10.0;

	double NowSeconds()
	{
		return FSlateApplication::Get().GetCurrentTime();
	}

	FLinearColor PhaseAccent(EDialogPhase Phase)
	{
		return Phase == EDialogPhase::Success ? Color::Mint : Phase == EDialogPhase::Failed ? Color::Hot
			: Phase == EDialogPhase::Cancelled ? Color::NavyDim : Phase == EDialogPhase::Pending ? Color::Sky : Color::Sun;
	}
}

FKartStorePurchaseDialog::FKartStorePurchaseDialog(int32 InDefId, EDialogPhase InPhase) : DefId(InDefId), Phase(InPhase)
{
	Cursor.Count = 2;
	Cursor.Index = 0;
}

FKartStorePurchaseDialog::~FKartStorePurchaseDialog()
{
	if (FKartStore* Store = UKartStoreSubsystem::Active(); Store && Store == BoundStore)
	{
		Store->OnPurchaseFinished.Remove(Handle);
	}
}

void FKartStorePurchaseDialog::OnShown()
{
	FocusAt = PhaseAt = NowSeconds();
	if (FKartStore* Store = UKartStoreSubsystem::Active(); Store && !BoundStore)
	{
		BoundStore = Store;
		Handle = Store->OnPurchaseFinished.AddSP(this, &FKartStorePurchaseDialog::OnFinished);
	}
}

bool FKartStorePurchaseDialog::HandleEvent(const FKartMenuEvent& Event)
{
	switch (Event.Action)
	{
	case EKartMenuAction::Left:
	case EKartMenuAction::Right:
	case EKartMenuAction::Up:
	case EKartMenuAction::Down:
		if (NumButtons() > 1 && Cursor.Move(1))
		{
			FocusAt = NowSeconds();
			Host->PlaySound(EKartUISound::Move);
		}
		return true;
	case EKartMenuAction::Accept:
		Choose(Cursor.Index);
		return true;
	case EKartMenuAction::Back:
		Choose(NumButtons() > 1 ? 1 : 0);
		return true;
	default:
		return true; // modal: nothing reaches the store underneath
	}
}

void FKartStorePurchaseDialog::OnHover(int32 HitId)
{
	if (HitId < NumButtons() && Cursor.Set(HitId))
	{
		FocusAt = NowSeconds();
		Host->PlaySound(EKartUISound::Move);
	}
}

void FKartStorePurchaseDialog::OnClick(int32 HitId)
{
	if (HitId < NumButtons())
	{
		Choose(HitId);
	}
}

void FKartStorePurchaseDialog::Paint(const FKartPainter& P, const FKartScreenFrame& Frame)
{
	const FItem* Item = FindItem(DefId);
	if (!Item)
	{
		return;
	}
	const FKartStore* Store = UKartStoreSubsystem::Active();
	const float In = Ease::OutBack(static_cast<float>(Frame.Age / 0.3), 1.8f);
	P.Box(FVector2D::ZeroVector, Frame.Size, Color::WithAlpha(Color::Night, 0.66f * FMath::Min(1.f, static_cast<float>(Frame.Age / 0.2))));
	const FVector2D& Size = DialogSize;
	const FVector2D Pos = Frame.Centre() - Size * 0.5;
	const FKartPainter D = P.Transformed(Frame.Centre(), 0.7f + 0.3f * In);
	const FLinearColor Accent = PhaseAccent(Phase);
	D.Card(Pos, Size, Color::Paper, Metric::RadiusLarge, 12.f, Color::White, 5.f);
	D.Box(Pos, FVector2D(Size.X, 16.0), Accent, 8.f);

	// The item on the left, the words on the right.
	const FVector2D Art(Pos.X + DialogPadding, Pos.Y + DialogTop);
	D.Gradient(Art, ArtSize, Lighter(Item->Visual.Primary, 0.55f), Lighter(Item->Visual.Primary, 0.2f), true, Metric::Radius);
	const float Pop = Phase == EDialogPhase::Success ? 1.f + 0.12f * Ease::Pulse(static_cast<float>((Frame.Now - PhaseAt) / 0.5)) : 1.f;
	PaintPreview(D.Transformed(Art + ArtSize * 0.5, Pop), *Item, Art + ArtSize * 0.5, PreviewSize, Frame.Now);
	if (Phase == EDialogPhase::Success)
	{
		PaintConfetti(D, Art + ArtSize * 0.5, Frame.Now - PhaseAt);
	}
	const double TX = Art.X + ArtSize.X + DialogPadding;
	const double TW = Pos.X + Size.X - DialogPadding - TX;
	double Y = Pos.Y + DialogTop;
	PaintChip(D, FVector2D(TX, Y), SlotName(Item->Slot), Accent, SlotChipHeight);
	Y += SlotChipAdvance;
	D.Text(DialogTitle(Phase, *Item, StorefrontOf(Store)), Font(EFont::Display, TitleSize), FVector2D(TX, Y), Color::Navy);
	Y += TitleAdvance;
	if (Phase == EDialogPhase::Confirm)
	{
		D.Text(Store ? Store->GetPriceText(DefId) : FText::GetEmpty(), Font(EFont::Display, PriceSize, 2.f), FVector2D(TX, Y), Color::Hot);
		Y += PriceAdvance;
	}
	else if (Phase == EDialogPhase::Waiting)
	{
		PaintSpinner(D, FVector2D(TX + 26.0, Y + 26.0), SpinnerRadius, Frame.Now, Color::Sky);
		Y += SpinnerAdvance;
	}
	for (const FText& Line : DialogLines(Phase, Store, *Item, Detail))
	{
		if (!Line.IsEmpty())
		{
			Y += D.TextWrapped(Line, Font(EFont::Body, BodySize), FVector2D(TX, Y), TW, Color::NavyDim).Y + LineGap;
		}
	}
	PaintButtons(P, D, Frame, Pos, Size);
}

void FKartStorePurchaseDialog::PaintButtons(const FKartPainter& P, const FKartPainter& Dialog, const FKartScreenFrame& Frame, const FVector2D& Pos, const FVector2D& Size) const
{
	// Along the bottom: one centred, or two side by side.
	const FItem* Item = FindItem(DefId);
	const TArray<FText> Labels = DialogButtons(Phase, UKartStoreSubsystem::Active(), *Item);
	const FVector2D ButtonSize(Labels.Num() > 1 ? TwoButtonWidth : OneButtonWidth, ButtonHeight);
	for (int32 i = 0; i < Labels.Num(); ++i)
	{
		const double CX = Labels.Num() > 1 ? Pos.X + Size.X * (i == 0 ? 0.27 : 0.73) : Pos.X + Size.X * 0.5;
		const FVector2D C(CX, Pos.Y + Size.Y - ButtonRise);
		const bool bFocus = Cursor.Index == i;
		FActionButton Button;
		Button.Centre = C;
		Button.Size = ButtonSize;
		Button.Label = Labels[i];
		Button.Fill = i == 0 ? (Phase == EDialogPhase::Confirm ? Color::Sun : Phase == EDialogPhase::Success ? Color::Mint : Color::Sky) : Color::Paper;
		Button.bFocused = bFocus;
		Button.FocusAge = Frame.Now - FocusAt;
		Button.Action = bFocus ? EKartMenuAction::Accept : (i == 1 ? EKartMenuAction::Back : EKartMenuAction::None);
		Button.Device = Frame.Device;
		PaintActionButton(bFocus ? Dialog : Dialog.Faded(0.92f), Button);
		if (bFocus)
		{
			Dialog.Box(C - ButtonSize * 0.5 - FVector2D(6.0, 6.0), ButtonSize + FVector2D(12.0, 12.0), FLinearColor::Transparent, 48.f, Color::Navy, 4.f);
		}
		// Settled hit regions (the transformed painter records none).
		P.Hit(i, C - ButtonSize * 0.5, ButtonSize);
	}
}

int32 FKartStorePurchaseDialog::NumButtons() const
{
	return NumDialogButtons(Phase, CanEquip());
}

bool FKartStorePurchaseDialog::CanEquip() const
{
	const FItem* Item = FindItem(DefId);
	return Item && Item->IsEquippable();
}

void FKartStorePurchaseDialog::SetPhase(EDialogPhase NewPhase)
{
	Phase = NewPhase;
	PhaseAt = FocusAt = NowSeconds();
	Cursor.Count = NumButtons();
	Cursor.Index = 0;
}

void FKartStorePurchaseDialog::Choose(int32 Button)
{
	FKartStore* Store = UKartStoreSubsystem::Active();
	switch (Phase)
	{
	case EDialogPhase::Confirm:
		if (Button == 0 && Store && Store->Purchase(DefId))
		{
			Host->PlaySound(EKartUISound::Accept);
			SetPhase(EDialogPhase::Waiting);
		}
		else if (Button == 0)
		{
			Host->PlaySound(EKartUISound::Error);
		}
		else
		{
			Host->PlaySound(EKartUISound::Back);
			Host->PopScreen();
		}
		break;
	case EDialogPhase::Failed:
		if (Button == 0)
		{
			Host->PlaySound(EKartUISound::Accept);
			SetPhase(EDialogPhase::Confirm);
		}
		else
		{
			Host->PlaySound(EKartUISound::Back);
			Host->PopScreen();
		}
		break;
	case EDialogPhase::Success:
		if (Button == 0 && CanEquip() && Store)
		{
			Store->Equip(DefId);
			Host->PlaySound(EKartUISound::Go);
		}
		else
		{
			Host->PlaySound(EKartUISound::Back);
		}
		Host->PopScreen();
		break;
	default:
		Host->PlaySound(EKartUISound::Back);
		Host->PopScreen();
		break;
	}
}

void FKartStorePurchaseDialog::OnFinished(int32 Def, EPurchaseResult Result, const FString& InDetail)
{
	if (Def != DefId || Phase != EDialogPhase::Waiting)
	{
		return;
	}
	Detail = InDetail;
	SetPhase(PhaseAfter(Result));
	if (Host)
	{
		Host->PlaySound(Phase == EDialogPhase::Success ? EKartUISound::Go : Phase == EDialogPhase::Pending ? EKartUISound::Tick : EKartUISound::Error);
	}
}
