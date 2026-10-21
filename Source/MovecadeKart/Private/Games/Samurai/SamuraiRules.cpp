#include "Games/Samurai/SamuraiRules.h"

namespace MovecadeKart::Samurai
{
	namespace
	{
		const FTargetDef Defs[] = {
			{ EKind::Fruit, 16, 0.42, EDirection::Left, TEXT("#f0506e") },
			{ EKind::Fruit, 24, 0.55, EDirection::Down, TEXT("#4ade80") },
			{ EKind::Fruit, 20, 0.48, EDirection::Right, TEXT("#fbbf24") },
			{ EKind::Fruit, 22, 0.40, EDirection::Down, TEXT("#34d399") },
			{ EKind::Bomb, 0, 0.45, EDirection::None, TEXT("#1f2937") },
			{ EKind::Bonus, 40, 0.44, EDirection::Up, TEXT("#fde68a") },
		};
		const EVariant Fruits[] = { EVariant::Apple, EVariant::Melon, EVariant::Lantern, EVariant::Bamboo };

		double SpawnIntervalMs(FMatch& Match)
		{
			const double P = FMath::Min(1.0, Match.ElapsedMs / RoundDurationMs);
			const double Base = 1150.0 - P * 630.0;
			return Base * (0.75 + Match.Rng.Next() * 0.5);
		}

		EVariant PickVariant(FMatch& Match)
		{
			const double P = FMath::Min(1.0, Match.ElapsedMs / RoundDurationMs);
			const double Roll = Match.Rng.Next();
			// Bombs ramp 12% to 24%; the bonus scroll stays rare and steady.
			const double BombChance = 0.12 + P * 0.12;
			if (Roll < BombChance && Match.ElapsedMs >= 5000.0)
			{
				return EVariant::Bomb;
			}
			if (Roll < BombChance + 0.08)
			{
				return EVariant::Scroll;
			}
			return Fruits[static_cast<int32>(FMath::FloorToDouble(Match.Rng.Next() * 4.0)) % 4];
		}

		bool SpawnTarget(FMatch& Match, FPlayer& Player)
		{
			TArray<const FTarget*> InLane;
			for (const FTarget& T : Match.Targets)
			{
				if (T.PlayerId == Player.Id)
				{
					InLane.Add(&T);
				}
			}
			const bool bPair = Match.ElapsedMs >= 15000.0 && Player.Launches % 4 == 3;
			// A breath before a volley, so the pair arrives together.
			if (bPair && InLane.Num() > 0)
			{
				return false;
			}
			const EVariant Variant = bPair ? ((Player.Launches / 4) % 2 == 0 ? EVariant::Apple : EVariant::Melon) : PickVariant(Match);
			const FTargetDef& D = Def(Variant);
			const double P = FMath::Min(1.0, Match.ElapsedMs / RoundDurationMs);
			const double FlightMs = 2800.0 - P * 700.0 + Match.Rng.Next() * 250.0;
			const double Preferred[] = { 0.0, -0.56, 0.56, 0.0 };
			const double Candidates[] = { Preferred[Player.Launches % 4], -0.72, 0.72, 0.0 };
			TOptional<double> Lateral;
			for (const double Position : Candidates)
			{
				bool bFits = true;
				for (const FTarget* T : InLane)
				{
					const bool bOpposite = (T->Kind == EKind::Bomb) != (D.Kind == EKind::Bomb);
					if (!bOpposite && T->Progress > 0.30)
					{
						continue;
					}
					const double Clearance = (T->Radius + D.Radius) * 1.15 + 0.10;
					if (FMath::Abs(T->Lateral - Position) * 1.1 < Clearance)
					{
						bFits = false;
						break;
					}
				}
				if (bFits)
				{
					Lateral = Position;
					break;
				}
			}
			if (!Lateral.IsSet())
			{
				return false;
			}
			TArray<double> Offsets;
			if (bPair)
			{
				Offsets = { -0.60, 0.60 };
			}
			else
			{
				Offsets = { Lateral.GetValue() };
			}
			for (const double Offset : Offsets)
			{
				FTarget& T = Match.Targets.AddDefaulted_GetRef();
				T.Id = Match.NextId++;
				T.PlayerId = Player.Id;
				T.LaneIndex = Player.LaneIndex;
				T.Variant = Variant;
				T.Kind = D.Kind;
				T.Speed = 1.0 / FlightMs;
				T.Lateral = Offset;
				T.Spin = (Match.Rng.Next() * 2.0 - 1.0) * 0.85;
				T.Radius = D.Radius;
				T.Score = D.Score;
				T.Preferred = D.Preferred;
			}
			Player.Launches += 1;
			return true;
		}

		FEffect& PushEffect(FMatch& Match, const FPlayer& Player, EEffectKind Kind)
		{
			FEffect& E = Match.Effects.AddDefaulted_GetRef();
			E.Id = Match.NextId++;
			E.PlayerId = Player.Id;
			E.LaneIndex = Player.LaneIndex;
			E.Kind = Kind;
			E.DurationMs = Kind == EEffectKind::Slash ? 260.0 : (Kind == EEffectKind::Combo ? 1200.0 : 800.0);
			E.TtlMs = E.DurationMs;
			return E;
		}

		void Finish(FMatch& Match)
		{
			Match.Phase = EMatchPhase::Ended;
			Match.Targets.Reset();
			int32 Best = 0;
			for (const FPlayer& P : Match.Players)
			{
				Best = FMath::Max(Best, P.Score);
			}
			Match.WinnerIds.Reset();
			for (const FPlayer& P : Match.Players)
			{
				if (P.Score == Best)
				{
					Match.WinnerIds.Add(P.Id);
				}
			}
		}
	}

	const FTargetDef& Def(EVariant Variant)
	{
		return Defs[static_cast<int32>(Variant)];
	}

	const TCHAR* VariantName(EVariant Variant)
	{
		static const TCHAR* const Names[] = { TEXT("apple"), TEXT("melon"), TEXT("lantern"), TEXT("bamboo"), TEXT("bomb"), TEXT("scroll") };
		return Names[static_cast<int32>(Variant)];
	}

	double FMulberry32::Next()
	{
		// a = (a + 0x6d2b79f5) >>> 0; t = imul(a ^ a >>> 15, 1 | a); t = (t + imul(t ^ t >>> 7, 61 | t)) ^ t;
		// return ((t ^ t >>> 14) >>> 0) / 2^32: every step wraps at 32 bits, as the JS does.
		A += 0x6d2b79f5u;
		uint32 T = (A ^ (A >> 15)) * (1u | A);
		T = (T + ((T ^ (T >> 7)) * (61u | T))) ^ T;
		return static_cast<double>(T ^ (T >> 14)) / 4294967296.0;
	}

	FPlayer* FMatch::FindPlayer(const FString& Id)
	{
		return Players.FindByPredicate([&Id](const FPlayer& P) { return P.Id == Id; });
	}

	const FPlayer* FMatch::FindPlayer(const FString& Id) const
	{
		return Players.FindByPredicate([&Id](const FPlayer& P) { return P.Id == Id; });
	}

	FMatch CreateMatch(const TArray<FSeed>& Seeds, uint32 Seed)
	{
		FMatch Match;
		Match.Rng = FMulberry32(Seed);
		for (int32 i = 0; i < Seeds.Num(); ++i)
		{
			FPlayer& P = Match.Players.AddDefaulted_GetRef();
			P.Id = Seeds[i].Id;
			P.Name = Seeds[i].Name;
			P.LaneIndex = i;
			// Staggered, so four lanes don't launch in lockstep.
			Match.SpawnCooldownMs.Add(400.0 + i * 220.0);
		}
		return Match;
	}

	void StepMatch(FMatch& Match, double DtMs)
	{
		if (Match.Phase == EMatchPhase::Ended)
		{
			return;
		}
		const double Dt = FMath::Clamp(DtMs, 0.0, 100.0);
		if (Match.Phase == EMatchPhase::Countdown)
		{
			Match.CountdownRemainingMs -= Dt;
			if (Match.CountdownRemainingMs > 0.0)
			{
				return;
			}
			// M5: the whole step goes to the round, as on the web.
			Match.Phase = EMatchPhase::Playing;
			Match.CountdownRemainingMs = 0.0;
		}
		Match.ElapsedMs += Dt;
		Match.TimeRemainingMs = FMath::Max(0.0, Match.TimeRemainingMs - Dt);
		for (int32 i = Match.Effects.Num() - 1; i >= 0; --i)
		{
			Match.Effects[i].TtlMs -= Dt;
			if (Match.Effects[i].TtlMs <= 0.0)
			{
				Match.Effects.RemoveAt(i);
			}
		}
		for (FPlayer& P : Match.Players)
		{
			P.FocusRemainingMs = FMath::Max(0.0, P.FocusRemainingMs - Dt);
		}
		for (int32 i = Match.Targets.Num() - 1; i >= 0; --i)
		{
			FTarget& T = Match.Targets[i];
			const FPlayer* Owner = Match.FindPlayer(T.PlayerId);
			const double Scale = T.Kind != EKind::Bomb && Owner && Owner->FocusRemainingMs > 0.0 ? FocusFlightScale : 1.0;
			T.Progress += T.Speed * Dt * Scale;
			if (T.Progress < 1.0)
			{
				continue;
			}
			const FTarget Fallen = T;
			Match.Targets.RemoveAt(i);
			if (Fallen.Kind == EKind::Bomb)
			{
				continue; // a bomb let fall is a bomb dodged
			}
			FPlayer* Player = Match.FindPlayer(Fallen.PlayerId);
			if (!Player || Player->bEliminated)
			{
				continue;
			}
			Player->FocusCharge = 0;
			if (Player->Combo > 0)
			{
				FEffect& E = PushEffect(Match, *Player, EEffectKind::Miss);
				E.Label = FString::Printf(TEXT("%dx lost"), Player->Combo);
				E.Color = Def(Fallen.Variant).Color;
				Player->Combo = 0;
			}
		}
		for (FPlayer& P : Match.Players)
		{
			if (P.bEliminated)
			{
				continue;
			}
			double& Cooldown = Match.SpawnCooldownMs[P.LaneIndex];
			Cooldown -= Dt;
			if (Cooldown > 0.0)
			{
				continue;
			}
			int32 InLane = 0;
			for (const FTarget& T : Match.Targets)
			{
				InLane += T.PlayerId == P.Id ? 1 : 0;
			}
			if (InLane >= MaxTargetsPerLane)
			{
				Cooldown = 200.0;
				continue;
			}
			const bool bSpawned = SpawnTarget(Match, P);
			Cooldown = bSpawned ? SpawnIntervalMs(Match) : 200.0;
		}
		bool bAllOut = Match.Players.Num() > 0;
		for (const FPlayer& P : Match.Players)
		{
			bAllOut &= P.bEliminated;
		}
		if (Match.TimeRemainingMs <= 0.0 || bAllOut)
		{
			Finish(Match);
		}
	}

	double CutQuality(double Progress)
	{
		const double Half = FMath::Max(CutApex - CutStart, CutEnd - CutApex);
		return FMath::Max(0.0, 1.0 - FMath::Abs(Progress - CutApex) / Half);
	}

	TOptional<FSwingOutcome> ResolveSwing(FMatch& Match, const FString& PlayerId, const FSwing& Swing, const TSet<int32>* Contacts)
	{
		if (Match.Phase != EMatchPhase::Playing)
		{
			return {};
		}
		FPlayer* Player = Match.FindPlayer(PlayerId);
		if (!Player || Player->bEliminated)
		{
			return {};
		}
		{
			FEffect& Slash = PushEffect(Match, *Player, EEffectKind::Slash);
			Slash.Direction = Swing.Direction;
			Slash.Intensity = Swing.Power;
		}
		// Back to front, as the web splices: Cut[0] is the last one in the list.
		TArray<FTarget> Cut;
		for (int32 i = Match.Targets.Num() - 1; i >= 0; --i)
		{
			const FTarget& T = Match.Targets[i];
			if (T.PlayerId != PlayerId)
			{
				continue;
			}
			if (Contacts ? !Contacts->Contains(T.Id) : (T.Progress < CutStart || T.Progress > CutEnd))
			{
				continue;
			}
			Cut.Add(T);
			Match.Targets.RemoveAt(i);
		}
		FSwingOutcome Out;
		if (Cut.Num() == 0)
		{
			const int32 Lost = Player->Combo;
			Player->Combo = 0;
			Player->FocusCharge = 0;
			Player->Misses += 1;
			FEffect& E = PushEffect(Match, *Player, EEffectKind::Miss);
			E.Direction = Swing.Direction;
			E.Label = Lost >= 2 ? FString::Printf(TEXT("%dx broken"), Lost) : FString(TEXT("Missed"));
			Out.Kind = EOutcome::Miss;
			Out.Label = Lost >= 2 ? TEXT("Combo broken") : TEXT("Missed");
			return Out;
		}
		int32 BombCount = 0;
		for (const FTarget& T : Cut)
		{
			BombCount += T.Kind == EKind::Bomb ? 1 : 0;
		}
		if (BombCount > 0)
		{
			// A bomb voids the swing: no rewarding a wild slash that caught fruit on the way through.
			Player->Combo = 0;
			Player->FocusCharge = 0;
			Player->FocusRemainingMs = 0.0;
			Player->Bombs += BombCount;
			Player->Lives = FMath::Max(0, Player->Lives - 1);
			Player->bEliminated = Player->Lives == 0;
			FEffect& E = PushEffect(Match, *Player, EEffectKind::Bomb);
			E.Direction = Swing.Direction;
			E.Color = Def(EVariant::Bomb).Color;
			E.Label = Player->bEliminated ? FString(TEXT("Out!")) : FString::Printf(TEXT("%d left"), Player->Lives);
			E.Variant = EVariant::Bomb;
			E.CutCount = BombCount;
			E.Struck = Cut;
			Out.Kind = EOutcome::Bomb;
			Out.Label = Player->bEliminated ? TEXT("Eliminated") : TEXT("Bomb!");
			Out.CutTargets = BombCount;
			return Out;
		}
		double Base = 0.0, Quality = 1.0, Directional = 0.0;
		for (const FTarget& T : Cut)
		{
			const double Q = CutQuality(T.Progress);
			Quality = FMath::Min(Quality, Q);
			// Timing pays up to +10; the matching direction pays a quarter of the target.
			Base += T.Score + JsRound(Q * 10.0);
			if (T.Preferred != EDirection::None && T.Preferred == Swing.Direction)
			{
				Directional += JsRound(T.Score * 0.25);
			}
		}
		const double PowerBonus = JsRound(Swing.Power * 8.0) * Cut.Num();
		const double MultiBonus = Cut.Num() > 1 ? (Cut.Num() - 1) * 12.0 : 0.0;
		// The combo multiplier tops out near 2x.
		const double Multiplier = 1.0 + FMath::Min(12, Player->Combo) * 0.0833;
		const bool bFocused = Player->FocusRemainingMs > 0.0;
		const int32 Delta = static_cast<int32>(JsRound((Base + Directional + PowerBonus + MultiBonus) * Multiplier)) * (bFocused ? 2 : 1);
		Player->Combo += Cut.Num();
		Player->BestCombo = FMath::Max(Player->BestCombo, Player->Combo);
		Player->Hits += Cut.Num();
		Player->Score += Delta;
		const bool bPerfect = Quality >= 0.85;
		const bool bRestored = Cut.ContainsByPredicate([](const FTarget& T) { return T.Variant == EVariant::Scroll; }) && Player->Lives < StartingLives;
		if (bRestored)
		{
			Player->Lives += 1;
		}
		bool bFocusStarted = false;
		if (!bFocused)
		{
			Player->FocusCharge = bPerfect ? Player->FocusCharge + Cut.Num() : 0;
			if (Player->FocusCharge >= FocusCuts)
			{
				Player->FocusCharge = 0;
				Player->FocusRemainingMs = FocusDurationMs;
				bFocusStarted = true;
			}
		}
		const FString Label = bFocusStarted ? FString(TEXT("Focus! Double points"))
			: bRestored ? FString(TEXT("+1 life"))
			: bFocused ? FString::Printf(TEXT("Focus · +%d"), Delta)
			: Cut.Num() > 1 ? FString::Printf(TEXT("%d cut! +%d"), Cut.Num(), Delta)
			: bPerfect ? FString(TEXT("Perfect!"))
			: FString::Printf(TEXT("+%d"), Delta);
		FEffect& E = PushEffect(Match, *Player, bFocusStarted || Cut.Num() >= 3 || Player->Combo >= 6 ? EEffectKind::Combo : EEffectKind::Hit);
		E.Direction = Swing.Direction;
		E.Intensity = Swing.Power;
		E.Color = Def(Cut[0].Variant).Color;
		E.ScoreDelta = Delta;
		E.Label = Label;
		E.Variant = Cut[0].Variant;
		E.CutCount = Cut.Num();
		E.Struck = Cut;
		Out.Kind = EOutcome::Hit;
		Out.ScoreDelta = Delta;
		Out.Combo = Player->Combo;
		Out.Label = bFocusStarted || bRestored || bFocused ? Label : (bPerfect ? FString(TEXT("Perfect")) : FString(TEXT("Hit")));
		Out.CutTargets = Cut.Num();
		return Out;
	}

	int32 WaveNumber(double ElapsedMs)
	{
		return FMath::Min(WaveCount, static_cast<int32>(FMath::FloorToDouble(FMath::Max(0.0, ElapsedMs) / (RoundDurationMs / WaveCount))) + 1);
	}

	double TargetHeight(double P)
	{
		return -1.6 + 14.0 * P - 13.0 * P * P;
	}

	double LaneX(int32 Lane, int32 LaneCount)
	{
		return (Lane - (LaneCount - 1) / 2.0) * 3.6;
	}

	FVector TargetCentre(const FTarget& Target, int32 LaneCount)
	{
		return FVector(LaneX(Target.LaneIndex, LaneCount) + Target.Lateral * 1.1, TargetHeight(Target.Progress), TargetDepth);
	}
}
