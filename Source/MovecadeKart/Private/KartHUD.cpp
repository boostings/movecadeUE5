#include "KartHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
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
	Line(Canvas, Large, SKartRaceOverlay::Ordinal(Racer->Place), X, Bottom - 72.f, FColor::White, 1.6f);
	Line(Canvas, Medium, FString::Printf(TEXT("Lap %d / %d"), FMath::Min(S.LapsCompleted + 1, Laps), Laps), X, Bottom - 22.f, FColor::White);

	// One unit for the port: SU is metres per second; show km/h (x3.6).
	const FString Speed = FString::Printf(TEXT("%d"), FMath::RoundToInt32(FMath::Abs(S.Speed) * 3.6));
	float W = 0.f, H = 0.f;
	Canvas->TextSize(Large, Speed, W, H, 2.f, 2.f);
	const bool bBoosting = S.BoostMultiplier > 1.0 || S.Turbo > 0.0;
	Line(Canvas, Large, Speed, Canvas->ClipX - 18.f - W - 52.f, Bottom - H, bBoosting ? FColor(255, 196, 64) : FColor::White, 2.f);
	Line(Canvas, Medium, TEXT("km/h"), Canvas->ClipX - 64.f, Bottom - 22.f, FColor(255, 255, 255, 200));

	FString Status;
	if (S.bWrongWay) Status = TEXT("WRONG WAY");
	else if (S.OffTrackSince >= 0.0) Status = TEXT("Back to the track...");
	else if (!S.ActiveBoost.IsEmpty() && S.BoostMultiplier < 1.0) Status = TEXT("Bogged start");
	else if (S.DriftDirection != 0.0) Status = S.DriftPower >= 1.8 ? TEXT("ULTRA") : S.DriftPower >= 1.15 ? TEXT("SUPER") : S.DriftPower >= 0.65 ? TEXT("MINI") : TEXT("Drift");
	else if (bBoosting) Status = S.ActiveBoost.Replace(TEXT("_"), TEXT(" "));
	else if (S.bOnDirt) Status = TEXT("Off-road");
	if (!Status.IsEmpty())
	{
		Canvas->TextSize(Large, Status, W, H, 1.4f, 1.4f);
		Line(Canvas, Large, Status, (Canvas->ClipX - W) * 0.5f, Canvas->ClipY * 0.72f, S.bWrongWay ? FColor(255, 80, 70) : FColor(255, 220, 120), 1.4f);
	}
}
