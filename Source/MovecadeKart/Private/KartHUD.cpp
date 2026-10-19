#include "KartHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"

void AKartHUD::DrawHUD()
{
	Super::DrawHUD();
	const AKartPawn* Kart = PlayerOwner ? Cast<AKartPawn>(PlayerOwner->GetPawn()) : nullptr;
	if (!Kart || !Kart->GetKartMovement()->HasSim() || !Canvas)
	{
		return;
	}
	const MovecadeKart::FKartState& S = Kart->GetKartMovement()->SimState();
	const int32 Laps = Kart->GetKartMovement()->GetTrack()->Laps;

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Speed %5.1f SU"), S.Speed));
	Lines.Add(FString::Printf(TEXT("Lap %d / %d   next checkpoint %d   progress %.1f%%"), FMath::Min(S.LapsCompleted + 1, Laps), Laps, S.NextCheckpoint, S.ProgressPercent));
	if (S.bFinished)
	{
		Lines.Add(FString::Printf(TEXT("FINISHED in %.2f s"), S.FinishTime));
	}
	if (S.DriftDirection != 0.0)
	{
		Lines.Add(FString::Printf(TEXT("Drift %s  %.2f s"), S.DriftDirection > 0.0 ? TEXT("left") : TEXT("right"), S.DriftPower));
	}
	if (S.BoostTimeRemaining > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("Boost %s x%.2f  %.2f s"), *S.ActiveBoost, S.BoostMultiplier, S.BoostTimeRemaining));
	}
	if (S.bOnDirt) { Lines.Add(TEXT("Off-road x0.75")); }
	if (S.bWrongWay) { Lines.Add(TEXT("WRONG WAY")); }
	if (S.OffTrackSince >= 0.0) { Lines.Add(TEXT("Off track: returning to the route...")); }
	Lines.Add(TEXT(""));
	Lines.Add(TEXT("W/S gas/brake  A/D steer  Space drift  R reset"));
	Lines.Add(TEXT("Pad: A/RT gas  B/LT brake  stick steer  RB/LB drift  Back reset"));

	UFont* Font = GEngine->GetMediumFont();
	float Y = 24.f;
	for (const FString& Line : Lines)
	{
		Canvas->SetDrawColor(FColor::Black);
		Canvas->DrawText(Font, Line, 25.f, Y + 1.f);
		Canvas->SetDrawColor(FColor::White);
		Canvas->DrawText(Font, Line, 24.f, Y);
		Y += 20.f;
	}
}
