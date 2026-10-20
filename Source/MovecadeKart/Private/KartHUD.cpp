#include "KartHUD.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"

void AKartHUD::DrawHUD()
{
	Super::DrawHUD();
	// The HUD itself (place, lap, speed, item, minimap, banners) is Slate: SKartRaceHud, one layout per view.
	// This canvas pass only keeps the item screen effects under it, clipped to this player's view, until the
	// FX workstream moves them into post-process (RaceUI.tsx:265-358).
	const AKartPawn* Kart = PlayerOwner ? Cast<AKartPawn>(PlayerOwner->GetPawn()) : nullptr;
	const AKartRaceManager* Race = AKartRaceManager::Get(this);
	if (!Kart || !Canvas || !Race || PlayerOwner->GetViewTarget() != Kart)
	{
		return;
	}
	const FKartRacer* Racer = Race->FindRacer(Kart);
	if (!Racer || (Race->GetPhase() != EKartRacePhase::Racing && Race->GetPhase() != EKartRacePhase::Countdown))
	{
		return;
	}

	// Item effects on this view: ink blots, explosion flash, Prism rainbow tint.
	const MovecadeKart::FKartItemWorld& Items = Race->GetItems();
	const MovecadeKart::FKartItemSlot& Slot = Items.Slots()[Racer->Seat];
	const double Now = Race->GetItemTime();
	auto Fill = [this](const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Pos, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	};
	if (Now < Slot.PrismUntil)
	{
		const FLinearColor Tint = FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(GetWorld()->GetTimeSeconds() * 200.0, 256.0)), 255, 255);
		Fill(FVector2D(0, 0), FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(Tint.R, Tint.G, Tint.B, 0.12f));
		const float B = 6.f;
		const FLinearColor Edge(Tint.R, Tint.G, Tint.B, 0.8f);
		Fill(FVector2D(0, 0), FVector2D(Canvas->ClipX, B), Edge);
		Fill(FVector2D(0, Canvas->ClipY - B), FVector2D(Canvas->ClipX, B), Edge);
		Fill(FVector2D(0, 0), FVector2D(B, Canvas->ClipY), Edge);
		Fill(FVector2D(Canvas->ClipX - B, 0), FVector2D(B, Canvas->ClipY), Edge);
	}
	if (Now < Slot.FlashUntil)
	{
		const float Alpha = static_cast<float>((Slot.FlashUntil - Now) / 0.5);
		Fill(FVector2D(0, 0), FVector2D(Canvas->ClipX, Canvas->ClipY), FLinearColor(1.f, 0.45f, 0.05f, 0.75f * Alpha));
	}
	if (Now < Slot.InkUntil)
	{
		// Five dark blots that drip in over 0.5 s.
		const double In = FMath::Clamp((Now - (Slot.InkUntil - 4.0)) / 0.5, 0.0, 1.0);
		const FVector2D Spots[] = { {0.3, 0.2}, {0.7, 0.3}, {0.5, 0.7}, {0.2, 0.6}, {0.8, 0.8} };
		const float Alphas[] = { 0.9f, 0.85f, 0.8f, 0.75f, 0.7f };
		for (int32 i = 0; i < 5; ++i)
		{
			const FVector2D Center(Spots[i].X * Canvas->ClipX, Spots[i].Y * Canvas->ClipY + (1.0 - In) * -40.0);
			FCanvasNGonItem Blot(Center, FVector2D(Canvas->ClipY * 0.28, Canvas->ClipY * 0.28), 24, FLinearColor(0.01f, 0.01f, 0.02f, Alphas[i]));
			Blot.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Blot);
		}
	}
}
