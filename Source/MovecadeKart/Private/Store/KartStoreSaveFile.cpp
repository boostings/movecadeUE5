#include "Store/KartStoreSaveFile.h"

#include "GameFramework/SaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"
#include "Store/KartStoreSave.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartStoreSave, Log, All);

namespace MovecadeKart::Store::SaveFile
{
	namespace
	{
		/** "MKS1": a Movecade Kart store save with the checked header. */
		constexpr uint32 Magic = 0x31534B4D;
		constexpr uint32 FormatVersion = 1;
		constexpr int32 HeaderBytes = 4 * sizeof(uint32);
		/** Bare GVAS: the engine's own save format, as every store save was written before the header. */
		constexpr uint8 GvasMagic[] = { 'G', 'V', 'A', 'S' };
		/** The only class a store save may name: the engine loads whatever class the file asks for. */
		const TCHAR* const SaveClassPath = TEXT("/Script/MovecadeKart.KartStoreSave");

		/** Little-endian bytes of an FString as an archive writes it (ANSI: length with the terminator, then the chars). */
		TArray<uint8> ArchiveString(const FString& Text)
		{
			TArray<uint8> Out;
			const int32 Len = Text.Len() + 1;
			Out.Append(reinterpret_cast<const uint8*>(&Len), sizeof(Len));
			for (const TCHAR Char : Text)
			{
				Out.Add(static_cast<uint8>(Char));
			}
			Out.Add(0);
			return Out;
		}

		TArray<uint8> ArchiveInt(int32 Value)
		{
			return TArray<uint8>(reinterpret_cast<const uint8*>(&Value), sizeof(Value));
		}

		int32 Find(const TArray<uint8>& Bytes, const TArray<uint8>& Pattern, int32 From = 0)
		{
			for (int32 i = From; i + Pattern.Num() <= Bytes.Num(); ++i)
			{
				if (FMemory::Memcmp(Bytes.GetData() + i, Pattern.GetData(), Pattern.Num()) == 0)
				{
					return i;
				}
			}
			return INDEX_NONE;
		}

		bool ReadInt(const TArray<uint8>& Bytes, int32 At, int32& Out)
		{
			if (At < 0 || At + static_cast<int32>(sizeof(int32)) > Bytes.Num())
			{
				return false;
			}
			FMemory::Memcpy(&Out, Bytes.GetData() + At, sizeof(int32));
			return true;
		}

		/** A container property of UKartStoreSave and the tag layout the engine writes for it (UE 5.4+ type names). */
		struct FContainerSpec
		{
			const TCHAR* Name;
			const TCHAR* Type;
			/** The type parameters after the type name: inner types, each followed by its own (empty) parameter count. */
			TArray<const TCHAR*> Params;
			/** Ints in the value before the element count (a map's keys-to-remove count). */
			int32 Prefix;
			/** Bytes each element takes at least. */
			int32 MinElementBytes;
			/** Elements have exactly MinElementBytes (an int array), so Size pins the count. */
			bool bFixedElement;
		};

		const FContainerSpec Containers[] = {
			{ TEXT("OwnedCache"), TEXT("ArrayProperty"), { TEXT("IntProperty") }, 0, sizeof(int32), true },
			// FName key (as a string: length + at least the terminator) and an int value.
			{ TEXT("Equipped"), TEXT("MapProperty"), { TEXT("NameProperty"), TEXT("IntProperty") }, 1, 2 * sizeof(int32) + 1, false },
			{ TEXT("Stats"), TEXT("MapProperty"), { TEXT("NameProperty"), TEXT("IntProperty") }, 1, 2 * sizeof(int32) + 1, false },
		};

		/**
		 * The engine trusts a container's element count: a negative one asserts (check(Count>=0)) and a huge one
		 * allocates before it reads. Every container this class has must carry the layout the engine writes and a
		 * count its Size can actually hold.
		 */
		bool CheckContainer(const TArray<uint8>& Payload, const FContainerSpec& Spec, FString& OutWhy)
		{
			const int32 NameAt = Find(Payload, ArchiveString(Spec.Name));
			if (NameAt == INDEX_NONE)
			{
				return true; // left at its default: not written
			}
			TArray<uint8> Tag = ArchiveString(Spec.Name);
			Tag.Append(ArchiveString(Spec.Type));
			Tag.Append(ArchiveInt(Spec.Params.Num()));
			for (const TCHAR* Param : Spec.Params)
			{
				Tag.Append(ArchiveString(Param));
				Tag.Append(ArchiveInt(0));
			}
			if (Find(Payload, Tag, NameAt) != NameAt)
			{
				OutWhy = FString::Printf(TEXT("%s has an unexpected type"), Spec.Name);
				return false;
			}
			const int32 SizeAt = NameAt + Tag.Num();
			const int32 ValueAt = SizeAt + sizeof(int32) + 1; // Size, then the tag flags byte
			int32 Size = 0;
			int32 Count = 0;
			// The count sits past the size and the flags byte, so reading it first proves both are inside the file.
			if (!ReadInt(Payload, SizeAt, Size) || !ReadInt(Payload, ValueAt + Spec.Prefix * sizeof(int32), Count) || Payload[SizeAt + sizeof(int32)] != 0)
			{
				OutWhy = FString::Printf(TEXT("%s is cut short or has tag flags"), Spec.Name);
				return false;
			}
			for (int32 i = 0; i < Spec.Prefix; ++i)
			{
				// A map's keys-to-remove count: the store never writes one.
				int32 Removed = 0;
				if (!ReadInt(Payload, ValueAt + i * sizeof(int32), Removed) || Removed != 0)
				{
					OutWhy = FString::Printf(TEXT("%s has keys to remove"), Spec.Name);
					return false;
				}
			}
			const int64 Overhead = (Spec.Prefix + 1) * sizeof(int32);
			const bool bSizeFits = Size >= Overhead && ValueAt + static_cast<int64>(Size) <= Payload.Num();
			const bool bCountFits = Count >= 0 && (Spec.bFixedElement
				? static_cast<int64>(Count) * Spec.MinElementBytes == Size - Overhead
				: static_cast<int64>(Count) * Spec.MinElementBytes <= Size - Overhead);
			if (!bSizeFits || !bCountFits)
			{
				OutWhy = FString::Printf(TEXT("%s claims %d entries in %d bytes"), Spec.Name, Count, Size);
				return false;
			}
			return true;
		}

		/** The payload must be GVAS for our class, with sane containers. */
		bool CheckPayload(const TArray<uint8>& Payload, FString& OutWhy)
		{
			if (Payload.Num() < static_cast<int32>(UE_ARRAY_COUNT(GvasMagic)) || FMemory::Memcmp(Payload.GetData(), GvasMagic, UE_ARRAY_COUNT(GvasMagic)) != 0)
			{
				OutWhy = TEXT("not a GVAS save");
				return false;
			}
			const int32 ClassAt = Find(Payload, ArchiveString(SaveClassPath));
			if (ClassAt == INDEX_NONE)
			{
				OutWhy = TEXT("names another save class");
				return false;
			}
			for (const FContainerSpec& Spec : Containers)
			{
				if (!CheckContainer(Payload, Spec, OutWhy))
				{
					return false;
				}
			}
			return true;
		}

		ISaveGameSystem* SaveSystem()
		{
			return IPlatformFeaturesModule::Get().GetSaveGameSystem();
		}

		void Quarantine(ISaveGameSystem& System, const FString& Slot, const TArray<uint8>& Bytes, const FString& Why)
		{
			const FString Aside = Slot + TEXT(".bad");
			System.SaveGame(false, *Aside, 0, Bytes);
			System.DeleteGame(false, *Slot, 0);
			UE_LOG(LogKartStoreSave, Warning, TEXT("Store save %s is unreadable (%s): moved to %s, starting clean"), *Slot, *Why, *Aside);
		}
	}

	TArray<uint8> Wrap(const TArray<uint8>& Payload)
	{
		const uint32 Header[] = { Magic, FormatVersion, static_cast<uint32>(Payload.Num()), FCrc::MemCrc32(Payload.GetData(), Payload.Num()) };
		TArray<uint8> Out(reinterpret_cast<const uint8*>(Header), HeaderBytes);
		Out.Append(Payload);
		return Out;
	}

	bool Validate(const TArray<uint8>& FileBytes, TArray<uint8>& OutPayload, FString& OutWhy)
	{
		OutPayload.Reset();
		if (FileBytes.Num() > MaxFileBytes)
		{
			OutWhy = FString::Printf(TEXT("%d bytes is too large"), FileBytes.Num());
			return false;
		}
		uint32 Header[4] = {};
		if (FileBytes.Num() >= HeaderBytes)
		{
			FMemory::Memcpy(Header, FileBytes.GetData(), HeaderBytes);
		}
		if (Header[0] == Magic)
		{
			if (Header[1] != FormatVersion || Header[2] != static_cast<uint32>(FileBytes.Num() - HeaderBytes))
			{
				OutWhy = TEXT("bad header");
				return false;
			}
			OutPayload.Append(FileBytes.GetData() + HeaderBytes, FileBytes.Num() - HeaderBytes);
			if (FCrc::MemCrc32(OutPayload.GetData(), OutPayload.Num()) != Header[3])
			{
				OutWhy = TEXT("checksum mismatch");
				return false;
			}
		}
		else
		{
			OutPayload = FileBytes; // a save from before the header: bare GVAS
		}
		return CheckPayload(OutPayload, OutWhy);
	}

	EReadResult Read(const FString& Slot, UKartStoreSave*& Out)
	{
		Out = nullptr;
		ISaveGameSystem* System = SaveSystem();
		if (!System || Slot.IsEmpty() || !System->DoesSaveGameExist(*Slot, 0))
		{
			return EReadResult::Missing;
		}
		TArray<uint8> Bytes;
		TArray<uint8> Payload;
		FString Why;
		if (!System->LoadGame(false, *Slot, 0, Bytes))
		{
			Why = TEXT("can't be read");
		}
		else if (Validate(Bytes, Payload, Why))
		{
			Out = Cast<UKartStoreSave>(UGameplayStatics::LoadGameFromMemory(Payload));
			Why = Out ? FString() : TEXT("the engine rejected it");
		}
		if (!Out)
		{
			Quarantine(*System, Slot, Bytes, Why);
			return EReadResult::Quarantined;
		}
		return EReadResult::Loaded;
	}

	bool Write(const FString& Slot, UKartStoreSave& Save)
	{
		ISaveGameSystem* System = SaveSystem();
		TArray<uint8> Payload;
		if (!System || Slot.IsEmpty() || !UGameplayStatics::SaveGameToMemory(&Save, Payload))
		{
			return false;
		}
		const bool bSaved = System->SaveGame(false, *Slot, 0, Wrap(Payload));
		UE_CLOG(!bSaved, LogKartStoreSave, Warning, TEXT("Couldn't write the store save %s"), *Slot);
		return bSaved;
	}
}
