#include "Games/Bowling/BowlingRules.h"

namespace MovecadeKart::Bowling
{
	FCard ScoreRolls(const FString& PlayerId, const TArray<int32>& Rolls)
	{
		FCard Card;
		Card.PlayerId = PlayerId;
		Card.Rolls = Rolls;
		auto At = [&Rolls](int32 i) -> TOptional<int32> { return Rolls.IsValidIndex(i) ? TOptional<int32>(Rolls[i]) : TOptional<int32>(); };

		int32 i = 0;
		int32 Total = 0;
		// Once a frame can't be totalled yet (its bonus balls are still to come), no later frame can be either.
		bool bStillTotalling = true;
		for (int32 Number = 1; Number <= Frames && i < Rolls.Num(); ++Number)
		{
			FFrameScore& F = Card.FrameList.AddDefaulted_GetRef();
			F.Number = Number;
			TOptional<int32> Value;
			if (Number < Frames)
			{
				const int32 First = Rolls[i];
				if (First == Pins)
				{
					F.Rolls = { First };
					F.Kind = EFrameKind::Strike;
					F.bComplete = true;
					const TOptional<int32> B1 = At(i + 1), B2 = At(i + 2);
					if (B1 && B2)
					{
						Value = Pins + *B1 + *B2;
					}
					i += 1;
				}
				else
				{
					const TOptional<int32> Second = At(i + 1);
					F.Rolls = { First };
					if (Second)
					{
						F.Rolls.Add(*Second);
						F.bComplete = true;
						if (First + *Second == Pins)
						{
							F.Kind = EFrameKind::Spare;
							if (const TOptional<int32> Bonus = At(i + 2))
							{
								Value = Pins + *Bonus;
							}
						}
						else
						{
							F.Kind = EFrameKind::Open;
							Value = First + *Second;
						}
					}
					i += 2;
				}
			}
			else
			{
				// The 10th: a strike or a spare earns a third ball; it totals as the plain sum.
				const int32 First = Rolls[i];
				const TOptional<int32> Second = At(i + 1);
				const bool bBonus = First == Pins || (Second && First + *Second == Pins);
				const int32 Needed = bBonus ? 3 : 2;
				for (int32 k = 0; k < Needed; ++k)
				{
					if (const TOptional<int32> R = At(i + k))
					{
						F.Rolls.Add(*R);
					}
				}
				F.Kind = First == Pins ? EFrameKind::Strike : (Second ? (First + *Second == Pins ? EFrameKind::Spare : EFrameKind::Open) : EFrameKind::None);
				F.bComplete = F.Rolls.Num() == Needed;
				if (F.bComplete)
				{
					int32 Sum = 0;
					for (const int32 R : F.Rolls)
					{
						Sum += R;
					}
					Value = Sum;
				}
				i += Needed;
			}
			if (bStillTotalling && Value)
			{
				Total += *Value;
				F.Cumulative = Total;
			}
			else
			{
				bStillTotalling = false;
			}
		}
		Card.Score = Total;
		Card.bDone = Card.FrameList.Num() == Frames && Card.FrameList.Last().bComplete;
		return Card;
	}

	const FCard* FMatch::CardOf(const FString& PlayerId) const
	{
		return Cards.FindByPredicate([&PlayerId](const FCard& C) { return C.PlayerId == PlayerId; });
	}

	FMatch CreateMatch(const TArray<FString>& Order)
	{
		FMatch Match;
		Match.Order = Order;
		for (const FString& Id : Order)
		{
			Match.Cards.Add(ScoreRolls(Id, {}));
		}
		Match.TurnIndex = Order.Num() > 0 ? 0 : -1;
		Match.bComplete = Order.Num() == 0;
		return Match;
	}

	int32 FrameNumberFor(const FCard& Card)
	{
		int32 Completed = 0;
		for (const FFrameScore& F : Card.FrameList)
		{
			Completed += F.bComplete ? 1 : 0;
		}
		return FMath::Min(Completed + 1, Frames);
	}

	namespace
	{
		FCard* MutableCard(FMatch& Match, const FString& Id)
		{
			return Match.Cards.FindByPredicate([&Id](const FCard& C) { return C.PlayerId == Id; });
		}

		/** The first card in turn order from Start (inclusive, wrapping) that isn't finished, or -1. */
		int32 NextOpen(const FMatch& Match, int32 Start)
		{
			const int32 N = Match.Order.Num();
			for (int32 k = 0; k < N; ++k)
			{
				const int32 Index = ((Start + k) % N + N) % N;
				const FCard* Card = Match.CardOf(Match.Order[Index]);
				if (Card && !Card->bDone)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		void EndTurn(FMatch& Match, FRollResult& Result)
		{
			Result.bTurnEnded = true;
			Result.bRackReset = true;
			Match.Standing = Pins;
			Match.RollInFrame = 1;
			Match.TurnIndex = NextOpen(Match, Match.TurnIndex + 1);
			Match.bComplete = Match.TurnIndex == INDEX_NONE;
		}
	}

	FRollResult Roll(FMatch& Match, double PinsDown)
	{
		FRollResult Result;
		const FString* Bowler = Match.Bowler();
		FCard* Card = Bowler ? MutableCard(Match, *Bowler) : nullptr;
		if (Match.bComplete || !Card)
		{
			return Result;
		}
		// Clamped as a double before the conversion, which is undefined past int32 (the same result for any count in range).
		const int32 Felled = FMath::IsFinite(PinsDown) ? FMath::FloorToInt32(FMath::Clamp(PinsDown, 0.0, static_cast<double>(Match.Standing))) : 0;
		Result.Felled = Felled;
		const int32 Frame = FrameNumberFor(*Card);
		TArray<int32> Rolls = Card->Rolls;
		Rolls.Add(Felled);
		*Card = ScoreRolls(Card->PlayerId, Rolls);

		if (Frame == Frames)
		{
			if (Card->bDone)
			{
				EndTurn(Match, Result);
				return Result;
			}
			const int32 After = Match.Standing - Felled;
			Match.RollInFrame += 1;
			if (After == 0)
			{
				Match.Standing = Pins;
				Result.bRackReset = true;
			}
			else
			{
				Match.Standing = After;
			}
			return Result;
		}
		if (Match.RollInFrame == 2 || Felled == Pins)
		{
			EndTurn(Match, Result);
			return Result;
		}
		Match.Standing -= Felled;
		Match.RollInFrame = 2;
		return Result;
	}

	TArray<FString> Standings(const FMatch& Match)
	{
		TArray<FString> Ids = Match.Order;
		Ids.StableSort([&Match](const FString& A, const FString& B)
		{
			const FCard* CA = Match.CardOf(A);
			const FCard* CB = Match.CardOf(B);
			return (CA ? CA->Score : 0) > (CB ? CB->Score : 0);
		});
		return Ids;
	}

	void DropPlayer(FMatch& Match, const FString& PlayerId)
	{
		const int32 Index = Match.Order.IndexOfByKey(PlayerId);
		if (Index == INDEX_NONE)
		{
			return;
		}
		const bool bWasUp = Index == Match.TurnIndex;
		Match.Order.RemoveAt(Index);
		Match.Cards.RemoveAll([&PlayerId](const FCard& C) { return C.PlayerId == PlayerId; });
		if (Match.Order.Num() == 0)
		{
			Match.TurnIndex = INDEX_NONE;
			Match.bComplete = true;
			return;
		}
		if (bWasUp)
		{
			// The lane owner left: a fresh rack, and the next player on from the vacated seat.
			Match.Standing = Pins;
			Match.RollInFrame = 1;
			Match.TurnIndex = NextOpen(Match, Index);
		}
		else if (Index < Match.TurnIndex)
		{
			Match.TurnIndex -= 1;
		}
		Match.bComplete = Match.TurnIndex == INDEX_NONE;
	}

	TArray<FString> FrameMarks(const FFrameScore& Frame)
	{
		TArray<FString> Marks;
		auto Digit = [](int32 V) { return V == 0 ? FString(TEXT("–")) : FString::FromInt(V); };
		if (Frame.Number < Frames)
		{
			if (Frame.Kind == EFrameKind::Strike)
			{
				Marks.Add(TEXT("X"));
				return Marks;
			}
			for (int32 k = 0; k < Frame.Rolls.Num(); ++k)
			{
				Marks.Add(k == 1 && Frame.Kind == EFrameKind::Spare ? FString(TEXT("/")) : Digit(Frame.Rolls[k]));
			}
			return Marks;
		}
		// The 10th, ball by ball: each strike or spare racks the pins again, so a ball after one is a fresh rack.
		int32 RackStart = 0;
		for (int32 k = 0; k < Frame.Rolls.Num(); ++k)
		{
			const int32 R = Frame.Rolls[k];
			if (k == RackStart)
			{
				Marks.Add(R == Pins ? FString(TEXT("X")) : Digit(R));
				RackStart = R == Pins ? k + 1 : RackStart;
			}
			else if (Frame.Rolls[RackStart] + R == Pins)
			{
				Marks.Add(TEXT("/"));
				RackStart = k + 1;
			}
			else
			{
				Marks.Add(Digit(R));
			}
		}
		return Marks;
	}
}
