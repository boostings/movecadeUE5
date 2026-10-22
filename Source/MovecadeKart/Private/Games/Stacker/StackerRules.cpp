#include "Games/Stacker/StackerRules.h"

#include "Algo/StableSort.h"

namespace MovecadeKart::Stacker
{
	namespace
	{
		constexpr double BaseRowScore = 18.0;
		constexpr double HeightBonusPerRow = 2.0;
		constexpr double PrecisionBonusMax = 12.0;
		constexpr double PerfectMultiplier = 2.0;
		constexpr double ComboStep = 0.12;
		constexpr int32 ComboCap = 10;
		constexpr double BaseSweepSpeed = 1.8;
		constexpr double SweepSpeedPerRow = 0.045;
		constexpr double MaxSweepSpeed = 3.6;

		double Sign(double X)
		{
			return X > 0.0 ? 1.0 : (X < 0.0 ? -1.0 : 0.0);
		}

		void PushEffect(FMatch& Match, const FTower& Tower, EEffectKind Kind, const FString& Label, double Intensity = 1.0, int32 ScoreDelta = 0, TOptional<int32> Row = {})
		{
			FEffect& E = Match.Effects.AddDefaulted_GetRef();
			E.Id = Match.NextId++;
			E.PlayerId = Tower.PlayerId;
			E.LaneIndex = Tower.LaneIndex;
			E.Kind = Kind;
			E.Row = Row.Get(Tower.Height());
			E.DurationMs = Kind == EEffectKind::Complete ? 1600.0 : (Kind == EEffectKind::Perfect ? 900.0 : (Kind == EEffectKind::Miss ? 800.0 : 900.0));
			E.TtlMs = E.DurationMs;
			E.Intensity = Intensity;
			E.ScoreDelta = ScoreDelta;
			E.Label = Label;
		}

		void PushShard(FMatch& Match, const FTower& Tower, double X, double Width, int32 Row)
		{
			if (Match.Shards.Num() >= MaxShards)
			{
				Match.Shards.RemoveAt(0);
			}
			const double Side = X < 0.0 ? -1.0 : 1.0;
			FShard& S = Match.Shards.AddDefaulted_GetRef();
			S.Id = Match.NextId++;
			S.LaneIndex = Tower.LaneIndex;
			S.Row = Row;
			S.X = X;
			S.Width = Width;
			S.Vx = Side * (1.15 + Width * 0.9);
			S.Vy = 1.9 + FMath::Min(1.2, Width) * 0.5;
			S.Spin = Side * (2.6 + 2.6 / (0.35 + Width));
		}

		void Finish(FMatch& Match)
		{
			Match.Phase = EMatchPhase::Ended;
			int32 Best = 0;
			for (const FTower& T : Match.Towers)
			{
				Best = FMath::Max(Best, T.Score);
			}
			Match.WinnerIds.Reset();
			for (const FTower& T : Match.Towers)
			{
				if (T.Score == Best)
				{
					Match.WinnerIds.Add(T.PlayerId);
				}
			}
		}

		bool AllDone(const FMatch& Match)
		{
			for (const FTower& T : Match.Towers)
			{
				if (!T.IsDone())
				{
					return false;
				}
			}
			return true;
		}
	}

	FVector2D WindAt(double ElapsedMs, double Height)
	{
		const double T = ElapsedMs / 1000.0;
		const double Strength = (3.0 + FMath::Min(4.0, Height * 0.14)) * FMath::Min(1.0, T / 18.0);
		return FVector2D(Strength * (FMath::Sin(T * 0.31) + 0.28 * FMath::Sin(T * 0.83)), Strength * 0.65 * FMath::Sin(T * 0.27 + 0.8));
	}

	double BalanceLimit(double Height, double Width)
	{
		return FMath::Max(7.6, 12.0 - FMath::Min(2.4, Height * 0.06) - FMath::Max(0.0, 1.0 - Width / 3.6) * 2.0);
	}

	bool IsBalanceSteady(const FBalance& S, double Limit)
	{
		return FMath::Sqrt(S.LeanX * S.LeanX + S.LeanY * S.LeanY) <= Limit * SteadyBalanceRatio;
	}

	double BalanceDisplacement(double Lean)
	{
		return Sign(Lean) * FMath::Min(0.45, FMath::Max(0.0, FMath::Abs(Lean) - 0.4) * 0.045);
	}

	bool BalanceInputMissing(const FBalance& S, double ElapsedMs)
	{
		return FMath::IsFinite(S.LastInputMs) && ElapsedMs - S.LastInputMs > BalanceStaleMs;
	}

	void ApplyLandingImpulse(FBalance& S, double Offset, double Width, double Height)
	{
		const double Kick = FMath::Clamp(Offset / FMath::Max(0.42, Width), -1.0, 1.0) * (10.0 + FMath::Min(12.0, Height * 0.5));
		S.VelocityX += Kick;
		if (FMath::Abs(Offset) < 0.14)
		{
			S.VelocityX *= 0.65;
			S.VelocityY *= 0.65;
		}
	}

	bool StepBalance(FBalance& S, double ElapsedMs, double Height, double DtMs, double Width)
	{
		const FVector2D Wind = WindAt(ElapsedMs, Height);
		S.WindX = Wind.X;
		S.WindY = Wind.Y;
		if (BalanceInputMissing(S, ElapsedMs))
		{
			return false;
		}
		const double Dt = FMath::Clamp(DtMs, 0.0, 100.0);
		double Remaining = Dt;
		const bool bHasInput = FMath::IsFinite(S.LastInputMs);
		while (Remaining > 0.0)
		{
			const double H = FMath::Min(Remaining, 10.0) / 1000.0;
			const double Roll = bHasInput ? FMath::Clamp(S.Roll, -35.0, 35.0) : 0.0;
			const double Pitch = bHasInput ? FMath::Clamp(S.Pitch, -35.0, 35.0) : 0.0;
			S.VelocityX += ((Roll + Wind.X - S.LeanX) * 28.0 - S.VelocityX * 8.0) * H;
			S.VelocityY += ((Pitch + Wind.Y - S.LeanY) * 28.0 - S.VelocityY * 8.0) * H;
			S.LeanX += S.VelocityX * H;
			S.LeanY += S.VelocityY * H;
			Remaining -= H * 1000.0;
		}
		S.RecoveryMs = FMath::Max(0.0, S.RecoveryMs - Dt);
		if (S.RecoveryMs > 0.0)
		{
			S.DangerMs = 0.0;
			return false;
		}
		const double Ratio = FMath::Sqrt(S.LeanX * S.LeanX + S.LeanY * S.LeanY) / BalanceLimit(Height, Width);
		S.DangerMs = Ratio > 1.0 ? S.DangerMs + Dt : FMath::Max(0.0, S.DangerMs - Dt * 2.0);
		if (S.DangerMs < BalanceFailureMs)
		{
			return false;
		}
		S.DangerMs = 0.0;
		S.RecoveryMs = 2000.0;
		S.LeanX *= 0.35;
		S.LeanY *= 0.35;
		S.VelocityX = 0.0;
		S.VelocityY = 0.0;
		return true;
	}

	FDropPreview PreviewDrop(const FTower& Tower, double DropCenter)
	{
		const FBlock& Below = Tower.Top();
		const double Width = Below.Width;
		FDropPreview Out;
		Out.Sway = BalanceDisplacement(Tower.Balance.LeanX);
		Out.Support = Below.Center + Out.Sway;
		Out.Offset = FMath::Abs(DropCenter - Out.Support);
		const bool bSteady = IsBalanceSteady(Tower.Balance, BalanceLimit(Tower.Height(), Width));
		// A steady palm earns the wider perfect window (never wider than half the block).
		const double Tolerance = FMath::Min(bSteady ? SteadyPerfectTolerance : PerfectTolerance, Width / 2.0);
		Out.Left = FMath::Max(DropCenter - Width / 2.0, Out.Support - Below.Width / 2.0);
		Out.Right = FMath::Min(DropCenter + Width / 2.0, Out.Support + Below.Width / 2.0);
		Out.bPerfect = Out.Offset <= Tolerance;
		Out.bMiss = !Out.bPerfect && Out.Right - Out.Left < MinWidth - 1e-9;
		return Out;
	}

	double SweepTrackHalfWidth(double Height)
	{
		return FMath::Max(MinSweepTrackHalfWidth, TrackHalfWidth - FMath::Max(0.0, Height) * SweepTrackNarrowingPerRow);
	}

	double SweepLimit(double Width, double Height)
	{
		return FMath::Max(0.0, SweepTrackHalfWidth(Height) - Width / 2.0);
	}

	double SweepSpeed(double Height)
	{
		return FMath::Min(MaxSweepSpeed, BaseSweepSpeed + Height * SweepSpeedPerRow);
	}

	FSweepPose Sweep(double Center, int32 Direction, double Speed, double DtMs, double Limit)
	{
		if (Limit <= 0.0)
		{
			return { 0.0, Direction };
		}
		double Position = FMath::Clamp(Center, -Limit, Limit);
		int32 Heading = Direction;
		double Remaining = Speed * DtMs / 1000.0;
		if (Remaining < 0.0)
		{
			Heading = -Heading;
			Remaining = -Remaining;
		}
		Remaining = FMath::Min(Remaining, Limit * 2.0 * 64.0);
		while (Remaining > 0.0)
		{
			const double ToWall = Heading > 0 ? Limit - Position : Position + Limit;
			if (Remaining <= ToWall)
			{
				Position += Heading * Remaining;
				break;
			}
			Position = Heading > 0 ? Limit : -Limit;
			Remaining -= ToWall;
			Heading = -Heading;
		}
		return { Position, Heading };
	}

	double RewoundCenter(const FTower& Tower, double Ms)
	{
		if (Ms <= 0.0)
		{
			return Tower.CursorCenter;
		}
		return Sweep(Tower.CursorCenter, Tower.CursorDirection, SweepSpeed(Tower.Height()), -Ms, SweepLimit(Tower.Top().Width, Tower.Height())).Center;
	}

	FTower* FMatch::FindTower(const FString& PlayerId)
	{
		return Towers.FindByPredicate([&PlayerId](const FTower& T) { return T.PlayerId == PlayerId; });
	}

	const FTower* FMatch::FindTower(const FString& PlayerId) const
	{
		return Towers.FindByPredicate([&PlayerId](const FTower& T) { return T.PlayerId == PlayerId; });
	}

	FMatch CreateMatch(const TArray<FSeed>& Seeds)
	{
		FMatch Match;
		for (int32 i = 0; i < Seeds.Num(); ++i)
		{
			FTower& T = Match.Towers.AddDefaulted_GetRef();
			T.PlayerId = Seeds[i].Id;
			T.Name = Seeds[i].Name;
			T.LaneIndex = i;
			FBlock Foundation;
			Foundation.AgeMs = 9999.0;
			T.Blocks.Add(Foundation);
			T.CursorDirection = i % 2 == 0 ? 1 : -1;
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
		if (Match.Effects.Num() > MaxEffects)
		{
			Match.Effects.RemoveAt(0, Match.Effects.Num() - MaxEffects);
		}
		const double Seconds = Dt / 1000.0;
		for (int32 i = Match.Shards.Num() - 1; i >= 0; --i)
		{
			FShard& S = Match.Shards[i];
			S.TtlMs -= Dt;
			if (S.TtlMs <= 0.0)
			{
				Match.Shards.RemoveAt(i);
				continue;
			}
			S.Vy -= 18.0 * Seconds;
			S.X += S.Vx * Seconds;
			S.Y += S.Vy * Seconds;
		}
		for (FTower& T : Match.Towers)
		{
			for (FBlock& B : T.Blocks)
			{
				if (B.AgeMs < 1000.0)
				{
					B.AgeMs += Dt;
				}
			}
			if (T.IsDone() || BalanceInputMissing(T.Balance, Match.ElapsedMs))
			{
				continue;
			}
			if (StepBalance(T.Balance, Match.ElapsedMs, T.Height(), Dt, T.Top().Width))
			{
				T.Lives = FMath::Max(0, T.Lives - 1);
				T.Combo = 0;
				T.bEliminated = T.Lives == 0;
				PushEffect(Match, T, EEffectKind::Topout, T.bEliminated ? TEXT("Lost balance — out!") : TEXT("Lost balance — steady your palm"));
				if (T.bEliminated)
				{
					continue;
				}
			}
			if (Match.ElapsedMs - T.LastDropAtMs < DropCooldownMs || T.Balance.RecoveryMs > 0.0)
			{
				continue;
			}
			const FSweepPose Next = Sweep(T.CursorCenter, T.CursorDirection, SweepSpeed(T.Height()), Dt, SweepLimit(T.Top().Width, T.Height()));
			T.CursorCenter = Next.Center;
			T.CursorDirection = Next.Direction;
		}
		if (Match.TimeRemainingMs <= 0.0 || AllDone(Match))
		{
			Finish(Match);
		}
	}

	TOptional<FDropOutcome> DropBlock(FMatch& Match, const FString& PlayerId, double RewindMs)
	{
		if (Match.Phase != EMatchPhase::Playing)
		{
			return {};
		}
		FTower* Tower = Match.FindTower(PlayerId);
		if (!Tower || Tower->IsDone() || BalanceInputMissing(Tower->Balance, Match.ElapsedMs) || Tower->Balance.RecoveryMs > 0.0
			|| Match.ElapsedMs - Tower->LastDropAtMs < DropCooldownMs)
		{
			return {};
		}
		FTower& T = *Tower;
		T.LastDropAtMs = Match.ElapsedMs;
		T.Drops += 1;
		const FBlock Below = T.Top();
		const double DropCenter = RewoundCenter(T, FMath::Max(0.0, RewindMs));
		const double Width = Below.Width;
		const FDropPreview Preview = PreviewDrop(T, DropCenter);
		const double Sway = Preview.Sway;
		const double Support = Preview.Support;
		const double Offset = Preview.Offset;
		const double Left = Preview.Left;
		const double Right = Preview.Right;
		const double Overlap = Right - Left;
		const bool bPerfect = Preview.bPerfect;
		const int32 Row = T.Height() + 1;
		FDropOutcome Out;
		Out.Offset = Offset;

		if (Preview.bMiss)
		{
			const int32 Lost = T.Combo;
			T.Combo = 0;
			T.Lives = FMath::Max(0, T.Lives - 1);
			T.bEliminated = T.Lives == 0;
			PushShard(Match, T, DropCenter, Width, Row);
			PushEffect(Match, T, EEffectKind::Miss, T.bEliminated ? FString(TEXT("Out!")) : FString::Printf(TEXT("%d left"), T.Lives), 1.0, 0, Row);
			Out.Kind = EDropKind::Miss;
			Out.Height = T.Height();
			Out.Width = Width;
			Out.Label = T.bEliminated ? TEXT("Eliminated") : (Lost >= 2 ? TEXT("Combo broken") : TEXT("Missed"));
			return Out;
		}
		const double NewWidth = bPerfect ? FMath::Min(BaseWidth, Below.Width + PerfectRegrow) : Overlap;
		const double NewCenter = bPerfect ? Below.Center : (Left + Right) / 2.0 - Sway;
		if (!bPerfect)
		{
			const double Trimmed = Width - Overlap;
			if (Trimmed > 0.02)
			{
				PushShard(Match, T, DropCenter > Support ? Right + Trimmed / 2.0 : Left - Trimmed / 2.0, Trimmed, Row);
			}
		}
		const double Precision = FMath::Max(0.0, 1.0 - Offset / FMath::Max(0.001, Below.Width / 2.0));
		const double Multiplier = 1.0 + FMath::Min(ComboCap, T.Combo) * ComboStep;
		const double Base = BaseRowScore + Row * HeightBonusPerRow + JsRound(Precision * PrecisionBonusMax);
		const int32 Delta = static_cast<int32>(JsRound(Base * Multiplier * (bPerfect ? PerfectMultiplier : 1.0)));
		ApplyLandingImpulse(T.Balance, bPerfect ? 0.0 : DropCenter - Support, Below.Width, Row);
		FBlock Block;
		Block.Index = Row;
		Block.Center = NewCenter;
		Block.Width = NewWidth;
		Block.bPerfect = bPerfect;
		T.Blocks.Add(Block);
		T.Score += Delta;
		if (bPerfect)
		{
			T.Combo += 1;
			T.Perfects += 1;
			T.BestCombo = FMath::Max(T.BestCombo, T.Combo);
		}
		else
		{
			T.Combo = 0;
		}
		const int32 Entry = T.CursorDirection;
		T.CursorCenter = Entry * SweepLimit(NewWidth, Row);
		T.CursorDirection = Entry == 1 ? -1 : 1;
		Out.Kind = bPerfect ? EDropKind::Perfect : EDropKind::Land;
		Out.Combo = T.Combo;
		Out.Height = T.Height();
		Out.Width = NewWidth;
		if (Row >= MaxTowerHeight)
		{
			const int32 Bonus = TowerCompleteBonus + T.Lives * TowerCompleteBonusPerLife;
			T.Score += Bonus;
			T.bCompleted = true;
			T.CursorCenter = NewCenter;
			PushEffect(Match, T, EEffectKind::Complete, FString::Printf(TEXT("Tower complete! +%d"), Bonus), 1.0, Delta + Bonus, Row);
			if (AllDone(Match))
			{
				Finish(Match);
			}
			Out.ScoreDelta = Delta + Bonus;
			Out.Label = TEXT("Tower complete");
			return Out;
		}
		PushEffect(Match, T, bPerfect ? EEffectKind::Perfect : EEffectKind::Land,
			bPerfect ? (T.Combo >= 2 ? FString::Printf(TEXT("Perfect ×%d"), T.Combo) : FString(TEXT("Perfect!"))) : FString::Printf(TEXT("+%d"), Delta),
			bPerfect ? FMath::Min(1.0, 0.55 + 0.15 * FMath::Min(T.Combo, 3)) : FMath::Max(0.25, Precision), Delta, Row);
		Out.ScoreDelta = Delta;
		Out.Label = bPerfect ? TEXT("Perfect") : TEXT("Stacked");
		return Out;
	}

	TArray<const FTower*> Leaderboard(const FMatch& Match)
	{
		TArray<const FTower*> Out;
		for (const FTower& T : Match.Towers)
		{
			Out.Add(&T);
		}
		Algo::StableSortBy(Out, [](const FTower* T) { return -T->Score; });
		return Out;
	}
}
