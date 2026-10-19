#include "Net/RoomHostSubsystem.h"

#include "Algo/Compare.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "IWebSocket.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Net/QrCode.h"
#include "Net/RoomProtocol.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY(LogRoomHost);

using namespace MovecadeKart::Net;

namespace
{
	/** A saved room is only worth reclaiming while the server can still be holding it (HOST_GRACE_MS 90 s). */
	constexpr int64 SavedRoomMaxAgeMs = 80000;
	constexpr double SaveIntervalSeconds = 10.0;
	constexpr int32 OutboxLimit = 64;
	constexpr double MomentaryMaxAgeSeconds = 1.5;
	constexpr double HapticTokensPerSecond = 15.0;
	constexpr double HapticMinSustainedInterval = 0.1;
	const double ReconnectLadderMs[] = { 250, 500, 1000, 2000, 4000, 8000 };

	int64 UnixMs()
	{
		return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object))
		{
			return nullptr;
		}
		return Object;
	}

	FString Describe(const FMcSeat& Seat)
	{
		return FString::Printf(TEXT("seat %d %s \"%s\""), Seat.Seat, *Seat.PlayerId, *Seat.Name);
	}
}

void URoomHostSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));

	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("RoomServer="), PartyHost);
	FParse::Value(Cmd, TEXT("RoomJoinBase="), JoinBaseUrl);
	FParse::Value(Cmd, TEXT("RoomOrigin="), Origin);
	FParse::Value(Cmd, TEXT("RoomMap="), PendingMapId);
	bSecure = bSecure || FParse::Param(Cmd, TEXT("RoomSecure"));
	// -RoomAutoStart[=Seconds]: start (solo too) once every seat has been ready for that long.
	bAutoStart = FParse::Param(Cmd, TEXT("RoomAutoStart")) || FParse::Value(Cmd, TEXT("RoomAutoStart="), AutoStartDelay);
	bSelfTest = FParse::Param(Cmd, TEXT("RoomSelfTest"));
	FString Shots;
	if (FParse::Value(Cmd, TEXT("RoomShots="), Shots, false))
	{
		TArray<FString> Parts;
		Shots.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			ShotTimes.Add(FCString::Atod(*Part));
		}
		ShotTimes.Sort();
	}
	StartTime = Now();
	HapticRefillTime = StartTime;
	LoadSaved();
	bInitialized = true;

	int32 PhoneSeat = INDEX_NONE;
	if (FParse::Param(Cmd, TEXT("KartLobby")) || FParse::Param(Cmd, TEXT("RoomOpen")) || FParse::Value(Cmd, TEXT("KartPhoneSeat="), PhoneSeat))
	{
		OpenRoom();
	}
}

void URoomHostSubsystem::Deinitialize()
{
	bInitialized = false;
	bWantOpen = false;
	if (Socket.IsValid())
	{
		if (bWelcomed)
		{
			Save();
		}
		TSharedPtr<IWebSocket> Old = MoveTemp(Socket);
		Old->Close(1000, TEXT("host shutting down"));
	}
	Super::Deinitialize();
}

double URoomHostSubsystem::Now() const
{
	return FPlatformTime::Seconds();
}

TStatId URoomHostSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URoomHostSubsystem, STATGROUP_Tickables);
}

// ───────────────────────────── connection ─────────────────────────────

void URoomHostSubsystem::OpenRoom()
{
	if (bWantOpen && Connection != EMcConnection::Failed)
	{
		return;
	}
	bWantOpen = true;
	ReconnectAttempt = 0;
	LastError.Reset();
	if (!IsValidRoomCode(Code))
	{
		Code = GenerateRoomCode();
		HostToken.Reset();
		JoinToken.Reset();
	}
	UE_LOG(LogRoomHost, Display, TEXT("Opening room %s on %s://%s (join base %s, identity %s)"),
		*Code, bSecure ? TEXT("wss") : TEXT("ws"), *PartyHost, *JoinBaseUrl, *Identity);
	Connect();
}

void URoomHostSubsystem::CloseRoom(bool bEndForEveryone)
{
	bWantOpen = false;
	ReconnectAt = -1.0;
	if (Socket.IsValid())
	{
		if (bEndForEveryone && Socket->IsConnected())
		{
			Socket->Send(Msg::CloseRoom());
		}
		TSharedPtr<IWebSocket> Old = MoveTemp(Socket);
		Old->Close(1000, TEXT("host left"));
	}
	bWelcomed = false;
	if (bEndForEveryone)
	{
		Code.Reset();
		HostToken.Reset();
		JoinToken.Reset();
		Save();
	}
	SetConnection(EMcConnection::Idle);
}

void URoomHostSubsystem::Connect()
{
	if (Socket.IsValid())
	{
		TSharedPtr<IWebSocket> Old = MoveTemp(Socket);
		Old->Close(1000, TEXT("reconnecting"));
	}
	bWelcomed = false;
	BinaryAccumulator.Reset();
	const FString Url = FString::Printf(TEXT("%s://%s/parties/main/%s"), bSecure ? TEXT("wss") : TEXT("ws"), *PartyHost, *RoomIdForCode(Code));
	TMap<FString, FString> Headers;
	if (!Origin.IsEmpty())
	{
		// F1: hosting needs an allow-listed Origin. On Windows UE's libwebsockets already sends
		// "Origin: http://<host>", so this second header only helps on other WebSocket backends.
		Headers.Add(TEXT("Origin"), Origin);
	}
	Socket = FWebSocketsModule::Get().CreateWebSocket(Url, TArray<FString>(), Headers);
	SetConnection(ReconnectAttempt > 0 ? EMcConnection::Reconnecting : EMcConnection::Connecting);
	UE_LOG(LogRoomHost, Log, TEXT("Connecting to %s%s"), *Url, Origin.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" with Origin %s"), *Origin));

	// Events from a socket we have since replaced are ignored.
	const TWeakObjectPtr<URoomHostSubsystem> Weak(this);
	const IWebSocket* Raw = Socket.Get();
	auto Live = [Weak, Raw]() -> URoomHostSubsystem*
	{
		URoomHostSubsystem* Self = Weak.Get();
		return Self && Self->Socket.Get() == Raw ? Self : nullptr;
	};
	Socket->OnConnected().AddLambda([Live]()
	{
		if (URoomHostSubsystem* Self = Live())
		{
			UE_LOG(LogRoomHost, Log, TEXT("Socket open; hello as host (%s)"), Self->HostToken.IsEmpty() ? TEXT("new room") : TEXT("reclaiming with hostToken"));
			Self->Socket->Send(Msg::Hello(Self->ClientId, Self->GameId, Self->Identity, Self->HostToken));
		}
	});
	Socket->OnConnectionError().AddLambda([Live](const FString& Error)
	{
		if (URoomHostSubsystem* Self = Live())
		{
			Self->LastError = FString::Printf(TEXT("Cannot reach the room server at %s (%s)"), *Self->PartyHost, *Error);
			Self->ScheduleReconnect(Error);
		}
	});
	Socket->OnClosed().AddLambda([Live](int32 Status, const FString& Reason, bool bWasClean)
	{
		if (URoomHostSubsystem* Self = Live())
		{
			UE_LOG(LogRoomHost, Warning, TEXT("Socket closed %d \"%s\" (clean=%d)"), Status, *Reason, bWasClean ? 1 : 0);
			Self->bWelcomed = false;
			if (IsTerminalCloseCode(Status))
			{
				if (Self->LastError.IsEmpty())
				{
					Self->LastError = FString::Printf(TEXT("Room server closed the connection (%d %s)"), Status, *Reason);
				}
				Self->bWantOpen = false;
				if (Status == 4100)
				{
					// The room is gone: the next OpenRoom mints a new code.
					Self->Code.Reset();
					Self->HostToken.Reset();
					Self->JoinToken.Reset();
					Self->Save();
				}
				Self->SetConnection(EMcConnection::Failed);
				return;
			}
			Self->ScheduleReconnect(FString::Printf(TEXT("closed %d"), Status));
		}
	});
	Socket->OnMessage().AddLambda([Live](const FString& Text)
	{
		if (URoomHostSubsystem* Self = Live())
		{
			Self->HandleText(Text);
		}
	});
	Socket->OnBinaryMessage().AddLambda([Live](const void* Data, SIZE_T Size, bool bIsLastFragment)
	{
		if (URoomHostSubsystem* Self = Live())
		{
			Self->HandleBinary(Data, Size, bIsLastFragment);
		}
	});
	Socket->Connect();
}

void URoomHostSubsystem::ScheduleReconnect(const FString& Why)
{
	bWelcomed = false;
	if (!bWantOpen)
	{
		SetConnection(EMcConnection::Idle);
		return;
	}
	// transportPolicy.ts: 250 ms doubling to 8 s, jittered over the lower half of each slot.
	const double Slot = ReconnectLadderMs[FMath::Min(ReconnectAttempt, static_cast<int32>(UE_ARRAY_COUNT(ReconnectLadderMs)) - 1)];
	const double DelayMs = Slot * 0.5 + FMath::FRand() * Slot * 0.5;
	++ReconnectAttempt;
	ReconnectAt = Now() + DelayMs / 1000.0;
	UE_LOG(LogRoomHost, Warning, TEXT("Room connection lost (%s); retry %d in %.0f ms"), *Why, ReconnectAttempt, DelayMs);
	SetConnection(EMcConnection::Reconnecting);
}

void URoomHostSubsystem::SetConnection(EMcConnection State)
{
	if (Connection != State)
	{
		Connection = State;
		OnConnectionChanged.Broadcast(State);
	}
}

void URoomHostSubsystem::Send(const FString& Json, const TCHAR* Type)
{
	if (Socket.IsValid() && Socket->IsConnected() && bWelcomed)
	{
		Socket->Send(Json);
		return;
	}
	if (Outbox.Num() >= OutboxLimit)
	{
		Outbox.RemoveAt(0);
	}
	Outbox.Add({ Json, Type, Now() });
}

void URoomHostSubsystem::FlushOutbox()
{
	const double T = Now();
	TArray<FQueued> Pending = MoveTemp(Outbox);
	for (const FQueued& Item : Pending)
	{
		if (IsMomentaryType(Item.Type) && T - Item.Time > MomentaryMaxAgeSeconds)
		{
			continue;
		}
		Socket->Send(Item.Json);
	}
}

bool URoomHostSubsystem::IsHost() const
{
	return bWelcomed && !SessionId.IsEmpty() && Room.HostSessionId == SessionId;
}

// ───────────────────────────── inbound ─────────────────────────────

void URoomHostSubsystem::HandleText(const FString& Text)
{
	const TSharedPtr<FJsonObject> Message = ParseObject(Text);
	if (!Message.IsValid())
	{
		return;
	}
	FString Type;
	Message->TryGetStringField(TEXT("t"), Type);
	if (Type == TEXT("ping"))
	{
		if (Socket.IsValid() && Socket->IsConnected())
		{
			Socket->Send(Msg::Pong());
		}
	}
	else if (Type == TEXT("welcome"))
	{
		HandleWelcome(*Message);
	}
	else if (Type == TEXT("state"))
	{
		const TSharedPtr<FJsonObject>* State = nullptr;
		if (Message->TryGetObjectField(TEXT("state"), State))
		{
			ApplyState(**State, false);
		}
	}
	else if (Type == TEXT("game-event"))
	{
		FString Event;
		FString From;
		Message->TryGetStringField(TEXT("event"), Event);
		Message->TryGetStringField(TEXT("fromPlayerId"), From);
		const TSharedPtr<FJsonObject>* Data = nullptr;
		if (Event == TEXT("kart-input") && Message->TryGetObjectField(TEXT("data"), Data))
		{
			HandleKartInput(From, **Data);
		}
	}
	else if (Type == TEXT("error"))
	{
		HandleError(*Message);
	}
	else if (Type == TEXT("host-changed"))
	{
		Message->TryGetStringField(TEXT("hostSessionId"), Room.HostSessionId);
		double Version = 0.0;
		if (Message->TryGetNumberField(TEXT("stateVersion"), Version))
		{
			// sessionStore.ts: host-changed advances the watermark too.
			HighestStateVersion = FMath::Max(HighestStateVersion, static_cast<int64>(Version) - 1);
		}
		FString Reason;
		Message->TryGetStringField(TEXT("reason"), Reason);
		UE_LOG(LogRoomHost, Log, TEXT("host-changed: %s (%s)"), *Room.HostSessionId, *Reason);
	}
	else if (Type == TEXT("signed-scores"))
	{
		UE_LOG(LogRoomHost, Display, TEXT("signed-scores for match %d: %s"), Room.MatchNumber, *Text.Left(600));
		OnSignedScores.Broadcast(Text);
		// The web host never acks (bug B9), so the outbox redelivers up to 4 times. Ack every delivery.
		AckedMatchNumber = Room.MatchNumber;
		Send(Msg::ScoresAck(Room.MatchNumber), TEXT("scores-ack"));
	}
	else if (Type == TEXT("room-closed"))
	{
		FString Reason;
		Message->TryGetStringField(TEXT("reason"), Reason);
		LastError = FString::Printf(TEXT("Room closed: %s"), *Reason);
		UE_LOG(LogRoomHost, Warning, TEXT("%s"), *LastError);
	}
	else if (Type == TEXT("action-ack") || Type == TEXT("resume-countdown"))
	{
		UE_LOG(LogRoomHost, Log, TEXT("%s"), *Text.Left(300));
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
	const TSharedPtr<FJsonObject>* State = nullptr;
	if (Message.TryGetObjectField(TEXT("state"), State))
	{
		ApplyState(**State, true);
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
	LastError = FString::Printf(TEXT("%s: %s"), *ErrorCode, *Text);
	UE_LOG(LogRoomHost, Warning, TEXT("Server error %s%s"), *LastError, bFatal ? TEXT(" (fatal)") : TEXT(""));
	if (ErrorCode == TEXT("UNAUTHORIZED_ORIGIN"))
	{
		UE_LOG(LogRoomHost, Error, TEXT("The room server refused this host's Origin. UE's libwebsockets sends 'Origin: http://<party host name>' "
			"(http://127.0.0.1 for -RoomServer=127.0.0.1:1999). Run the local server with --var EXTRA_ALLOWED_ORIGINS=http://localhost,http://127.0.0.1 "
			"(Tools/net-sim/run-party.ps1 does)."));
	}
	if (ErrorCode == TEXT("PLAYERS_NOT_READY") || ErrorCode == TEXT("NO_ACTIVE_PLAYERS") || ErrorCode == TEXT("INVALID_STATE_TRANSITION") || ErrorCode == TEXT("ENTITLEMENT_REQUIRED"))
	{
		bStartSent = false;
	}
}

void URoomHostSubsystem::ApplyState(const FJsonObject& State, bool bFromWelcome)
{
	FMcRoomState Next;
	if (!ParseRoomState(State, Next))
	{
		return;
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

void URoomHostSubsystem::OnStateApplied(const FMcRoomState& Previous)
{
	// Seats: joins, leaves, reconnects, readiness.
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
	if (Previous.SelectedMapId != Room.SelectedMapId && !Previous.SelectedMapId.IsEmpty())
	{
		UE_LOG(LogRoomHost, Display, TEXT("Map selected: %s"), *Room.SelectedMapId);
	}

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
		for (int32 i = 0; i < 4; ++i)
		{
			ResetSeatInput(i);
		}
		UE_LOG(LogRoomHost, Display, TEXT("Match %d started on %s with %d seat(s)"), Room.MatchNumber, *Room.SelectedMapId, Room.Players.Num());
		if (bSelfTest)
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
	if (Room.Status == EMcRoomStatus::Lobby && Previous.Status != EMcRoomStatus::Lobby && Previous.Status != EMcRoomStatus::Unknown)
	{
		UE_LOG(LogRoomHost, Display, TEXT("Back in the lobby (match %d next)"), Room.MatchNumber);
		bStartSent = false;
		for (int32 i = 0; i < 4; ++i)
		{
			ResetSeatInput(i);
		}
		OnReturnedToLobby.Broadcast();
	}
	if (Room.Status == EMcRoomStatus::Lobby && Previous.Status != EMcRoomStatus::Lobby)
	{
		bStartSent = false;
	}

	UpdateJoinUrl();
	if (bSeatsChanged)
	{
		OnSeatsChanged.Broadcast(Room.Players);
	}
	OnRoomStateChanged.Broadcast();
}

void URoomHostSubsystem::HandleBinary(const void* Data, SIZE_T Size, bool bIsLastFragment)
{
	BinaryAccumulator.Append(static_cast<const uint8*>(Data), static_cast<int32>(Size));
	if (!bIsLastFragment)
	{
		if (BinaryAccumulator.Num() > 256)
		{
			BinaryAccumulator.Reset(); // nothing binary is this large; never buffer junk
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
	In.Steering = SteeringFromGamma(Frame.Gamma, SteeringSensitivity);
	In.LastSequence = Frame.Sequence;
	In.LastSensorTime = Now();
	++In.SensorFrames;
	if (In.LastSensorTime - In.LastLogTime >= 1.0)
	{
		In.LastLogTime = In.LastSensorTime;
		UE_LOG(LogRoomHost, Display, TEXT("Sensor %s: seq=%u gamma=%.2f -> steering %+.3f (%d frames, %d bytes, orientation %u)"),
			*Describe(*Seat), Frame.Sequence, Frame.Gamma, In.Steering, In.SensorFrames, Size, Frame.ScreenOrientation);
	}
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
		UE_LOG(LogRoomHost, Display, TEXT("kart-input %s: gas=%d brake=%d drift=%d%s"), *Describe(*Seat),
			In.bAccelerating ? 1 : 0, In.bReversing ? 1 : 0, In.bDrifting ? 1 : 0,
			Event.bUseItem ? (bItemAccepted ? TEXT(" ITEM") : TEXT(" ITEM (debounced)")) : TEXT(""));
	}
}

// ───────────────────────────── input ─────────────────────────────

bool URoomHostSubsystem::GetSeatInput(int32 Seat, MovecadeKart::FKartInput& Out) const
{
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
	Out.SteeringX = In.Steering;
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
	if (SeatIndex >= 0 && SeatIndex < MaxPlayers)
	{
		FSeatInput& In = Inputs[SeatIndex];
		const double LastLog = In.LastLogTime;
		In = FSeatInput();
		In.LastLogTime = LastLog;
	}
}

// ───────────────────────────── lobby ─────────────────────────────

void URoomHostSubsystem::SelectMap(const FString& MapId)
{
	if (MapId != TEXT("violet-loop") && MapId != TEXT("sunset-harbor"))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("Unknown map %s"), *MapId);
		return;
	}
	if (!bWelcomed)
	{
		PendingMapId = MapId;
		return;
	}
	if (Room.Status == EMcRoomStatus::Lobby && MapId != Room.SelectedMapId)
	{
		Send(Msg::ConfigureRoomMap(MapId), TEXT("configure-room"));
	}
}

bool URoomHostSubsystem::CanStart() const
{
	return GetStartBlockedReason().IsEmpty();
}

FString URoomHostSubsystem::GetStartBlockedReason() const
{
	if (Connection != EMcConnection::Open)
	{
		return LastError.IsEmpty() ? TEXT("Connecting to the room server...") : LastError;
	}
	if (!IsHost())
	{
		return TEXT("Another screen is hosting this room.");
	}
	if (Room.Status != EMcRoomStatus::Lobby)
	{
		return TEXT("A match is in progress.");
	}
	int32 People = 0;
	int32 NotReady = 0;
	for (const FMcSeat& Seat : Room.Players)
	{
		People += Seat.bBot ? 0 : 1;
		NotReady += (Seat.bReady && Seat.bConnected && !Seat.bEliminated) ? 0 : 1;
	}
	if (Room.Players.Num() == 0)
	{
		return TEXT("Scan the code with a phone to join.");
	}
	if (People == 0)
	{
		return TEXT("Bots are ready. Scan the code to play with them.");
	}
	if (!bPreparationReady)
	{
		return TEXT("Preparing Kart Racer...");
	}
	if (NotReady > 0)
	{
		return FString::Printf(TEXT("Waiting for %d %s to get ready."), NotReady, NotReady == 1 ? TEXT("player") : TEXT("players"));
	}
	return FString();
}

bool URoomHostSubsystem::RequestStart()
{
	if (!CanStart() || bStartSent)
	{
		return false;
	}
	StartGameNow();
	return true;
}

void URoomHostSubsystem::StartGameNow()
{
	if (bStartSent)
	{
		return;
	}
	bStartSent = true;
	LobbyCountdownEndsAt = -1.0;
	UE_LOG(LogRoomHost, Display, TEXT("start-game (%d seat(s), map %s)"), Room.Players.Num(), *Room.SelectedMapId);
	Send(Msg::StartGame(), TEXT("start-game"));
}

int32 URoomHostSubsystem::GetLobbyCountdownRemainingMs() const
{
	return LobbyCountdownEndsAt < 0.0 ? -1 : FMath::Max(0, FMath::CeilToInt32((LobbyCountdownEndsAt - Now()) * 1000.0));
}

void URoomHostSubsystem::UpdateLobbyCountdown(double T)
{
	// useReadyCountdown.ts + LobbyScreen.tsx: a lone person never starts on a timer ("Start solo");
	// a second seat runs the 5 s count, restarted by any roster or map change.
	const bool bSolo = Room.Players.Num() == 1 && !Room.Players[0].bBot;
	const bool bActive = !bStartSent && !bSolo && CanStart();
	if (bActive)
	{
		TArray<FString> Ids;
		for (const FMcSeat& Seat : Room.Players)
		{
			Ids.Add(Seat.PlayerId);
		}
		Ids.Sort();
		const FString Key = FString::Printf(TEXT("%s:%s:%s:%s"), *Code, *GameId, *FString::Join(Ids, TEXT(",")), *Room.SelectedMapId);
		if (LobbyCountdownEndsAt < 0.0 || Key != LobbyCountdownKey)
		{
			LobbyCountdownKey = Key;
			LobbyCountdownEndsAt = T + LobbyCountdownMs / 1000.0;
			UE_LOG(LogRoomHost, Display, TEXT("Everyone ready: lobby countdown %d ms"), LobbyCountdownMs);
			Send(Msg::SetCountdown(TEXT("lobby"), LobbyCountdownMs), TEXT("set-countdown"));
		}
		else if (T >= LobbyCountdownEndsAt)
		{
			StartGameNow();
		}
	}
	else if (LobbyCountdownEndsAt >= 0.0)
	{
		LobbyCountdownEndsAt = -1.0;
		LobbyCountdownKey.Reset();
		if (!bStartSent)
		{
			UE_LOG(LogRoomHost, Display, TEXT("Lobby countdown cancelled"));
			Send(Msg::SetCountdown(TEXT("lobby"), 0), TEXT("set-countdown"));
		}
	}
	if (bAutoStart && bSolo && !bStartSent && CanStart())
	{
		if (AutoStartAt < 0.0)
		{
			AutoStartAt = T + AutoStartDelay;
		}
		else if (T >= AutoStartAt)
		{
			UE_LOG(LogRoomHost, Display, TEXT("-RoomAutoStart: starting solo"));
			RequestStart();
		}
	}
	else
	{
		AutoStartAt = -1.0;
	}
}

// ───────────────────────────── race publishing ─────────────────────────────

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

void URoomHostSubsystem::UpdateLiveScores(const TArray<FMcScoreEntry>& Scores)
{
	if (!AreScoresValid(Scores))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("UpdateLiveScores: invalid score list ignored"));
		return;
	}
	const double T = Now();
	const bool bSame = Scores.Num() == LastLiveScores.Num() && Algo::Compare(Scores, LastLiveScores, [](const FMcScoreEntry& A, const FMcScoreEntry& B)
	{
		return A.PlayerId == B.PlayerId && A.Score == B.Score;
	});
	if (bSame || T - LastLiveScoreTime < 1.0)
	{
		return;
	}
	LastLiveScores = Scores;
	LastLiveScoreTime = T;
	Send(Msg::ScoreUpdate(Scores), TEXT("score-update"));
}

void URoomHostSubsystem::SubmitResults(const TArray<FMcRaceResult>& Results)
{
	if (Room.Status != EMcRoomStatus::InProgress && Room.Status != EMcRoomStatus::Paused)
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

void URoomHostSubsystem::SendHaptic(int32 Seat, float Intensity, FName CollisionType, bool bSustained)
{
	const FMcSeat* Player = Seat >= 0 && Seat < MaxPlayers ? Room.FindBySeat(Seat + 1) : nullptr;
	if (!Player || !Player->bConnected || !bWelcomed)
	{
		return;
	}
	const double T = Now();
	HapticTokens = FMath::Min(HapticTokensPerSecond, HapticTokens + (T - HapticRefillTime) * HapticTokensPerSecond);
	HapticRefillTime = T;
	FSeatInput& In = Inputs[Seat];
	if (HapticTokens < 1.0 || (bSustained && T - In.LastHapticTime < HapticMinSustainedInterval))
	{
		return; // the host's control budget is 40 frames/s; haptics never spend more than 15 of it
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

// ───────────────────────────── join URL and QR ─────────────────────────────

void URoomHostSubsystem::UpdateJoinUrl()
{
	if (!IsValidRoomCode(Code))
	{
		return;
	}
	const FString Next = BuildJoinUrl(JoinBaseUrl, Code, JoinToken);
	if (Next == JoinUrl)
	{
		return;
	}
	JoinUrl = Next;
	UE_LOG(LogRoomHost, Display, TEXT("JoinCode=%s JoinUrl=%s"), *Code, *JoinUrl);

	const MovecadeKart::Qr::FQrMatrix Matrix = MovecadeKart::Qr::EncodeText(JoinUrl);
	if (!Matrix.IsValid())
	{
		UE_LOG(LogRoomHost, Error, TEXT("Join URL too long for a version 1-10 QR code (%d chars)"), JoinUrl.Len());
		return;
	}
	constexpr int32 Quiet = 4;
	constexpr int32 Scale = 8;
	const int32 Modules = Matrix.Size + Quiet * 2;
	const int32 Pixels = Modules * Scale;
	TArray64<uint8> Image;
	Image.SetNumUninitialized(int64(Pixels) * Pixels * 4);
	for (int32 Y = 0; Y < Pixels; ++Y)
	{
		const int32 My = Y / Scale - Quiet;
		for (int32 X = 0; X < Pixels; ++X)
		{
			const int32 Mx = X / Scale - Quiet;
			const bool bDark = Mx >= 0 && My >= 0 && Mx < Matrix.Size && My < Matrix.Size && Matrix.Get(Mx, My);
			const uint8 V = bDark ? 0 : 255;
			uint8* P = &Image[(int64(Y) * Pixels + X) * 4];
			P[0] = V;
			P[1] = V;
			P[2] = V;
			P[3] = 255;
		}
	}
	QrTexture = UTexture2D::CreateTransient(Pixels, Pixels, PF_B8G8R8A8, TEXT("RoomJoinQr"), Image);
	if (QrTexture)
	{
		QrTexture->Filter = TF_Nearest;
		QrTexture->LODGroup = TEXTUREGROUP_UI;
		QrTexture->NeverStream = true;
		QrTexture->UpdateResource();
	}
	UE_LOG(LogRoomHost, Log, TEXT("QR: version %d, mask %d, %dx%d modules, %d px"), Matrix.Version, Matrix.Mask, Matrix.Size, Matrix.Size, Pixels);
}

// ───────────────────────────── tick and persistence ─────────────────────────────

void URoomHostSubsystem::Tick(float DeltaTime)
{
	const double T = Now();
	if (ReconnectAt > 0.0 && T >= ReconnectAt && bWantOpen)
	{
		ReconnectAt = -1.0;
		Connect();
	}
	if (Connection == EMcConnection::Open)
	{
		UpdateLobbyCountdown(T);
		if (T - LastSaveTime >= SaveIntervalSeconds)
		{
			Save();
		}
	}
	if (bSelfTest && SelfTestStep >= 0)
	{
		RunSelfTest(T);
	}
	if (ShotCount < ShotTimes.Num() && T - StartTime >= ShotTimes[ShotCount])
	{
		++ShotCount;
		const FString Path = FPaths::Combine(FPaths::ScreenShotDir(), FString::Printf(TEXT("RoomHost_%d.png"), ShotCount));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
		UE_LOG(LogRoomHost, Display, TEXT("Screenshot with UI requested: %s"), *FPaths::ConvertRelativePathToFull(Path));
	}
}

void URoomHostSubsystem::RunSelfTest(double T)
{
	const double Elapsed = T - SelfTestStart;
	const FMcSeat* First = Room.Players.Num() > 0 ? &Room.Players[0] : nullptr;
	switch (SelfTestStep)
	{
	case 0: // the web host's intro starts 1 s after the karts exist
		if (Elapsed >= 1.0)
		{
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: intro"));
			PublishRacePhase(EMcRacePhase::Intro);
			++SelfTestStep;
		}
		break;
	case 1:
		if (Elapsed >= 2.0)
		{
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: countdown (set-countdown match 5000)"));
			PublishRacePhase(EMcRacePhase::Countdown);
			++SelfTestStep;
		}
		break;
	case 2:
		if (Elapsed >= 7.0)
		{
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: GO (set-countdown match 0)"));
			PublishRacePhase(EMcRacePhase::Racing);
			SelfTestRaceStart = T;
			SelfTestLastPulse = T;
			++SelfTestStep;
		}
		break;
	case 3:
		if (First && T - SelfTestLastPulse >= 1.0)
		{
			SelfTestLastPulse = T;
			SendHaptic(First->SeatIndex(), 0.5f, TEXT("barrier"), false);
			UpdateLiveScores({ { First->PlayerId, FMath::Clamp(static_cast<int32>((T - SelfTestRaceStart) * 7.0), 0, 99) } });
			UE_LOG(LogRoomHost, Display, TEXT("SelfTest: haptic + live score for %s"), *First->PlayerId);
		}
		if (First && T - SelfTestRaceStart >= 12.0)
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
	case 4:
		if (Room.Status == EMcRoomStatus::Complete && AckedMatchNumber == Room.MatchNumber && Elapsed >= 22.0)
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

FString URoomHostSubsystem::SavePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoomHost"), TEXT("host.json"));
}

void URoomHostSubsystem::LoadSaved()
{
	FString Text;
	TSharedPtr<FJsonObject> Saved;
	if (FFileHelper::LoadFileToString(Text, *SavePath()))
	{
		Saved = ParseObject(Text);
	}
	if (Saved.IsValid())
	{
		Saved->TryGetStringField(TEXT("identity"), Identity);
	}
	if (!IsV4Uuid(Identity))
	{
		Identity = MakeIdentity();
	}
	Identity = Identity.ToLower();
	if (!Saved.IsValid() || FParse::Param(FCommandLine::Get(), TEXT("RoomNew")))
	{
		return;
	}
	FString SavedCode;
	FString SavedToken;
	double SavedAt = 0.0;
	Saved->TryGetStringField(TEXT("code"), SavedCode);
	Saved->TryGetStringField(TEXT("hostToken"), SavedToken);
	Saved->TryGetNumberField(TEXT("savedAt"), SavedAt);
	if (IsValidRoomCode(SavedCode) && UnixMs() - static_cast<int64>(SavedAt) < SavedRoomMaxAgeMs)
	{
		// HostRoute.tsx pins the code so a reload reconnects to the same room; the hostToken reclaims it instantly.
		Code = SavedCode;
		HostToken = SavedToken;
		UE_LOG(LogRoomHost, Display, TEXT("Reclaiming room %s saved %.0f s ago"), *Code, (UnixMs() - SavedAt) / 1000.0);
	}
}

void URoomHostSubsystem::Save()
{
	LastSaveTime = Now();
	const FString Json = FString::Printf(TEXT("{\n  \"identity\": \"%s\",\n  \"code\": \"%s\",\n  \"hostToken\": \"%s\",\n  \"savedAt\": %lld\n}\n"),
		*Identity, *Code, *HostToken, UnixMs());
	FFileHelper::SaveStringToFile(Json, *SavePath());
}
