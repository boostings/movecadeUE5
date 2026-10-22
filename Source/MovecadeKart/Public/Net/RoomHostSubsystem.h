#pragma once

#include "CoreMinimal.h"
#include "Net/RoomProtocol.h"
#include "Net/RoomTypes.h"
#include "Sim/KartSim.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "RoomHostSubsystem.generated.h"

class IWebSocket;
class FJsonObject;
class UTexture2D;
namespace UE::Tasks { class FPipe; }

DECLARE_LOG_CATEGORY_EXTERN(LogRoomHost, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcSeatsChanged, const TArray<FMcSeat>&, Seats);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcStartRequested, const FString&, MapId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMcPauseChanged, bool, bPaused, const FString&, Reason, const FString&, PlayerId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMcRoomEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcConnectionChanged, EMcConnection, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMcSignedScores, const FString&, SignedScoresJson);
/** A phone's game-event other than kart-input and practice:action: (from player id, event, data or null). */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FMcGameEvent, const FString&, const FString&, const TSharedPtr<FJsonObject>&);
/** Every accepted sensor frame: (seat index 0..3, frame). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMcSensorFrameEvent, int32, const MovecadeKart::Net::FSensorFrame&);

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
 * -RoomNew (ignore the saved room) -RoomMap=sunset-harbor -RoomGame=bowling. Shipping ignores the server settings in
 * both places and always uses production over wss (F-09; Net::ResolveServerSettings). Test hooks (not in Shipping):
 * -RoomAutoStart[=Seconds], -RoomShots=6,20 (UI screenshots), -RoomSelfTest (plays the race layer's part after
 * each start: phases, haptics, live scores, results, restart), -RoomLogJoinUrl (the join URL with its token in the log,
 * which is otherwise redacted, for the phone simulator).
 * The room opens on OpenRoom(), or at startup with -KartLobby, -KartPhoneSeat=N or -RoomOpen.
 *
 * Everything runs on the game thread (UE fires WebSocket events there); only the saved-room file write is async.
 * The implementation is split by job: RoomHostSubsystem.cpp (settings, lifecycle, tick), RoomHostConnection.cpp
 * (socket, reconnect, outbox), RoomHostInbound.cpp (server frames, state, seat input), RoomHostLobby.cpp (start
 * rules, lobby countdown, join URL), RoomHostPublishing.cpp (match frames to the room), RoomHostPersistence.cpp
 * (the saved room) and RoomHostSelfTest.cpp.
 */
UCLASS(Config = Game)
class MOVECADEKART_API URoomHostSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	URoomHostSubsystem();
	virtual ~URoomHostSubsystem() override;

	// Settings
	/**
	 * Party host and port: production by default. For a local server use 127.0.0.1 rather than localhost:
	 * UE's libwebsockets tries ::1 for "localhost" and `partykit dev` listens on IPv4 only.
	 */
	UPROPERTY(Config)
	FString PartyHost = MovecadeKart::Net::ProductionPartyHost;

	UPROPERTY(Config)
	bool bSecure = true;

	/** The web app phones open; it must talk to the same party server. Production: movecade.com. */
	UPROPERTY(Config)
	FString JoinBaseUrl = MovecadeKart::Net::ProductionJoinBaseUrl;

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

	/**
	 * Bumped whenever something the lobby shows changes: an applied state, the connection or its error, the
	 * selected game, preparation. A lobby repaints its text only when this moves (the countdown it polls).
	 */
	uint32 GetLobbyRevision() const { return LobbyRevision; }

	// Lobby
	/** configure-room {mapId}; lobby only. The server answers with the new selectedMapId. */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SelectMap(const FString& MapId);

	/**
	 * configure-room {gameId}; lobby only. The game this host runs: "kart" or a party game that has been ported
	 * (IsPlayableGame). It is also the gameId of every hello, so a reconnecting lobby keeps it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SelectGame(const FString& InGameId);

	/** The game this host runs (GameId), which the room adopts in the lobby. */
	UFUNCTION(BlueprintPure, Category = "Room")
	FString GetGameId() const { return GameId; }

	static bool IsPlayableGame(const FString& InGameId);

	/** True while the room plays a match of InGameId (in progress or paused). */
	bool IsMatchRunning(const FString& InGameId) const;

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
	void SetPreparationReady(bool bReady);

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

	/**
	 * Live points (laps*100 + ..., always < 10000). Sent at most once a second and only when changed. Returns true
	 * when the room has this list (sent now, or the same as the last one sent), false when it was refused or throttled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	bool UpdateLiveScores(const TArray<FMcScoreEntry>& Scores);

	/**
	 * Ends the match: score-update then complete-game with finishers only, ascending whole-ms times,
	 * each in [10000, 600000]. DNFs and 0 ms are never reported. The server then signs and sends
	 * signed-scores (OnSignedScores), which this acks with scores-ack.
	 */
	UFUNCTION(BlueprintCallable, Category = "Room")
	void SubmitResults(const TArray<FMcRaceResult>& Results);

	/** Ends a party game's match: score-update then complete-game with every player's score (once per match). */
	void CompleteGame(const TArray<FMcScoreEntry>& Scores);

	/** advance-turn: the bowler up (the server drops other phones' frames), or empty for everyone (null). */
	void AdvanceTurn(const FString& ActivePlayerId);

	/** A host game-event to every phone; DataJson is a JSON value (Net::QuoteJson for strings). */
	void SendGameEvent(const FString& Event, const FString& DataJson);

	/**
	 * The motion pad's prompt (Docs/Agents/BRIEF.md "The motion pad contract"): what PlayerId's phone shows, big.
	 * A / B are the button labels (empty hides that button), AccentHex "#rrggbb", Mode tilt | swing | aim | flick
	 * (the phone's hint animation). PlayerId Net::PadPromptEveryone ("*") addresses every phone. Send it after the
	 * turn or phase changes it describes; a phone that (re)joins mid-match shows the game's defaults until the next one.
	 * The answers arrive as OnGameEvent pad:press / pad:release with {button}; read them with Net::ReadPadButton.
	 */
	void SendPadPrompt(const FString& PlayerId, const FString& Title, const FString& Subtitle, const FString& A,
		const FString& B, const FString& AccentHex, const FString& Mode);

	/** play:feedback for one player's phone (toast and haptic); see Net::Msg::PlayFeedback. */
	void SendPlayFeedback(const FString& PlayerId, const FString& Title, const FString& Detail, const FString& Tone, const FString& Haptic = FString(), bool bMajor = false);

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

	/** Party games' phone events (bowling:release, darts:release, stacker:drop, ...). */
	FMcGameEvent OnGameEvent;

	/** Party games' motion: every accepted sensor frame, by seat. */
	FMcSensorFrameEvent OnSensorFrame;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bInitialized; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }

private:
	/** One seat's latest phone controls and the throttles that go with them. */
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

	/** A frame held while the socket is down. Type is a TEXT() literal (the frame's `t`). */
	struct FQueuedFrame
	{
		FString Json;
		const TCHAR* Type = nullptr;
		double Time = 0.0;
	};

	/** The test hooks, parsed once in Initialize (all off in Shipping). */
	struct FDebugFlags
	{
		/** -RoomAutoStart[=Seconds]: start (solo too) once every seat has been ready for that long. */
		bool bAutoStart = false;
		double AutoStartDelay = 0.0;
		/** -RoomSelfTest: plays the race layer's part after each match start. */
		bool bSelfTest = false;
		/** -RoomShots=t1,t2: UI screenshots (RoomHost_N.png) at these seconds after startup, ascending. */
		TArray<double> ShotTimes;
		/** -RoomLogJoinUrl: also log the join URL with its token, for Tools/net-sim/phone-sim.mjs --from-log. */
		bool bLogJoinUrl = false;

		static FDebugFlags Parse(const TCHAR* CommandLine);
	};

	// RoomHostSubsystem.cpp: settings, lifecycle, tick.
	void ApplyServerSettings(const TCHAR* CommandLine);
	void TakeDueScreenshot(double T);
	double Now() const;
	void MarkLobbyChanged() { ++LobbyRevision; }

	// RoomHostConnection.cpp: the socket, reconnects and the outbox.
	void Connect();
	void BindSocketEvents();
	void HandleSocketClosed(int32 Status, const FString& Reason, bool bWasClean);
	void CloseSocket(const TCHAR* Reason);
	void ScheduleReconnect(const FString& Why);
	void SetConnection(EMcConnection State);
	/** The room is over for good: forget its code and tokens so the next OpenRoom mints a new one. */
	void ForgetRoom();
	void Send(const FString& Json, const TCHAR* Type);
	void FlushOutbox();

	// RoomHostInbound.cpp: server frames, the room state and the seats' input.
	void HandleText(const FString& Text);
	void HandleWelcome(const FJsonObject& Message);
	void HandleError(const FJsonObject& Message);
	void HandleHostChanged(const FJsonObject& Message);
	void HandleSignedScores(const FString& Text);
	void HandleGameEventMessage(const FJsonObject& Message);
	void HandleKartInput(const FString& FromPlayerId, const FJsonObject& Data);
	void HandlePracticeAction(const FString& FromPlayerId, const FJsonObject& Data);
	void HandleBinary(const void* Data, SIZE_T Size, bool bIsLastFragment);
	void HandleSensorFrame(const uint8* Data, int32 Size);
	void ApplyState(const FJsonObject& State, bool bFromWelcome);
	/** A state frame ParseRoomState had to clean up: a warning the first time on each connection. */
	void ReportStateIssues(const MovecadeKart::Net::FRoomStateIssues& Issues, int64 StateVersion);
	void OnStateApplied(const FMcRoomState& Previous);
	/** Logs joins, leaves and readiness and resets the input of seats that changed hands. True if the lobby must redraw. */
	bool ApplySeatChanges(const FMcRoomState& Previous);
	/** Match start, pause, completion and the return to the lobby. */
	void ApplyStatusChanges(const FMcRoomState& Previous);
	void ResetSeatInput(int32 SeatIndex);
	void ResetAllSeatInputs();

	// RoomHostLobby.cpp: start rules, the lobby countdown, the join URL and QR.
	MovecadeKart::Net::FStartCheck EvaluateStartCheck() const;
	void UpdateLobbyCountdown(double T);
	void UpdateAutoStart(double T, bool bSolo);
	/** The roster key the lobby countdown restarts on (code, game, players, map): rebuilt on change only. */
	void RefreshRosterKey();
	/** Forgets everything about the previous room (a new code is about to be minted): versions, match numbers, seats. */
	void ResetRoomState();
	void StartGameNow();
	void UpdateJoinUrl();

	// RoomHostPersistence.cpp: the saved room (code, host token, identity), one file per server.
	void LoadSaved(bool bIgnoreSavedRoom);
	/** Writes the saved room on a background pipe; bBlocking waits for it (shutdown). */
	void Save(bool bBlocking = false);
	FString SavePath() const;

	// RoomHostSelfTest.cpp
	void RunSelfTest(double T);

	// Connection
	TSharedPtr<IWebSocket> Socket;
	EMcConnection Connection = EMcConnection::Idle;
	bool bInitialized = false;
	bool bWantOpen = false;
	bool bWelcomed = false;
	int32 ReconnectAttempt = 0;
	double ReconnectAt = -1.0;
	TArray<FQueuedFrame> Outbox;
	TArray<uint8> BinaryAccumulator;

	// The room
	FString Code;
	FString Identity;
	FString HostToken;
	FString JoinToken;
	FString SessionId;
	FString JoinUrl;
	FString LastError;
	int64 HighestStateVersion = -1;
	FMcRoomState Room;
	/** ReportStateIssues has warned on this connection. */
	bool bReportedStateIssues = false;
	uint32 LobbyRevision = 0;

	UPROPERTY()
	TObjectPtr<UTexture2D> QrTexture;

	// Seats' input: one slot per seat (MovecadeKart::Net::MaxPlayers, checked in RoomHostInbound.cpp).
	static constexpr int32 SeatCount = 4;
	FSeatInput Inputs[SeatCount];

	// Lobby
	bool bPreparationReady = true;
	double AutoStartAt = -1.0;
	FString PendingMapId;
	FString PendingGameId;
	/** Code, game, sorted player ids and map, as of the last state change. */
	FString RosterKey;
	/** RosterKey when the running lobby countdown started. */
	FString LobbyCountdownKey;
	double LobbyCountdownEndsAt = -1.0;
	bool bStartSent = false;
	/** When start-game went out: a start lost to a dropped socket must not block the lobby for good. */
	double StartSentAt = -1.0;
	/** When we last asked the server for a game: until it echoes, the local pick wins over Room.GameId. */
	double GameAskedAt = -1000.0;
	int32 AdvancedMatchNumber = -1;
	int32 StartedMatchNumber = -1;
	int32 AckedMatchNumber = -1;

	// Publishing
	TArray<FMcScoreEntry> LastLiveScores;
	double LastLiveScoreTime = -1000.0;
	int32 SubmittedMatchNumber = -1;
	double HapticTokens = 15.0;
	double HapticRefillTime = 0.0;
	int32 ActionCounter = 0;

	// Persistence
	double LastSaveTime = -1000.0;
	/** Serialises the background writes of the saved room (one at a time, in order). */
	TUniquePtr<UE::Tasks::FPipe> SavePipe;

	// Test hooks
	FDebugFlags Debug;
	int32 SelfTestStep = -1;
	double SelfTestStart = 0.0;
	double SelfTestRaceStart = 0.0;
	double SelfTestLastPulse = 0.0;
	int32 ShotCount = 0;
	double StartTime = 0.0;
};
