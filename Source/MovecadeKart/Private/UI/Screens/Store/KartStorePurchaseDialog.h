#pragma once

#include "CoreMinimal.h"
#include "UI/KartScreen.h"
#include "UI/Screens/Store/KartStorePresenter.h"

/**
 * Buy one item: confirm (the price, what it is, who takes the payment: Steam, Apple or Google), then wait for the
 * platform's checkout, then the result (or "waiting for approval" on mobile). Modal over the store screen, which
 * pushes it. The store finishes the purchase whether or not this dialog is still open.
 */
class FKartStorePurchaseDialog final : public FKartScreen
{
public:
	FKartStorePurchaseDialog(int32 InDefId, KartStoreUI::EDialogPhase InPhase);
	virtual ~FKartStorePurchaseDialog() override;

	virtual FName GetId() const override { return KartStoreUI::DialogId(Phase); }
	virtual bool IsModal() const override { return true; }
	virtual TArray<FKartHint> GetHints() const override { return {}; }
	virtual void OnShown() override;
	virtual bool HandleEvent(const FKartMenuEvent& Event) override;
	virtual void OnHover(int32 HitId) override;
	virtual void OnClick(int32 HitId) override;
	virtual void Paint(const FKartPainter& P, const FKartScreenFrame& Frame) override;

private:
	int32 NumButtons() const;
	bool CanEquip() const;
	void SetPhase(KartStoreUI::EDialogPhase NewPhase);
	/** Button 0 is the action (buy, retry, equip), 1 the way out. */
	void Choose(int32 Button);
	void OnFinished(int32 Def, MovecadeKart::Store::EPurchaseResult Result, const FString& InDetail);
	void PaintButtons(const FKartPainter& P, const FKartPainter& Dialog, const FKartScreenFrame& Frame, const FVector2D& Pos, const FVector2D& Size) const;

	int32 DefId = 0;
	KartStoreUI::EDialogPhase Phase = KartStoreUI::EDialogPhase::Confirm;
	FKartMenuCursor Cursor;
	double FocusAt = 0.0;
	double PhaseAt = 0.0;
	/** The backend's reason for a failure. */
	FString Detail;
	/** The store the purchase callback is bound on (unbound in the destructor if it's still the live one). */
	MovecadeKart::Store::FKartStore* BoundStore = nullptr;
	FDelegateHandle Handle;
};
