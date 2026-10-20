#pragma once

#include "CoreMinimal.h"
#include "Sim/KartItems.h"

class FKartPainter;

namespace KartItemIcons
{
	/** A flat, generated icon for Item, centred on Centre and about Size across (no external art). */
	void Draw(const FKartPainter& P, MovecadeKart::EKartItem Item, const FVector2D& Centre, float Size, double Now);
	/** The colour that identifies the item (roulette flashes, popups). */
	FLinearColor Tint(MovecadeKart::EKartItem Item);
}
