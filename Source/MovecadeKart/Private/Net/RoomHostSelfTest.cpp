// URoomHostSubsystem -RoomSelfTest: stands in for the race layer after each match start, so the room protocol can be
// exercised end to end without a race (phases, haptics, live scores, results, play again). Not in Shipping builds.

#include "Net/RoomHostSubsystem.h"

#include "Net/RoomProtocol.h"

namespace
{
	/** Seconds after the match start (the web host's intro starts 1 s after the karts exist). */
	constexpr double IntroAt = 1.0;
	constexpr double CountdownAt = 2.0;
	constexpr double GoAt = 7.0;
	/** Seconds after GO. */
	constexpr double RaceLength = 12.0;
	constexpr double PulseInterval = 1.0;
	/** The seated phone's live score climbs this many points per second of racing (capped at 99). */
	constexpr double ScorePerSecond = 7.0;
	/** Seconds after the match start before "play again", once the signed scores were acked. */
	constexpr double RestartAt = 22.0;

	enum class ESelfTestStep : int32 { Intro, Countdown, Go, Racing, Restart };
}

void URoomHostSubsystem::RunSelfTest(double T)
{
	const double Elapsed = T - SelfTestStart;
	const FMcSeat* First = Room.Players.Num() > 0 ? &Room.Players[0] : nullptr;
	switch (static_cast<ESelfTestStep>(SelfTestStep))
	{
	case ESelfTestStep::Intro:
		if (Elapsed >= IntroAt)
		{
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: intro"));
			PublishRacePhase(EMcRacePhase::Intro);
			++SelfTestStep;
		}
		break;
	case ESelfTestStep::Countdown:
		if (Elapsed >= CountdownAt)
		{
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: countdown (set-countdown match 5000)"));
			PublishRacePhase(EMcRacePhase::Countdown);
			++SelfTestStep;
		}
		break;
	case ESelfTestStep::Go:
		if (Elapsed >= GoAt)
		{
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: GO (set-countdown match 0)"));
			PublishRacePhase(EMcRacePhase::Racing);
			SelfTestRaceStart = T;
			SelfTestLastPulse = T;
			++SelfTestStep;
		}
		break;
	case ESelfTestStep::Racing:
		if (First && T - SelfTestLastPulse >= PulseInterval)
		{
			SelfTestLastPulse = T;
			SendHaptic(First->SeatIndex(), 0.5f, TEXT("barrier"), false);
			UpdateLiveScores({ { First->PlayerId, FMath::Clamp(static_cast<int32>((T - SelfTestRaceStart) * ScorePerSecond), 0, 99) } });
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: haptic + live score for %s"), *First->PlayerId);
		}
		if (First && T - SelfTestRaceStart >= RaceLength)
		{
			FMcRaceResult Finisher;
			Finisher.SeatIndex = First->SeatIndex();
			Finisher.bFinished = true;
			Finisher.FinishTimeMs = (T - SelfTestRaceStart) * 1000.0;
			TArray<FMcRaceResult> Results = { Finisher };
			for (const FMcSeat& Other : Room.Players)
			{
				if (Other.PlayerId != First->PlayerId)
				{
					FMcRaceResult Dnf;
					Dnf.SeatIndex = Other.SeatIndex();
					Results.Add(Dnf);
				}
			}
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: finished, submitting results"));
			PublishRacePhase(EMcRacePhase::Finished);
			SubmitResults(Results);
			++SelfTestStep;
		}
		break;
	case ESelfTestStep::Restart:
		if (Room.Status == EMcRoomStatus::Complete && AckedMatchNumber == Room.MatchNumber && Elapsed >= RestartAt)
		{
			if (Room.MatchNumber <= 1)
			{
				UE_LOG(LogRoomHost, Display, TEXT("SelfTest: play again (restart-game)"));
				RestartGame();
			}
			SelfTestStep = -1;
		}
		break;
	default:
		SelfTestStep = -1;
	}
}
