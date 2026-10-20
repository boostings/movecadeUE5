#include "KartHUD.h"

void AKartHUD::DrawHUD()
{
	Super::DrawHUD();
	// The HUD itself (place, lap, speed, item, minimap, banners) is Slate: SKartRaceHud, one layout per view.
	// The item screen effects (ink, explosion flash, Prism rainbow, hit and lightning flashes) are a post-process
	// on each kart's camera: FX/KartFxComponent (RaceUI.tsx:265-358).
}
