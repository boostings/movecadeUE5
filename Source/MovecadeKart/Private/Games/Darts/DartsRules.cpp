#include "Games/Darts/DartsRules.h"

#include "Algo/StableSort.h"

namespace MovecadeKart::Darts
{
	namespace
	{
		const int32 SectorOrder[SectorCount] = { 20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5 };
		const int32 BountySectors[] = { 19, 16, 20 };

		constexpr double PowerToVertical = 1.9;
		constexpr double SpinReferenceDegPerSec = 420.0;
		constexpr double SpinToLateral = 0.85;
		constexpr double RollReferenceDegPerSec = 520.0;
		constexpr double RollToVertical = 0.45;
		constexpr double UnsteadyPull = 0.55;

		double ClampSigned(double Value, double Limit)
		{
			if (!FMath::IsFinite(Value))
			{
				return 0.0;
			}
			return FMath::Clamp(Value, -Limit, Limit);
		}

		void PushEffect(FDartsMatch& Match, const FDartsPlayer& Player, EEffectKind Kind, const FString& Label = FString(),
			int32 ScoreDelta = 0, double Intensity = 1.0, double X = 0.0, double Y = 0.0)
		{
			const double Duration = Kind == EEffectKind::Round || Kind == EEffectKind::Finished ? 1600.0 : CalloutMs;
			FDartsEffect& E = Match.Effects.AddDefaulted_GetRef();
			E.Id = Match.NextId++;
			E.PlayerId = Player.Id;
			E.LaneIndex = Player.LaneIndex;
			E.Kind = Kind;
			E.TtlMs = Duration;
			E.DurationMs = Duration;
			E.Intensity = Intensity;
			E.ScoreDelta = ScoreDelta;
			E.Label = Label;
			E.X = X;
			E.Y = Y;
			if (Match.Effects.Num() > MaxEffects)
			{
				Match.Effects.RemoveAt(0, Match.Effects.Num() - MaxEffects);
			}
		}

		void Finish(FDartsMatch& Match)
		{
			Match.Phase = EMatchPhase::Ended;
			int32 Best = 0;
			for (const FDartsPlayer& P : Match.Players)
			{
				Best = FMath::Max(Best, P.Score);
			}
			Match.WinnerIds.Reset();
			for (const FDartsPlayer& P : Match.Players)
			{
				if (P.Score == Best)
				{
					Match.WinnerIds.Add(P.Id);
				}
			}
		}

		void AdvanceTurn(FDartsMatch& Match)
		{
			const int32 Count = Match.Players.Num();
			if (Count == 0)
			{
				Finish(Match);
				return;
			}
			for (int32 Offset = 1; Offset <= Count; ++Offset)
			{
				const int32 Index = (Match.CurrentPlayerIndex + Offset) % Count;
				if (!Match.Players[Index].bFinished)
				{
					Match.CurrentPlayerIndex = Index;
					Match.TurnDarts = 0;
					return;
				}
			}
			Finish(Match);
		}

		bool LandingInFlight(const TArray<FDartFlight>& Flights, int32 LandingId)
		{
			for (const FDartFlight& F : Flights)
			{
				if (F.Landing.Id == LandingId)
				{
					return true;
				}
			}
			return false;
		}

		EEffectKind LandingEffectKind(const FDartLanding& L)
		{
			if (L.Hit.Ring == ERing::Bull)
			{
				return EEffectKind::Bull;
			}
			if (L.ScoreDelta == 0)
			{
				return EEffectKind::Miss;
			}
			return L.bBounty ? EEffectKind::Bounty : EEffectKind::Hit;
		}

		FString LandingEffectLabel(const FDartLanding& L)
		{
			if (L.ScoreDelta == 0)
			{
				return TEXT("Miss");
			}
			return L.bBounty ? L.Hit.Label + TEXT(" ×2") : L.Hit.Label;
		}

		double LandingEffectIntensity(const FDartLanding& L)
		{
			return L.ScoreDelta == 0 ? 0.4 : FMath::Min(1.0, 0.35 + L.ScoreDelta / 60.0);
		}
	}

	const int32* Sectors()
	{
		return SectorOrder;
	}

	int32 SectorIndexAt(double X, double Y)
	{
		const double Angle = FMath::Atan2(Y, X);
		const double FromTop = UE_DOUBLE_PI / 2.0 - Angle;
		const double Shifted = FromTop + SectorArc / 2.0;
		const double Full = UE_DOUBLE_PI * 2.0;
		const double Wrapped = FMath::Fmod(FMath::Fmod(Shifted, Full) + Full, Full);
		return static_cast<int32>(FMath::FloorToDouble(Wrapped / SectorArc)) % SectorCount;
	}

	double SectorCenterAngle(int32 Index)
	{
		return UE_DOUBLE_PI / 2.0 - Index * SectorArc;
	}

	FBoardHit ScoreAt(double X, double Y)
	{
		FBoardHit Hit;
		const double Radius = FMath::Sqrt(X * X + Y * Y);
		if (!FMath::IsFinite(Radius))
		{
			return Hit;
		}
		Hit.Radius = Radius;
		if (Radius <= BullRadius)
		{
			Hit.Score = 50;
			Hit.Ring = ERing::Bull;
			Hit.Label = TEXT("Bullseye");
			return Hit;
		}
		if (Radius <= OuterBullRadius)
		{
			Hit.Score = 25;
			Hit.Ring = ERing::OuterBull;
			Hit.Label = TEXT("Bull");
			return Hit;
		}
		if (Radius > DoubleOuterRadius)
		{
			return Hit;
		}
		Hit.Sector = SectorOrder[SectorIndexAt(X, Y)];
		// The treble ring sits inside the double ring, but trebles: the ring and its factor are decided together.
		Hit.Ring = Radius >= DoubleInnerRadius ? ERing::Double
			: (Radius >= TripleInnerRadius && Radius <= TripleOuterRadius ? ERing::Triple : ERing::Single);
		Hit.Multiplier = Hit.Ring == ERing::Double ? 2 : (Hit.Ring == ERing::Triple ? 3 : 1);
		Hit.Score = Hit.Sector * Hit.Multiplier;
		Hit.Label = Hit.Ring == ERing::Double ? FString::Printf(TEXT("D%d"), Hit.Sector)
			: (Hit.Ring == ERing::Triple ? FString::Printf(TEXT("T%d"), Hit.Sector) : FString::FromInt(Hit.Sector));
		return Hit;
	}

	double MatchDurationForPlayers(int32 PlayerCount)
	{
		return MatchDurationMs * FMath::Max(1, PlayerCount);
	}

	int32 BountyForRound(int32 Round)
	{
		return Round < 0 ? BountySectors[0] : BountySectors[Round % UE_ARRAY_COUNT(BountySectors)];
	}

	FVector2D LandingPoint(const FDartGesture& Gesture)
	{
		const double AimX = ClampSigned(Gesture.AimX, 1.0);
		const double AimY = ClampSigned(Gesture.AimY, 1.0);
		double X = AimX * AimReach;
		double Y = AimY * AimReach;
		// Under the sweet spot the dart drops, over it the dart sails.
		Y += ((FMath::IsFinite(Gesture.Power) ? Gesture.Power : SweetSpot) - SweetSpot) * PowerToVertical;
		X += ClampSigned(Gesture.SpinDegPerSec / SpinReferenceDegPerSec, 1.5) * SpinToLateral;
		Y += ClampSigned(Gesture.RollDegPerSec / RollReferenceDegPerSec, 1.5) * RollToVertical;
		// An unsettled hand carries the dart along the line it was sweeping; a centred one drifts low.
		const double Unsteady = 1.0 - FMath::Clamp(FMath::IsFinite(Gesture.Steadiness) ? Gesture.Steadiness : 1.0, 0.0, 1.0);
		if (Unsteady > 0.0)
		{
			const double Length = FMath::Sqrt(AimX * AimX + AimY * AimY);
			if (Length > 0.001)
			{
				X += AimX / Length * Unsteady * UnsteadyPull;
				Y += AimY / Length * Unsteady * UnsteadyPull;
			}
			else
			{
				Y -= Unsteady * UnsteadyPull;
			}
		}
		const double Radius = FMath::Sqrt(X * X + Y * Y);
		if (Radius > CatchRadius && Radius > 0.0)
		{
			X = X / Radius * CatchRadius;
			Y = Y / Radius * CatchRadius;
		}
		return FVector2D(X, Y);
	}

	FDartsPlayer* FDartsMatch::FindPlayer(const FString& PlayerId)
	{
		return Players.FindByPredicate([&PlayerId](const FDartsPlayer& P) { return P.Id == PlayerId; });
	}

	const FDartsPlayer* FDartsMatch::FindPlayer(const FString& PlayerId) const
	{
		return Players.FindByPredicate([&PlayerId](const FDartsPlayer& P) { return P.Id == PlayerId; });
	}

	const FDartsPlayer* FDartsMatch::ActivePlayer() const
	{
		return Phase == EMatchPhase::Ended || !Players.IsValidIndex(CurrentPlayerIndex) ? nullptr : &Players[CurrentPlayerIndex];
	}

	FDartsMatch CreateMatch(const TArray<FMatchSeed>& Seeds)
	{
		FDartsMatch Match;
		Match.TimeRemainingMs = MatchDurationForPlayers(Seeds.Num());
		for (int32 i = 0; i < Seeds.Num(); ++i)
		{
			FDartsPlayer& P = Match.Players.AddDefaulted_GetRef();
			P.Id = Seeds[i].Id;
			P.Name = Seeds[i].Name;
			P.LaneIndex = i;
		}
		return Match;
	}

	int32 DartsLeftInRound(const FDartsPlayer& Player)
	{
		return DartsPerRound - Player.DartsThrown % DartsPerRound;
	}

	void StepMatch(FDartsMatch& Match, double DtMs)
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
		for (int32 i = Match.Flights.Num() - 1; i >= 0; --i)
		{
			FDartFlight& Flight = Match.Flights[i];
			Flight.Progress += Dt / FlightMs;
			if (Flight.Progress < 1.0)
			{
				continue;
			}
			// The score was committed at the throw; the arrival only reveals it.
			const FDartLanding Landing = Flight.Landing;
			const FString PlayerId = Flight.PlayerId;
			Match.Flights.RemoveAt(i);
			if (const FDartsPlayer* Player = Match.FindPlayer(PlayerId))
			{
				PushEffect(Match, *Player, LandingEffectKind(Landing), LandingEffectLabel(Landing), Landing.ScoreDelta,
					LandingEffectIntensity(Landing), Landing.X, Landing.Y);
			}
		}
		for (FDartsPlayer& Player : Match.Players)
		{
			for (FDartLanding& Landing : Player.Landings)
			{
				if (!LandingInFlight(Match.Flights, Landing.Id) && Landing.AgeMs < 2000.0)
				{
					Landing.AgeMs += Dt;
				}
			}
		}
		// A visit owns the board until all three darts have arrived.
		if (Match.Flights.Num() == 0 && Match.TurnDarts >= DartsPerRound)
		{
			AdvanceTurn(Match);
		}
		bool bAllFinished = true;
		for (const FDartsPlayer& P : Match.Players)
		{
			bAllFinished &= P.bFinished;
		}
		if (Match.Flights.Num() == 0 && (Match.TimeRemainingMs <= 0.0 || bAllFinished))
		{
			Finish(Match);
		}
	}

	TOptional<FThrowOutcome> ThrowDart(FDartsMatch& Match, const FString& PlayerId, const FDartGesture& Gesture)
	{
		if (Match.Phase != EMatchPhase::Playing)
		{
			return {};
		}
		FDartsPlayer* Player = Match.FindPlayer(PlayerId);
		const FDartsPlayer* Active = Match.ActivePlayer();
		if (!Player || Player->bFinished || !Active || Active->Id != PlayerId || Match.TurnDarts >= DartsPerRound)
		{
			return {};
		}
		const int32 Round = Player->Round;
		const FVector2D Point = LandingPoint(Gesture);
		const FBoardHit Hit = ScoreAt(Point.X, Point.Y);
		const bool bBounty = Hit.Score > 0 && Hit.Sector == BountyForRound(Round);
		const int32 Delta = bBounty ? Hit.Score * BountyMultiplier : Hit.Score;

		FDartLanding Landing;
		Landing.Id = Match.NextId++;
		Landing.PlayerId = Player->Id;
		Landing.LaneIndex = Player->LaneIndex;
		Landing.X = Point.X;
		Landing.Y = Point.Y;
		Landing.Hit = Hit;
		Landing.ScoreDelta = Delta;
		Landing.bBounty = bBounty;
		Landing.Round = Round;
		Landing.DartIndex = Player->DartsThrown % DartsPerRound;

		Player->Landings.Add(Landing);
		Player->Score += Delta;
		Player->DartsThrown += 1;
		Match.TurnDarts += 1;
		Player->RoundScore += Delta;
		Player->RoundHits += Delta > 0 ? 1 : 0;
		Player->Bulls += Hit.Ring == ERing::Bull ? 1 : 0;
		Player->Trebles += Hit.Ring == ERing::Triple ? 1 : 0;
		Player->Bounties += bBounty ? 1 : 0;

		FDartFlight& Flight = Match.Flights.AddDefaulted_GetRef();
		Flight.Id = Match.NextId++;
		Flight.PlayerId = Player->Id;
		Flight.LaneIndex = Player->LaneIndex;
		Flight.X = Point.X;
		Flight.Y = Point.Y;
		Flight.Power = FMath::Clamp(Gesture.Power, 0.0, 1.0);
		Flight.Curve = ClampSigned(Gesture.SpinDegPerSec / SpinReferenceDegPerSec, 1.0);
		Flight.Landing = Landing;

		FThrowOutcome Out;
		Out.Landing = Landing;
		Out.bRoundComplete = Player->DartsThrown % DartsPerRound == 0;
		if (Out.bRoundComplete)
		{
			Out.RoundBonus += Player->RoundHits == DartsPerRound ? TidyRoundBonus : 0;
			Out.RoundBonus += Player->RoundScore >= TonThreshold ? TonBonus : 0;
			Player->Score += Out.RoundBonus;
			Player->BestRound = FMath::Max(Player->BestRound, Player->RoundScore + Out.RoundBonus);
			PushEffect(Match, *Player, EEffectKind::Round,
				Out.RoundBonus > 0 ? FString::Printf(TEXT("Round %d: %d (+%d)"), Round + 1, Player->RoundScore + Out.RoundBonus, Out.RoundBonus)
					: FString::Printf(TEXT("Round %d: %d"), Round + 1, Player->RoundScore),
				Out.RoundBonus, Out.RoundBonus > 0 ? 1.0 : 0.5);
			Player->Round += 1;
			Player->RoundScore = 0;
			Player->RoundHits = 0;
			if (Player->Round >= RoundCount)
			{
				Player->bFinished = true;
				PushEffect(Match, *Player, EEffectKind::Finished, FString::FromInt(Player->Score));
			}
		}
		Out.bFinished = Player->bFinished;
		Out.Label = Delta == 0 ? FString(TEXT("Miss")) : (bBounty ? FString::Printf(TEXT("Bounty %s!"), *Hit.Label) : Hit.Label);
		return Out;
	}

	TArray<const FDartsPlayer*> Leaderboard(const FDartsMatch& Match)
	{
		TArray<const FDartsPlayer*> Out;
		for (const FDartsPlayer& P : Match.Players)
		{
			Out.Add(&P);
		}
		Algo::StableSortBy(Out, [](const FDartsPlayer* P) { return -P->Score; });
		return Out;
	}
}
