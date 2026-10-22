// URoomHostSubsystem: frames from the room server (protocol messages.ts), the room state they carry, and the seats'
// phone input (kart-input events and binary sensor frames).

#include "Net/RoomHostSubsystem.h"

#include "Dom/JsonObject.h"
#include "IWebSocket.h"
#include "Net/RoomHostInternal.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Net;
using MovecadeKart::Net::RoomHost::Describe;

// ───────────────────────────── text frames ─────────────────────────────

void URoomHostSubsystem::HandleText(const FString& Text)
{
	const TSharedPtr<FJsonObject> Message = RoomHost::ParseObject(Text);
	if (!Message.IsValid())
	{
		return;
	}
	FString Type;
	Message->TryGetStringField(TEXT("t"), Type);
	switch (ParseServerMessageType(Type))
	{
	case EServerMessage::Ping:
		if (Socket.IsValid() && Socket->IsConnected())
		{
			Socket->Send(Msg::Pong());
		}
		break;
	case EServerMessage::Welcome:
		HandleWelcome(*Message);
		break;
	case EServerMessage::State:
		if (const TSharedPtr<FJsonObject>* State = nullptr; Message->TryGetObjectField(TEXT("state"), State))
		{
			ApplyState(**State, /*bFromWelcome*/ false);
		}
		break;
	case EServerMessage::GameEvent:
		HandleGameEventMessage(*Message);
		break;
	case EServerMessage::Error:
		HandleError(*Message);
		break;
	case EServerMessage::HostChanged:
		HandleHostChanged(*Message);
		break;
	case EServerMessage::SignedScores:
		HandleSignedScores(Text);
		break;
	case EServerMessage::RoomClosed:
	{
		FString Reason;
		Message->TryGetStringField(TEXT("reason"), Reason);
		LastError = FString::Printf(TEXT("Room closed: %s"), *SanitizeServerText(Reason));
		MarkLobbyChanged();
		UE_LOG(LogRoomHost, Warning, TEXT("%s"), *LastError);
		break;
	}
	case EServerMessage::ActionAck:
	case EServerMessage::ResumeCountdown:
		UE_LOG(LogRoomHost, Log, TEXT("%s"), *Text.Left(300));
		break;
	case EServerMessage::Unknown:
		break;
	}
}

void URoomHostSubsystem::HandleWelcome(const FJsonObject& Message)
{
	Message.TryGetStringField(TEXT("sessionId"), SessionId);
	FString Token;
	if (Message.TryGetStringField(TEXT("hostToken"), Token) && !Token.IsEmpty())
	{
		HostToken = Token;
	}
	if (Message.TryGetStringField(TEXT("joinToken"), Token))
	{
		JoinToken = Token;
	}
	bWelcomed = true;
	ReconnectAttempt = 0;
	if (const TSharedPtr<FJsonObject>* State = nullptr; Message.TryGetObjectField(TEXT("state"), State))
	{
		ApplyState(**State, /*bFromWelcome*/ true);
	}
	if (Room.Status == EMcRoomStatus::Lobby)
	{
		// A start-game lost with the old socket must not block this one.
		bStartSent = false;
	}
	SetConnection(EMcConnection::Open);
	UE_LOG(LogRoomHost, Display, TEXT("Welcome: session %s, %s of room %s (%s), map %s, %d seat(s)"),
		*SessionId, IsHost() ? TEXT("host") : TEXT("STANDBY (another screen hosts this room)"), *Room.JoinCode, *Room.RoomId, *Room.SelectedMapId, Room.Players.Num());
	FlushOutbox();
	if (!PendingMapId.IsEmpty() && Room.Status == EMcRoomStatus::Lobby && PendingMapId != Room.SelectedMapId)
	{
		SelectMap(PendingMapId);
	}
	PendingMapId.Reset();
	if (!PendingGameId.IsEmpty())
	{
		SelectGame(PendingGameId);
	}
	PendingGameId.Reset();
	Save();
}

void URoomHostSubsystem::HandleError(const FJsonObject& Message)
{
	FString ErrorCode;
	FString Text;
	Message.TryGetStringField(TEXT("code"), ErrorCode);
	Message.TryGetStringField(TEXT("message"), Text);
	bool bFatal = false;
	Message.TryGetBoolField(TEXT("fatal"), bFatal);
	// The lobby shows this line as it is.
	LastError = FString::Printf(TEXT("%s: %s"), *SanitizeServerText(ErrorCode), *SanitizeServerText(Text));
	MarkLobbyChanged();
	UE_LOG(LogRoomHost, Warning, TEXT("Server error %s%s"), *LastError, bFatal ? TEXT(" (fatal)") : TEXT(""));
	if (ErrorCode.Equals(ErrorUnauthorizedOrigin, ESearchCase::CaseSensitive))
	{
		if (IsLocalPartyHost(PartyHost))
		{
			UE_LOG(LogRoomHost, Error, TEXT("The room server refused this host's Origin. UE's libwebsockets sends 'Origin: http://<party host name>' "
				"(http://127.0.0.1 for -RoomServer=127.0.0.1:1999). Run the local server with --var EXTRA_ALLOWED_ORIGINS=http://localhost,http://127.0.0.1 "
				"(Tools/net-sim/run-party.ps1 does)."));
		}
		else
		{
			FString HostName = PartyHost;
			HostName.Split(TEXT(":"), &HostName, nullptr);
			UE_LOG(LogRoomHost, Error, TEXT("%s refused this host's Origin (http://%s): the server does not admit the Unreal host yet. "
				"Deploy Docs/Port/server/0001-*.patch to it, or run with -RoomLocal."), *PartyHost, *HostName);
		}
	}
	if (IsStartRejectedError(ErrorCode))
	{
		bStartSent = false;
	}
}

void URoomHostSubsystem::HandleHostChanged(const FJsonObject& Message)
{
	Message.TryGetStringField(TEXT("hostSessionId"), Room.HostSessionId);
	double Version = 0.0;
	// The int64 cast is undefined past ~9.2e18.
	if (Message.TryGetNumberField(TEXT("stateVersion"), Version) && FMath::Abs(Version) < 9.2e18)
	{
		// sessionStore.ts: host-changed advances the watermark too.
		HighestStateVersion = FMath::Max(HighestStateVersion, static_cast<int64>(Version) - 1);
	}
	FString Reason;
	Message.TryGetStringField(TEXT("reason"), Reason);
	MarkLobbyChanged();
	UE_LOG(LogRoomHost, Log, TEXT("host-changed: %s (%s)"), *Room.HostSessionId, *Reason);
}

void URoomHostSubsystem::HandleSignedScores(const FString& Text)
{
	UE_LOG(LogRoomHost, Display, TEXT("signed-scores for match %d (%d bytes)"), Room.MatchNumber, Text.Len());
	OnSignedScores.Broadcast(Text);
	// The web host never acks (bug B9), so the outbox redelivers up to 4 times. Ack every delivery.
	AckedMatchNumber = Room.MatchNumber;
	Send(Msg::ScoresAck(Room.MatchNumber), TEXT("scores-ack"));
}

void URoomHostSubsystem::HandleGameEventMessage(const FJsonObject& Message)
{
	FString Event;
	FString From;
	Message.TryGetStringField(TEXT("event"), Event);
	Message.TryGetStringField(TEXT("fromPlayerId"), From);
	const TSharedPtr<FJsonObject>* Data = nullptr;
	const bool bHasData = Message.TryGetObjectField(TEXT("data"), Data);
	if (Event.Equals(KartInputEvent, ESearchCase::CaseSensitive))
	{
		if (bHasData)
		{
			HandleKartInput(From, **Data);
		}
		return;
	}
	if (Event.Equals(PracticeActionEvent, ESearchCase::CaseSensitive))
	{
		if (bHasData)
		{
			HandlePracticeAction(From, **Data);
		}
		return;
	}
	if (!Event.IsEmpty() && !From.IsEmpty())
	{
		OnGameEvent.Broadcast(From, Event, bHasData ? *Data : TSharedPtr<FJsonObject>());
	}
}

void URoomHostSubsystem::HandlePracticeAction(const FString& FromPlayerId, const FJsonObject& Data)
{
	// useLobbyPractice.ts: lobby only, from a connected seat that isn't ready yet; never touches the race.
	const FMcSeat* Seat = Room.FindByPlayerId(FromPlayerId);
	FString ActionId;
	if (Room.Status != EMcRoomStatus::Lobby || !Seat || !Seat->bConnected || Seat->bEliminated || Seat->bReady
		|| !ReadPracticeAction(Data, GameId, ActionId))
	{
		return;
	}
	Send(Msg::PracticeAck(GameId, Seat->PlayerId, ActionId), TEXT("game-event"));
	UE_LOG(LogRoomHost, Display, TEXT("Practice move from seat %d \"%s\" acknowledged"), Seat->Seat, *Seat->Name);
}

void URoomHostSubsystem::HandleKartInput(const FString& FromPlayerId, const FJsonObject& Data)
{
	const FMcSeat* Seat = Room.FindByPlayerId(FromPlayerId);
	if (!Seat || Seat->bEliminated || Seat->Seat < 1 || Seat->Seat > MaxPlayers)
	{
		return;
	}
	const FKartInputEvent Event = ParseKartInput(Data);
	FSeatInput& In = Inputs[Seat->SeatIndex()];
	if (Event.Accelerating) In.bAccelerating = *Event.Accelerating;
	if (Event.Reversing) In.bReversing = *Event.Reversing;
	if (Event.Drifting) In.bDrifting = *Event.Drifting;
	if (Event.Steering) In.Steering = *Event.Steering;
	bool bItemAccepted = false;
	if (Event.bUseItem)
	{
		const double T = Now();
		if (T - In.LastItemTime >= ItemDebounceSeconds)
		{
			In.LastItemTime = T;
			In.bItemPending = true;
			bItemAccepted = true;
		}
	}
	if (Event.Accelerating || Event.Reversing || Event.Drifting || Event.bUseItem)
	{
		UE_LOG(LogRoomHost, Verbose, TEXT("kart-input %s: gas=%d brake=%d drift=%d%s"), *Describe(*Seat),
			In.bAccelerating ? 1 : 0, In.bReversing ? 1 : 0, In.bDrifting ? 1 : 0,
			Event.bUseItem ? (bItemAccepted ? TEXT(" ITEM") : TEXT(" ITEM (debounced)")) : TEXT(""));
	}
}

// ───────────────────────────── binary frames ─────────────────────────────

void URoomHostSubsystem::HandleBinary(const void* Data, SIZE_T Size, bool bIsLastFragment)
{
	// Most frames arrive whole: decode them in place rather than through the accumulator.
	if (bIsLastFragment && BinaryAccumulator.Num() == 0)
	{
		HandleSensorFrame(static_cast<const uint8*>(Data), static_cast<int32>(Size));
		return;
	}
	BinaryAccumulator.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
	if (!bIsLastFragment)
	{
		if (BinaryAccumulator.Num() > RoomHost::MaxBinaryMessageBytes)
		{
			BinaryAccumulator.Reset(); // never buffer junk
		}
		return;
	}
	HandleSensorFrame(BinaryAccumulator.GetData(), BinaryAccumulator.Num());
	BinaryAccumulator.Reset();
}

void URoomHostSubsystem::HandleSensorFrame(const uint8* Data, int32 Size)
{
	FSensorFrame Frame;
	ESensorError Error = ESensorError::None;
	if (!DecodeSensorFrame(Data, Size, Frame, &Error) || Frame.PlayerIndex < 0 || Frame.PlayerIndex >= MaxPlayers)
	{
		UE_LOG(LogRoomHost, Verbose, TEXT("Dropped a %d-byte binary frame (%s)"), Size, LexToString(Error));
		return;
	}
	// legacyBridge.ts playerForRelayedFrame: by seat, never by roster index.
	const FMcSeat* Seat = Room.FindBySeat(Frame.PlayerIndex + 1);
	if (!Seat || Seat->bEliminated)
	{
		return;
	}
	FSeatInput& In = Inputs[Frame.PlayerIndex];
	// The relay doesn't guarantee order: drop a frame older than the last one (sequence numbers wrap at 256).
	// After a gap, accept anything, in case the phone restarted its counter.
	const uint8 Behind = static_cast<uint8>(In.LastSequence - Frame.Sequence);
	if (In.SensorFrames > 0 && Behind > 0 && Behind < 128 && Now() - In.LastSensorTime < SensorStaleSeconds)
	{
		return;
	}
	In.Steering = SteeringFromGamma(Frame.Gamma, SteeringSensitivity);
	In.LastSequence = Frame.Sequence;
	In.LastSensorTime = Now();
	++In.SensorFrames;
	OnSensorFrame.Broadcast(Frame.PlayerIndex, Frame);
	if (In.LastSensorTime - In.LastLogTime >= RoomHost::SensorLogIntervalSeconds)
	{
		In.LastLogTime = In.LastSensorTime;
		UE_LOG(LogRoomHost, Verbose, TEXT("Sensor %s: seq=%u gamma=%.2f -> steering %+.3f (%d frames, %d bytes, orientation %u)"),
			*Describe(*Seat), Frame.Sequence, Frame.Gamma, In.Steering, In.SensorFrames, Size, Frame.ScreenOrientation);
	}
}

// ───────────────────────────── room state ─────────────────────────────

void URoomHostSubsystem::ApplyState(const FJsonObject& State, bool bFromWelcome)
{
	FMcRoomState Next;
	FRoomStateIssues Issues;
	if (!ParseRoomState(State, Next, &Issues))
	{
		return;
	}
	if (Issues.HasAny())
	{
		ReportStateIssues(Issues, Next.StateVersion);
	}
	if (!bFromWelcome && Next.StateVersion < HighestStateVersion)
	{
		return; // stale (sessionStore.ts watermark)
	}
	HighestStateVersion = FMath::Max(HighestStateVersion, Next.StateVersion);
	if (!bFromWelcome)
	{
		Send(Msg::StateAck(Next.StateVersion), TEXT("state-ack"));
	}
	const FMcRoomState Previous = MoveTemp(Room);
	Room = MoveTemp(Next);
	if (IsValidRoomCode(Room.JoinCode))
	{
		Code = Room.JoinCode;
	}
	OnStateApplied(Previous);
}

void URoomHostSubsystem::ReportStateIssues(const FRoomStateIssues& Issues, int64 StateVersion)
{
	// An honest server never gets here, and a hostile one may say it in every frame: one warning per connection.
	if (bReportedStateIssues)
	{
		UE_LOG(LogRoomHost, Verbose, TEXT("State %lld from the room server was cleaned up again"), StateVersion);
		return;
	}
	bReportedStateIssues = true;
	UE_LOG(LogRoomHost, Warning, TEXT("State %lld from the room server broke the protocol's limits and was cleaned up: %d roster entries unread, "
		"%d skipped, %d strings sanitised"), StateVersion, Issues.UnreadEntries, Issues.SkippedEntries, Issues.CleanedStrings);
}

void URoomHostSubsystem::OnStateApplied(const FMcRoomState& Previous)
{
	const bool bSeatsChanged = ApplySeatChanges(Previous);
	if (Previous.SelectedMapId != Room.SelectedMapId && !Previous.SelectedMapId.IsEmpty())
	{
		UE_LOG(LogRoomHost, Display, TEXT("Map selected: %s"), *Room.SelectedMapId);
	}
	ApplyStatusChanges(Previous);

	// The server's game is the truth (a rejected configure-room, another host); our own pick wins only while in flight.
	if (!Room.GameId.IsEmpty() && Room.GameId != GameId && Now() - GameAskedAt > RoomHost::GameSelectGraceSeconds)
	{
		GameId = Room.GameId;
	}
	UpdateJoinUrl();
	RefreshRosterKey();
	MarkLobbyChanged();
	if (bSeatsChanged)
	{
		OnSeatsChanged.Broadcast(Room.Players);
	}
	OnRoomStateChanged.Broadcast();
}

bool URoomHostSubsystem::ApplySeatChanges(const FMcRoomState& Previous)
{
	bool bSeatsChanged = Previous.Players.Num() != Room.Players.Num();
	for (const FMcSeat& Seat : Room.Players)
	{
		const FMcSeat* Old = Previous.FindByPlayerId(Seat.PlayerId);
		if (!Old)
		{
			UE_LOG(LogRoomHost, Display, TEXT("Seat joined: %s paint=%s vehicle=%s connected=%d"), *Describe(Seat),
				Seat.KartPaint.IsEmpty() ? TEXT("(seat colour)") : *Seat.KartPaint, Seat.Vehicle.IsEmpty() ? TEXT("kart") : *Seat.Vehicle, Seat.bConnected ? 1 : 0);
			ResetSeatInput(Seat.SeatIndex());
			bSeatsChanged = true;
			continue;
		}
		if (Old->bConnected != Seat.bConnected)
		{
			UE_LOG(LogRoomHost, Display, TEXT("Seat %s: %s"), Seat.bConnected ? TEXT("reconnected") : TEXT("disconnected"), *Describe(Seat));
			ResetSeatInput(Seat.SeatIndex());
		}
		if (Old->bReady != Seat.bReady || Old->bCalibrated != Seat.bCalibrated)
		{
			UE_LOG(LogRoomHost, Display, TEXT("Seat %s: calibrated=%d ready=%d"), *Describe(Seat), Seat.bCalibrated ? 1 : 0, Seat.bReady ? 1 : 0);
		}
		if (!Old->bEliminated && Seat.bEliminated)
		{
			UE_LOG(LogRoomHost, Display, TEXT("Seat eliminated: %s"), *Describe(Seat));
			ResetSeatInput(Seat.SeatIndex());
		}
		// The reconnect grace counts in whole seconds on screen: a sub-second change is not a redraw.
		bSeatsChanged |= Old->Name != Seat.Name || Old->KartPaint != Seat.KartPaint || Old->bConnected != Seat.bConnected || Old->bReady != Seat.bReady
			|| Old->bCalibrated != Seat.bCalibrated || Old->bEliminated != Seat.bEliminated || Old->Seat != Seat.Seat || Old->Screen != Seat.Screen
			|| Old->GraceRemainingMs / 1000 != Seat.GraceRemainingMs / 1000;
	}
	for (const FMcSeat& Old : Previous.Players)
	{
		if (!Room.FindByPlayerId(Old.PlayerId))
		{
			UE_LOG(LogRoomHost, Display, TEXT("Seat left: %s"), *Describe(Old));
			ResetSeatInput(Old.SeatIndex());
			bSeatsChanged = true;
		}
	}
	return bSeatsChanged;
}

void URoomHostSubsystem::ApplyStatusChanges(const FMcRoomState& Previous)
{
	// Match start: the server accepted start-game.
	if (Room.Status == EMcRoomStatus::InProgress && StartedMatchNumber != Room.MatchNumber)
	{
		StartedMatchNumber = Room.MatchNumber;
		LobbyCountdownEndsAt = -1.0;
		if (AdvancedMatchNumber != Room.MatchNumber)
		{
			// HostSurface.tsx:288-297: once per match. Never name a player in kart.
			AdvancedMatchNumber = Room.MatchNumber;
			Send(Msg::AdvanceTurnSimultaneous(), TEXT("advance-turn"));
		}
		ResetAllSeatInputs();
		UE_LOG(LogRoomHost, Display, TEXT("Match %d started on %s with %d seat(s)"), Room.MatchNumber, *Room.SelectedMapId, Room.Players.Num());
		if (Debug.bSelfTest)
		{
			SelfTestStep = 0;
			SelfTestStart = Now();
		}
		OnStartRequested.Broadcast(Room.SelectedMapId);
	}

	const bool bWasPaused = Previous.Status == EMcRoomStatus::Paused;
	const bool bPaused = Room.Status == EMcRoomStatus::Paused;
	if (bWasPaused != bPaused && (Previous.Status == EMcRoomStatus::InProgress || bWasPaused))
	{
		UE_LOG(LogRoomHost, Display, TEXT("Match %s (%s %s)"), bPaused ? TEXT("paused") : TEXT("resumed"), *Room.PauseReason, *Room.PausedForPlayerId);
		OnPauseChanged.Broadcast(bPaused, bPaused ? Room.PauseReason : Previous.PauseReason, bPaused ? Room.PausedForPlayerId : Previous.PausedForPlayerId);
	}
	if (Room.Status == EMcRoomStatus::Complete && Previous.Status != EMcRoomStatus::Complete)
	{
		UE_LOG(LogRoomHost, Display, TEXT("Match %d complete"), Room.MatchNumber);
	}
	const bool bBackInLobby = Room.Status == EMcRoomStatus::Lobby && Previous.Status != EMcRoomStatus::Lobby;
	if (bBackInLobby)
	{
		bStartSent = false;
	}
	if (bBackInLobby && Previous.Status != EMcRoomStatus::Unknown)
	{
		UE_LOG(LogRoomHost, Display, TEXT("Back in the lobby (match %d next)"), Room.MatchNumber);
		ResetAllSeatInputs();
		OnReturnedToLobby.Broadcast();
	}
}

// ───────────────────────────── seat input ─────────────────────────────

bool URoomHostSubsystem::GetSeatInput(int32 Seat, MovecadeKart::FKartInput& Out) const
{
	static_assert(SeatCount == MaxPlayers, "one input slot per seat");
	if (Seat < 0 || Seat >= MaxPlayers)
	{
		return false;
	}
	const FMcSeat* Player = Room.FindBySeat(Seat + 1);
	if (!Player || Player->bEliminated)
	{
		return false;
	}
	const FSeatInput& In = Inputs[Seat];
	Out = MovecadeKart::FKartInput();
	// Tilt steering goes neutral when the frames stop (the phone locked or lost focus), rather than holding the
	// last lean forever. Button steering (kart-input) has no frames and isn't affected.
	const bool bTiltStale = In.SensorFrames > 0 && Now() - In.LastSensorTime > SensorStaleSeconds;
	Out.SteeringX = bTiltStale ? 0.0 : In.Steering;
	Out.bForward = In.bAccelerating;
	Out.bBackward = In.bReversing;
	Out.bJump = In.bDrifting;
	return true;
}

bool URoomHostSubsystem::ConsumeUseItem(int32 Seat)
{
	if (Seat < 0 || Seat >= MaxPlayers || !Inputs[Seat].bItemPending)
	{
		return false;
	}
	Inputs[Seat].bItemPending = false;
	return true;
}

void URoomHostSubsystem::ResetSeatInput(int32 SeatIndex)
{
	if (SeatIndex < 0 || SeatIndex >= MaxPlayers)
	{
		return;
	}
	// The log throttle survives: a seat that changes hands must not flood the log.
	FSeatInput& In = Inputs[SeatIndex];
	const double LastLog = In.LastLogTime;
	In = FSeatInput();
	In.LastLogTime = LastLog;
}

void URoomHostSubsystem::ResetAllSeatInputs()
{
	for (int32 SeatIndex = 0; SeatIndex < SeatCount; ++SeatIndex)
	{
		ResetSeatInput(SeatIndex);
	}
}
