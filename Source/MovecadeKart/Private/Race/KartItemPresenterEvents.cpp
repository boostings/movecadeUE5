// AKartItemPresenter: one-off effects for the item world's events and the finish cue.
#include "Race/KartItemPresenter.h"

#include "Components/StaticMeshComponent.h"
#include "FX/KartFx.h"
#include "FX/KartFxComponent.h"
#include "FX/KartFxSubsystem.h"
#include "FX/KartItemFx.h"
#include "KartCoords.h"
#include "KartMovementComponent.h"
#include "KartPawn.h"
#include "Race/KartItemPresenterTuning.h"
#include "Race/KartRaceManager.h"

using namespace MovecadeKart;
using namespace MovecadeKart::Fx;

namespace
{
	/** Kart velocity shares the effects inherit, so they trail with the kart instead of hanging in the air. */
	constexpr double FinishInherit = 0.6;
	constexpr double HitInherit = 0.5;
	/** The fireball's centre sits this far over the blast point; its lobes scatter by this share of the radius. */
	constexpr double BlastCentreHeightCm = 60.0;
	constexpr double BlastLobeScatter = 0.12;
	/** Global events shake every view at half strength (kartFxBus.ts:91-125). */
	constexpr double GlobalShakeScale = 0.5;

	UKartFxComponent* FxOf(const AKartPawn* Kart)
	{
		return Kart ? Kart->FindComponentByClass<UKartFxComponent>() : nullptr;
	}
}

void AKartItemPresenter::HandleCue(int32 Seat, FName Cue)
{
	// Finish line: a confetti cannon and a golden flourish over the kart that crossed it.
	static const FName Finish(TEXT("kart.finish"));
	UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this);
	const AKartPawn* Kart = KartAt(Seat);
	if (Cue != Finish || !Fx || !Kart)
	{
		return;
	}
	TArray<FKartFxEmit> Out;
	const FKartFxLight Light = FinishCelebration(Kart->GetActorLocation(), Kart->GetActorForwardVector(), Kart->GetVelocity() * FinishInherit, Out);
	FlashLight(Light, GetWorld()->GetTimeSeconds());
	Fx->Emit(Out);
}

void AKartItemPresenter::Strike(const FVector& Ground, double Time)
{
	if (Strikes.Num() == 0)
	{
		return;
	}
	const int32 i = NextStrike++ % Strikes.Num();
	Strikes[i] = { Time, Ground };
	StrikeMeshes[i]->SetWorldLocationAndRotation(Ground, FRotator(0.0, FMath::FRandRange(0.0, 360.0), 0.0));
}

void AKartItemPresenter::HandleEvents(const TArray<FKartItemEvent>& Events, double Time)
{
	AKartRaceManager* Race = Manager.Get();
	TArray<FKartFxEmit> Out;
	for (const FKartItemEvent& E : Events)
	{
		// Events carry sim (x, z); the height comes from the kart involved.
		const FVector Ground = Coords::ToUE(E.Position, 0.0) + FVector(0.0, 0.0, KartGround(FMath::Max(0, E.Seat)).Z);
		AKartPawn* Kart = KartAt(E.Seat);
		switch (E.Type)
		{
		case EKartItemEventType::Explosion:
		{
			const FVector At(Ground.X, Ground.Y, KartGround(FMath::Max(0, E.Other)).Z);
			FBlast Blast{ At + FVector(0.0, 0.0, BlastCentreHeightCm), E.Radius, Time, {} };
			for (int32 i = 0; i < 3; ++i)
			{
				Blast.Lobes[i] = FMath::VRand() * FVector(1.0, 1.0, 0.4) * E.Radius * 100.0 * BlastLobeScatter;
			}
			ActiveBlasts.Add(Blast);
			ExplosionBurst(At, E.Radius, Out);
			RecentImpacts.Add({ At, Time });
			for (const FKartRacer& Racer : Race->GetRacers())
			{
				Racer.Kart->AddShake(ImpactProfile(EImpact::Explosion).Shake * GlobalShakeScale);
			}
			FlashLight(ExplosionLight(Blast.Center, E.Radius), Time);
			break;
		}
		case EKartItemEventType::Hit:
			if (Kart)
			{
				const FImpactProfile& Profile = ImpactProfile(EImpact::ProjectileHit);
				Kart->AddShake(Profile.Shake);
				ImpactBurst(EImpact::ProjectileHit, Kart->GetActorLocation(), 1.0, Out, -1.0, ItemColors(E.Item).Color);
				RecentImpacts.Add({ Kart->GetActorLocation(), Time });
				const FKartFxLight Light = ProjectileHitFlash(Kart->GetActorLocation(), KartGround(E.Seat).Z, ItemColors(E.Item),
					Kart->GetKartMovement()->Velocity * HitInherit, Out);
				FlashLight(Light, Time);
				if (UKartFxComponent* VictimFx = FxOf(Kart))
				{
					VictimFx->Flash(Profile.Flash, Profile.FlashColor);
				}
			}
			break;
		case EKartItemEventType::SpinOut:
			if (Kart)
			{
				Kart->AddShake(ImpactProfile(EImpact::SpinOut).Shake);
				ImpactBurst(EImpact::SpinOut, Kart->GetActorLocation(), 1.0, Out);
				DizzyStars(Kart->GetActorLocation(), Kart->GetKartMovement()->Velocity, Out);
			}
			break;
		case EKartItemEventType::Bump:
			ImpactBurst(EImpact::Bump, KartGround(E.Seat), 1.0, Out);
			break;
		case EKartItemEventType::Pickup:
			BoxCollectBurst(Coords::ToUE(E.Position, 1.0), Coords::ToUE(E.Position, 0.08), Out);
			break;
		case EKartItemEventType::Used:
			if (!ItemPresenter::IsThrown(E.Item) && E.Item != EKartItem::Rocket)
			{
				Popups.Add({ E.Item, E.Seat, Time });
			}
			if (E.Item == EKartItem::Lightning)
			{
				// Lightning lights up every view at once.
				for (const FKartRacer& Racer : Race->GetRacers())
				{
					if (UKartFxComponent* RacerFx = FxOf(Racer.Kart))
					{
						RacerFx->Zap();
					}
				}
			}
			break;
		case EKartItemEventType::Launch:
		case EKartItemEventType::Drop:
			if (Kart)
			{
				const FItemColors C = ItemColors(E.Item);
				ThrowPuff(Kart->GetActorLocation() + FVector(0.0, 0.0, 50.0), E.Item == EKartItem::Bomb ? C.Emissive : C.Color, Kart->GetKartMovement()->Velocity, Out);
			}
			break;
		case EKartItemEventType::Bounce:
			BounceSparks(Ground, Out);
			break;
		case EKartItemEventType::Lightning:
			if (Kart)
			{
				Strike(Kart->GetActorLocation(), Time);
				LightningStrikeBurst(Kart->GetActorLocation(), Out);
			}
			break;
		case EKartItemEventType::Inked:
			if (Kart)
			{
				InkSplash(Kart->GetActorLocation(), Kart->GetKartMovement()->Velocity, Out);
			}
			break;
		case EKartItemEventType::Invincible:
		case EKartItemEventType::RocketStart:
			if (Kart)
			{
				PowerUpBurst(Kart->GetActorLocation(), E.Type == EKartItemEventType::RocketStart, Kart->GetKartMovement()->Velocity, Out);
			}
			break;
		default:
			break;
		}
	}
	if (UKartFxSubsystem* Fx = UKartFxSubsystem::Get(this))
	{
		Fx->Emit(Out);
	}
}
