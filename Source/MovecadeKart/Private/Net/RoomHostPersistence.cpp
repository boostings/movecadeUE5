// URoomHostSubsystem: the saved room (Saved/RoomHost/host-<server>.json): this host's identity, and the code and
// host token of its room while the server can still be holding it, so a restart reclaims the same room (HostRoute.tsx).

#include "Net/RoomHostSubsystem.h"

#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/RoomHostInternal.h"
#include "Net/RoomProtocol.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tasks/Pipe.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

using namespace MovecadeKart::Net;

namespace
{
	const TCHAR* const SavedHostTokenField = TEXT("hostToken");
	const TCHAR* const SavedSealedHostTokenField = TEXT("hostTokenSealed");

#if PLATFORM_WINDOWS
	/** DPAPI's optional entropy: another program running as this user can't unseal the token without knowing it. */
	constexpr ANSICHAR SealEntropy[] = "MovecadeKart.RoomHost.hostToken";

	DATA_BLOB EntropyBlob()
	{
		return DATA_BLOB{ static_cast<DWORD>(sizeof(SealEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(SealEntropy)) };
	}
#endif

	/** The saved room as JSON. Runs on the save pipe: sealing and serialising stay off the game thread. */
	FString SerializeSavedRoom(const FString& Identity, const FString& Code, const FString& HostToken, int64 SavedAtMs)
	{
		// Written with the JSON writer: the host token comes from the server and may hold any character.
		const TSharedRef<FJsonObject> Saved = MakeShared<FJsonObject>();
		Saved->SetStringField(TEXT("identity"), Identity);
		Saved->SetStringField(TEXT("code"), Code);
		RoomHost::WriteSavedHostToken(*Saved, HostToken);
		Saved->SetNumberField(TEXT("savedAt"), static_cast<double>(SavedAtMs));
		FString Json;
		FJsonSerializer::Serialize(Saved, TJsonWriterFactory<>::Create(&Json));
		return Json;
	}

	void WriteSavedRoom(const FString& Json, const FString& Path)
	{
		if (!FFileHelper::SaveStringToFile(Json, *Path))
		{
			UE_LOG(LogRoomHost, Warning, TEXT("Could not save the room to %s"), *Path);
		}
	}
}

namespace MovecadeKart::Net::RoomHost
{
	bool SealSecret(const FString& Plain, FString& OutSealed)
	{
#if PLATFORM_WINDOWS
		FTCHARToUTF8 Utf8(*Plain);
		DATA_BLOB In{ static_cast<DWORD>(Utf8.Length()), reinterpret_cast<BYTE*>(const_cast<ANSICHAR*>(Utf8.Get())) };
		DATA_BLOB Entropy = EntropyBlob();
		DATA_BLOB Out{};
		if (!::CryptProtectData(&In, nullptr, &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		{
			return false;
		}
		OutSealed = FBase64::Encode(Out.pbData, Out.cbData);
		::LocalFree(Out.pbData);
		return true;
#else
		return false;
#endif
	}

	bool UnsealSecret(const FString& Sealed, FString& OutPlain)
	{
#if PLATFORM_WINDOWS
		TArray<uint8> Bytes;
		if (Sealed.IsEmpty() || !FBase64::Decode(Sealed, Bytes))
		{
			return false;
		}
		DATA_BLOB In{ static_cast<DWORD>(Bytes.Num()), Bytes.GetData() };
		DATA_BLOB Entropy = EntropyBlob();
		DATA_BLOB Out{};
		if (!::CryptUnprotectData(&In, nullptr, &Entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &Out))
		{
			return false;
		}
		OutPlain = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Out.pbData), Out.cbData));
		FPlatformMemory::Memzero(Out.pbData, Out.cbData);
		::LocalFree(Out.pbData);
		return true;
#else
		return false;
#endif
	}

	FString ReadSavedHostToken(const FJsonObject& Saved)
	{
		FString Token;
		if (FString Sealed; Saved.TryGetStringField(SavedSealedHostTokenField, Sealed) && UnsealSecret(Sealed, Token))
		{
			return Token;
		}
		// An older build's plaintext token (or this platform's own). The next save seals it where it can.
		Saved.TryGetStringField(SavedHostTokenField, Token);
		return Token;
	}

	void WriteSavedHostToken(FJsonObject& Saved, const FString& HostToken)
	{
		if (HostToken.IsEmpty())
		{
			return;
		}
		if (!bCanSealSecrets)
		{
			Saved.SetStringField(SavedHostTokenField, HostToken);
			return;
		}
		if (FString Sealed; SealSecret(HostToken, Sealed))
		{
			Saved.SetStringField(SavedSealedHostTokenField, Sealed);
			return;
		}
		// Never fall back to plaintext: without the token a restart only waits for the server's grace instead.
		UE_LOG(LogRoomHost, Warning, TEXT("Could not seal the host token; the saved room goes without it"));
	}
}

FString URoomHostSubsystem::SavePath() const
{
	// One saved room per server: a code reclaimed on the local server means nothing to production.
	FString Server = PartyHost;
	for (TCHAR& C : Server)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('.'))
		{
			C = TEXT('_');
		}
	}
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RoomHost"), FString::Printf(TEXT("host-%s.json"), *Server));
}

void URoomHostSubsystem::LoadSaved(bool bIgnoreSavedRoom)
{
	FString Text;
	TSharedPtr<FJsonObject> Saved;
	if (FFileHelper::LoadFileToString(Text, *SavePath()))
	{
		Saved = RoomHost::ParseObject(Text);
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
	if (!Saved.IsValid() || bIgnoreSavedRoom)
	{
		return;
	}
	FString SavedCode;
	double SavedAt = 0.0;
	Saved->TryGetStringField(TEXT("code"), SavedCode);
	Saved->TryGetNumberField(TEXT("savedAt"), SavedAt);
	const int64 AgeMs = RoomHost::UnixMs() - static_cast<int64>(SavedAt);
	if (IsValidRoomCode(SavedCode) && AgeMs < RoomHost::SavedRoomMaxAgeMs)
	{
		// HostRoute.tsx pins the code so a reload reconnects to the same room; the hostToken reclaims it instantly.
		Code = SavedCode;
		HostToken = RoomHost::ReadSavedHostToken(*Saved);
		UE_LOG(LogRoomHost, Display, TEXT("Reclaiming room %s saved %.0f s ago"), *Code, (RoomHost::UnixMs() - SavedAt) / 1000.0);
	}
}

void URoomHostSubsystem::Save(bool bBlocking)
{
	LastSaveTime = Now();
	const int64 SavedAtMs = RoomHost::UnixMs();

	// Sealing the token and the disk write are the slow parts, and they happen every few seconds while the room is
	// open: off the game thread, through a pipe so the writes land in order.
	if (!SavePipe)
	{
		WriteSavedRoom(SerializeSavedRoom(Identity, Code, HostToken, SavedAtMs), SavePath());
		return;
	}
	SavePipe->Launch(TEXT("SaveRoomHost"), [Identity = Identity, Code = Code, HostToken = HostToken, SavedAtMs, Path = SavePath()]()
	{
		WriteSavedRoom(SerializeSavedRoom(Identity, Code, HostToken, SavedAtMs), Path);
	});
	if (bBlocking)
	{
		SavePipe->WaitUntilEmpty();
	}
}
