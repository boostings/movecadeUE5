// URoomHostSubsystem: the saved room (Saved/RoomHost/host-<server>.json): this host's identity, and the code and
// host token of its room while the server can still be holding it, so a restart reclaims the same room (HostRoute.tsx).

#include "Net/RoomHostSubsystem.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/RoomHostInternal.h"
#include "Net/RoomProtocol.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tasks/Pipe.h"

using namespace MovecadeKart::Net;

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
	FString SavedToken;
	double SavedAt = 0.0;
	Saved->TryGetStringField(TEXT("code"), SavedCode);
	Saved->TryGetStringField(TEXT("hostToken"), SavedToken);
	Saved->TryGetNumberField(TEXT("savedAt"), SavedAt);
	const int64 AgeMs = RoomHost::UnixMs() - static_cast<int64>(SavedAt);
	if (IsValidRoomCode(SavedCode) && AgeMs < RoomHost::SavedRoomMaxAgeMs)
	{
		// HostRoute.tsx pins the code so a reload reconnects to the same room; the hostToken reclaims it instantly.
		Code = SavedCode;
		HostToken = SavedToken;
		UE_LOG(LogRoomHost, Display, TEXT("Reclaiming room %s saved %.0f s ago"), *Code, (RoomHost::UnixMs() - SavedAt) / 1000.0);
	}
}

void URoomHostSubsystem::Save(bool bBlocking)
{
	LastSaveTime = Now();
	// Written with the JSON writer: the host token comes from the server and may hold any character.
	const TSharedRef<FJsonObject> Saved = MakeShared<FJsonObject>();
	Saved->SetStringField(TEXT("identity"), Identity);
	Saved->SetStringField(TEXT("code"), Code);
	Saved->SetStringField(TEXT("hostToken"), HostToken);
	Saved->SetNumberField(TEXT("savedAt"), static_cast<double>(RoomHost::UnixMs()));
	FString Json;
	FJsonSerializer::Serialize(Saved, TJsonWriterFactory<>::Create(&Json));

	// The write is the slow part (disk), and it happens every few seconds while the room is open: off the game thread,
	// through a pipe so the writes land in order.
	if (!SavePipe)
	{
		FFileHelper::SaveStringToFile(Json, *SavePath());
		return;
	}
	SavePipe->Launch(TEXT("SaveRoomHost"), [Json = MoveTemp(Json), Path = SavePath()]()
	{
		if (!FFileHelper::SaveStringToFile(Json, *Path))
		{
			UE_LOG(LogRoomHost, Warning, TEXT("Could not save the room to %s"), *Path);
		}
	});
	if (bBlocking)
	{
		SavePipe->WaitUntilEmpty();
	}
}
