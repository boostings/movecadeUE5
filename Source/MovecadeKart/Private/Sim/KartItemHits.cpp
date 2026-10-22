#include "Sim/KartItems.h"

#include "Sim/KartItemRacers.h"
#include "Sim/KartItemTuning.h"

// What a hit does to a kart: a spin-out or an explosion (00-PLAN.md "better powerups").

namespace MovecadeKart
{
	using namespace ItemTuning;
	using namespace ItemRacers;

	namespace
	{
		/** A hit cancels the kart's turbo and drift. */
		void CancelDrift(FKartState& Kart)
		{
			Kart.Turbo = 0.0;
			Kart.DriftDirection = 0.0;
			Kart.DriftPower = 0.0;
		}
	}

	void FKartItemWorld::SpinOut(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		FKartItemSlot& Slot = ItemSlots[Seat];
		FKartSim& Sim = *Racers[Seat].Sim;
		FKartState& Kart = Sim.MutableState();
		Kart.Speed *= SpinSpeedKeep;
		CancelDrift(Kart);
		Sim.ApplyBoost(SpinSlow.Multiplier, SpinSlow.Seconds, TEXT("SPUN_OUT"));
		Slot.SpinStart = Now;
		Slot.SpinUntil = Now + SpinSeconds;
		Slot.LockUntil = Now + SpinLockSeconds;
		Slot.ProtectedUntil = Now + SpinProtectSeconds;
		Events.Add({ EKartItemEventType::SpinOut, Seat });
	}

	void FKartItemWorld::Explode(const FVector2D& Center, double Radius, int32 Owner, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		Events.Add({ EKartItemEventType::Explosion, -1, Owner, EKartItem::Bomb, Center, Radius });
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			FKartItemSlot& Slot = ItemSlots[Seat];
			if (FVector2D::Distance(PositionOf(Racers[Seat]), Center) >= Radius || IsInvincible(Seat, Now) || Now < Slot.ProtectedUntil)
			{
				continue;
			}
			FKartSim& Sim = *Racers[Seat].Sim;
			FKartState& Kart = Sim.MutableState();
			Kart.Speed = 0.0;
			CancelDrift(Kart);
			Sim.ApplyBoost(ExplodedSlow.Multiplier, ExplodedSlow.Seconds, TEXT("EXPLODED"));
			Slot.ExplodeStart = Now;
			Slot.LockUntil = Now + ExplodeLockSeconds;
			Slot.ProtectedUntil = Now + ExplodeProtectSeconds;
			Slot.FlashUntil = Now + ExplodeFlashSeconds;
		}
	}
}
