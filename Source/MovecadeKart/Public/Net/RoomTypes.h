#pragma once

#include "CoreMinimal.h"
#include "RoomTypes.generated.h"

/** game.status (protocol/src/state.ts). Kart never uses Countdown, but the type has it. */
UENUM(BlueprintType)
enum class EMcRoomStatus : uint8
{
	Unknown,
	Lobby,
	Countdown,
	InProgress,
	Paused,
	Complete,
};

/** Where the host socket is. Open means welcomed by the room server. */
UENUM(BlueprintType)
enum class EMcConnection : uint8
{
	Idle,
	Connecting,
	Open,
	Reconnecting,
	/** A terminal close or fatal error (bad origin, room closed, version mismatch). OpenRoom() starts again. */
	Failed,
};

/** The host's local race phases (legacy store.ts RaceStatus), as the race layer reports them. */
UENUM(BlueprintType)
enum class EMcRacePhase : uint8
{
	Intro,
	Countdown,
	Racing,
	Finished,
};

/** One seated phone (protocol PlayerState). Seat is 1..4 and never renumbered; SeatIndex() is 0..3. */
USTRUCT(BlueprintType)
struct MOVECADEKART_API FMcSeat
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString PlayerId;

	/** 1..4, as the server numbers it. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int32 Seat = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Emoji;

	/** avatar.kartPaint: pink|cyan|gold|violet|white|navy, or empty for the seat colour. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString KartPaint;

	/** avatar.vehicle (kart|moto|coupe|truck), empty = stock kart. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Vehicle;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Face;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Body;

	/** The kart paint's primary colour, or the seat colour when there is no paint. sRGB-decoded. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FLinearColor PaintColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bReady = false;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bCalibrated = false;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bConnected = false;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bEliminated = false;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bBot = false;

	/** Render reconnect countdowns from this, never a local constant. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int32 GraceRemainingMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int32 Score = 0;

	/** That phone's controller screen (join|calibrate|ready|active|...). */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Screen;

	int32 SeatIndex() const { return Seat - 1; }
};

/** The parts of protocol RoomState the host uses. */
USTRUCT(BlueprintType)
struct MOVECADEKART_API FMcRoomState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString RoomId;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString JoinCode;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString GameId;

	/** violet-loop (protocol default) or sunset-harbor. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString SelectedMapId;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int64 StateVersion = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString HostSessionId;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	bool bHostMissing = false;

	/** Sorted by seat. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	TArray<FMcSeat> Players;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	EMcRoomStatus Status = EMcRoomStatus::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString Phase;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int32 MatchNumber = 0;

	/** Server epoch ms of start-game, or 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int64 StartedAt = 0;

	/** player-disconnected | host-transfer | host-request, or empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString PauseReason;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString PausedForPlayerId;

	/** lobby | match, or empty when nothing is counting. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	FString CountdownKind;

	/** Server epoch ms; remaining = CountdownEndsAt - ServerTime. */
	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int64 CountdownEndsAt = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Room")
	int64 ServerTime = 0;

	const FMcSeat* FindBySeat(int32 Seat) const;
	const FMcSeat* FindByPlayerId(const FString& Id) const;
};

/** One live or final score (protocol ScoreEntry). */
USTRUCT(BlueprintType)
struct MOVECADEKART_API FMcScoreEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Room")
	FString PlayerId;

	UPROPERTY(BlueprintReadWrite, Category = "Room")
	int32 Score = 0;
};

/** The race layer's result for one seat at the end of a race. */
USTRUCT(BlueprintType)
struct MOVECADEKART_API FMcRaceResult
{
	GENERATED_BODY()

	/** 0..3. Used to find the player when PlayerId is empty. */
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	int32 SeatIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Room")
	FString PlayerId;

	UPROPERTY(BlueprintReadWrite, Category = "Room")
	bool bFinished = false;

	/** Race-clock finish time (pauses excluded). Rounded to whole ms on the wire. */
	UPROPERTY(BlueprintReadWrite, Category = "Room")
	double FinishTimeMs = 0.0;
};
