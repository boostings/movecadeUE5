#include "KartHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Race/KartRaceManager.h"
#include "Race/SKartRaceOverlay.h"

namespace
{
	void Line(UCanvas* Canvas, UFont* Font, const FString& Text, float X, float Y, const FColor& Color, float Scale = 1.f)
	{
		Canvas->SetDrawColor(FColor(0, 0, 0, 160));
		Canvas->DrawText(Font, Text, X + 1.f, Y + 1.f, Scale, Scale);
		Canvas->SetDrawColor(Color);
		Canvas->DrawText(Font, Text, X, Y, Scale, Scale);
	}
}

void AKartHUD::DrawHUD()
{
	Super::DrawHUD();
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
	const MovecadeKart::FKartState& S = Kart->GetKartMovement()->SimState();
	const int32 Laps = Race->GetTrack()->Laps;
	UFont* Large = GEngine->GetLargeFont();
	UFont* Medium = GEngine->GetMediumFont();

	// Bottom-left of this player's view: name in seat colour, place, lap; bottom-right: speed (spec 04 §8.3).
	const float X = 18.f;
	const float Bottom = Canvas->ClipY - 18.f;
	Line(Canvas, Medium, Racer->Name, X, Bottom - 92.f, Racer->Paint.ToFColor(true));

	// Minimap above the name (RaceUI.tsx:597-704): racing only; the track line, every kart as a dot in its
	// paint, and this view's kart larger with a heading tick. x = (x - minX) / scaleX, y = (z - minZ) / scaleZ.
	const MovecadeKart::FTrack* Track = Race->GetTrack();
	if (Race->GetPhase() == EKartRacePhase::Racing && Track->MinimapBounds.bIsValid && Track->Centre.Num() > 1)
	{
		const FVector2D Extent = Track->MinimapBounds.GetSize();
		const float MapH = FMath::Clamp(Canvas->ClipY * 0.3f, 90.f, 220.f);
		const float MapW = static_cast<float>(MapH * Extent.X / FMath::Max(1.0, Extent.Y));
		const FVector2D Origin(X, Bottom - 104.f - MapH);
		auto ToMap = [&](const FVector2D& P)
		{
			const FVector2D T = (P - Track->MinimapBounds.Min) / Extent;
			return Origin + FVector2D(FMath::Clamp(T.X, 0.02, 0.98) * MapW, FMath::Clamp(T.Y, 0.02, 0.98) * MapH);
		};
		FCanvasTileItem Back(Origin - FVector2D(6.0, 6.0), FVector2D(MapW + 12.0, MapH + 12.0), FLinearColor(0.02f, 0.02f, 0.035f, 0.55f));
		Back.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Back);
		const int32 Points = Track->Centre.Num();
		const int32 Stride = FMath::Max(1, Points / 160);
		for (int32 i = 0; i < Points; i += Stride)
		{
			FCanvasLineItem Road(ToMap(Track->Centre[i]), ToMap(Track->Centre[(i + Stride) % Points]));
			Road.SetColor(FLinearColor(1.f, 1.f, 1.f, 0.85f));
			Road.LineThickness = 3.f;
			Canvas->DrawItem(Road);
		}
		// Start/finish marker.
		FCanvasTileItem Finish(ToMap(Track->Checkpoints[0].Position) - FVector2D(4.0, 4.0), FVector2D(8.0, 8.0), FLinearColor::Black);
		Canvas->DrawItem(Finish);
		for (const FKartRacer& Other : Race->GetRacers())
		{
			const FVector2D P = ToMap(Other.Kart->GetKartMovement()->RenderPosition());
			const bool bFocus = Other.Kart == Kart;
			FCanvasNGonItem Dot(P, FVector2D(bFocus ? 7.0 : 5.0), 12, Other.Paint);
			Canvas->DrawItem(Dot);
			if (bFocus)
			{
				const double Yaw = Kart->GetKartMovement()->RenderYaw();
				FCanvasLineItem Heading(P, P + FVector2D(-FMath::Sin(Yaw), -FMath::Cos(Yaw)) * 14.0);
				Heading.SetColor(FLinearColor::White);
				Heading.LineThickness = 2.f;
				Canvas->DrawItem(Heading);
			}
		}
	}
	Line(Canvas, Large, SKartRaceOverlay::Ordinal(Racer->Place), X, Bottom - 72.f, FColor::White, 1.6f);
	Line(Canvas, Medium, FString::Printf(TEXT("Lap %d / %d"), FMath::Min(S.LapsCompleted + 1, Laps), Laps), X, Bottom - 22.f, FColor::White);

	// One unit for the port: SU is metres per second; show km/h (x3.6).
	const FString Speed = FString::Printf(TEXT("%d"), FMath::RoundToInt32(FMath::Abs(S.Speed) * 3.6));
	float W = 0.f, H = 0.f;
	Canvas->TextSize(Large, Speed, W, H, 2.f, 2.f);
	const bool bBoosting = S.BoostMultiplier > 1.0 || S.Turbo > 0.0;
	Line(Canvas, Large, Speed, Canvas->ClipX - 18.f - W - 52.f, Bottom - H, bBoosting ? FColor(255, 196, 64) : FColor::White, 2.f);
	Line(Canvas, Medium, TEXT("km/h"), Canvas->ClipX - 64.f, Bottom - 22.f, FColor(255, 255, 255, 200));

	// Item effects on this view (RaceUI.tsx:265-358): ink blots, explosion flash, Prism rainbow tint.
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

	// Item slot (top centre): roulette strip while rolling, then the item and the button to use it.
	if (Slot.bRolling || Slot.Held != MovecadeKart::EKartItem::None)
	{
		const float SlotW = 200.f, SlotH = 58.f;
		const float SX = (Canvas->ClipX - SlotW) * 0.5f, SY = 74.f;
		Fill(FVector2D(SX, SY), FVector2D(SlotW, SlotH), FLinearColor(0.02f, 0.02f, 0.035f, 0.7f));
		FString Label;
		FColor Color = FColor::White;
		if (Slot.bRolling)
		{
			// 25 items/s easing down to a stop over 1.8 s, then the real item held for 1.2 s.
			const double Elapsed = MovecadeKart::FKartItemWorld::RouletteSeconds - (Slot.RollEnds - Now);
			const double Spin = Elapsed < 1.8 ? 25.0 * (1.8 * Elapsed - 0.5 * Elapsed * Elapsed) / 1.8 : -1.0;
			const int32 Index = Spin < 0.0 ? static_cast<int32>(Slot.Held) : 1 + FMath::FloorToInt32(Spin) % (static_cast<int32>(MovecadeKart::EKartItem::Count) - 1);
			Label = MovecadeKart::ItemName(static_cast<MovecadeKart::EKartItem>(Index));
			Color = Spin < 0.0 ? FColor(255, 220, 120) : FColor(200, 200, 210);
		}
		else
		{
			Label = MovecadeKart::ItemName(Slot.Held);
			Color = FColor(255, 220, 120);
		}
		Canvas->TextSize(Large, Label, W, H, 1.2f, 1.2f);
		Line(Canvas, Large, Label, (Canvas->ClipX - W) * 0.5f, SY + 6.f, Color, 1.2f);
		const FString Hint = Slot.bRolling ? TEXT("Choosing item...") : TEXT("E / X to use");
		Canvas->TextSize(Medium, Hint, W, H);
		Line(Canvas, Medium, Hint, (Canvas->ClipX - W) * 0.5f, SY + 36.f, FColor(255, 255, 255, 190));
	}
	else if (Now - Slot.LastUsedAt < 1.6 && Slot.LastUsed != MovecadeKart::EKartItem::None)
	{
		// Activation popup.
		const FString Used = FString(MovecadeKart::ItemName(Slot.LastUsed)) + TEXT("!");
		Canvas->TextSize(Large, Used, W, H, 1.3f, 1.3f);
		Line(Canvas, Large, Used, (Canvas->ClipX - W) * 0.5f, 84.f, FColor(255, 220, 120), 1.3f);
	}

	FString Status;
	if (Now < Slot.SpinUntil) Status = TEXT("SPUN OUT!");
	else if (S.bWrongWay) Status = TEXT("WRONG WAY");
	else if (S.OffTrackSince >= 0.0) Status = TEXT("Back to the track...");
	else if (S.BoostMultiplier < 1.0)
	{
		Status = S.ActiveBoost == TEXT("EXPLODED") ? TEXT("BOOM!") : S.ActiveBoost == TEXT("LIGHTNING") ? TEXT("Zapped!")
			: S.ActiveBoost == TEXT("SPUN_OUT") ? TEXT("Spun out") : TEXT("Bogged start");
	}
	else if (S.DriftDirection != 0.0) Status = S.DriftPower >= 1.8 ? TEXT("ULTRA") : S.DriftPower >= 1.15 ? TEXT("SUPER") : S.DriftPower >= 0.65 ? TEXT("MINI") : TEXT("Drift");
	else if (bBoosting) Status = S.ActiveBoost.Replace(TEXT("_"), TEXT(" "));
	else if (S.bOnDirt) Status = TEXT("Off-road");
	if (!Status.IsEmpty())
	{
		Canvas->TextSize(Large, Status, W, H, 1.4f, 1.4f);
		Line(Canvas, Large, Status, (Canvas->ClipX - W) * 0.5f, Canvas->ClipY * 0.72f, S.bWrongWay ? FColor(255, 80, 70) : FColor(255, 220, 120), 1.4f);
	}
}
