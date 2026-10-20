#pragma once

#include "CoreMinimal.h"
#include "Net/RoomTypes.h"
#include "Sim/KartSim.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RoomHostSubsystem.generated.h"

class IWebSocket;
class FJsonObject;
class UTexture2D;

DECLARE_LOG_CATEGORY_EXTERN(LogRoomHost, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcSeatsChanged, const TArray<FMcSeat>&, Seats);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcStartRequested, const FString&, MapId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMcPauseChanged, bool, bPaused, const FString&, Reason, const FString&, PlayerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMcRoomEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcConnectionChanged, EMcConnection, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcSignedScores, const FString&, SignedScoresJson);

/**
 * Makes this game the Movecade room host (Docs/Port/04 §1-§7, Docs/Port/WORKSTREAMS.md "Room host API").
 *
 * It holds one WebSocket to the Movecade room server, mints the room code, says hello as `role:'host'`, and
 * keeps the room state, the seats and every phone's input. By default that is production
 * (party.movecade.com), so phones join with the existing controller: movecade.com/join or the Movecade app.
 * Production admits this host by its Origin (Docs/Port/server); -RoomLocal uses `npm run dev:party` instead.
 * Phones join by scanning the QR (JoinUrl). Their 30 Hz sensor frames and `kart-input` events become a
 * per-seat MovecadeKart::FKartInput that the race layer reads with GetSeatInput().
 *
 * Settings come from [/Script/MovecadeKart.RoomHostSubsystem] in DefaultGame.ini, then the command
 * line: -RoomLocal (127.0.0.1:1999 and http://localhost:5173), -RoomServer=host[:port] -RoomSecure/-RoomInsecure
 * (a local server defaults to plain ws, anything else to wss) -RoomJoinBase=<url> -RoomOrigin=...
 * -RoomNew (ignore the saved room) -RoomMap=sunset-harbor -RoomAutoStart[=Seconds] -RoomShots=6,20 (UI screenshots)
 * -RoomSelfTest (plays the race layer's part after each start: phases, haptics, live scores, results, restart).
 * The room opens on OpenRoom(), or at startup with -KartLobby, -KartPhoneSeat=N or -Room.
 *
 * Everything runs on the game thread (UE fires WebSocket events there).
 */
UCLASS(Config = Game)
class MOVECADEKART_API URoomHostSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// Settings
	/**
	 * Party host and port: production by default. For a local server use 127.0.0.1 rather than localhost:
	 * UE's libwebsockets tries ::1 for "localhost" and `partykit dev` listens on IPv4 only.
	 */
	UPROPERTY(Config)
	FString PartyHost = TEXT("party.movecade.com");

	UPROPERTY(Config)
	bool bSecure = true;

	/** The web app phones open; it must talk to the same party server. Production: movecade.com. */
	UPROPERTY(Config)
	FString JoinBaseUrl = TEXT("https://movecade.com");

	/** True for a room server on this machine (`npm run dev:party`), which speaks plain ws. */
	static bool IsLocalPartyHost(const FString& Host);

	/**
	 * Extra Origin upgrade header. Leave it empty on Windows: UE's libwebsockets backend already sends
	 * `Origin: http://<party host name>` and a second Origin header makes the server refuse the socket.
	 */
	UPROPERTY(Config)
	FString Origin;

	UPROPERTY(Config)
	FString ClientId = TEXT("ue5-host/0.1.0");

	UPROPERTY(Config)
	FString GameId = TEXT("kart");

	/** Phone steering sensitivity (kartRaceStore default 1.0). */
	UPROPERTY(Config)
	float SteeringSensitivity = 1.f;

	// Lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Connects and claims a room (reusing the saved code if the room can still be alive). Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void OpenRoom();

	/** Leaves the room. bEndForEveryone sends close-room first. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void CloseRoom(bool bEndForEveryone);

	// State
	UFUNCTION(BlueprintPure, Category = "Room")
	FString GetJoinCode() const { return Code; }

	/** `<JoinBaseUrl>/join/CODE#v=2&t=<joinToken>`, the QR payload. Empty until the server welcomed us. */
	UFUNCTION(BlueprintPure, Category = "Room")
	FString GetJoinUrl() const { return JoinUrl; }

	/** The join URL as a QR code (nearest-filtered, with the 4-module quiet zone), or null before welcome. */
	UFUNCTION(BlueprintPure, Category = "Room")
	UTexture2D* GetJoinQrTexture() const { return QrTexture; }

	UFUNCTION(BlueprintPure, Category = "Room")
	EMcConnection GetConnectionState() const { return Connection; }

	UFUNCTION(BlueprintPure, Category = "Room")
	FString GetLastError() const { return LastError; }

	/** True when the server made this session the room's host (not a standby). */
	UFUNCTION(BlueprintPure, Category = "Room")
	bool IsHost() const;

	UFUNCTION(BlueprintPure, Category = "Room")
	TArray<FMcSeat> GetSeats() const { return Room.Players; }

	const FMcRoomState& GetRoom() const { return Room; }

	// Lobby
	/** configure-room {mapId}; lobby only. The server answers with the new selectedMapId. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SelectMap(const FString& MapId);

	/** HostSurface.tsx start conditions: open, host, lobby, a person seated, every seat ready and connected. */
	UFUNCTION(BlueprintPure, Category = "Room")
	bool CanStart() const;

	/** "Scan the code with a phone to join." etc. (HostSurface.tsx:337-348); empty when CanStart(). */
	UFUNCTION(BlueprintPure, Category = "Room")
	FString GetStartBlockedReason() const;

	/** Start / Enter. Starts now if allowed (also skips a running lobby count). Returns false when blocked. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	bool RequestStart();

	/** The local 5 s lobby count (useReadyCountdown), or -1 when none is running. */
	UFUNCTION(BlueprintPure, Category = "Room")
	int32 GetLobbyCountdownRemainingMs() const;

	/** The race layer clears this while the track loads; start waits for it (web `preparation.ready`). */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SetPreparationReady(bool bReady) { bPreparationReady = bReady; }

	// Input (Docs/Port/WORKSTREAMS.md "Seat input")
	/**
	 * The latest input for seat 0..3: steering from the phone's wheel angle (sample-and-hold) or a
	 * bot's steeringInput, pedals from kart-input. False when nobody (non-eliminated) holds the seat.
	 */
	bool GetSeatInput(int32 Seat, MovecadeKart::FKartInput& Out) const;

	/** True once per ITEM press on seat 0..3 (200 ms debounce per seat). */
	UFUNCTION(BlueprintCallable, Category = "Room")
	bool ConsumeUseItem(int32 Seat);

	// Outbound: race publishing (the web host's legacyBridge.ts / useKartRaceNetworkSync.ts)
	/** Intro: nothing extra on the wire. Countdown: set-countdown match 5000. Racing: match 0 (GO). Finished: nothing. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void PublishRacePhase(EMcRacePhase Phase);

	UFUNCTION(BlueprintCallable, Category = "Room")
	void PublishMatchCountdown(int32 DurationMs);

	/** Live points (laps*100 + ..., always < 10000). Sent at most once a second and only when changed. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void UpdateLiveScores(const TArray<FMcScoreEntry>& Scores);

	/**
	 * Ends the match: score-update then complete-game with finishers only, ascending whole-ms times,
	 * each in [10000, 600000]. DNFs and 0 ms are never reported. The server then signs and sends
	 * signed-scores (OnSignedScores), which this acks with scores-ack.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SubmitResults(const TArray<FMcRaceResult>& Results);

	/** Phone haptic for seat 0..3 (game-event `collision`), throttled to stay inside the 40/s control budget. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SendHaptic(int32 Seat, float Intensity, FName CollisionType = TEXT("barrier"), bool bSustained = false);

	/** restart-game: the room goes back to the lobby, everyone re-readies (engine.ts:2053-2094). */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void RestartGame();

	UFUNCTION(BlueprintCallable, Category = "Room")
	void ReturnToLobby();

	UFUNCTION(BlueprintCallable, Category = "Room")
	void KickPlayer(const FString& PlayerId);

	/** "Continue without {name}": only for a disconnected player. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void DropPlayer(const FString& PlayerId);

	// Events
	UPROPERTY(BlueprintAssignable, Category = "Room")
	FMcSeatsChanged OnSeatsChanged;

	/** The server accepted start-game: the match is in progress on MapId. Build the race now. */
	UPROPERTY(BlueprintAssignable, Category = "Room")
	FMcStartRequested OnStartRequested;

	UPROPERTY(BlueprintAssignable, Category = "Room")
	FMcPauseChanged OnPauseChanged;

	/** The room went back to the lobby (restart, return-to-lobby, or everyone left). */
	UPROPERTY(BlueprintAssignable, Category = "Room")
	FMcRoomEvent OnReturnedToLobby;

	/** Any applied state frame. */
	UPROPERTY(BlueprintAssignable, Category = "Room")
	FMcRoomEvent OnRoomStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Room")
	FMcConnectionChanged OnConnectionChanged;

	/** The verbatim signed-scores frame, for leaderboard submission (POST /api/scores). */
	UPROPERTY(BlueprintAssignable, Category = "Room")
	FMcSignedScores OnSignedScores;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bInitialized; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }

private:
	struct FSeatInput
	{
		double Steering = 0.0;
		bool bAccelerating = false;
		bool bReversing = false;
		bool bDrifting = false;
		bool bItemPending = false;
		double LastItemTime = -1000.0;
		/** When the last tilt frame arrived: steering from tilt goes neutral once frames stop (a locked phone). */
		double LastSensorTime = -1000.0;
		double LastLogTime = -1000.0;
		int32 SensorFrames = 0;
		/** The last tilt frame's sequence number, to drop frames that arrive out of order. */
		uint8 LastSequence = 0;
		double LastHapticTime = -1000.0;
	};

	struct FQueued
	{
		FString Json;
		FString Type;
		double Time = 0.0;
	};

	void Connect();
	void ScheduleReconnect(const FString& Why);
	void SetConnection(EMcConnection State);
	void Send(const FString& Json, const TCHAR* Type);
	void FlushOutbox();

	void HandleText(const FString& Text);
	void HandleBinary(const void* Data, SIZE_T Size, bool bIsLastFragment);
	void HandleSensorFrame(const uint8* Data, int32 Size);
	void HandleKartInput(const FString& FromPlayerId, const FJsonObject& Data);
	void HandleWelcome(const FJsonObject& Message);
	void HandleError(const FJsonObject& Message);
	void ApplyState(const FJsonObject& State, bool bFromWelcome);
	void OnStateApplied(const FMcRoomState& Previous);

	void UpdateJoinUrl();
	void UpdateLobbyCountdown(double Now);
	void StartGameNow();
	void ResetSeatInput(int32 SeatIndex);

	void LoadSaved();
	void Save();
	FString SavePath() const;

	double Now() const;

	TSharedPtr<IWebSocket> Socket;
	EMcConnection Connection = EMcConnection::Idle;
	bool bInitialized = false;
	bool bWantOpen = false;
	bool bWelcomed = false;
	int32 ReconnectAttempt = 0;
	double ReconnectAt = -1.0;
	double LastSaveTime = -1000.0;

	FString Code;
	FString Identity;
	FString HostToken;
	FString JoinToken;
	FString SessionId;
	FString JoinUrl;
	FString LastError;
	int64 SavedAtUnixMs = 0;
	int64 HighestStateVersion = -1;

	FMcRoomState Room;
	/** One per seat; equal to MovecadeKart::Net::MaxPlayers (checked in the .cpp). */
	static constexpr int32 SeatCount = 4;
	FSeatInput Inputs[SeatCount];
	TArray<uint8> BinaryAccumulator;
	TArray<FQueued> Outbox;

	UPROPERTY()
	TObjectPtr<UTexture2D> QrTexture;

	// Lobby
	bool bPreparationReady = true;
	bool bAutoStart = false;
	double AutoStartDelay = 0.0;
	double AutoStartAt = -1.0;
	FString PendingMapId;
	FString LobbyCountdownKey;
	double LobbyCountdownEndsAt = -1.0;
	bool bStartSent = false;
	int32 AdvancedMatchNumber = -1;
	int32 StartedMatchNumber = -1;
	int32 AckedMatchNumber = -1;

	// Race publishing
	TArray<FMcScoreEntry> LastLiveScores;
	double LastLiveScoreTime = -1000.0;
	int32 SubmittedMatchNumber = -1;
	double HapticTokens = 15.0;
	double HapticRefillTime = 0.0;
	int32 ActionCounter = 0;

	// -RoomSelfTest: stands in for the race layer after each match start (phases, haptics, scores, results, restart).
	void RunSelfTest(double T);
	bool bSelfTest = false;
	int32 SelfTestStep = -1;
	double SelfTestStart = 0.0;
	double SelfTestRaceStart = 0.0;
	double SelfTestLastPulse = 0.0;

	// -RoomShots
	TArray<double> ShotTimes;
	int32 ShotCount = 0;
	double StartTime = 0.0;
};
