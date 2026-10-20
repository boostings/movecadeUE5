#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "UI/KartScreen.h"
#include "KartPaintWidget.generated.h"

/**
 * A UMG widget that paints with FKartPainter: lets UMG screens (the phone lobby) use the same drawn
 * decorations as the Slate menus (backdrops, button glyphs and the hint bar). Hit-test invisible.
 */
UCLASS()
class MOVECADEKART_API UKartPaintWidget : public UWidget
{
	GENERATED_BODY()

public:
	/** Paints into the widget's allotted rectangle; the frame's safe area is the TV-safe inset of that rectangle. */
	TFunction<void(const FKartPainter&, const FKartScreenFrame&)> OnPaintKart;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
