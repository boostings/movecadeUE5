// URoomHostSubsystem: what a running match tells the room and the phones (the web host's legacyBridge.ts and
// useKartRaceNetworkSync.ts): phases and countdowns, live and final scores, turns, game events, toasts, haptics, and
// the host's room actions (restart, back to the lobby, kick, drop).

#include "Net/RoomHostSubsystem.h"

#include "Algo/Compare.h"
#include "Net/RoomHostInternal.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Net;

namespace
{
	/** The room's live scores go out at most this often (legacyBridge.ts). */
	constexpr double LiveScoreMinIntervalSeconds = 1.0;

	bool SameScores(const TArray<FMcScoreEntry>& A, const TArray<FMcScoreEntry>& B)
	{
		return A.Num() == B.Num() && Algo::Compare(A, B, [](const FMcScoreEntry& X, const FMcScoreEntry& Y)
		{
			return X.PlayerId == Y.PlayerId && X.Score == Y.Score;
		});
	}

	bool IsMatchLive(EMcRoomStatus Status)
	{
		return Status == EMcRoomStatus::InProgress || Status == EMcRoomStatus::Paused;
	}
}

void URoomHostSubsystem::PublishRacePhase(EMcRacePhase Phase)
{
	switch (Phase)
	{
	case EMcRacePhase::Intro:
		if (AdvancedMatchNumber != Room.MatchNumber && Room.Status == EMcRoomStatus::InProgress)
		{
			AdvancedMatchNumber = Room.MatchNumber;
			Send(Msg::AdvanceTurnSimultaneous(), TEXT("advance-turn"));
		}
		break;
	case EMcRacePhase::Countdown:
		PublishMatchCountdown(MatchCountdownMs);
		break;
	case EMcRacePhase::Racing:
		PublishMatchCountdown(0);
		break;
	case EMcRacePhase::Finished:
		break;
	}
}

void URoomHostSubsystem::PublishMatchCountdown(int32 DurationMs)
{
	Send(Msg::SetCountdown(TEXT("match"), DurationMs), TEXT("set-countdown"));
}

bool URoomHostSubsystem::UpdateLiveScores(const TArray<FMcScoreEntry>& Scores)
{
	if (!AreScoresValid(Scores))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("UpdateLiveScores: invalid score list ignored"));
		return false;
	}
	if (SameScores(Scores, LastLiveScores))
	{
		return true;
	}
	const double T = Now();
	if (T - LastLiveScoreTime < LiveScoreMinIntervalSeconds)
	{
		return false;
	}
	LastLiveScores = Scores;
	LastLiveScoreTime = T;
	Send(Msg::ScoreUpdate(Scores), TEXT("score-update"));
	return true;
}

void URoomHostSubsystem::SubmitResults(const TArray<FMcRaceResult>& Results)
{
	if (!IsMatchLive(Room.Status))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("SubmitResults: the match is not running (status %d)"), static_cast<int32>(Room.Status));
		return;
	}
	if (SubmittedMatchNumber == Room.MatchNumber)
	{
		UE_LOG(LogRoomHost, Warning, TEXT("SubmitResults: match %d already submitted"), Room.MatchNumber);
		return;
	}
	if (Room.StartedAt > 0 && Room.ServerTime > 0 && Room.ServerTime - Room.StartedAt < MinFinishTimeMs)
	{
		UE_LOG(LogRoomHost, Warning, TEXT("SubmitResults: under 10 s since start-game; the server may answer MATCH_TOO_SHORT"));
	}
	SubmittedMatchNumber = Room.MatchNumber;
	const TArray<FMcScoreEntry> Scores = BuildFinalScores(Results, Room);
	for (const FMcScoreEntry& Entry : Scores)
	{
		UE_LOG(LogRoomHost, Display, TEXT("Result %s: %d ms"), *Entry.PlayerId, Entry.Score);
	}
	// RaceUI.tsx: the update goes first, so a rejected completion still leaves the scores.
	if (Scores.Num() > 0)
	{
		Send(Msg::ScoreUpdate(Scores), TEXT("score-update"));
	}
	Send(Msg::CompleteGame(Scores), TEXT("complete-game"));
}

void URoomHostSubsystem::CompleteGame(const TArray<FMcScoreEntry>& Scores)
{
	if (!IsMatchLive(Room.Status))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("CompleteGame: the match is not running (status %d)"), static_cast<int32>(Room.Status));
		return;
	}
	if (SubmittedMatchNumber == Room.MatchNumber || !AreScoresValid(Scores))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("CompleteGame: match %d already submitted, or an invalid score list"), Room.MatchNumber);
		return;
	}
	SubmittedMatchNumber = Room.MatchNumber;
	for (const FMcScoreEntry& Entry : Scores)
	{
		UE_LOG(LogRoomHost, Display, TEXT("Score %s: %d"), *Entry.PlayerId, Entry.Score);
	}
	// RaceUI.tsx: the update goes first, so a rejected completion still leaves the scores.
	if (Scores.Num() > 0)
	{
		Send(Msg::ScoreUpdate(Scores), TEXT("score-update"));
	}
	Send(Msg::CompleteGame(Scores), TEXT("complete-game"));
}

void URoomHostSubsystem::AdvanceTurn(const FString& ActivePlayerId)
{
	if (IsMatchLive(Room.Status))
	{
		// Empty: every seated phone streams (activePlayerId null), as darts asks.
		Send(ActivePlayerId.IsEmpty() ? Msg::AdvanceTurnSimultaneous() : Msg::AdvanceTurn(ActivePlayerId), TEXT("advance-turn"));
	}
}

void URoomHostSubsystem::SendGameEvent(const FString& Event, const FString& DataJson)
{
	if (bWelcomed)
	{
		Send(Msg::GameEvent(Event, DataJson), TEXT("game-event"));
	}
}

void URoomHostSubsystem::SendPadPrompt(const FString& PlayerId, const FString& Title, const FString& Subtitle, const FString& A,
	const FString& B, const FString& AccentHex, const FString& Mode)
{
	SendGameEvent(PadPromptEvent, PadPromptData(PlayerId, Title, Subtitle, A, B, AccentHex, Mode));
}

void URoomHostSubsystem::SendPlayFeedback(const FString& PlayerId, const FString& Title, const FString& Detail, const FString& Tone, const FString& Haptic, bool bMajor)
{
	if (bWelcomed)
	{
		Send(Msg::PlayFeedback(GameId, Room.MatchNumber, PlayerId, Title, Detail, Tone, Haptic, bMajor), TEXT("game-event"));
	}
}

void URoomHostSubsystem::SendHaptic(int32 Seat, float Intensity, FName CollisionType, bool bSustained)
{
	const FMcSeat* Player = Seat >= 0 && Seat < MaxPlayers ? Room.FindBySeat(Seat + 1) : nullptr;
	if (!Player || !Player->bConnected || !bWelcomed)
	{
		return;
	}
	// A token bucket: haptics never spend more than their share of the host's 40 frames/s control budget.
	const double T = Now();
	HapticTokens = FMath::Min(RoomHost::HapticTokensPerSecond, HapticTokens + (T - HapticRefillTime) * RoomHost::HapticTokensPerSecond);
	HapticRefillTime = T;
	FSeatInput& In = Inputs[Seat];
	if (HapticTokens < 1.0 || (bSustained && T - In.LastHapticTime < RoomHost::HapticMinSustainedInterval))
	{
		return;
	}
	HapticTokens -= 1.0;
	In.LastHapticTime = T;
	Send(Msg::Haptic(Player->PlayerId, Intensity, CollisionType.ToString(), bSustained), TEXT("game-event"));
}

void URoomHostSubsystem::RestartGame()
{
	Send(Msg::RestartGame(FString::Printf(TEXT("ue-restart-%d"), ++ActionCounter)), TEXT("restart-game"));
}

void URoomHostSubsystem::ReturnToLobby()
{
	Send(Msg::ReturnToLobby(FString::Printf(TEXT("ue-lobby-%d"), ++ActionCounter)), TEXT("return-to-lobby"));
}

void URoomHostSubsystem::KickPlayer(const FString& PlayerId)
{
	Send(Msg::KickPlayer(PlayerId), TEXT("kick-player"));
}

void URoomHostSubsystem::DropPlayer(const FString& PlayerId)
{
	Send(Msg::DropPlayer(PlayerId), TEXT("drop-player"));
}
