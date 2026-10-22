#pragma once

#include "CoreMinimal.h"
#include "Net/RoomTypes.h"

class FJsonObject;

/**
 * The Movecade v2 realtime protocol as the kart host needs it: plain functions with no sockets or
 * UObjects, so every rule is unit-tested (MovecadeKart.Net.*). Sources are movecade-v2
 * protocol/src (sensor.ts, roomCode.ts, messages.ts, constants.ts) and the live kart host code
 * cited in Docs/Port/04-flow-network-ui-audio.md.
 */
namespace MovecadeKart::Net
{
	constexpr int32 ProtocolVersion = 2;

	// Sensor frames (sensor.ts). The relay appends one byte, seat - 1.
	constexpr int32 SensorFrameBytes = 30;
	constexpr int32 SensorFrameWithPlayerBytes = 31;
	constexpr int32 SensorFrameV2Bytes = 38;
	constexpr int32 SensorFrameV2WithPlayerBytes = 39;
	constexpr uint8 SensorFrameType = 0x02;
	constexpr uint8 SensorFlagCalibrated = 1 << 0;
	constexpr uint8 SensorFlagScreenCompensated = 1 << 1;
	constexpr uint8 SensorFlagHasGyro = 1 << 2;
	constexpr uint8 SensorFlagHasAccel = 1 << 3;
	constexpr uint8 SensorFlagHasGravity = 1 << 4;
	constexpr uint8 SensorFlagHasQuaternion = 1 << 5;
	constexpr uint8 SensorFlagMask = 0x3F;

	// Steering (legacy kartRaceConstants.ts KART_MOTION).
	constexpr double MaxSteerAngleDeg = 90.0;
	constexpr double SteerDeadzoneDeg = 2.0;

	// Timing and limits (constants.ts, legacy bridge, spec 04).
	constexpr int32 LobbyCountdownMs = 5000;
	constexpr int32 MatchCountdownMs = 5000;
	constexpr int32 CountdownMaxMs = 60000;
	constexpr int32 MaxPlayers = 4;
	constexpr int32 MaxScore = 600000;
	/** MIN_SIGNED_TIME_MS: a kart finish below this is not a finish (scoring.ts). */
	constexpr int32 MinFinishTimeMs = 10000;
	/** Spec 04 §6.2: implement a 200 ms item debounce per player. */
	constexpr double ItemDebounceSeconds = 0.2;
	/** Tilt steering goes neutral after this long without a sensor frame. */
	constexpr double SensorStaleSeconds = 1.5;
	constexpr int32 HostGraceMs = 90000;

	constexpr int32 RoomCodeLength = 6;
	extern MOVECADEKART_API const TCHAR* const RoomCodeAlphabet;

	enum class ESensorError : uint8
	{
		None,
		BadSize,
		BadFrameType,
		BadFlags,
		BadOrientation,
	};

	MOVECADEKART_API const TCHAR* LexToString(ESensorError Error);

	struct FSensorFrame
	{
		uint8 Flags = 0;
		/** 0 portrait, 1 landscape-left, 2 upside-down, 3 landscape-right. */
		uint8 ScreenOrientation = 0;
		uint8 Sequence = 0;
		/** Degrees. In kart wheel mode Gamma is the calibrated wheel angle, positive = clockwise. */
		double Alpha = 0.0;
		double Beta = 0.0;
		double Gamma = 0.0;
		/** deg/s */
		FVector3d Gyro = FVector3d::ZeroVector;
		/** m/s^2, gravity removed */
		FVector3d Accel = FVector3d::ZeroVector;
		/** m/s^2 */
		FVector3d Gravity = FVector3d::ZeroVector;
		uint16 DtMs = 0;
		bool bHasQuaternion = false;
		/** x, y, z, w; only when bHasQuaternion. */
		FVector4d Quaternion = FVector4d(0.0, 0.0, 0.0, 1.0);
		/** seat - 1 on relayed frames, INDEX_NONE on a bare phone frame. */
		int32 PlayerIndex = INDEX_NONE;
	};

	/** validateSensorFrame: size 30/31/38/39, type 0x02, flags within 0x3F, quaternion flag needs 38/39, orientation <= 3. */
	MOVECADEKART_API ESensorError ValidateSensorFrame(const uint8* Data, int32 Size);
	/** decodeSensorFrame, little-endian. Returns false (and leaves Out alone) on an invalid frame. */
	MOVECADEKART_API bool DecodeSensorFrame(const uint8* Data, int32 Size, FSensorFrame& Out, ESensorError* OutError = nullptr);

	/** A JSON string literal, escaped. */
	MOVECADEKART_API FString QuoteJson(const FString& Value);

	/**
	 * The host steering transform (MultiplayerMobileControlBridge.tsx:228-291): wrap gamma to
	 * [-180, 180], clamp(g / 90), 2 degree deadzone with rescale, x sensitivity, clamp, invert.
	 * The result follows FKartInput::SteeringX: positive steers LEFT, so a phone turned clockwise
	 * (positive gamma) steers right.
	 */
	MOVECADEKART_API double SteeringFromGamma(double GammaDeg, double Sensitivity = 1.0);

	// Server -> host frames (messages.ts ServerMessage). The wire is exact: types and codes compare case-sensitively.
	enum class EServerMessage : uint8
	{
		Unknown,
		Ping,
		Welcome,
		State,
		GameEvent,
		Error,
		HostChanged,
		SignedScores,
		RoomClosed,
		ActionAck,
		ResumeCountdown,
	};
	/** A frame's `t`; Unknown for anything the host does not handle. */
	MOVECADEKART_API EServerMessage ParseServerMessageType(const FString& Type);

	/** The phone game events the host itself consumes (every other one goes to the game through OnGameEvent). */
	constexpr const TCHAR* KartInputEvent = TEXT("kart-input");
	constexpr const TCHAR* PracticeActionEvent = TEXT("practice:action");

	/** error {code: UNAUTHORIZED_ORIGIN}: the server does not admit this host's Origin (Docs/Port/server). */
	constexpr const TCHAR* ErrorUnauthorizedOrigin = TEXT("UNAUTHORIZED_ORIGIN");
	/** The error codes that refuse a start-game (the host may ask again). */
	MOVECADEKART_API bool IsStartRejectedError(const FString& Code);

	/** Why the host may not start the match yet, in the order the lobby checks (HostSurface.tsx:337-348). */
	enum class EStartBlock : uint8
	{
		None,
		/** The socket is not open (connecting, reconnecting, failed). */
		NotConnected,
		/** Another screen hosts this room. */
		NotHost,
		/** The room is not in its lobby. */
		MatchRunning,
		NobodySeated,
		/** Only bots are seated: a person has to join. */
		OnlyBots,
		/** The race layer is still loading the track. */
		Preparing,
		/** Someone is not ready, not connected or eliminated (FStartCheck::NotReady of them). */
		NotReady,
	};

	struct FStartCheck
	{
		EStartBlock Block = EStartBlock::None;
		int32 NotReady = 0;

		bool CanStart() const { return Block == EStartBlock::None; }
	};

	/** The start rules, without building any text: the lobby asks every frame. */
	MOVECADEKART_API FStartCheck EvaluateStart(const FMcRoomState& Room, bool bConnectionOpen, bool bIsHost, bool bPreparationReady);
	/** The lobby's line for a blocked start ("Scan the code with a phone to join."); empty when it can start. */
	MOVECADEKART_API FString DescribeStartBlock(const FStartCheck& Check, const FString& LastError);

	/** game-event `kart-input` data (legacyBridge.ts:318-335). Each phone event carries only the fields that changed. */
	struct FKartInputEvent
	{
		TOptional<bool> Accelerating;
		TOptional<bool> Reversing;
		TOptional<bool> Drifting;
		/** Bots and assist mode only, already in the kart convention, clamped to [-1, 1]. */
		TOptional<double> Steering;
		bool bUseItem = false;

		bool IsEmpty() const { return !Accelerating && !Reversing && !Drifting && !Steering && !bUseItem; }
	};
	MOVECADEKART_API FKartInputEvent ParseKartInput(const FJsonObject& Data);

	/**
	 * game-event `practice:action` data, the phone's lobby rehearsal (useLobbyPractice.ts readPracticeAction):
	 * {gameId, kind, actionId} with this game's id, its kind (kart: "steer") and a 1..80 char action id.
	 */
	MOVECADEKART_API bool ReadPracticeAction(const FJsonObject& Data, const FString& GameId, FString& OutActionId);

	// The motion pad (Docs/Agents/BRIEF.md "The motion pad contract", server patch Docs/Port/server/0002): the generic
	// phone controller for new motion games. Phones send pad:press / pad:release {button:"a"|"b"}; the host sends
	// pad:prompt {playerId, title, subtitle, a, b, accent, mode} (host-only on the server).
	constexpr const TCHAR* PadPressEvent = TEXT("pad:press");
	constexpr const TCHAR* PadReleaseEvent = TEXT("pad:release");
	constexpr const TCHAR* PadPromptEvent = TEXT("pad:prompt");
	/** pad:prompt playerId that addresses every phone (a player's own, newer prompt still wins on that phone). */
	constexpr const TCHAR* PadPromptEveryone = TEXT("*");
	/** pad:press / pad:release data. True with OutButton 'a' or 'b'; any other shape is refused. */
	MOVECADEKART_API bool ReadPadButton(const FJsonObject& Data, TCHAR& OutButton);
	/** "#rrggbb" (lower case, sRGB) for a pad prompt's accent, from a linear colour. */
	MOVECADEKART_API FString HexColor(const FLinearColor& Color);
	/** True for a hint animation the phone knows: tilt, swing, aim, flick. */
	MOVECADEKART_API bool IsPadMode(const FString& Mode);
	/**
	 * pad:prompt data, the JSON object the phone reads (motionPad.ts readMotionPadPrompt). Title <= 80, Subtitle <= 160,
	 * A / B labels <= 16 characters (empty hides that button). AccentHex is "#rrggbb" and Mode one of IsPadMode; either
	 * is left out when it is not, and the phone then uses the game's own accent and hint.
	 */
	MOVECADEKART_API FString PadPromptData(const FString& PlayerId, const FString& Title, const FString& Subtitle,
		const FString& A, const FString& B, const FString& AccentHex, const FString& Mode);

	// Room codes and the QR join URL (roomCode.ts).
	/** generateRoomCode: 6 chars, uniform rejection sampling over the 31-glyph alphabet. */
	MOVECADEKART_API FString GenerateRoomCode(TFunctionRef<void(uint8* Bytes, int32 Count)> RandomBytes);
	MOVECADEKART_API FString GenerateRoomCode();
	MOVECADEKART_API bool IsValidRoomCode(const FString& Code);
	MOVECADEKART_API FString RoomIdForCode(const FString& Code);
	/** buildJoinUrl: `<base>/join/CODE#v=2&t=<token>`; the token only ever goes in the fragment. */
	MOVECADEKART_API FString BuildJoinUrl(const FString& BaseUrl, const FString& Code, const FString& JoinToken);
	/** application/x-www-form-urlencoded, as URLSearchParams.toString() writes it. */
	MOVECADEKART_API FString FormEncode(const FString& Value);

	/** A lower-case v4 UUID (the server's identity regex requires v4). */
	MOVECADEKART_API FString MakeIdentity();
	MOVECADEKART_API bool IsV4Uuid(const FString& Value);

	// Room state.
	MOVECADEKART_API EMcRoomStatus ParseStatus(const FString& Status);
	MOVECADEKART_API bool ParseRoomState(const FJsonObject& State, FMcRoomState& Out);
	/** Kart paint primary (games/avatar/appearanceColors.ts), else the seat colour (games/render/palette.ts). */
	MOVECADEKART_API FLinearColor PaintColor(const FString& KartPaint, int32 Seat);

	/**
	 * Final scores for complete-game: finishers only, whole ms in [MinFinishTimeMs, MaxScore],
	 * ascending (fastest first). DNFs and zero times are never reported (GAMES-01/02).
	 * PlayerId is filled from Room by SeatIndex when the result has none.
	 */
	MOVECADEKART_API TArray<FMcScoreEntry> BuildFinalScores(const TArray<FMcRaceResult>& Results, const FMcRoomState& Room);

	/** Validates a score list the way messages.ts does: <= 4 entries, unique p<N> ids, integers 0..600000. */
	MOVECADEKART_API bool AreScoresValid(const TArray<FMcScoreEntry>& Scores);

	/** Host -> server frames. Every one is a compact JSON object with v:2 and t. */
	namespace Msg
	{
		MOVECADEKART_API FString Hello(const FString& Client, const FString& GameId, const FString& Identity, const FString& HostToken);
		MOVECADEKART_API FString Pong();
		MOVECADEKART_API FString StateAck(int64 StateVersion);
		MOVECADEKART_API FString ConfigureRoomMap(const FString& MapId);
		/** configure-room {gameId}: lobby only; the server clears everyone's ready and calibration on a change. */
		MOVECADEKART_API FString ConfigureRoomGame(const FString& GameId);
		/** Kind is "lobby" or "match"; DurationMs 0 cancels / marks GO. */
		MOVECADEKART_API FString SetCountdown(const FString& Kind, int32 DurationMs);
		MOVECADEKART_API FString StartGame();
		/** advance-turn {activePlayerId:null, phase:'active'}: simultaneous play, so every seat's frames are relayed. */
		MOVECADEKART_API FString AdvanceTurnSimultaneous();
		/** advance-turn {activePlayerId, phase: 'active'}: a turn-based game names who may send frames now. */
		MOVECADEKART_API FString AdvanceTurn(const FString& ActivePlayerId);
		/** game-event {event, data}; Data is a JSON value written by the caller (QuoteJson for strings). */
		MOVECADEKART_API FString GameEvent(const FString& Event, const FString& DataJson);
		MOVECADEKART_API FString ScoreUpdate(const TArray<FMcScoreEntry>& Scores);
		MOVECADEKART_API FString CompleteGame(const TArray<FMcScoreEntry>& Scores);
		MOVECADEKART_API FString RestartGame(const FString& ActionId);
		MOVECADEKART_API FString ReturnToLobby(const FString& ActionId);
		MOVECADEKART_API FString KickPlayer(const FString& PlayerId);
		MOVECADEKART_API FString DropPlayer(const FString& PlayerId);
		MOVECADEKART_API FString CloseRoom();
		/**
		 * play:feedback (playFeedback.ts): the phone's toast and haptic after a play. Tone success | warning | neutral;
		 * Haptic impact | warning | celebration or empty; title <= 80 and detail <= 160 characters.
		 */
		MOVECADEKART_API FString PlayFeedback(const FString& GameId, int32 MatchNumber, const FString& PlayerId, const FString& Title,
			const FString& Detail, const FString& Tone, const FString& Haptic, bool bMajor);
		/**
		 * The web host's phone haptic (MultiplayerKart.tsx:1447-1569): game-event `collision` with
		 * {type:'collision', targetPlayerId, intensity, collisionType, sustained}. Intensity is clamped to 0..1.
		 */
		MOVECADEKART_API FString Haptic(const FString& TargetPlayerId, double Intensity, const FString& CollisionType, bool bSustained);
		/** Out-of-union ack that stops signed-scores redelivery (engine.ts handleScoresAck). */
		MOVECADEKART_API FString ScoresAck(int32 MatchNumber);
		/** game-event `practice:ack` {gameId, playerId, actionId}: the phone's practice step waits for it. */
		MOVECADEKART_API FString PracticeAck(const FString& GameId, const FString& PlayerId, const FString& ActionId);
	}

	/** Types the web transport drops when they sat in the offline queue for more than 1.5 s (transportPolicy.ts). */
	MOVECADEKART_API bool IsMomentaryType(const FString& Type);
	/** Close codes after which the client must not reconnect (transportPolicy.ts). */
	MOVECADEKART_API bool IsTerminalCloseCode(int32 Code);
}
