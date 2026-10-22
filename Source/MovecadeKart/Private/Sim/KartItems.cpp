#include "Sim/KartItems.h"

#include "Sim/KartItemRacers.h"
#include "Sim/KartItemTuning.h"
#include "Sim/KartSimMath.h"
#include "Sim/KartSimTuning.h"

// The item world's lifecycle and its per-step phases except projectiles: the roulette, box pickups and the
// odds, item use, and invincible karts ramming others. Projectiles are in KartItemProjectiles.cpp, hits in
// KartItemHits.cpp.

namespace MovecadeKart
{
	using namespace ItemTuning;
	using namespace ItemRacers;

	namespace
	{
		/** One entry of an odds pool: the entry count is the weight. */
		struct FPoolEntry
		{
			EKartItem Item;
			int32 Count;
		};

		// store.ts:2019-2070
		const FPoolEntry FrontPool[] = { { EKartItem::Banana, 4 }, { EKartItem::Shot, 3 }, { EKartItem::Nitro, 1 }, { EKartItem::Seeker, 1 } };
		const FPoolEntry MidPoolSmall[] = { { EKartItem::Nitro, 3 }, { EKartItem::Shot, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Banana, 1 }, { EKartItem::TripleNitro, 1 }, { EKartItem::Bomb, 1 } };
		const FPoolEntry MidPoolLarge[] = { { EKartItem::Nitro, 3 }, { EKartItem::Shot, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Banana, 2 }, { EKartItem::TripleNitro, 1 }, { EKartItem::Bomb, 1 }, { EKartItem::Ink, 1 } };
		const FPoolEntry BackPoolSmall[] = { { EKartItem::TripleNitro, 2 }, { EKartItem::Nitro, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Prism, 1 }, { EKartItem::Bomb, 1 } };
		const FPoolEntry BackPoolThree[] = { { EKartItem::TripleNitro, 2 }, { EKartItem::Nitro, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Prism, 1 }, { EKartItem::Ink, 1 }, { EKartItem::Lightning, 1 }, { EKartItem::Bomb, 1 } };
		const FPoolEntry BackPoolLarge[] = { { EKartItem::Nitro, 3 }, { EKartItem::TripleNitro, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Prism, 1 }, { EKartItem::Rocket, 1 }, { EKartItem::Lightning, 1 }, { EKartItem::SkySeeker, 1 }, { EKartItem::Bomb, 1 } };

		/** A weighted pick: one RandRange draw over the pool's total count. */
		template <int32 N>
		EKartItem Pick(const FPoolEntry (&Pool)[N], FRandomStream& Random)
		{
			int32 Total = 0;
			for (const FPoolEntry& Entry : Pool)
			{
				Total += Entry.Count;
			}
			int32 Roll = Random.RandRange(0, Total - 1);
			for (const FPoolEntry& Entry : Pool)
			{
				if (Roll < Entry.Count)
				{
					return Entry.Item;
				}
				Roll -= Entry.Count;
			}
			return Pool[0].Item;
		}
	}

	const TCHAR* ItemName(EKartItem Item)
	{
		switch (Item)
		{
		case EKartItem::Shot: return TEXT("Shot");
		case EKartItem::Seeker: return TEXT("Seeker");
		case EKartItem::SkySeeker: return TEXT("Sky Seeker");
		case EKartItem::Banana: return TEXT("Banana");
		case EKartItem::Nitro: return TEXT("Nitro");
		case EKartItem::TripleNitro: return TEXT("Triple Nitro");
		case EKartItem::Prism: return TEXT("Prism");
		case EKartItem::Lightning: return TEXT("Lightning");
		case EKartItem::Ink: return TEXT("Ink");
		case EKartItem::Bomb: return TEXT("Bomb");
		case EKartItem::Rocket: return TEXT("Rocket");
		default: return TEXT("Empty");
		}
	}

	void FKartItemWorld::Init(const FTrack* InTrack, const TArray<FVector>& BoxPositions, int32 KartCount, int32 Seed)
	{
		check(InTrack);
		TrackData = InTrack;
		ItemBoxes.Reset();
		for (const FVector& Position : BoxPositions)
		{
			FKartItemBox& Box = ItemBoxes.AddDefaulted_GetRef();
			Box.Pos = FVector2D(Position.X, Position.Z);
			Box.Height = Position.Y;
		}
		ItemSlots.SetNum(KartCount);
		Reset(Seed);
	}

	void FKartItemWorld::Reset(int32 Seed)
	{
		for (FKartItemSlot& Slot : ItemSlots)
		{
			Slot = FKartItemSlot();
		}
		for (FKartItemBox& Box : ItemBoxes)
		{
			Box.CollectedAt.Init(-1.0, ItemSlots.Num());
		}
		LiveProjectiles.Reset();
		Random.Initialize(Seed);
		NextId = 1;
	}

	bool FKartItemWorld::IsInvincible(int32 Seat, double Now) const { return Now < ItemSlots[Seat].InvincibleUntil; }
	bool FKartItemWorld::IsInputLocked(int32 Seat, double Now) const { return Now < ItemSlots[Seat].LockUntil; }
	bool FKartItemWorld::IsRocket(int32 Seat, double Now) const { return Now < ItemSlots[Seat].RocketUntil; }

	bool FKartItemWorld::IsBoxVisible(int32 Box, double Now) const
	{
		for (const double CollectedAt : ItemBoxes[Box].CollectedAt)
		{
			if (CollectedAt < 0.0 || Now - CollectedAt >= BoxRespawnSeconds)
			{
				return true;
			}
		}
		return false;
	}

	void FKartItemWorld::Give(int32 Seat, EKartItem Item, double Now)
	{
		FKartItemSlot& Slot = ItemSlots[Seat];
		Slot.Held = Item;
		Slot.bRolling = false;
		Slot.RollEnds = Now;
		Slot.BotUseAt = -1.0;
	}

	EKartItem FKartItemWorld::RollItem(int32 InPosition, int32 KartCount, double TrailingFactor)
	{
		// store.ts getPositionBasedItem:2003-2091: the top fifth draws from the front pool, the middle (unless far
		// behind) from the mid pool, the rest from the back pool; small lobbies use smaller pools.
		const double Ratio = KartCount > 1 ? static_cast<double>(InPosition - 1) / (KartCount - 1) : SoloPositionRatio;
		enum class EBracket { Front, Mid, Back };
		EBracket Bracket = Ratio <= FrontBracketMaxRatio ? EBracket::Front
			: (Ratio <= MidBracketMaxRatio && TrailingFactor < MidBracketMaxTrailing) ? EBracket::Mid : EBracket::Back;
		if (InPosition == 1 && KartCount <= 2)
		{
			Bracket = EBracket::Front;
		}
		switch (Bracket)
		{
		case EBracket::Front: return Pick(FrontPool, Random);
		case EBracket::Mid: return KartCount <= 2 ? Pick(MidPoolSmall, Random) : Pick(MidPoolLarge, Random);
		default: return KartCount <= 2 ? Pick(BackPoolSmall, Random) : KartCount == 3 ? Pick(BackPoolThree, Random) : Pick(BackPoolLarge, Random);
		}
	}

	void FKartItemWorld::Step(TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		check(Racers.Num() == ItemSlots.Num());
		FinishRoulettes(Now, Events);
		CollectBoxes(Racers, Now, Events);
		UseItems(Racers, Now, Events);

		for (FKartItemProjectile& Projectile : LiveProjectiles)
		{
			if (Projectile.bAlive)
			{
				StepProjectile(Projectile, Racers, Now, Events);
			}
		}
		LiveProjectiles.RemoveAll([](const FKartItemProjectile& Projectile) { return !Projectile.bAlive; });

		RamWithInvincibles(Racers, Now, Events);
	}

	void FKartItemWorld::FinishRoulettes(double Now, TArray<FKartItemEvent>& Events)
	{
		// A gameplay timer here; the web ended the roulette from the HUD component.
		for (int32 Seat = 0; Seat < ItemSlots.Num(); ++Seat)
		{
			FKartItemSlot& Slot = ItemSlots[Seat];
			if (Slot.bRolling && Now >= Slot.RollEnds)
			{
				Slot.bRolling = false;
				Events.Add({ EKartItemEventType::Ready, Seat, -1, Slot.Held });
			}
		}
	}

	void FKartItemWorld::CollectBoxes(const TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		double LeaderScore = 0.0;
		for (const FKartItemRacer& Racer : Racers)
		{
			LeaderScore = FMath::Max(LeaderScore, RaceScore(Racer));
		}

		// Per-player 15 s lock, 2.5 m horizontal pickup. A kart already holding an item drives through.
		for (FKartItemBox& Box : ItemBoxes)
		{
			for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
			{
				double& CollectedAt = Box.CollectedAt[Seat];
				if (CollectedAt >= 0.0 && Now - CollectedAt >= BoxRespawnSeconds)
				{
					CollectedAt = -1.0;
				}
				FKartItemSlot& Slot = ItemSlots[Seat];
				if (CollectedAt >= 0.0 || Slot.Held != EKartItem::None || FVector2D::Distance(PositionOf(Racers[Seat]), Box.Pos) >= BoxRadius)
				{
					continue;
				}
				CollectedAt = Now;
				const double Trailing = FMath::Min(1.0, FMath::Max(0.0, LeaderScore - RaceScore(Racers[Seat])) / TrailingScoreRange);
				Slot.Held = RollItem(RacePosition(Seat, Racers), Racers.Num(), Trailing);
				Slot.bRolling = true;
				Slot.RollEnds = Now + RouletteSeconds;
				Slot.BotUseAt = -1.0;
				Events.Add({ EKartItemEventType::Pickup, Seat, -1, Slot.Held, Box.Pos });
			}
		}
	}

	void FKartItemWorld::UseItems(TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		// A player's press edge, or the bot's 1.2-3.7 s beat (kartPilot.ts:222-231).
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			FKartItemSlot& Slot = ItemSlots[Seat];
			if (Slot.Held == EKartItem::None || Slot.bRolling)
			{
				continue;
			}
			bool bUse = Racers[Seat].bUseItem;
			if (Racers[Seat].bBot)
			{
				if (Slot.BotUseAt < 0.0)
				{
					Slot.BotUseAt = Now + BotUseMinDelay + Random.FRand() * BotUseDelayRange;
				}
				bUse = Now >= Slot.BotUseAt;
			}
			if (bUse)
			{
				Use(Seat, Racers, Now, Events);
			}
		}
	}

	void FKartItemWorld::RamWithInvincibles(TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		// Upgrade: a Prism or Rocket kart spins out the karts it touches.
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			if (!IsInvincible(Seat, Now))
			{
				continue;
			}
			for (int32 Other = 0; Other < Racers.Num(); ++Other)
			{
				if (Other != Seat && !IsInvincible(Other, Now) && Now >= ItemSlots[Other].ProtectedUntil
					&& FVector2D::Distance(PositionOf(Racers[Seat]), PositionOf(Racers[Other])) < KartTuning::KartContactDistance)
				{
					Events.Add({ EKartItemEventType::Bump, Other, Seat });
					SpinOut(Other, Racers, Now, Events);
				}
			}
		}
	}

	void FKartItemWorld::Use(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		FKartItemSlot& Slot = ItemSlots[Seat];
		const EKartItem Item = Slot.Held;
		Slot.Held = EKartItem::None;
		Slot.BotUseAt = -1.0;
		Slot.LastUsed = Item;
		Slot.LastUsedAt = Now;
		Events.Add({ EKartItemEventType::Used, Seat, -1, Item });

		FKartSim& Sim = *Racers[Seat].Sim;

		// useItemAction.ts:35-185
		switch (Item)
		{
		case EKartItem::Nitro:
			Sim.ApplyBoost(NitroBoost.Multiplier, NitroBoost.Seconds, TEXT("NITRO"));
			break;
		case EKartItem::TripleNitro:
			Sim.ApplyBoost(TripleNitroBoost.Multiplier, TripleNitroBoost.Seconds, TEXT("TRIPLE_NITRO"));
			break;
		case EKartItem::Prism:
			Slot.InvincibleUntil = FMath::Max(Slot.InvincibleUntil, Now + PrismBoost.Seconds);
			Slot.PrismUntil = Now + PrismBoost.Seconds;
			Sim.ApplyBoost(PrismBoost.Multiplier, PrismBoost.Seconds, TEXT("PRISM"));
			Events.Add({ EKartItemEventType::Invincible, Seat, -1, Item });
			break;
		case EKartItem::Rocket:
			Slot.RocketUntil = Now + RocketBoost.Seconds;
			Slot.InvincibleUntil = FMath::Max(Slot.InvincibleUntil, Now + RocketBoost.Seconds);
			Sim.ApplyBoost(RocketBoost.Multiplier, RocketBoost.Seconds, TEXT("ROCKET"));
			Events.Add({ EKartItemEventType::RocketStart, Seat, -1, Item });
			break;
		case EKartItem::Lightning:
			StrikeOthersWithLightning(Seat, Racers, Now, Events);
			break;
		case EKartItem::Ink:
			InkOthers(Seat, Racers.Num(), Now, Events);
			break;
		case EKartItem::Shot:
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, SpawnProjectile(Item, Seat, Sim.State(), ThrowOffset, ShotSpeed).Pos });
			break;
		case EKartItem::Seeker:
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, SpawnProjectile(Item, Seat, Sim.State(), ThrowOffset, SeekerSpeed).Pos });
			break;
		case EKartItem::SkySeeker:
			LaunchSkySeeker(Seat, Racers, Events);
			break;
		case EKartItem::Banana:
			Events.Add({ EKartItemEventType::Drop, Seat, -1, Item, SpawnProjectile(Item, Seat, Sim.State(), DropOffset, 0.0).Pos });
			break;
		case EKartItem::Bomb:
		{
			FKartItemProjectile& Projectile = SpawnProjectile(Item, Seat, Sim.State(), ThrowOffset, BombThrowSpeed);
			Projectile.Height = BombLaunchHeight;
			Projectile.VerticalSpeed = BombLaunchUpSpeed;
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, Projectile.Pos });
			break;
		}
		default:
			break;
		}
	}

	FKartItemProjectile& FKartItemWorld::SpawnProjectile(EKartItem Item, int32 Owner, const FKartState& Kart, double Offset, double Speed)
	{
		const FVector2D Forward = SimMath::Forward(Kart.Yaw);
		FKartItemProjectile& Projectile = LiveProjectiles.AddDefaulted_GetRef();
		Projectile.Id = NextId++;
		Projectile.Type = Item;
		Projectile.Owner = Owner;
		Projectile.Pos = Kart.Pos + Forward * Offset;
		Projectile.Vel = Forward * Speed;
		return Projectile;
	}

	void FKartItemWorld::LaunchSkySeeker(int32 Seat, const TArray<FKartItemRacer>& Racers, TArray<FKartItemEvent>& Events)
	{
		FKartItemProjectile& Projectile = SpawnProjectile(EKartItem::SkySeeker, Seat, Racers[Seat].Sim->State(), ThrowOffset, SkySeekerSpeed);
		Projectile.Height = SkySeekerLaunchHeight;
		const int32 Target = Leader(Racers);
		if (Target != Seat)
		{
			// Aimed straight at first place on launch (useItemAction.ts:148-162).
			const FVector2D ToTarget = PositionOf(Racers[Target]) - Projectile.Pos;
			if (ToTarget.SizeSquared() > 0.0)
			{
				Projectile.Vel = ToTarget.GetSafeNormal() * SkySeekerSpeed;
			}
		}
		Events.Add({ EKartItemEventType::Launch, Seat, -1, EKartItem::SkySeeker, Projectile.Pos });
	}

	void FKartItemWorld::StrikeOthersWithLightning(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		for (int32 Other = 0; Other < Racers.Num(); ++Other)
		{
			if (Other == Seat || IsInvincible(Other, Now))
			{
				continue;
			}
			FKartSim& Victim = *Racers[Other].Sim;
			ItemSlots[Other].ShrinkUntil = Now + LightningSlow.Seconds;
			Victim.ApplyBoost(LightningSlow.Multiplier, LightningSlow.Seconds, TEXT("LIGHTNING"));
			Victim.MutableState().Speed = FMath::Max(0.0, Victim.State().Speed * LightningSpeedKeep);
			Events.Add({ EKartItemEventType::Lightning, Other, Seat, EKartItem::Lightning });
		}
	}

	void FKartItemWorld::InkOthers(int32 Seat, int32 KartCount, double Now, TArray<FKartItemEvent>& Events)
	{
		for (int32 Other = 0; Other < KartCount; ++Other)
		{
			if (Other != Seat && !IsInvincible(Other, Now))
			{
				ItemSlots[Other].InkUntil = Now + InkSeconds;
				Events.Add({ EKartItemEventType::Inked, Other, Seat, EKartItem::Ink });
			}
		}
	}
}
