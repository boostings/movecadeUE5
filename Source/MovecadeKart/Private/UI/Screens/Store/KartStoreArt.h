#pragma once

#include "CoreMinimal.h"
#include "UI/KartWidgets.h"

namespace MovecadeKart::Store
{
	struct FItem;
}

/**
 * What the store screens paint besides their layout: each item's little animated preview (a kart, a driver, a ball,
 * a blade...) drawn in code, and the store's own widgets (chips, the big action button, the lock, the spinner,
 * confetti). Painting only; no store state.
 */
namespace KartStoreUI
{
	FLinearColor Lighter(const FLinearColor& C, float T);
	FLinearColor Darker(const FLinearColor& C, float T);

	/** The item's preview centred on Centre, Size wide (about 2 x Size), animated by Now (0 holds it still). */
	void PaintPreview(const FKartPainter& P, const MovecadeKart::Store::FItem& Item, const FVector2D& Centre, float Size, double Now);

	/** A small padlock (earned items not earned yet). */
	void PaintLock(const FKartPainter& P, const FVector2D& Centre, float Size, const FLinearColor& Fill);
	/** A ring of fading dots turning (a call in flight). */
	void PaintSpinner(const FKartPainter& P, const FVector2D& Centre, float Radius, double Now, const FLinearColor& Fill);
	/** A burst of paper from Centre, Age seconds in (gone after 1.6 s). */
	void PaintConfetti(const FKartPainter& P, const FVector2D& Centre, double Age);
	/** A pill with Label at Pos (Align 0 left edge .. 1 right edge). Returns its width. */
	float PaintChip(const FKartPainter& P, const FVector2D& Pos, const FText& Label, const FLinearColor& Fill, float Height = 36.f, float Align = 0.f);

	/** A big rounded action button; disabled ones are grey and don't record their hit region. */
	struct FActionButton
	{
		FVector2D Centre;
		FVector2D Size;
		FText Label;
		FLinearColor Fill;
		bool bEnabled = true;
		bool bFocused = false;
		/** Seconds since it took focus (it breathes while focused). */
		double FocusAge = 0.0;
		/** The input glyph shown before the label (None: no glyph). */
		EKartMenuAction Action = EKartMenuAction::None;
		EKartInputDevice Device = EKartInputDevice::Keyboard;
		int32 HitId = -1;
	};
	void PaintActionButton(const FKartPainter& P, const FActionButton& Button);
}
