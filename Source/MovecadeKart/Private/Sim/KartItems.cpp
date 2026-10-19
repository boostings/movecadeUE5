#include "Sim/KartItems.h"

namespace MovecadeKart
{
	namespace
	{
		constexpr double ItemDt = FKartSim::StepSeconds;
		constexpr double BarrierHeight = 1.3;

		FVector2D ItemForward(double Yaw)
		{
			return FVector2D(-FMath::Sin(Yaw), -FMath::Cos(Yaw));
		}

		double ItemScore(const FKartItemRacer& R)
		{
			return (R.bFinished ? 100000.0 : 0.0) + R.Laps * 100.0 + R.Progress;
		}

		struct FPoolEntry
		{
			EKartItem Item;
			int32 Count;
		};

		// store.ts:2019-2070, entry counts are the weights.
		const FPoolEntry Front[] = { { EKartItem::Banana, 4 }, { EKartItem::Shot, 3 }, { EKartItem::Nitro, 1 }, { EKartItem::Seeker, 1 } };
		const FPoolEntry MidSmall[] = { { EKartItem::Nitro, 3 }, { EKartItem::Shot, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Banana, 1 }, { EKartItem::TripleNitro, 1 }, { EKartItem::Bomb, 1 } };
		const FPoolEntry MidLarge[] = { { EKartItem::Nitro, 3 }, { EKartItem::Shot, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Banana, 2 }, { EKartItem::TripleNitro, 1 }, { EKartItem::Bomb, 1 }, { EKartItem::Ink, 1 } };
		const FPoolEntry BackSmall[] = { { EKartItem::TripleNitro, 2 }, { EKartItem::Nitro, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Prism, 1 }, { EKartItem::Bomb, 1 } };
		const FPoolEntry BackThree[] = { { EKartItem::TripleNitro, 2 }, { EKartItem::Nitro, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Prism, 1 }, { EKartItem::Ink, 1 }, { EKartItem::Lightning, 1 }, { EKartItem::Bomb, 1 } };
		const FPoolEntry BackLarge[] = { { EKartItem::Nitro, 3 }, { EKartItem::TripleNitro, 2 }, { EKartItem::Seeker, 2 }, { EKartItem::Prism, 1 }, { EKartItem::Rocket, 1 }, { EKartItem::Lightning, 1 }, { EKartItem::SkySeeker, 1 }, { EKartItem::Bomb, 1 } };

		template <int32 N>
		EKartItem Pick(const FPoolEntry (&Pool)[N], FRandomStream& Random)
		{
			int32 Total = 0;
			for (const FPoolEntry& E : Pool) Total += E.Count;
			int32 Roll = Random.RandRange(0, Total - 1);
			for (const FPoolEntry& E : Pool)
			{
				if (Roll < E.Count) return E.Item;
				Roll -= E.Count;
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
		T = InTrack;
		BoxesArr.Reset();
		for (const FVector& P : BoxPositions)
		{
			FKartItemBox& Box = BoxesArr.AddDefaulted_GetRef();
			Box.Pos = FVector2D(P.X, P.Z);
			Box.Y = P.Y;
		}
		SlotsArr.SetNum(KartCount);
		Reset(Seed);
	}

	void FKartItemWorld::Reset(int32 Seed)
	{
		for (FKartItemSlot& Slot : SlotsArr)
		{
			Slot = FKartItemSlot();
		}
		for (FKartItemBox& Box : BoxesArr)
		{
			Box.CollectedAt.Init(-1.0, SlotsArr.Num());
		}
		ProjectilesArr.Reset();
		Random.Initialize(Seed);
		NextId = 1;
	}

	bool FKartItemWorld::IsInvincible(int32 Seat, double Now) const { return Now < SlotsArr[Seat].InvincibleUntil; }
	bool FKartItemWorld::IsInputLocked(int32 Seat, double Now) const { return Now < SlotsArr[Seat].LockUntil; }
	bool FKartItemWorld::IsRocket(int32 Seat, double Now) const { return Now < SlotsArr[Seat].RocketUntil; }

	bool FKartItemWorld::IsBoxVisible(int32 Box, double Now) const
	{
		for (const double At : BoxesArr[Box].CollectedAt)
		{
			if (At < 0.0 || Now - At >= BoxRespawnSeconds)
			{
				return true;
			}
		}
		return false;
	}

	void FKartItemWorld::Give(int32 Seat, EKartItem Item, double Now)
	{
		FKartItemSlot& Slot = SlotsArr[Seat];
		Slot.Held = Item;
		Slot.bRolling = false;
		Slot.RollEnds = Now;
		Slot.BotUseAt = -1.0;
	}

	EKartItem FKartItemWorld::RollItem(int32 InPosition, int32 KartCount, double TrailingFactor)
	{
		// store.ts getPositionBasedItem:2003-2091
		const double Ratio = KartCount > 1 ? static_cast<double>(InPosition - 1) / (KartCount - 1) : 0.5;
		enum { BracketFront, BracketMid, BracketBack } Bracket = Ratio <= 0.2 ? BracketFront : (Ratio <= 0.55 && TrailingFactor < 0.55) ? BracketMid : BracketBack;
		if (InPosition == 1 && KartCount <= 2)
		{
			Bracket = BracketFront;
		}
		switch (Bracket)
		{
		case BracketFront: return Pick(Front, Random);
		case BracketMid: return KartCount <= 2 ? Pick(MidSmall, Random) : Pick(MidLarge, Random);
		default: return KartCount <= 2 ? Pick(BackSmall, Random) : KartCount == 3 ? Pick(BackThree, Random) : Pick(BackLarge, Random);
		}
	}

	int32 FKartItemWorld::Position(int32 Seat, const TArray<FKartItemRacer>& Racers) const
	{
		int32 Ahead = 0;
		for (int32 i = 0; i < Racers.Num(); ++i)
		{
			Ahead += (i != Seat && ItemScore(Racers[i]) > ItemScore(Racers[Seat])) ? 1 : 0;
		}
		return Ahead + 1;
	}

	int32 FKartItemWorld::NearestAhead(int32 Seat, const TArray<FKartItemRacer>& Racers) const
	{
		// store.ts getNearestPlayerAhead:1943-1980
		int32 Best = -1;
		double BestDistance = TNumericLimits<double>::Max();
		const FKartItemRacer& Me = Racers[Seat];
		for (int32 i = 0; i < Racers.Num(); ++i)
		{
			const FKartItemRacer& R = Racers[i];
			const bool bAhead = R.Laps > Me.Laps || (R.Laps == Me.Laps && R.Progress > Me.Progress);
			if (i == Seat || !bAhead)
			{
				continue;
			}
			const double D = FVector2D::Distance(R.Sim->State().Pos, Me.Sim->State().Pos);
			if (D < BestDistance)
			{
				BestDistance = D;
				Best = i;
			}
		}
		return Best;
	}

	int32 FKartItemWorld::Leader(const TArray<FKartItemRacer>& Racers) const
	{
		// store.ts getFirstPlacePlayer:1923-1941 (laps, then progress)
		int32 Best = 0;
		for (int32 i = 1; i < Racers.Num(); ++i)
		{
			const FKartItemRacer& R = Racers[i];
			const FKartItemRacer& B = Racers[Best];
			if (R.Laps > B.Laps || (R.Laps == B.Laps && R.Progress > B.Progress))
			{
				Best = i;
			}
		}
		return Best;
	}

	void FKartItemWorld::Step(TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		check(Racers.Num() == SlotsArr.Num());
		double LeaderScore = 0.0;
		for (const FKartItemRacer& R : Racers)
		{
			LeaderScore = FMath::Max(LeaderScore, R.Laps * 100.0 + R.Progress);
		}

		// Roulette (a gameplay timer here; the web ended it from the HUD component).
		for (int32 Seat = 0; Seat < SlotsArr.Num(); ++Seat)
		{
			FKartItemSlot& Slot = SlotsArr[Seat];
			if (Slot.bRolling && Now >= Slot.RollEnds)
			{
				Slot.bRolling = false;
				Events.Add({ EKartItemEventType::Ready, Seat, -1, Slot.Held });
			}
		}

		// Item boxes: per-player 15 s lock, 2.5 m horizontal pickup.
		for (FKartItemBox& Box : BoxesArr)
		{
			for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
			{
				double& At = Box.CollectedAt[Seat];
				if (At >= 0.0 && Now - At >= BoxRespawnSeconds)
				{
					At = -1.0;
				}
				FKartItemSlot& Slot = SlotsArr[Seat];
				if (At >= 0.0 || Slot.Held != EKartItem::None || FVector2D::Distance(Racers[Seat].Sim->State().Pos, Box.Pos) >= BoxRadius)
				{
					continue;
				}
				At = Now;
				const FKartItemRacer& Me = Racers[Seat];
				const double Trailing = FMath::Min(1.0, FMath::Max(0.0, LeaderScore - (Me.Laps * 100.0 + Me.Progress)) / 120.0);
				Slot.Held = RollItem(Position(Seat, Racers), Racers.Num(), Trailing);
				Slot.bRolling = true;
				Slot.RollEnds = Now + RouletteSeconds;
				Slot.BotUseAt = -1.0;
				Events.Add({ EKartItemEventType::Pickup, Seat, -1, Slot.Held, Box.Pos });
			}
		}

		// Use: phone/keyboard edge, or the bot's 1.2-3.7 s beat (kartPilot.ts:222-231).
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			FKartItemSlot& Slot = SlotsArr[Seat];
			if (Slot.Held == EKartItem::None || Slot.bRolling)
			{
				continue;
			}
			bool bUse = Racers[Seat].bUseItem;
			if (Racers[Seat].bBot)
			{
				if (Slot.BotUseAt < 0.0)
				{
					Slot.BotUseAt = Now + 1.2 + Random.FRand() * 2.5;
				}
				bUse = Now >= Slot.BotUseAt;
			}
			if (bUse)
			{
				Use(Seat, Racers, Now, Events);
			}
		}

		for (FKartItemProjectile& P : ProjectilesArr)
		{
			if (P.bAlive)
			{
				StepProjectile(P, Racers, Now, Events);
			}
		}
		ProjectilesArr.RemoveAll([](const FKartItemProjectile& P) { return !P.bAlive; });

		// Upgrade: a Prism or Rocket kart spins out the karts it touches.
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			if (!IsInvincible(Seat, Now))
			{
				continue;
			}
			for (int32 Other = 0; Other < Racers.Num(); ++Other)
			{
				if (Other != Seat && !IsInvincible(Other, Now) && Now >= SlotsArr[Other].ProtectedUntil
					&& FVector2D::Distance(Racers[Seat].Sim->State().Pos, Racers[Other].Sim->State().Pos) < 1.8)
				{
					Events.Add({ EKartItemEventType::Bump, Other, Seat });
					SpinOut(Other, Racers, Now, Events);
				}
			}
		}
	}

	void FKartItemWorld::Use(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		FKartItemSlot& Slot = SlotsArr[Seat];
		const EKartItem Item = Slot.Held;
		Slot.Held = EKartItem::None;
		Slot.BotUseAt = -1.0;
		Slot.LastUsed = Item;
		Slot.LastUsedAt = Now;
		Events.Add({ EKartItemEventType::Used, Seat, -1, Item });

		FKartSim& Sim = *Racers[Seat].Sim;
		const FKartState& S = Sim.State();
		const FVector2D Fwd = ItemForward(S.Yaw);
		auto Spawn = [&](double Offset, double Speed) -> FKartItemProjectile&
		{
			FKartItemProjectile& P = ProjectilesArr.AddDefaulted_GetRef();
			P.Id = NextId++;
			P.Type = Item;
			P.Owner = Seat;
			P.Pos = S.Pos + Fwd * Offset;
			P.Vel = Fwd * Speed;
			return P;
		};

		// useItemAction.ts:35-185
		switch (Item)
		{
		case EKartItem::Nitro:
			Sim.ApplyBoost(2.0, 2.0, TEXT("NITRO"));
			break;
		case EKartItem::TripleNitro:
			Sim.ApplyBoost(2.0, 5.0, TEXT("TRIPLE_NITRO"));
			break;
		case EKartItem::Prism:
			Slot.InvincibleUntil = FMath::Max(Slot.InvincibleUntil, Now + 8.0);
			Slot.PrismUntil = Now + 8.0;
			Sim.ApplyBoost(1.5, 8.0, TEXT("PRISM"));
			Events.Add({ EKartItemEventType::Invincible, Seat, -1, Item });
			break;
		case EKartItem::Rocket:
			Slot.RocketUntil = Now + 6.0;
			Slot.InvincibleUntil = FMath::Max(Slot.InvincibleUntil, Now + 6.0);
			Sim.ApplyBoost(2.5, 6.0, TEXT("ROCKET"));
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
				SlotsArr[Other].ShrinkUntil = Now + 5.0;
				Victim.ApplyBoost(0.5, 5.0, TEXT("LIGHTNING"));
				Victim.MutableState().Speed = FMath::Max(0.0, Victim.State().Speed * 0.3);
				Events.Add({ EKartItemEventType::Lightning, Other, Seat, Item });
			}
			break;
		case EKartItem::Ink:
			for (int32 Other = 0; Other < Racers.Num(); ++Other)
			{
				if (Other != Seat && !IsInvincible(Other, Now))
				{
					SlotsArr[Other].InkUntil = Now + 4.0;
					Events.Add({ EKartItemEventType::Inked, Other, Seat, Item });
				}
			}
			break;
		case EKartItem::Shot:
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, Spawn(2.0, 80.0).Pos });
			break;
		case EKartItem::Seeker:
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, Spawn(2.0, 90.0).Pos });
			break;
		case EKartItem::SkySeeker:
		{
			FKartItemProjectile& P = Spawn(2.0, 100.0);
			P.Y = 0.5;
			const int32 Target = Leader(Racers);
			if (Target != Seat)
			{
				// Aimed straight at first place on launch (useItemAction.ts:148-162).
				const FVector2D To = Racers[Target].Sim->State().Pos - P.Pos;
				if (To.SizeSquared() > 0.0)
				{
					P.Vel = To.GetSafeNormal() * 100.0;
				}
			}
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, P.Pos });
			break;
		}
		case EKartItem::Banana:
			Events.Add({ EKartItemEventType::Drop, Seat, -1, Item, Spawn(-2.0, 0.0).Pos });
			break;
		case EKartItem::Bomb:
		{
			FKartItemProjectile& P = Spawn(2.0, 30.0);
			P.Y = 1.0;
			P.VY = 15.0;
			Events.Add({ EKartItemEventType::Launch, Seat, -1, Item, P.Pos });
			break;
		}
		default:
			break;
		}
	}

	bool FKartItemWorld::Bounce(FKartItemProjectile& P, double Travel, double Retention)
	{
		// ItemPhysics.ts calculateShellBounce: probe ahead, reflect horizontally, keep `Retention` of the speed,
		// push out 0.3 m along the wall normal (turned to face the projectile).
		const double Speed = P.Vel.Size();
		if (Speed <= 0.0)
		{
			return false;
		}
		const FVector2D Dir = P.Vel / Speed;
		double Distance = 0.0;
		FVector2D Normal;
		if (!T->Walls.Raycast(P.Pos, Dir, FMath::Max(0.8, Travel + 0.3), Distance, Normal))
		{
			return false;
		}
		if (FVector2D::DotProduct(Normal, Dir) > 0.0)
		{
			Normal = -Normal;
		}
		P.Vel = (P.Vel - Normal * (2.0 * FVector2D::DotProduct(P.Vel, Normal))) * Retention;
		P.Pos = P.Pos + Dir * Distance + Normal * 0.3;
		++P.Bounces;
		return true;
	}

	void FKartItemWorld::StepProjectile(FKartItemProjectile& P, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		P.Age += ItemDt;
		const double Lifetime = P.Type == EKartItem::Banana ? BananaLifetime : ThrownLifetime;
		if (P.Age > Lifetime)
		{
			if (P.Type == EKartItem::Bomb)
			{
				Explode(P.Pos, 8.0, P.Owner, Racers, Now, Events);
			}
			P.bAlive = false;
			return;
		}

		auto Home = [&P](const FVector2D& Target, double Rate, double Speed)
		{
			// calculateHomingDirection: v += (dir * speed - v) * rate per 60 Hz step, renormalised.
			const FVector2D Dir = (Target - P.Pos).GetSafeNormal();
			if (!Dir.IsZero())
			{
				P.Vel += (Dir * Speed - P.Vel) * Rate;
				P.Vel = P.Vel.GetSafeNormal() * Speed;
			}
		};

		const double Travel = P.Vel.Size() * ItemDt;
		switch (P.Type)
		{
		case EKartItem::Shot:
			if (Bounce(P, Travel, 0.75)) { Events.Add({ EKartItemEventType::Bounce, -1, P.Owner, P.Type, P.Pos }); }
			else { P.Pos += P.Vel * ItemDt; }
			P.bAlive = P.Bounces < 5;
			break;
		case EKartItem::Seeker:
		{
			const int32 Target = NearestAhead(P.Owner, Racers);
			if (Target >= 0)
			{
				Home(Racers[Target].Sim->State().Pos, 0.08, 90.0);
			}
			if (Bounce(P, Travel, 0.8)) { Events.Add({ EKartItemEventType::Bounce, -1, P.Owner, P.Type, P.Pos }); }
			else { P.Pos += P.Vel * ItemDt; }
			P.bAlive = P.Bounces < 8;
			break;
		}
		case EKartItem::SkySeeker:
		{
			const int32 Target = Leader(Racers);
			if (Target != P.Owner)
			{
				const FVector2D TargetPos = Racers[Target].Sim->State().Pos;
				Home(TargetPos, 0.12, 100.0);
				const double Distance = FVector2D::Distance(P.Pos, TargetPos);
				const double TargetHeight = Distance > 20.0 ? 4.0 : FMath::Max(0.5, Distance / 5.0);
				P.Y += (TargetHeight - P.Y) * 0.05;
			}
			// Walls are probed at y + 0.3, so at cruise height it flies over the 1.3 m barriers.
			if (P.Y + 0.3 <= BarrierHeight && Bounce(P, Travel, 0.9)) { Events.Add({ EKartItemEventType::Bounce, -1, P.Owner, P.Type, P.Pos }); }
			else { P.Pos += P.Vel * ItemDt; }
			P.bAlive = P.Bounces < 10;
			break;
		}
		case EKartItem::Bomb:
		{
			// calculateBombArc: gravity -20, grounded at y <= 0.5.
			constexpr double Gravity = -20.0;
			P.Y += P.VY * ItemDt + 0.5 * Gravity * ItemDt * ItemDt;
			P.VY += Gravity * ItemDt;
			if (Bounce(P, Travel, 0.5)) { Events.Add({ EKartItemEventType::Bounce, -1, P.Owner, P.Type, P.Pos }); }
			else { P.Pos += P.Vel * ItemDt; }
			if (P.Y <= 0.5)
			{
				P.Y = 0.5;
				if (P.Age > 0.5)
				{
					Explode(P.Pos, 8.0, P.Owner, Racers, Now, Events);
					P.bAlive = false;
					return;
				}
			}
			break;
		}
		default:
			break;
		}
		if (!P.bAlive)
		{
			return;
		}

		// Kart hit: 1.5 m horizontal; the owner is safe for the first 0.5 s (MultiplayerItems.tsx:578-605).
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			if ((Seat == P.Owner && P.Age < OwnerGraceSeconds) || FVector2D::Distance(Racers[Seat].Sim->State().Pos, P.Pos) >= HitRadius)
			{
				continue;
			}
			if (P.Type == EKartItem::Bomb || P.Type == EKartItem::SkySeeker)
			{
				Explode(P.Pos, P.Type == EKartItem::Bomb ? 8.0 : 10.0, P.Owner, Racers, Now, Events);
			}
			else
			{
				Events.Add({ EKartItemEventType::Hit, Seat, P.Owner, P.Type, P.Pos });
				// An invincible kart eats the projectile.
				if (!IsInvincible(Seat, Now) && Now >= SlotsArr[Seat].ProtectedUntil)
				{
					SpinOut(Seat, Racers, Now, Events);
				}
			}
			P.bAlive = false;
			return;
		}
	}

	void FKartItemWorld::SpinOut(int32 Seat, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		FKartItemSlot& Slot = SlotsArr[Seat];
		FKartSim& Sim = *Racers[Seat].Sim;
		FKartState& S = Sim.MutableState();
		S.Speed *= 0.1;
		S.Turbo = 0.0;
		S.DriftDirection = 0.0;
		S.DriftPower = 0.0;
		Sim.ApplyBoost(0.3, 1.5, TEXT("SPUN_OUT"));
		Slot.SpinStart = Now;
		Slot.SpinUntil = Now + 1.0;
		Slot.LockUntil = Now + 0.8;
		Slot.ProtectedUntil = Now + 1.8;
		Events.Add({ EKartItemEventType::SpinOut, Seat });
	}

	void FKartItemWorld::Explode(const FVector2D& Center, double Radius, int32 Owner, TArray<FKartItemRacer>& Racers, double Now, TArray<FKartItemEvent>& Events)
	{
		Events.Add({ EKartItemEventType::Explosion, -1, Owner, EKartItem::Bomb, Center, Radius });
		for (int32 Seat = 0; Seat < Racers.Num(); ++Seat)
		{
			FKartItemSlot& Slot = SlotsArr[Seat];
			if (FVector2D::Distance(Racers[Seat].Sim->State().Pos, Center) >= Radius || IsInvincible(Seat, Now) || Now < Slot.ProtectedUntil)
			{
				continue;
			}
			FKartSim& Sim = *Racers[Seat].Sim;
			FKartState& S = Sim.MutableState();
			S.Speed = 0.0;
			S.Turbo = 0.0;
			S.DriftDirection = 0.0;
			S.DriftPower = 0.0;
			Sim.ApplyBoost(0.2, 2.5, TEXT("EXPLODED"));
			Slot.ExplodeStart = Now;
			Slot.LockUntil = Now + 1.2;
			Slot.ProtectedUntil = Now + 2.2;
			Slot.FlashUntil = Now + 0.5;
		}
	}
}
