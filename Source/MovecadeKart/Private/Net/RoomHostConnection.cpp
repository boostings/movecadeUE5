// URoomHostSubsystem: the socket to the room server, reconnects (transportPolicy.ts) and the offline outbox.

#include "Net/RoomHostSubsystem.h"

#include "IWebSocket.h"
#include "Net/RoomHostInternal.h"
#include "Net/RoomProtocol.h"
#include "WebSocketsModule.h"

using namespace MovecadeKart::Net;

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
		CloseSocket(TEXT("host left"));
	}
	bWelcomed = false;
	if (bEndForEveryone)
	{
		ForgetRoom();
	}
	SetConnection(EMcConnection::Idle);
}

void URoomHostSubsystem::ForgetRoom()
{
	Code.Reset();
	HostToken.Reset();
	JoinToken.Reset();
	ResetRoomState();
	Save();
}

void URoomHostSubsystem::CloseSocket(const TCHAR* Reason)
{
	// Moved out first, so the socket's own late events fail BindSocketEvents' "still our socket" guard.
	if (const TSharedPtr<IWebSocket> Old = MoveTemp(Socket))
	{
		Old->Close(RoomHost::CloseNormal, Reason);
	}
}

void URoomHostSubsystem::Connect()
{
	CloseSocket(TEXT("reconnecting"));
	bWelcomed = false;
	bReportedStateIssues = false;
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
	BindSocketEvents();
	Socket->Connect();
}

void URoomHostSubsystem::BindSocketEvents()
{
	// Events from a socket we have since replaced (or from after this subsystem died) are ignored.
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
			Self->MarkLobbyChanged();
			Self->ScheduleReconnect(Error);
		}
	});
	Socket->OnClosed().AddLambda([Live](int32 Status, const FString& Reason, bool bWasClean)
	{
		if (URoomHostSubsystem* Self = Live())
		{
			Self->HandleSocketClosed(Status, Reason, bWasClean);
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
}

void URoomHostSubsystem::HandleSocketClosed(int32 Status, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogRoomHost, Warning, TEXT("Socket closed %d \"%s\" (clean=%d)"), Status, *Reason, bWasClean ? 1 : 0);
	bWelcomed = false;
	if (!IsTerminalCloseCode(Status))
	{
		ScheduleReconnect(FString::Printf(TEXT("closed %d"), Status));
		return;
	}
	if (LastError.IsEmpty())
	{
		LastError = FString::Printf(TEXT("Room server closed the connection (%d %s)"), Status, *SanitizeServerText(Reason));
	}
	MarkLobbyChanged();
	bWantOpen = false;
	if (Status == RoomHost::CloseRoomGone)
	{
		ForgetRoom();
	}
	SetConnection(EMcConnection::Failed);
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
	constexpr int32 LastSlot = UE_ARRAY_COUNT(RoomHost::ReconnectLadderMs) - 1;
	const double Slot = RoomHost::ReconnectLadderMs[FMath::Min(ReconnectAttempt, LastSlot)];
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
		MarkLobbyChanged();
		OnConnectionChanged.Broadcast(State);
	}
}

void URoomHostSubsystem::Send(const FString& Json, const TCHAR* Type)
{
	check(Type);
	if (Socket.IsValid() && Socket->IsConnected() && bWelcomed)
	{
		Socket->Send(Json);
		return;
	}
	if (Outbox.Num() >= RoomHost::OutboxLimit)
	{
		Outbox.RemoveAt(0);
	}
	Outbox.Add({ Json, Type, Now() });
}

void URoomHostSubsystem::FlushOutbox()
{
	const double T = Now();
	const TArray<FQueuedFrame> Pending = MoveTemp(Outbox);
	Outbox.Reset();
	for (const FQueuedFrame& Item : Pending)
	{
		if (IsMomentaryType(Item.Type) && T - Item.Time > RoomHost::MomentaryMaxAgeSeconds)
		{
			continue;
		}
		Socket->Send(Item.Json);
	}
}
