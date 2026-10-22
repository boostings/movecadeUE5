#pragma once

#include "CoreMinimal.h"

class UKartStoreSave;

/**
 * Reads and writes the store's save slots (KartStore.sav, and the dev mock's KartStoreMock.sav) safely.
 *
 * On disk: a 16-byte header (magic, format version, payload size, CRC32 of the payload), then the UKartStoreSave as
 * the engine writes it (GVAS). Before anything reaches the engine's deserialiser the size, the CRC and the counts
 * of the save's container properties are checked: a negative or oversized count asserts or allocates gigabytes deep
 * inside the engine. A file that fails is moved aside to "<Slot>.bad" and the store starts clean. Saves written
 * before the header existed (bare GVAS) are still read, with the same container checks, and rewritten on the next
 * save. The CRC catches corruption, not tampering: ownership never rests on this file (FKartStore wears paid items
 * only once the backend confirms them).
 */
namespace MovecadeKart::Store::SaveFile
{
	enum class EReadResult : uint8
	{
		/** No such slot. */
		Missing,
		Loaded,
		/** Unreadable or failed a check: quarantined to "<Slot>.bad". */
		Quarantined,
	};

	/** Out is set only when the result is Loaded. */
	EReadResult Read(const FString& Slot, UKartStoreSave*& Out);
	bool Write(const FString& Slot, UKartStoreSave& Save);

	/** The checks Read runs, on raw file bytes (exposed for tests). Payload receives the GVAS bytes to deserialise. */
	bool Validate(const TArray<uint8>& FileBytes, TArray<uint8>& OutPayload, FString& OutWhy);
	/** The bytes Write puts on disk for a GVAS payload (exposed for tests). */
	TArray<uint8> Wrap(const TArray<uint8>& Payload);

	/** Files larger than this are refused unread: the real save is about 2 KB. */
	constexpr int32 MaxFileBytes = 1 << 20;
}
