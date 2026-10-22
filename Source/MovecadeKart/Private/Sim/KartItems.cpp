#include "Sim/KartItems.h"

#include "Sim/KartSimMath.h"
#include "Sim/KartSimTuning.h"

namespace MovecadeKart
{
	namespace
	{
		constexpr double ItemDt = FKartSim::StepSeconds;

		struct FTimedBoost
		{
			double Multiplier;
			double Seconds;
		};

		// ---- Using an item (useItemAction.ts:35-185) ----
		constexpr FTimedBoost NitroBoost = { 2.0, 2.0 };
		constexpr FTimedBoost TripleNitroBoost = { 2.0, 5.0 };
		/** The Prism's boost; it is invincible for as long. */
		constexpr FTimedBoost PrismBoost = { 1.5, 8.0 };
		/** The Rocket's boost; it is invincible and on autopilot for as long. */
		constexpr FTimedBoost RocketBoost = { 2.5, 6.0 };
		/** Lightning shrinks and slows every other kart for this long. */
		constexpr FTimedBoost LightningSlow = { 0.5, 5.0 };
		constexpr double LightningSpeedKeep = 0.3;
		constexpr double InkSeconds = 4.0;
		/** Thrown items spawn this far ahead of the kart; bananas drop as far behind. */
		constexpr double ThrowOffset = 2.0;
		constexpr double DropOffset = -2.0;
		constexpr double ShotSpeed = 80.0;
		constexpr double SeekerSpeed = 90.0;
		constexpr double SkySeekerSpeed = 100.0;
		constexpr double SkySeekerLaunchHeight = 0.5;
		constexpr double BombThrowSpeed = 30.0;
		constexpr double BombLaunchHeight = 1.0;
		constexpr double BombLaunchUpSpeed = 15.0;

		// ---- Projectiles in flight (ItemPhysics.ts, MultiplayerItems.tsx) ----
		constexpr double ShotRetention = 0.75;
		constexpr int32 ShotMaxBounces = 5;
		/** calculateHomingDirection's per-step blend toward the target. */
		constexpr double SeekerHomingRate = 0.08;
		constexpr double SeekerRetention = 0.8;
		constexpr int32 SeekerMaxBounces = 8;
		constexpr double SkySeekerHomingRate = 0.12;
		constexpr double SkySeekerRetention = 0.9;
		constexpr int32 SkySeekerMaxBounces = 10;
		/** The Sky Seeker cruises at this height until it is within SkySeekerDiveDistance, then dives. */
		constexpr double SkySeekerCruiseHeight = 4.0;
		constexpr double SkySeekerDiveDistance = 20.0;
		/** While diving its target height is distance / SkySeekerDiveSlope, never below the launch height. */
		constexpr double SkySeekerDiveSlope = 5.0;
		constexpr double SkySeekerClimbRate = 0.05;
		constexpr double SkySeekerExplosionRadius = 10.0;
		/** calculateBombArc: gravity, and the height at which a bomb lands. */
		constexpr double BombGravity = -20.0;
		constexpr double BombGroundHeight = 0.5;
		constexpr double BombRetention = 0.5;
		/** A landed bomb explodes once it has flown this long. */
		constexpr double BombArmSeconds = 0.5;
		constexpr double BombExplosionRadius = 8.0;
		/** Walls stand this tall; projectiles probe them this far above their own height. */
		constexpr double BarrierHeight = 1.3;
		constexpr double WallProbeLift = 0.3;
		/** calculateShellBounce: the probe reaches this far past the step's travel, at least BounceMinProbe. */
		constexpr double BounceProbeLead = 0.3;
		constexpr double BounceMinProbe = 0.8;
		constexpr double BouncePushOut = 0.3;

		// ---- Hits (00-PLAN.md "better powerups") ----
		constexpr double SpinSpeedKeep = 0.1;
		constexpr FTimedBoost SpinSlow = { 0.3, 1.5 };
		constexpr double SpinSeconds = 1.0;
		constexpr double SpinLockSeconds = 0.8;
		constexpr double SpinProtectSeconds = 1.8;
		constexpr FTimedBoost ExplodedSlow = { 0.2, 2.5 };
		constexpr double ExplodeLockSeconds = 1.2;
		constexpr double ExplodeProtectSeconds = 2.2;
		constexpr double ExplodeFlashSeconds = 0.5;

		// ---- Odds brackets (store.ts getPositionBasedItem:2003-2091), by race position 0 (first) .. 1 (last) ----
		constexpr double FrontBracketMaxRatio = 0.2;
		constexpr double MidBracketMaxRatio = 0.55;
		/** A mid-pack kart this far behind the leader draws from the back pool instead. */
		constexpr double MidBracketMaxTrailing = 0.55;
		/** A lone kart rolls as if mid-pack. */
		constexpr double SoloPositionRatio = 0.5;

		// ---- Boxes and bots ----
		/** Score gap to the leader at which a kart counts as fully trailing for the odds. */
		constexpr double TrailingScoreRange = 120.0;
		/** A bot uses its item after 1.2 to 3.7 s (kartPilot.ts:222-231). */
		constexpr double BotUseMinDelay = 1.2;
		constexpr double BotUseDelayRange = 2.5;

		// ---- Race order (store.ts) ----
		constexpr double ScorePerLap = 100.0;
		constexpr double FinishedScoreBonus = 100000.0;

		/** Race score for item odds: laps * 100 + progress %. */
		double RaceScore(const FKartItemRacer& Racer)
		{
			return Racer.Laps * ScorePerLap + Racer.Progress;
		}

		/** RaceScore with finishers always ahead, for ranking. */
		double RankScore(const FKartItemRacer& Racer)
		{
			return (Racer.bFinished ? FinishedScoreBonus : 0.0) + Racer.Laps * ScorePerLap + Racer.Progress;
		}

		/** store.ts ordering: more laps, or the same laps and more progress. */
		bool IsAheadOf(const FKartItemRacer& Racer, const FKartItemRacer& Other)
		{
			return Racer.Laps > Other.Laps || (Racer.Laps == Other.Laps && Racer.Progress > Other.Progress);
		}

		const FVector2D& PositionOf(const FKartItemRacer& Racer)
		{
			return Racer.Sim->State().Pos;
		}

		/** 1-based race position by RankScore; ties share the better place. */
		int32 RacePosition(int32 Seat, const TArray<FKartItemRacer>& Racers)
		{
			int32 Ahead = 0;
			for (int32 Other = 0; Other < Racers.Num(); ++Other)
			{
				Ahead += (Other != Seat && RankScore(Racers[Other]) > RankScore(Racers[Seat])) ? 1 : 0;
			}
			return Ahead + 1;
		}

		/** store.ts getNearestPlayerAhead:1943-1980. INDEX_NONE when nobody is ahead. */
		int32 NearestAhead(int32 Seat, const TArray<FKartItemRacer>& Racers)
		{
			int32 Best = INDEX_NONE;
			double BestDistance = TNumericLimits<double>::Max();
			const FKartItemRacer& Me = Racers[Seat];
			for (int32 Other = 0; Other < Racers.Num(); ++Other)
			{
				if (Other == Seat || !IsAheadOf(Racers[Other], Me))
				{
					continue;
				}
				const double Distance = FVector2D::Distance(PositionOf(Racers[Other]), PositionOf(Me));
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Other;
				}
			}
			return Best;
		}

		/** store.ts getFirstPlacePlayer:1923-1941 (laps, then progress; the lowest seat wins ties). */
		int32 Leader(const TArray<FKartItemRacer>& Racers)
		{
			int32 Best = 0;
			for (int32 Seat = 1; Seat < Racers.Num(); ++Seat)
			{
				if (IsAheadOf(Racers[Seat], Racers[Best]))
				{
					Best = Seat;
				}
			}
			return Best;
		}

		/** A hit cancels the kart's turbo and drift. */
		void CancelDrift(FKartState& Kart)
		{
			Kart.Turbo = 0.0;
			Kart.DriftDirection = 0.0;
			Kart.DriftPower = 0.0;
		}

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
			Box.Y = Position.Y;
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
		const FKartState& Kart = Sim.State();
		const FVector2D Forward = SimMath::Forward(Kart.Yaw);
		auto Spawn = [&](double Offset, double Speed) -> FKartItemProjectile&
		{
			FKartItemProjectile& Projectile = LiveProjectiles.AddDefaulted_GetRef();
			Projectile.Id = NextId++;
			Projectile.Type = Item;
			Projectile.Owner = Seat;
			Projectile.Pos = Kart.Pos + Forward * Offset;
			Projectile.Vel = Forward * Speed;
			return Projectile;
		};

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
				Events.Add({ EKartItemEventType::Lightning, Other, Seat, Item });
			}
			break;
		case EKartItem::Ink:
			for (int32 Other = 0; Other < Racers.Num(); ++Other)
			{
				if (Other != Seat && !IsInvincible(Other, Now))
				{
					ItemSlots[Other].InkUntil = Now + InkSeconds;
					Events.Add({ EKartItemEventType::Inked, Other, Seat, Item });
				}
			}
			break;
		case EKartItem::Shot:
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, Spawn(ThrowOffset, ShotSpeed).Pos });
			break;
		case EKartItem::Seeker:
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, Spawn(ThrowOffset, SeekerSpeed).Pos });
			break;
		case EKartItem::SkySeeker:
		{
			FKartItemProjectile& Projectile = Spawn(ThrowOffset, SkySeekerSpeed);
			Projectile.Y = SkySeekerLaunchHeight;
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
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, Projectile.Pos });
			break;
		}
		case EKartItem::Banana:
			Events.Add({ EKartItemEventType::Drop, Seat, -1, Item, Spawn(DropOffset, 0.0).Pos });
			break;
		case EKartItem::Bomb:
		{
			FKartItemProjectile& Projectile = Spawn(ThrowOffset, BombThrowSpeed);
			Projectile.Y = BombLaunchHeight;
			Projectile.VY = BombLaunchUpSpeed;
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, Projectile.Pos });
			break;
		}
		default:
			break;
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

		auto Home = [&Projectile](const FVector2D& Target, double Rate, double Speed)
		{
			// calculateHomingDirection: v += (dir * speed - v) * rate per 60 Hz step, renormalised.
			const FVector2D Dir = (Target - Projectile.Pos).GetSafeNormal();
			if (!Dir.IsZero())
			{
				Projectile.Vel += (Dir * Speed - Projectile.Vel) * Rate;
				Projectile.Vel = Projectile.Vel.GetSafeNormal() * Speed;
			}
		};

		const double Travel = Projectile.Vel.Size() * ItemDt;
		switch (Projectile.Type)
		{
		case EKartItem::Shot:
			BounceOrMove(Projectile, Travel, ShotRetention, Events);
			Projectile.bAlive = Projectile.Bounces < ShotMaxBounces;
			break;
		case EKartItem::Seeker:
		{
			const int32 Target = NearestAhead(Projectile.Owner, Racers);
			if (Target != INDEX_NONE)
			{
				Home(PositionOf(Racers[Target]), SeekerHomingRate, SeekerSpeed);
			}
			BounceOrMove(Projectile, Travel, SeekerRetention, Events);
			Projectile.bAlive = Projectile.Bounces < SeekerMaxBounces;
			break;
		}
		case EKartItem::SkySeeker:
		{
			const int32 Target = Leader(Racers);
			if (Target != Projectile.Owner)
			{
				const FVector2D TargetPos = PositionOf(Racers[Target]);
				Home(TargetPos, SkySeekerHomingRate, SkySeekerSpeed);
				const double Distance = FVector2D::Distance(Projectile.Pos, TargetPos);
				const double TargetHeight = Distance > SkySeekerDiveDistance ? SkySeekerCruiseHeight : FMath::Max(SkySeekerLaunchHeight, Distance / SkySeekerDiveSlope);
				Projectile.Y += (TargetHeight - Projectile.Y) * SkySeekerClimbRate;
			}
			// At cruise height it flies over the barriers.
			if (Projectile.Y + WallProbeLift <= BarrierHeight)
			{
				BounceOrMove(Projectile, Travel, SkySeekerRetention, Events);
			}
			else
			{
				Projectile.Pos += Projectile.Vel * ItemDt;
			}
			Projectile.bAlive = Projectile.Bounces < SkySeekerMaxBounces;
			break;
		}
		case EKartItem::Bomb:
		{
			Projectile.Y += Projectile.VY * ItemDt + 0.5 * BombGravity * ItemDt * ItemDt;
			Projectile.VY += BombGravity * ItemDt;
			BounceOrMove(Projectile, Travel, BombRetention, Events);
			if (Projectile.Y <= BombGroundHeight)
			{
				Projectile.Y = BombGroundHeight;
				if (Projectile.Age > BombArmSeconds)
				{
					Explode(Projectile.Pos, BombExplosionRadius, Projectile.Owner, Racers, Now, Events);
					Projectile.bAlive = false;
					return;
				}
			}
			break;
		}
		default:
			break;
		}
		if (!Projectile.bAlive)
		{
			return;
		}
		HitKarts(Projectile, Racers, Now, Events);
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
