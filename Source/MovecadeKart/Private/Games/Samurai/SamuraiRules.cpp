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

		// engine.ts, all in ms unless named otherwise. The round ramps with P = elapsed / round (0..1).
		/** Launch interval: from 1150 ms down to 520 ms, jittered 0.75..1.25x. */
		constexpr double SpawnIntervalStartMs = 1150.0;
		constexpr double SpawnIntervalRampMs = 630.0;
		constexpr double SpawnJitterMin = 0.75;
		constexpr double SpawnJitterRange = 0.5;
		/** A lane at its target cap, or with no room for a launch, tries again this soon. */
		constexpr double SpawnRetryMs = 200.0;
		/** Lane i's first launch, staggered so four lanes don't launch in lockstep. */
		constexpr double FirstSpawnMs = 400.0;
		constexpr double FirstSpawnStaggerMs = 220.0;
		/** Bombs ramp 12% to 24% (none in the first 5 s); the bonus scroll stays rare and steady. */
		constexpr double BombChanceStart = 0.12;
		constexpr double BombChanceRamp = 0.12;
		constexpr double BombFreeMs = 5000.0;
		constexpr double ScrollChance = 0.08;
		/** From 15 s every fourth launch is a pair of apples or melons, side by side. */
		constexpr double PairsFromMs = 15000.0;
		constexpr int32 LaunchCycle = 4;
		constexpr double PairOffset = 0.60;
		/** Flight time: 2800 ms down to 2100 ms, plus up to 250 ms. */
		constexpr double FlightStartMs = 2800.0;
		constexpr double FlightRampMs = 700.0;
		constexpr double FlightJitterMs = 250.0;
		/** Where launches go across the lane (-1..1): a preferred slot per launch, then the fallbacks. */
		constexpr double LateralSlot = 0.56;
		constexpr double LateralFallback = 0.72;
		/** Targets past this progress no longer block a launch of the same kind. */
		constexpr double ClearOfLaunchProgress = 0.30;
		constexpr double ClearanceScale = 1.15;
		constexpr double ClearanceMargin = 0.10;
		/** Lateral is -1..1 but spans 1.1 m either side (TargetCentre). */
		constexpr double LateralMetres = 1.1;
		constexpr double MaxSpin = 0.85;
		/** Effects live this long. */
		constexpr double SlashEffectMs = 260.0;
		constexpr double ComboEffectMs = 1200.0;
		constexpr double EffectMs = 800.0;
		/** A step longer than this is played as this long. */
		constexpr double MaxStepMs = 100.0;
		/** Scoring: timing pays up to +10, the matching direction a quarter of the target, power 8 per target, every
		 *  extra target 12; the combo multiplier tops out near 2x at 12. */
		constexpr double TimingBonus = 10.0;
		constexpr double DirectionalShare = 0.25;
		constexpr double PowerBonusPerTarget = 8.0;
		constexpr double MultiCutBonus = 12.0;
		constexpr int32 MaxComboSteps = 12;
		constexpr double ComboStep = 0.0833;
		/** A cut this precise is Perfect (and charges Focus). */
		constexpr double PerfectQuality = 0.85;
		/** A swing that cuts this many, or reaches this combo, is a Combo effect (the big callout). */
		constexpr int32 ComboEffectCuts = 3;
		constexpr int32 ComboEffectCombo = 6;
		/** SamuraiScene.tsx: the arc's height (m) at progress P, and the lanes' spacing (m). */
		constexpr double ArcBase = -1.6;
		constexpr double ArcRise = 14.0;
		constexpr double ArcFall = 13.0;
		constexpr double LaneSpacing = 3.6;

		double SpawnIntervalMs(FMatch& Match)
		{
			const double P = FMath::Min(1.0, Match.ElapsedMs / RoundDurationMs);
			const double Base = SpawnIntervalStartMs - P * SpawnIntervalRampMs;
			return Base * (SpawnJitterMin + Match.Rng.Next() * SpawnJitterRange);
		}

		EVariant PickVariant(FMatch& Match)
		{
			const double P = FMath::Min(1.0, Match.ElapsedMs / RoundDurationMs);
			const double Roll = Match.Rng.Next();
			const double BombChance = BombChanceStart + P * BombChanceRamp;
			if (Roll < BombChance && Match.ElapsedMs >= BombFreeMs)
			{
				return EVariant::Bomb;
			}
			if (Roll < BombChance + ScrollChance)
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
			const bool bPair = Match.ElapsedMs >= PairsFromMs && Player.Launches % LaunchCycle == LaunchCycle - 1;
			// A breath before a volley, so the pair arrives together.
			if (bPair && InLane.Num() > 0)
			{
				return false;
			}
			const EVariant Variant = bPair ? ((Player.Launches / LaunchCycle) % 2 == 0 ? EVariant::Apple : EVariant::Melon) : PickVariant(Match);
			const FTargetDef& D = Def(Variant);
			const double P = FMath::Min(1.0, Match.ElapsedMs / RoundDurationMs);
			const double FlightMs = FlightStartMs - P * FlightRampMs + Match.Rng.Next() * FlightJitterMs;
			const double Preferred[] = { 0.0, -LateralSlot, LateralSlot, 0.0 };
			const double Candidates[] = { Preferred[Player.Launches % LaunchCycle], -LateralFallback, LateralFallback, 0.0 };
			TOptional<double> Lateral;
			for (const double Position : Candidates)
			{
				bool bFits = true;
				for (const FTarget* T : InLane)
				{
					const bool bOpposite = (T->Kind == EKind::Bomb) != (D.Kind == EKind::Bomb);
					if (!bOpposite && T->Progress > ClearOfLaunchProgress)
					{
						continue;
					}
					const double Clearance = (T->Radius + D.Radius) * ClearanceScale + ClearanceMargin;
					if (FMath::Abs(T->Lateral - Position) * LateralMetres < Clearance)
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
				Offsets = { -PairOffset, PairOffset };
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
				T.Spin = (Match.Rng.Next() * 2.0 - 1.0) * MaxSpin;
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
			E.DurationMs = Kind == EEffectKind::Slash ? SlashEffectMs : (Kind == EEffectKind::Combo ? ComboEffectMs : EffectMs);
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
			Match.SpawnCooldownMs.Add(FirstSpawnMs + i * FirstSpawnStaggerMs);
		}
		return Match;
	}

	void StepMatch(FMatch& Match, double DtMs)
	{
		if (Match.Phase == EMatchPhase::Ended)
		{
			return;
		}
		const double Dt = FMath::Clamp(DtMs, 0.0, MaxStepMs);
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
				Cooldown = SpawnRetryMs;
				continue;
			}
			const bool bSpawned = SpawnTarget(Match, P);
			Cooldown = bSpawned ? SpawnIntervalMs(Match) : SpawnRetryMs;
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
		// A swing's power is 0..1 (the blade clamps it). Past that, the score below overflows the int32 cast.
		const double Power = FMath::IsFinite(Swing.Power) ? FMath::Clamp(Swing.Power, 0.0, 1.0) : 0.0;
		{
			FEffect& Slash = PushEffect(Match, *Player, EEffectKind::Slash);
			Slash.Direction = Swing.Direction;
			Slash.Intensity = Power;
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
			Base += T.Score + JsRound(Q * TimingBonus);
			if (T.Preferred != EDirection::None && T.Preferred == Swing.Direction)
			{
				Directional += JsRound(T.Score * DirectionalShare);
			}
		}
		const double PowerBonus = JsRound(Power * PowerBonusPerTarget) * Cut.Num();
		const double MultiBonus = Cut.Num() > 1 ? (Cut.Num() - 1) * MultiCutBonus : 0.0;
		const double Multiplier = 1.0 + FMath::Min(MaxComboSteps, Player->Combo) * ComboStep;
		const bool bFocused = Player->FocusRemainingMs > 0.0;
		const int32 Delta = static_cast<int32>(JsRound((Base + Directional + PowerBonus + MultiBonus) * Multiplier)) * (bFocused ? 2 : 1);
		Player->Combo += Cut.Num();
		Player->BestCombo = FMath::Max(Player->BestCombo, Player->Combo);
		Player->Hits += Cut.Num();
		Player->Score += Delta;
		const bool bPerfect = Quality >= PerfectQuality;
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
		FEffect& E = PushEffect(Match, *Player, bFocusStarted || Cut.Num() >= ComboEffectCuts || Player->Combo >= ComboEffectCombo ? EEffectKind::Combo : EEffectKind::Hit);
		E.Direction = Swing.Direction;
		E.Intensity = Power;
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
		return ArcBase + ArcRise * P - ArcFall * P * P;
	}

	double LaneX(int32 Lane, int32 LaneCount)
	{
		return (Lane - (LaneCount - 1) / 2.0) * LaneSpacing;
	}

	FVector TargetCentre(const FTarget& Target, int32 LaneCount)
	{
		return FVector(LaneX(Target.LaneIndex, LaneCount) + Target.Lateral * LateralMetres, TargetHeight(Target.Progress), TargetDepth);
	}
}
