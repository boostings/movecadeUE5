#include "Games/Fishing/FishingRules.h"

#include "Algo/StableSort.h"

namespace MovecadeKart::Fishing
{
	namespace
	{
		/** Reel to tension, and the fish's pull to tension (SettledTension). */
		constexpr double ReelTension = 0.62;
		constexpr double PullTension = 0.45;
		/** How fast the line's tension follows its target (seconds). */
		constexpr double TensionLag = 0.16;
		/** Metres a second at a full-speed crank, in the green. */
		constexpr double ReelSpeedMps = 3.2;
		constexpr double RetrieveSpeedMps = 4.0;
		constexpr double BurstWarnMs = 450.0;
		constexpr double SubstepMs = 10.0;

		int32 Round(double X)
		{
			return static_cast<int32>(FMath::FloorToDouble(X + 0.5));
		}

		void Push(FMatch& Match, const FAngler& A, EEffectKind Kind, const FString& Label, double DurationMs, int32 Species = -1, int32 WeightG = 0, int32 ScoreG = 0)
		{
			FEffect& E = Match.Effects.AddDefaulted_GetRef();
			E.Id = Match.NextId++;
			E.PlayerId = A.PlayerId;
			E.LaneIndex = A.LaneIndex;
			E.Kind = Kind;
			E.DurationMs = DurationMs;
			E.TtlMs = DurationMs;
			E.DistanceM = A.State == EAnglerState::Fighting || A.State == EAnglerState::Landed ? A.Fight.DistanceM : A.LureM;
			E.Species = Species;
			E.WeightG = WeightG;
			E.ScoreG = ScoreG;
			E.Label = Label;
			if (Match.Effects.Num() > MaxEffects)
			{
				Match.Effects.RemoveAt(0, Match.Effects.Num() - MaxEffects);
			}
		}

		void Enter(FAngler& A, EAnglerState State)
		{
			A.State = State;
			A.StateMs = 0.0;
		}

		/** A fresh wait after the lure lands or the fish spook: the farther out, the longer; 0-2 nibbles before it. */
		void StartWait(FAngler& A, double MinMs, double MaxMs)
		{
			A.BiteInMs = A.Rng.Range(MinMs, MaxMs) + static_cast<int32>(BandAt(A.LureM)) * 450.0;
			A.NibblesLeft = static_cast<int32>(A.Rng.Unit() * 3.0);
			A.NibbleInMs = A.NibblesLeft > 0 ? A.BiteInMs * A.Rng.Range(0.3, 0.6) : -1.0;
		}

		int32 PickSpecies(FAngler& A)
		{
			const int32 Band = static_cast<int32>(BandAt(A.LureM));
			const TArray<FSpecies>& All = Species();
			double Total = 0.0;
			for (const FSpecies& S : All)
			{
				Total += S.Odds[Band];
			}
			double Roll = A.Rng.Unit() * Total;
			for (int32 i = 0; i < All.Num(); ++i)
			{
				Roll -= All[i].Odds[Band];
				if (Roll < 0.0)
				{
					return i;
				}
			}
			return 0;
		}

		int32 RollWeight(FAngler& A, const FSpecies& S, double DistanceM)
		{
			// Skewed toward the small end; the farther the cast, the more of the range opens up.
			const double Reach = FMath::Clamp((DistanceM - MinCastM) / (MaxCastM - MinCastM), 0.0, 1.0);
			const double U = FMath::Min(1.0, FMath::Pow(A.Rng.Unit(), 1.6) * (0.8 + 0.4 * Reach));
			return Round(FMath::Lerp(S.MinKg, S.MaxKg, U) * 1000.0);
		}

		void Lose(FMatch& Match, FAngler& A, ELoss Why)
		{
			A.LastLoss = Why;
			A.Losses += 1;
			const int32 Lost = A.Combo;
			A.Combo = 0;
			Enter(A, EAnglerState::Lost);
			A.Reel = 0.0;
			switch (Why)
			{
			case ELoss::Snap:
				Push(Match, A, EEffectKind::Snap, TEXT("SNAP!"), 1400.0, A.Fight.Species);
				break;
			case ELoss::Escape:
				Push(Match, A, EEffectKind::Escape, Lost >= 2 ? TEXT("It got away! Combo lost") : TEXT("It got away!"), 1400.0, A.Fight.Species);
				break;
			default:
				Push(Match, A, EEffectKind::StolenBait, TEXT("Bait stolen!"), 1400.0, A.BiteSpecies);
				break;
			}
		}

		void Land(FMatch& Match, FAngler& A)
		{
			const FSpecies& S = Species()[A.Fight.Species];
			FCatch C;
			C.Species = A.Fight.Species;
			C.WeightG = A.Fight.WeightG;
			C.DistanceM = A.CastM;
			Enter(A, EAnglerState::Landed);
			A.Reel = 0.0;
			if (S.Kind == EKind::Junk)
			{
				A.JunkCount += 1;
				C.Combo = A.Combo;
				A.LastCatch = C;
				Push(Match, A, EEffectKind::Junk, FString::Printf(TEXT("%s!"), S.Name), LandedHoldMs, C.Species, C.WeightG, 0);
				return;
			}
			A.Catches += 1;
			A.Combo += 1;
			A.BestCombo = FMath::Max(A.BestCombo, A.Combo);
			C.Combo = A.Combo;
			C.ScoreG = Round(C.WeightG * ComboMultiplier(A.Combo));
			A.Score += C.ScoreG;
			A.TotalG += C.WeightG;
			if (C.WeightG > A.BiggestG)
			{
				A.BiggestG = C.WeightG;
				A.BiggestSpecies = C.Species;
			}
			C.bRecord = C.WeightG > Match.RecordG && C.WeightG >= 1500;
			if (C.WeightG > Match.RecordG)
			{
				Match.RecordG = C.WeightG;
				Match.RecordHolder = A.PlayerId;
			}
			A.LastCatch = C;
			Push(Match, A, EEffectKind::Catch, FString::Printf(TEXT("+%s"), *FormatWeight(C.ScoreG)), LandedHoldMs, C.Species, C.WeightG, C.ScoreG);
			if (C.bRecord)
			{
				Push(Match, A, EEffectKind::Record, TEXT("NEW RECORD!"), LandedHoldMs, C.Species, C.WeightG, C.ScoreG);
			}
		}

		/** One substep of a fight (H seconds). */
		void StepFight(FMatch& Match, FAngler& A, double H)
		{
			FFight& F = A.Fight;
			const FSpecies& S = Species()[F.Species];
			const double Ms = H * 1000.0;
			F.FightMs += Ms;
			// Runs: a warning, then the pull; a spent fish no longer runs.
			if (S.BurstGapMs > 0.0 && F.Stamina > 0.12 && F.BurstMs <= 0.0)
			{
				const double Before = F.NextBurstMs;
				F.NextBurstMs -= Ms;
				if (Before > BurstWarnMs && F.NextBurstMs <= BurstWarnMs)
				{
					F.WarnMs = BurstWarnMs;
					Push(Match, A, EEffectKind::Burst, TEXT("It's running!"), 900.0, F.Species);
				}
				if (F.NextBurstMs <= 0.0)
				{
					F.WarnMs = 0.0;
					F.BurstMs = A.Rng.Range(500.0, 1000.0);
					F.NextBurstMs = S.BurstGapMs * A.Rng.Range(0.6, 1.4) + BurstWarnMs;
					F.Bursts += 1;
				}
			}
			F.WarnMs = FMath::Max(0.0, F.WarnMs - Ms);
			const bool bRun = F.BurstMs > 0.0;
			F.BurstMs = FMath::Max(0.0, F.BurstMs - Ms);
			const double Fresh = 0.5 + 0.5 * F.Stamina;
			const double Base = S.Strength * (0.4 + 0.6 * F.Stamina);
			F.Pull = Base + (bRun ? S.BurstPower * Fresh : 0.0);
			const double Target = A.Reel * ReelTension + Base * PullTension + (bRun ? S.BurstPower * 0.3 * Fresh : 0.0);
			F.Tension = FMath::Clamp(F.Tension + (Target - F.Tension) * (1.0 - FMath::Exp(-H / TensionLag)), 0.0, 1.6);
			// The red line: cranking into it snaps the line fast; letting the fish run on the drag only slowly.
			const double Over = F.Tension - RedLine;
			if (Over > 0.0)
			{
				F.StrainMs += Ms * (0.25 + 0.75 * FMath::Min(1.0, A.Reel / 0.5)) * (1.0 + Over / 0.1);
			}
			else
			{
				F.StrainMs = FMath::Max(0.0, F.StrainMs - Ms * 1.5);
			}
			const bool bSlack = F.Tension < GreenLow;
			F.SlackMs = bSlack ? F.SlackMs + Ms : FMath::Max(0.0, F.SlackMs - Ms * 2.0);
			const bool bGreen = !bSlack && F.Tension <= GreenHigh;
			// Only a line in the green tires the fish at full rate and reels at full speed.
			const double Drain = H / FMath::Max(0.5, S.StaminaS) * (bRun ? 2.0 : 1.0);
			F.Stamina = FMath::Clamp(F.Stamina + (bSlack ? Drain / 3.0 : -Drain * (bGreen ? 1.0 : 1.3)), 0.0, 1.0);
			const double Efficiency = bGreen ? 1.0 : (bSlack ? 0.4 : 0.55);
			const double Run = Base * 0.4 + (bRun ? S.BurstPower * 2.0 * Fresh : 0.0);
			F.DistanceM = FMath::Max(0.0, F.DistanceM + (Run - A.Reel * ReelSpeedMps * Efficiency) * H);
			const double T = F.FightMs / 1000.0;
			F.Lateral = FMath::Sin(T * (0.8 + 0.25 * (F.Species % 3)) + F.Species) * (0.35 + 0.65 * F.Stamina) * (bRun ? 1.4 : 1.0);
			if (F.StrainMs >= SnapStrainMs)
			{
				Lose(Match, A, ELoss::Snap);
			}
			else if (F.SlackMs >= EscapeSlackMs || F.DistanceM > MaxCastM + 8.0)
			{
				Lose(Match, A, ELoss::Escape);
			}
			else if (F.DistanceM <= LandM)
			{
				Land(Match, A);
			}
		}

		void StepAngler(FMatch& Match, FAngler& A, double Ms)
		{
			A.StateMs += Ms;
			const double H = Ms / 1000.0;
			switch (A.State)
			{
			case EAnglerState::Casting:
				A.LureM = A.CastM * FMath::Min(1.0, A.StateMs / FMath::Max(1.0, A.FlightMs));
				if (A.StateMs >= A.FlightMs)
				{
					A.LureM = A.CastM;
					Enter(A, EAnglerState::Waiting);
					StartWait(A, 2200.0, 5200.0);
					Push(Match, A, EEffectKind::Splash, FString(BandName(BandAt(A.LureM))), 1000.0);
				}
				break;
			case EAnglerState::Waiting:
				if (A.Reel > 0.2)
				{
					A.LureM = FMath::Max(0.0, A.LureM - A.Reel * RetrieveSpeedMps * H);
					if (A.LureM <= LandM)
					{
						Enter(A, EAnglerState::Idle);
						A.Reel = 0.0;
						Push(Match, A, EEffectKind::Retrieved, TEXT("Reeled in"), 900.0);
						break;
					}
				}
				A.BiteInMs -= Ms;
				if (A.NibbleInMs >= 0.0)
				{
					A.NibbleInMs -= Ms;
					if (A.NibbleInMs < 0.0)
					{
						Push(Match, A, EEffectKind::Nibble, FString(), 500.0);
						A.NibblesLeft -= 1;
						A.NibbleInMs = A.NibblesLeft > 0 && A.BiteInMs > 900.0 ? A.BiteInMs * A.Rng.Range(0.35, 0.6) : -1.0;
					}
				}
				if (A.BiteInMs <= 0.0)
				{
					A.BiteSpecies = PickSpecies(A);
					A.WindowMs = Species()[A.BiteSpecies].HookWindowMs;
					Enter(A, EAnglerState::Bite);
					Push(Match, A, EEffectKind::Bite, TEXT("BITE!"), A.WindowMs, A.BiteSpecies);
				}
				break;
			case EAnglerState::Bite:
				A.WindowMs -= Ms;
				if (A.WindowMs <= 0.0)
				{
					Lose(Match, A, ELoss::StolenBait);
				}
				break;
			case EAnglerState::Fighting:
			{
				double Remaining = Ms;
				while (Remaining > 1e-9 && A.State == EAnglerState::Fighting)
				{
					const double Step = FMath::Min(Remaining, SubstepMs);
					StepFight(Match, A, Step / 1000.0);
					Remaining -= Step;
				}
				break;
			}
			case EAnglerState::Landed:
				if (A.StateMs >= LandedHoldMs)
				{
					Enter(A, EAnglerState::Idle);
					A.LureM = 0.0;
				}
				break;
			case EAnglerState::Lost:
				if (A.StateMs >= LostHoldMs)
				{
					Enter(A, EAnglerState::Idle);
					A.LureM = 0.0;
				}
				break;
			default:
				break;
			}
		}

		void Finish(FMatch& Match)
		{
			Match.Phase = EMatchPhase::Ended;
			int32 Best = 0;
			for (const FAngler& A : Match.Anglers)
			{
				Best = FMath::Max(Best, A.Score);
			}
			Match.WinnerIds.Reset();
			for (const FAngler& A : Match.Anglers)
			{
				if (A.Score == Best)
				{
					Match.WinnerIds.Add(A.PlayerId);
				}
			}
		}
	}

	const TArray<FSpecies>& Species()
	{
		// Odds by band: shallows, reeds, deep, far.
		static const TArray<FSpecies> List = {
			{ TEXT("perch"), TEXT("Sunny Perch"), EKind::Fish, 0.15, 0.7, { 30.0, 10.0, 2.0, 0.0 }, 0.30, 3.0, 0.0, 0.0, 950.0 },
			{ TEXT("bluegill"), TEXT("Bluegill"), EKind::Fish, 0.1, 0.5, { 28.0, 6.0, 0.0, 0.0 }, 0.25, 2.5, 0.0, 0.0, 1000.0 },
			{ TEXT("trout"), TEXT("Rainbow Trout"), EKind::Fish, 0.6, 2.8, { 6.0, 26.0, 10.0, 3.0 }, 0.55, 6.0, 3200.0, 0.7, 800.0 },
			{ TEXT("carp"), TEXT("Mirror Carp"), EKind::Fish, 1.5, 6.0, { 2.0, 18.0, 18.0, 8.0 }, 0.75, 9.0, 4200.0, 0.6, 750.0 },
			{ TEXT("pike"), TEXT("Northern Pike"), EKind::Fish, 2.5, 9.0, { 0.0, 5.0, 20.0, 16.0 }, 0.85, 9.0, 2600.0, 1.1, 620.0 },
			{ TEXT("catfish"), TEXT("Whiskered Catfish"), EKind::Fish, 4.0, 15.0, { 0.0, 2.0, 14.0, 18.0 }, 1.0, 13.0, 5000.0, 0.8, 700.0 },
			{ TEXT("sturgeon"), TEXT("Lake Sturgeon"), EKind::Fish, 9.0, 26.0, { 0.0, 0.0, 3.0, 12.0 }, 1.15, 16.0, 3600.0, 1.0, 600.0 },
			{ TEXT("goldie"), TEXT("Old Goldie"), EKind::Trophy, 18.0, 32.0, { 0.0, 0.0, 0.4, 3.0 }, 1.3, 18.0, 2800.0, 1.3, 520.0 },
			{ TEXT("boot"), TEXT("Old Boot"), EKind::Junk, 0.4, 1.1, { 5.0, 4.0, 3.0, 2.0 }, 0.2, 1.0, 0.0, 0.0, 1000.0 },
			{ TEXT("tyre"), TEXT("Bike Tyre"), EKind::Junk, 1.2, 2.6, { 3.0, 3.0, 3.0, 2.0 }, 0.35, 1.5, 0.0, 0.0, 1000.0 },
			{ TEXT("duck"), TEXT("Rubber Duck"), EKind::Junk, 0.05, 0.1, { 3.0, 1.0, 0.5, 0.5 }, 0.1, 0.5, 0.0, 0.0, 1100.0 },
		};
		return List;
	}

	int32 SpeciesIndex(const TCHAR* Id)
	{
		return Species().IndexOfByPredicate([Id](const FSpecies& S) { return FCString::Strcmp(S.Id, Id) == 0; });
	}

	EBand BandAt(double DistanceM)
	{
		return DistanceM < ShallowsEndM ? EBand::Shallows : DistanceM < ReedsEndM ? EBand::Reeds : DistanceM < DeepEndM ? EBand::Deep : EBand::Far;
	}

	const TCHAR* BandName(EBand Band)
	{
		switch (Band)
		{
		case EBand::Shallows: return TEXT("Shallows");
		case EBand::Reeds: return TEXT("Reeds");
		case EBand::Deep: return TEXT("Deep water");
		default: return TEXT("Far water");
		}
	}

	double CastDistance(double Power)
	{
		return MinCastM + (MaxCastM - MinCastM) * FMath::Clamp(Power, 0.0, 1.0);
	}

	double ComboMultiplier(int32 Combo)
	{
		return 1.0 + ComboStep * FMath::Clamp(Combo - 1, 0, ComboCap - 1);
	}

	double SettledTension(double Pull, double Reel)
	{
		return Reel * ReelTension + Pull * PullTension;
	}

	double ReelForTension(double Pull, double Target)
	{
		return FMath::Clamp((Target - Pull * PullTension) / ReelTension, 0.0, MaxReel);
	}

	uint32 FRng::Next()
	{
		uint32 T = (State += 0x6D2B79F5u);
		T = (T ^ (T >> 15)) * (T | 1u);
		T ^= T + (T ^ (T >> 7)) * (T | 61u);
		return T ^ (T >> 14);
	}

	double FRng::Unit()
	{
		return Next() / 4294967296.0;
	}

	FAngler* FMatch::Find(const FString& PlayerId)
	{
		return Anglers.FindByPredicate([&PlayerId](const FAngler& A) { return A.PlayerId == PlayerId; });
	}

	const FAngler* FMatch::Find(const FString& PlayerId) const
	{
		return Anglers.FindByPredicate([&PlayerId](const FAngler& A) { return A.PlayerId == PlayerId; });
	}

	FMatch CreateMatch(const TArray<FSeed>& Players, uint32 Seed)
	{
		FMatch Match;
		Match.Seed = Seed;
		for (int32 i = 0; i < Players.Num(); ++i)
		{
			FAngler& A = Match.Anglers.AddDefaulted_GetRef();
			A.PlayerId = Players[i].Id;
			A.Name = Players[i].Name;
			A.LaneIndex = i;
			A.Rng.State = (Seed ^ (0x9E3779B9u * static_cast<uint32>(i + 1))) | 1u;
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
		for (FAngler& A : Match.Anglers)
		{
			StepAngler(Match, A, Dt);
		}
		if (Match.TimeRemainingMs <= 0.0)
		{
			Finish(Match);
		}
	}

	bool Cast(FMatch& Match, const FString& PlayerId, double Power)
	{
		FAngler* Found = Match.Find(PlayerId);
		if (Match.Phase != EMatchPhase::Playing || !Found || Found->State != EAnglerState::Idle)
		{
			return false;
		}
		FAngler& A = *Found;
		A.CastPower = FMath::Clamp(Power, 0.0, 1.0);
		A.CastM = CastDistance(A.CastPower);
		A.FlightMs = 500.0 + 700.0 * A.CastPower;
		A.LureM = 0.0;
		A.Casts += 1;
		A.LastLoss = ELoss::None;
		Enter(A, EAnglerState::Casting);
		Push(Match, A, EEffectKind::Cast, FString(), A.FlightMs);
		return true;
	}

	bool Hook(FMatch& Match, const FString& PlayerId)
	{
		FAngler* Found = Match.Find(PlayerId);
		if (Match.Phase != EMatchPhase::Playing || !Found)
		{
			return false;
		}
		FAngler& A = *Found;
		if (A.State == EAnglerState::Waiting)
		{
			// Too early: the fish spook and the wait starts over (no nibbles this time).
			A.BiteInMs = A.Rng.Range(1800.0, 3500.0);
			A.NibblesLeft = 0;
			A.NibbleInMs = -1.0;
			Push(Match, A, EEffectKind::TooEarly, TEXT("Too early!"), 1000.0);
			return false;
		}
		if (A.State != EAnglerState::Bite)
		{
			return false;
		}
		const FSpecies& S = Species()[A.BiteSpecies];
		FFight& F = A.Fight;
		F = FFight();
		F.Species = A.BiteSpecies;
		F.WeightG = RollWeight(A, S, A.LureM);
		F.DistanceM = A.LureM;
		F.Tension = 0.45;
		F.NextBurstMs = S.BurstGapMs > 0.0 ? S.BurstGapMs * A.Rng.Range(0.5, 1.0) + BurstWarnMs : 0.0;
		Enter(A, EAnglerState::Fighting);
		Push(Match, A, EEffectKind::Hooked, S.Kind == EKind::Junk ? TEXT("Something heavy...") : (S.Strength >= 1.0 ? TEXT("A monster!") : TEXT("Fish on!")), 1000.0, F.Species);
		return true;
	}

	void SetReel(FMatch& Match, const FString& PlayerId, double Reel)
	{
		if (FAngler* A = Match.Find(PlayerId))
		{
			A->Reel = FMath::Clamp(FMath::IsFinite(Reel) ? Reel : 0.0, 0.0, MaxReel);
		}
	}

	TArray<const FAngler*> Leaderboard(const FMatch& Match)
	{
		TArray<const FAngler*> Out;
		for (const FAngler& A : Match.Anglers)
		{
			Out.Add(&A);
		}
		Algo::StableSort(Out, [](const FAngler* A, const FAngler* B) { return A->Score != B->Score ? A->Score > B->Score : A->BiggestG > B->BiggestG; });
		return Out;
	}

	FString FormatWeight(int32 Grams)
	{
		if (Grams < 1000)
		{
			return FString::Printf(TEXT("%d g"), Grams);
		}
		return FString::Printf(TEXT("%.2f kg"), Grams / 1000.0);
	}
}
