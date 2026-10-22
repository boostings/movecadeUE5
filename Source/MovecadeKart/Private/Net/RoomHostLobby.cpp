// URoomHostSubsystem: the lobby's rules (HostSurface.tsx start conditions, useReadyCountdown.ts), the room's game
// and map selection, and the join URL with its QR texture.

#include "Net/RoomHostSubsystem.h"

#include "Engine/Texture2D.h"
#include "Games/ArcadeCatalog.h"
#include "Net/QrCode.h"
#include "Net/RoomHostInternal.h"
#include "Net/RoomProtocol.h"

using namespace MovecadeKart::Net;

namespace
{
	/** The kart tracks the server accepts in configure-room {mapId} (maps.ts). */
	bool IsKnownMap(const FString& MapId)
	{
		return MapId == TEXT("violet-loop") || MapId == TEXT("sunset-harbor");
	}

	bool IsKart(const FString& InGameId)
	{
		return InGameId.Equals(TEXT("kart"), ESearchCase::CaseSensitive);
	}
}

// ───────────────────────────── selection ─────────────────────────────

void URoomHostSubsystem::SelectMap(const FString& MapId)
{
	if (!IsKnownMap(MapId))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("Unknown map %s"), *MapId);
		return;
	}
	if (!bWelcomed)
	{
		PendingMapId = MapId;
		return;
	}
	// Maps are Kart's: the server refuses one for any other game (MALFORMED_MESSAGE "Unknown mapId for game").
	if (!IsKart(GameId))
	{
		return;
	}
	if (Room.Status == EMcRoomStatus::Lobby && MapId != Room.SelectedMapId)
	{
		Send(Msg::ConfigureRoomMap(MapId), TEXT("configure-room"));
	}
}

bool URoomHostSubsystem::IsPlayableGame(const FString& InGameId)
{
	return IsKart(InGameId) || MovecadeKart::Arcade::Find(InGameId) != nullptr;
}

void URoomHostSubsystem::SelectGame(const FString& InGameId)
{
	if (!IsPlayableGame(InGameId))
	{
		UE_LOG(LogRoomHost, Warning, TEXT("Unknown or unported game %s"), *InGameId);
		return;
	}
	if (!bWelcomed)
	{
		GameId = InGameId;
		PendingGameId = InGameId;
		MarkLobbyChanged();
		return;
	}
	if (Room.Status != EMcRoomStatus::Lobby)
	{
		UE_LOG(LogRoomHost, Warning, TEXT("SelectGame %s ignored: the room is not in its lobby"), *InGameId);
		return;
	}
	GameId = InGameId;
	GameAskedAt = Now();
	RefreshRosterKey();
	MarkLobbyChanged();
	if (InGameId != Room.GameId)
	{
		UE_LOG(LogRoomHost, Display, TEXT("Room game -> %s"), *InGameId);
		Send(Msg::ConfigureRoomGame(InGameId), TEXT("configure-room"));
	}
}

bool URoomHostSubsystem::IsMatchRunning(const FString& InGameId) const
{
	return (Room.Status == EMcRoomStatus::InProgress || Room.Status == EMcRoomStatus::Paused) && Room.GameId == InGameId;
}

void URoomHostSubsystem::SetPreparationReady(bool bReady)
{
	if (bPreparationReady != bReady)
	{
		bPreparationReady = bReady;
		MarkLobbyChanged();
	}
}

// ───────────────────────────── starting ─────────────────────────────

FStartCheck URoomHostSubsystem::EvaluateStartCheck() const
{
	return EvaluateStart(Room, Connection == EMcConnection::Open, IsHost(), bPreparationReady);
}

bool URoomHostSubsystem::CanStart() const
{
	return EvaluateStartCheck().CanStart();
}

FString URoomHostSubsystem::GetStartBlockedReason() const
{
	return DescribeStartBlock(EvaluateStartCheck(), LastError);
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
	StartSentAt = Now();
	Send(Msg::StartGame(), TEXT("start-game"));
}

void URoomHostSubsystem::ResetRoomState()
{
	HighestStateVersion = -1;
	Room = FMcRoomState();
	ResetAllSeatInputs();
	LastLiveScores.Reset();
	RosterKey.Reset();
	LobbyCountdownKey.Reset();
	LobbyCountdownEndsAt = -1.0;
	bStartSent = false;
	StartSentAt = -1.0;
	AdvancedMatchNumber = -1;
	StartedMatchNumber = -1;
	AckedMatchNumber = -1;
	SubmittedMatchNumber = -1;
	AutoStartAt = -1.0;
	MarkLobbyChanged();
}

// ───────────────────────────── the lobby countdown ─────────────────────────────

int32 URoomHostSubsystem::GetLobbyCountdownRemainingMs() const
{
	return LobbyCountdownEndsAt < 0.0 ? -1 : FMath::Max(0, FMath::CeilToInt32((LobbyCountdownEndsAt - Now()) * 1000.0));
}

void URoomHostSubsystem::RefreshRosterKey()
{
	// useReadyCountdown.ts restarts the count on any roster, game or map change.
	TArray<FString, TInlineAllocator<MaxPlayers>> Ids;
	for (const FMcSeat& Seat : Room.Players)
	{
		Ids.Add(Seat.PlayerId);
	}
	Ids.Sort();
	RosterKey = FString::Printf(TEXT("%s:%s:%s:%s"), *Code, *GameId, *FString::Join(Ids, TEXT(",")), *Room.SelectedMapId);
}

void URoomHostSubsystem::UpdateLobbyCountdown(double T)
{
	if (bStartSent && Room.Status == EMcRoomStatus::Lobby && StartSentAt >= 0.0 && T - StartSentAt > RoomHost::StartAnswerTimeoutSeconds)
	{
		UE_LOG(LogRoomHost, Warning, TEXT("start-game was never answered; the lobby may start again"));
		bStartSent = false;
	}
	// useReadyCountdown.ts + LobbyScreen.tsx: a lone person never starts on a timer ("Start solo");
	// a second seat runs the 5 s count, restarted by any roster or map change.
	const bool bSolo = Room.Players.Num() == 1 && !Room.Players[0].bBot;
	const bool bCanStart = CanStart();
	if (!bStartSent && !bSolo && bCanStart)
	{
		if (LobbyCountdownEndsAt < 0.0 || RosterKey != LobbyCountdownKey)
		{
			LobbyCountdownKey = RosterKey;
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
	UpdateAutoStart(T, bSolo && !bStartSent && bCanStart);
}

void URoomHostSubsystem::UpdateAutoStart(double T, bool bSoloReady)
{
	if (!Debug.bAutoStart || !bSoloReady)
	{
		AutoStartAt = -1.0;
		return;
	}
	if (AutoStartAt < 0.0)
	{
		AutoStartAt = T + Debug.AutoStartDelay;
	}
	else if (T >= AutoStartAt)
	{
		UE_LOG(LogRoomHost, Display, TEXT("-RoomAutoStart: starting solo"));
		RequestStart();
	}
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
	int32 Pixels = 0;
	TArray64<uint8> Image;
	MovecadeKart::Qr::RasterizeBgra(Matrix, RoomHost::QrQuietModules, RoomHost::QrPixelsPerModule, Image, Pixels);
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
