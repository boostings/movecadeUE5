#include "Sim/KartItems.h"

#include "Sim/KartItemRacers.h"
#include "Sim/KartItemTuning.h"

// Projectiles in flight: homing, wall bounces, the bomb's arc, lifetimes and kart hits (ItemPhysics.ts,
// MultiplayerItems.tsx).

namespace MovecadeKart
{
	using namespace ItemTuning;
	using namespace ItemRacers;

	namespace
	{
		/** calculateHomingDirection: v += (dir * speed - v) * rate per 60 Hz step, renormalised to Speed. */
		void HomeTowards(FKartItemProjectile& Projectile, const FVector2D& Target, double Rate, double Speed)
		{
			const FVector2D Dir = (Target - Projectile.Pos).GetSafeNormal();
			if (!Dir.IsZero())
			{
				Projectile.Vel += (Dir * Speed - Projectile.Vel) * Rate;
				Projectile.Vel = Projectile.Vel.GetSafeNormal() * Speed;
			}
		}
	}

	bool FKartItemWorld::Bounce(FKartItemProjectile& Projectile, double Travel, double Retention) const
	{
		// ItemPhysics.ts calculateShellBounce: probe ahead, reflect horizontally, keep `Retention` of the speed,
		// push out 0.3 m along the wall normal (turned to face the projectile).
		const double Speed = Projectile.Vel.Size();
		if (Speed <= 0.0)
		{
			return false;
		}
		const FVector2D Dir = Projectile.Vel / Speed;
		double Distance = 0.0;
		FVector2D Normal;
		if (!TrackData->Walls.Raycast(Projectile.Pos, Dir, FMath::Max(BounceMinProbe, Travel + BounceProbeLead), Distance, Normal))
		{
			return false;
		}
		if (FVector2D::DotProduct(Normal, Dir) > 0.0)
		{
			Normal = -Normal;
		}
		Projectile.Vel = (Projectile.Vel - Normal * (2.0 * FVector2D::DotProduct(Projectile.Vel, Normal))) * Retention;
		Projectile.Pos = Projectile.Pos + Dir * Distance + Normal * BouncePushOut;
		++Projectile.Bounces;
		return true;
	}

	void FKartItemWorld::BounceOrMove(FKartItemProjectile& Projectile, double Travel, double Retention, TArray<FKartItemEvent>& Events) const
	{
		if (Bounce(Projectile, Travel, Retention))
		{
			Events.Add({ EKartItemEventType::Bounce, -1, Projectile.Owner, Projectile.Type, Projectile.Pos });
		}
		else
		{
			Projectile.Pos += Projectile.Vel * ItemDt;
		}
	}

	void FKartItemWorld::StepProjectile(FKartItemProjectile& Projectile, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		Projectile.Age += ItemDt;
		const double Lifetime = Projectile.Type == EKartItem::Banana ? BananaLifetime : ThrownLifetime;
		if (Projectile.Age > Lifetime)
		{
			if (Projectile.Type == EKartItem::Bomb)
			{
				Explode(Projectile.Pos, BombExplosionRadius, Projectile.Owner, Racers, Now, Events);
			}
			Projectile.bAlive = false;
			return;
		}

		// The speed before this step's homing sets how far ahead the bounce probe looks.
		const double Travel = Projectile.Vel.Size() * ItemDt;
		switch (Projectile.Type)
		{
		case EKartItem::Shot:
			BounceOrMove(Projectile, Travel, ShotRetention, Events);
			Projectile.bAlive = Projectile.Bounces < ShotMaxBounces;
			break;
		case EKartItem::Seeker:
			FlySeeker(Projectile, Racers, Travel, Events);
			break;
		case EKartItem::SkySeeker:
			FlySkySeeker(Projectile, Racers, Travel, Events);
			break;
		case EKartItem::Bomb:
			FlyBomb(Projectile, Racers, Travel, Now, Events);
			break;
		default:
			// A banana sits where it was dropped.
			break;
		}
		if (Projectile.bAlive)
		{
			HitKarts(Projectile, Racers, Now, Events);
		}
	}

	void FKartItemWorld::FlySeeker(FKartItemProjectile& Projectile, const TArray<FKartItemRacer>& Racers, double Travel, TArray<FKartItemEvent>& Events) const
	{
		const int32 Target = NearestAhead(Projectile.Owner, Racers);
		if (Target != INDEX_NONE)
		{
			HomeTowards(Projectile, PositionOf(Racers[Target]), SeekerHomingRate, SeekerSpeed);
		}
		BounceOrMove(Projectile, Travel, SeekerRetention, Events);
		Projectile.bAlive = Projectile.Bounces < SeekerMaxBounces;
	}

	void FKartItemWorld::FlySkySeeker(FKartItemProjectile& Projectile, const TArray<FKartItemRacer>& Racers, double Travel, TArray<FKartItemEvent>& Events) const
	{
		const int32 Target = Leader(Racers);
		if (Target != Projectile.Owner)
		{
			const FVector2D TargetPos = PositionOf(Racers[Target]);
			HomeTowards(Projectile, TargetPos, SkySeekerHomingRate, SkySeekerSpeed);
			const double Distance = FVector2D::Distance(Projectile.Pos, TargetPos);
			const double TargetHeight = Distance > SkySeekerDiveDistance ? SkySeekerCruiseHeight : FMath::Max(SkySeekerLaunchHeight, Distance / SkySeekerDiveSlope);
			Projectile.Height += (TargetHeight - Projectile.Height) * SkySeekerClimbRate;
		}
		// At cruise height it flies over the barriers.
		if (Projectile.Height + WallProbeLift <= BarrierHeight)
		{
			BounceOrMove(Projectile, Travel, SkySeekerRetention, Events);
		}
		else
		{
			Projectile.Pos += Projectile.Vel * ItemDt;
		}
		Projectile.bAlive = Projectile.Bounces < SkySeekerMaxBounces;
	}

	void FKartItemWorld::FlyBomb(FKartItemProjectile& Projectile, TArray<FKartItemRacer>& Racers, double Travel, double Now, TArray<FKartItemEvent>& Events)
	{
		// calculateBombArc: a ballistic lob that explodes where it lands once armed.
		Projectile.Height += Projectile.VerticalSpeed * ItemDt + 0.5 * BombGravity * ItemDt * ItemDt;
		Projectile.VerticalSpeed += BombGravity * ItemDt;
		BounceOrMove(Projectile, Travel, BombRetention, Events);
		if (Projectile.Height <= BombGroundHeight)
		{
			Projectile.Height = BombGroundHeight;
			if (Projectile.Age > BombArmSeconds)
			{
				Explode(Projectile.Pos, BombExplosionRadius, Projectile.Owner, Racers, Now, Events);
				Projectile.bAlive = false;
			}
		}
	}

	void FKartItemWorld::HitKarts(FKartItemProjectile& Projectile, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		// 1.5 m horizontal; the owner is safe for the first 0.5 s (MultiplayerItems.tsx:578-605). The first kart
		// in seat order takes the hit.
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			if ((Seat == Projectile.Owner && Projectile.Age < OwnerGraceSeconds) || FVector2D::Distance(PositionOf(Racers[Seat]), Projectile.Pos) >= HitRadius)
			{
				continue;
			}
			if (Projectile.Type == EKartItem::Bomb || Projectile.Type == EKartItem::SkySeeker)
			{
				Explode(Projectile.Pos, Projectile.Type == EKartItem::Bomb ? BombExplosionRadius : SkySeekerExplosionRadius, Projectile.Owner, Racers, Now, Events);
			}
			else
			{
				Events.Add({ EKartItemEventType::Hit, Seat, Projectile.Owner, Projectile.Type, Projectile.Pos });
				// An invincible kart eats the projectile.
				if (!IsInvincible(Seat, Now) && Now >= ItemSlots[Seat].ProtectedUntil)
				{
					SpinOut(Seat, Racers, Now, Events);
				}
			}
			Projectile.bAlive = false;
			return;
		}
	}
}
