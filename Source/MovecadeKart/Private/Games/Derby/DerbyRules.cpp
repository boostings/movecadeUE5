#include "Games/Derby/DerbyRules.h"

namespace MovecadeKart::Derby
{
	uint32 FDerbyRng::Next()
	{
		State += 0x6D2B79F5u;
		uint32 Z = State;
		Z = (Z ^ (Z >> 15)) * (Z | 1u);
		Z ^= Z + (Z ^ (Z >> 7)) * (Z | 61u);
		return Z ^ (Z >> 14);
	}

	const TCHAR* PitchName(EPitchType Type)
	{
		switch (Type)
		{
		case EPitchType::Changeup: return TEXT("CHANGEUP");
		case EPitchType::Curve: return TEXT("CURVEBALL");
		default: return TEXT("FASTBALL");
		}
	}

	uint32 RoundSeed(uint32 MatchSeed, int32 Round)
	{
		return MatchSeed ^ (0x9E3779B9u * static_cast<uint32>(Round + 1));
	}

	TArray<FPitch> PitchSchedule(uint32 Seed, int32 Count)
	{
		FDerbyRng Rng(Seed);
		TArray<FPitch> Out;
		for (int32 i = 0; i < Count; ++i)
		{
			const double RType = Rng.Float();
			const double RSpeed = Rng.Float();
			const double RWindup = Rng.Float();
			const double RBias = Rng.Float();
			const double RBreak = Rng.Float();
			FPitch& P = Out.AddDefaulted_GetRef();
			P.Index = i;
			// The first three are fastballs; changeups join from the fourth, curves from the seventh.
			if (i < 3)
			{
				P.Type = EPitchType::Fastball;
			}
			else if (i < 6)
			{
				P.Type = RType < 0.6 ? EPitchType::Fastball : EPitchType::Changeup;
			}
			else
			{
				P.Type = RType < 0.3 ? EPitchType::Fastball : (RType < 0.6 ? EPitchType::Changeup : EPitchType::Curve);
			}
			switch (P.Type)
			{
			case EPitchType::Fastball:
				P.SpeedMph = 88.0 + 7.0 * RSpeed + 0.4 * i;
				P.PerfectMs = 30.0;
				P.ContactMs = 165.0;
				P.Break = 0.0;
				break;
			case EPitchType::Changeup:
				P.SpeedMph = 77.0 + 5.0 * RSpeed;
				P.PerfectMs = 28.0;
				P.ContactMs = 155.0;
				P.Break = 0.2 * RBreak;
				break;
			case EPitchType::Curve:
				P.SpeedMph = 72.0 + 6.0 * RSpeed;
				P.PerfectMs = 24.0;
				P.ContactMs = 140.0;
				P.Break = 0.6 + 0.4 * RBreak;
				break;
			}
			P.FlightMs = PitchDistanceM / (P.SpeedMph * 0.44704) * 1000.0 * PitchDragFactor;
			P.WindupMs = 1300.0 + 600.0 * RWindup;
			P.ContactBias = 2.0 * RBias - 1.0;
		}
		return Out;
	}

	bool IsHomeRun(EResult Result)
	{
		return Result == EResult::HomeRun || Result == EResult::Moonshot;
	}

	const TCHAR* ResultName(EResult Result)
	{
		switch (Result)
		{
		case EResult::Take: return TEXT("take");
		case EResult::Whiff: return TEXT("whiff");
		case EResult::Foul: return TEXT("foul");
		case EResult::InPlay: return TEXT("in-play");
		case EResult::HomeRun: return TEXT("home-run");
		default: return TEXT("moonshot");
		}
	}

	FVector2D FFlight::At(double T) const
	{
		if (Path.Num() == 0)
		{
			return FVector2D(0.0, ContactHeightM);
		}
		const double F = FMath::Clamp(T, 0.0, HangS) / FlightStepS;
		const int32 I = FMath::Clamp(FMath::FloorToInt32(F), 0, Path.Num() - 1);
		const int32 J = FMath::Min(I + 1, Path.Num() - 1);
		return FMath::Lerp(Path[I], Path[J], FMath::Clamp(F - I, 0.0, 1.0));
	}

	double FenceDistanceM(double SprayDeg)
	{
		return FenceLineM + FenceCentreExtraM * FMath::Cos(FMath::DegreesToRadians(2.0 * FMath::Clamp(SprayDeg, -FoulLineDeg, FoulLineDeg)));
	}

	double TimingQuality(double TimingMs, const FPitch& Pitch)
	{
		const double A = FMath::Abs(TimingMs);
		if (A <= Pitch.PerfectMs)
		{
			return 1.0;
		}
		if (A > Pitch.ContactMs)
		{
			return 0.0;
		}
		return 1.0 - FMath::Pow((A - Pitch.PerfectMs) / (Pitch.ContactMs - Pitch.PerfectMs), 1.25);
	}

	FContact MakeContact(const FPitch& Pitch, const FSwing& Swing)
	{
		FContact C;
		const double Q = TimingQuality(Swing.TimingMs, Pitch);
		const double S = FMath::Clamp(Swing.Strength, 0.0, 1.0);
		const double Aim = FMath::Clamp(Swing.Aim, -1.0, 1.0);
		const double Miss = 1.0 - Q;
		C.Quality = Q;
		// Early pulls (left field for a right-handed batter), late goes the other way; a poor swing slices foul.
		const double Side = Swing.TimingMs < 0.0 ? -1.0 : 1.0;
		C.SprayDeg = Aim * 28.0 + Swing.TimingMs / Pitch.ContactMs * 32.0 + Side * Miss * Miss * 25.0;
		// Off-time contact catches the ball high or low on the bat: a pop-up or a grounder.
		C.LaunchDeg = IdealLaunchDeg + 2.0 * S + Pitch.ContactBias * FMath::Pow(Miss, 0.7) * 40.0 - Miss * 4.0;
		C.ExitMph = (62.0 + 38.0 * S) * (0.42 + 0.58 * FMath::Pow(Q, 0.9)) + 0.18 * Pitch.SpeedMph;
		return C;
	}

	FFlight SimulateFlight(double ExitMph, double LaunchDeg, double SprayDeg)
	{
		FFlight F;
		F.FenceM = FenceDistanceM(SprayDeg);
		const double V = ExitMph * 0.44704;
		const double A = FMath::DegreesToRadians(LaunchDeg);
		double X = 0.0, Y = ContactHeightM;
		double Vx = V * FMath::Cos(A), Vy = V * FMath::Sin(A);
		double T = 0.0;
		F.ApexM = Y;
		F.Path.Add(FVector2D(X, Y));
		const int32 MaxSteps = FMath::CeilToInt32(MaxFlightS / FlightStepS);
		for (int32 Step = 0; Step < MaxSteps; ++Step)
		{
			const double Speed = FMath::Sqrt(Vx * Vx + Vy * Vy);
			const double Ax = -DragK * Speed * Vx - LiftK * Speed * Vy;
			const double Ay = -Gravity - DragK * Speed * Vy + LiftK * Speed * Vx;
			Vx += Ax * FlightStepS;
			Vy += Ay * FlightStepS;
			const double Px = X, Py = Y;
			X += Vx * FlightStepS;
			Y += Vy * FlightStepS;
			T += FlightStepS;
			F.ApexM = FMath::Max(F.ApexM, Y);
			if (F.HeightAtFenceM < 0.0 && Px < F.FenceM && X >= F.FenceM)
			{
				const double K = (F.FenceM - Px) / (X - Px);
				F.HeightAtFenceM = FMath::Max(0.0, Py + (Y - Py) * K);
				if (Y > 0.0 && F.HeightAtFenceM <= FenceHeightM)
				{
					// Off the wall: the ball stops at the fence.
					F.bOffWall = true;
					F.DistanceM = F.FenceM;
					F.HangS = T - FlightStepS + FlightStepS * K;
					F.Path.Add(FVector2D(F.FenceM, F.HeightAtFenceM));
					return F;
				}
				F.bCleared = F.HeightAtFenceM > FenceHeightM;
			}
			if (Y < 0.0)
			{
				const double K = Py / (Py - Y);
				F.DistanceM = Px + (X - Px) * K;
				F.HangS = T - FlightStepS + FlightStepS * K;
				F.Path.Add(FVector2D(F.DistanceM, 0.0));
				return F;
			}
			F.Path.Add(FVector2D(X, Y));
		}
		F.DistanceM = X;
		F.HangS = T;
		return F;
	}

	int32 PointsFor(EResult Result, int32 DistanceFt)
	{
		switch (Result)
		{
		case EResult::Moonshot: return RoundHalfUp(DistanceFt * 1.5);
		case EResult::HomeRun: return DistanceFt;
		case EResult::InPlay: return RoundHalfUp(DistanceFt / 10.0);
		default: return 0;
		}
	}

	FOutcome ResolvePitch(const FPitch& Pitch, const FSwing& Swing, int32 StrikesBefore)
	{
		FOutcome O;
		O.TimingMs = Swing.TimingMs;
		if (!Swing.bSwung)
		{
			O.Result = EResult::Take;
			O.bStrike = true;
			return O;
		}
		if (FMath::Abs(Swing.TimingMs) > Pitch.ContactMs)
		{
			O.Result = EResult::Whiff;
			O.bStrike = true;
			return O;
		}
		O.Contact = MakeContact(Pitch, Swing);
		O.Flight = SimulateFlight(O.Contact.ExitMph, O.Contact.LaunchDeg, O.Contact.SprayDeg);
		O.DistanceFt = RoundHalfUp(O.Flight.DistanceM / MetresPerFoot);
		if (FMath::Abs(O.Contact.SprayDeg) > FoulLineDeg || O.Contact.LaunchDeg < -5.0)
		{
			O.Result = EResult::Foul;
			O.bStrike = StrikesBefore < StrikesPerOut - 1;
		}
		else if (O.Flight.bCleared)
		{
			O.Result = O.DistanceFt >= MoonshotFt ? EResult::Moonshot : EResult::HomeRun;
		}
		else
		{
			O.Result = EResult::InPlay;
		}
		O.Points = PointsFor(O.Result, O.DistanceFt);
		return O;
	}

	FMatch CreateMatch(const TArray<FString>& BatterIds, uint32 Seed, int32 Rounds)
	{
		FMatch M;
		M.Seed = Seed;
		M.Rounds = FMath::Max(1, Rounds);
		for (const FString& Id : BatterIds)
		{
			FBatter& B = M.Batters.AddDefaulted_GetRef();
			B.Id = Id;
			B.RoundScores.Init(0, M.Rounds);
		}
		M.Schedule = PitchSchedule(RoundSeed(Seed, 0));
		M.bComplete = M.Batters.Num() == 0;
		return M;
	}

	FRecord Record(FMatch& Match, const FOutcome& Outcome)
	{
		FRecord R;
		if (Match.bComplete || !Match.Batters.IsValidIndex(Match.Up))
		{
			return R;
		}
		FBatter& B = Match.Batters[Match.Up];
		B.Score += Outcome.Points;
		if (B.RoundScores.IsValidIndex(Match.Round))
		{
			B.RoundScores[Match.Round] += Outcome.Points;
		}
		if (IsHomeRun(Outcome.Result))
		{
			++B.HomeRuns;
			B.LongestFt = FMath::Max(B.LongestFt, Outcome.DistanceFt);
		}
		if (Outcome.Result == EResult::Moonshot)
		{
			++B.Moonshots;
		}
		if (Outcome.Result == EResult::InPlay || IsHomeRun(Outcome.Result))
		{
			++B.Hits;
		}
		if (Outcome.bStrike)
		{
			++Match.Strikes;
		}
		++Match.Pitch;
		R.bStruckOut = Match.Strikes >= StrikesPerOut && Match.Pitch < PitchesPerTurn;
		if (Match.Strikes < StrikesPerOut && Match.Pitch < PitchesPerTurn)
		{
			return R;
		}
		R.bTurnEnded = true;
		Match.Pitch = 0;
		Match.Strikes = 0;
		++Match.Up;
		if (Match.Up >= Match.Batters.Num())
		{
			R.bRoundEnded = true;
			Match.Up = 0;
			++Match.Round;
			if (Match.Round >= Match.Rounds)
			{
				Match.bComplete = true;
				R.bMatchComplete = true;
			}
			else
			{
				Match.Schedule = PitchSchedule(RoundSeed(Match.Seed, Match.Round));
			}
		}
		return R;
	}

	TArray<int32> Ranking(const FMatch& Match)
	{
		TArray<int32> Order;
		for (int32 i = 0; i < Match.Batters.Num(); ++i)
		{
			Order.Add(i);
		}
		Order.StableSort([&Match](int32 A, int32 B)
		{
			const FBatter& X = Match.Batters[A];
			const FBatter& Y = Match.Batters[B];
			if (X.Score != Y.Score) return X.Score > Y.Score;
			if (X.HomeRuns != Y.HomeRuns) return X.HomeRuns > Y.HomeRuns;
			return X.LongestFt > Y.LongestFt;
		});
		return Order;
	}
}
