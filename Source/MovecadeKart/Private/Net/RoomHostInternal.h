#pragma once

#include "CoreMinimal.h"
#include "Net/RoomTypes.h"

class FJsonObject;

/**
 * Helpers shared by the URoomHostSubsystem translation units (RoomHost*.cpp), and the subsystem's tunables. Private
 * to the Net/ implementation: nothing outside the subsystem includes this.
 */
namespace MovecadeKart::Net::RoomHost
{
	/** A saved room is only worth reclaiming while the server can still be holding it (HOST_GRACE_MS 90 s). */
	constexpr int64 SavedRoomMaxAgeMs = 80000;
	/** The saved room's freshness is rewritten this often while the room is open. */
	constexpr double SaveIntervalSeconds = 10.0;
	/** Frames queued while the socket is down; the oldest goes first when full. */
	constexpr int32 OutboxLimit = 64;
	/** transportPolicy.ts: a queued momentary frame older than this is dropped on reconnect. */
	constexpr double MomentaryMaxAgeSeconds = 1.5;
	/** Haptics never spend more than 15 of the host's 40 frames/s control budget. */
	constexpr double HapticTokensPerSecond = 15.0;
	constexpr double HapticMinSustainedInterval = 0.1;
	/** transportPolicy.ts: reconnect slots, 250 ms doubling to 8 s (jittered over each slot's lower half). */
	constexpr double ReconnectLadderMs[] = { 250, 500, 1000, 2000, 4000, 8000 };
	/** A start-game the server never answered (a dropped socket) stops blocking the lobby after this long. */
	constexpr double StartAnswerTimeoutSeconds = 8.0;
	/** Our own configure-room {gameId} wins over the room's echo for this long (until the server has seen it). */
	constexpr double GameSelectGraceSeconds = 2.0;
	/** Nothing binary on this socket is larger: an unfinished message past this is junk and is dropped. */
	constexpr int32 MaxBinaryMessageBytes = 256;
	/** Per-seat sensor logging (Verbose) at most this often. */
	constexpr double SensorLogIntervalSeconds = 1.0;
	/** The join QR: quiet zone (modules, ISO 18004 asks for 4) and pixels per module. */
	constexpr int32 QrQuietModules = 4;
	constexpr int32 QrPixelsPerModule = 8;
	/** The room server's close codes: 4100 says the room is gone (the next OpenRoom mints a new code). */
	constexpr int32 CloseRoomGone = 4100;
	constexpr int32 CloseNormal = 1000;

	/**
	 * The saved room's host token at rest (NET-03): it reclaims the room, so it isn't left readable to other users of
	 * the PC. On Windows it is sealed with DPAPI for the current user (CryptProtectData) under hostTokenSealed; a
	 * plaintext hostToken from an older build is still read and is replaced by the sealed one on the next save.
	 * Other platforms have no equivalent wired up yet and keep writing the plaintext hostToken, as before.
	 */
	constexpr bool bCanSealSecrets = PLATFORM_WINDOWS;
	/** DPAPI-sealed, base64; false where bCanSealSecrets is false or the OS refuses. */
	bool SealSecret(const FString& Plain, FString& OutSealed);
	/** The other way; false for anything this user did not seal (another account, another PC, tampered). */
	bool UnsealSecret(const FString& Sealed, FString& OutPlain);
	/** hostTokenSealed when it unseals, else the legacy plaintext hostToken, else empty. */
	FString ReadSavedHostToken(const FJsonObject& Saved);
	/** hostTokenSealed where it can be sealed; plaintext hostToken only where bCanSealSecrets is false. */
	void WriteSavedHostToken(FJsonObject& Saved, const FString& HostToken);

	int64 UnixMs();
	TSharedPtr<FJsonObject> ParseObject(const FString& Text);
	/** `seat 2 p3 "Name"`, for the log. */
	FString Describe(const FMcSeat& Seat);
}
