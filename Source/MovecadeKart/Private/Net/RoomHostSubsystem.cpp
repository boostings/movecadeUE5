#include "Net/RoomHostSubsystem.h"

#include "Dom/JsonObject.h"
#include "IWebSocket.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Net/RoomHostInternal.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tasks/Pipe.h"
#include "UnrealClient.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY(LogRoomHost);

using namespace MovecadeKart::Net;

namespace MovecadeKart::Net::RoomHost
{
	int64 UnixMs()
	{
		return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds();
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Object))
		{
			return nullptr;
		}
		return Object;
	}

	FString Describe(const FMcSeat& Seat)
	{
		return FString::Printf(TEXT("seat %d %s \"%s\""), Seat.Seat, *Seat.PlayerId, *Seat.Name);
	}
}

URoomHostSubsystem::URoomHostSubsystem() = default;

// Out of line: TUniquePtr<FPipe> needs the complete type to destroy it.
URoomHostSubsystem::~URoomHostSubsystem() = default;

URoomHostSubsystem::FDebugFlags URoomHostSubsystem::FDebugFlags::Parse(const TCHAR* CommandLine)
{
	FDebugFlags Flags;
#if !UE_BUILD_SHIPPING
	Flags.bAutoStart = FParse::Param(CommandLine, TEXT("RoomAutoStart")) || FParse::Value(CommandLine, TEXT("RoomAutoStart="), Flags.AutoStartDelay);
	Flags.bSelfTest = FParse::Param(CommandLine, TEXT("RoomSelfTest"));
	FString Shots;
	if (FParse::Value(CommandLine, TEXT("RoomShots="), Shots, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> Parts;
		Shots.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			Flags.ShotTimes.Add(FCString::Atod(*Part));
		}
		Flags.ShotTimes.Sort();
	}
#endif
	return Flags;
}

void URoomHostSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
	SavePipe = MakeUnique<UE::Tasks::FPipe>(TEXT("RoomHostSave"));

	const TCHAR* Cmd = FCommandLine::Get();
	ApplyServerSettings(Cmd);
	Debug = FDebugFlags::Parse(Cmd);
	StartTime = Now();
	HapticRefillTime = StartTime;
	LoadSaved(FParse::Param(Cmd, TEXT("RoomNew")));
	bInitialized = true;

	int32 PhoneSeat = INDEX_NONE;
	if (FParse::Param(Cmd, TEXT("KartLobby")) || FParse::Param(Cmd, TEXT("RoomOpen")) || FParse::Value(Cmd, TEXT("KartPhoneSeat="), PhoneSeat))
	{
		OpenRoom();
	}
}

void URoomHostSubsystem::ApplyServerSettings(const TCHAR* Cmd)
{
	// F-09: a shipped game only ever talks to production over TLS; nothing on the command line or in an ini can point
	// it (and the host token it sends) at another server.
#if UE_BUILD_SHIPPING
	constexpr EServerOverrides Overrides = EServerOverrides::Pinned;
#else
	constexpr EServerOverrides Overrides = EServerOverrides::Allowed;
#endif
	const FRoomServerSettings Configured{ PartyHost, JoinBaseUrl, Origin, bSecure };
	const FRoomServerSettings Settings = ResolveServerSettings(Configured, Cmd, Overrides);
	PartyHost = Settings.PartyHost;
	JoinBaseUrl = Settings.JoinBaseUrl;
	Origin = Settings.Origin;
	bSecure = Settings.bSecure;

	FParse::Value(Cmd, TEXT("RoomMap="), PendingMapId);
	if (FString Game; FParse::Value(Cmd, TEXT("RoomGame="), Game) && IsPlayableGame(Game))
	{
		GameId = Game;
	}
	UE_LOG(LogRoomHost, Display, TEXT("Room server %s://%s, phones join at %s"), bSecure ? TEXT("wss") : TEXT("ws"), *PartyHost, *JoinBaseUrl);
}

void URoomHostSubsystem::Deinitialize()
{
	bInitialized = false;
	bWantOpen = false;
	if (Socket.IsValid())
	{
		if (bWelcomed)
		{
			Save(/*bBlocking*/ true);
		}
		CloseSocket(TEXT("host shutting down"));
	}
	if (SavePipe)
	{
		SavePipe->WaitUntilEmpty();
	}
	Super::Deinitialize();
}

double URoomHostSubsystem::Now() const
{
	return FPlatformTime::Seconds();
}

TStatId URoomHostSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URoomHostSubsystem, STATGROUP_Tickables);
}

bool URoomHostSubsystem::IsHost() const
{
	return bWelcomed && !SessionId.IsEmpty() && Room.HostSessionId == SessionId;
}

bool URoomHostSubsystem::IsLocalPartyHost(const FString& Host)
{
	return IsLocalHost(Host);
}

void URoomHostSubsystem::Tick(float DeltaTime)
{
	const double T = Now();
	if (ReconnectAt > 0.0 && T >= ReconnectAt && bWantOpen)
	{
		ReconnectAt = -1.0;
		Connect();
	}
	if (Connection == EMcConnection::Open)
	{
		UpdateLobbyCountdown(T);
		if (T - LastSaveTime >= RoomHost::SaveIntervalSeconds)
		{
			Save();
		}
	}
	if (Debug.bSelfTest && SelfTestStep >= 0)
	{
		RunSelfTest(T);
	}
	TakeDueScreenshot(T);
}

void URoomHostSubsystem::TakeDueScreenshot(double T)
{
	if (ShotCount >= Debug.ShotTimes.Num() || T - StartTime < Debug.ShotTimes[ShotCount])
	{
		return;
	}
	++ShotCount;
	const FString Path = FPaths::Combine(FPaths::ScreenShotDir(), FString::Printf(TEXT("RoomHost_%d.png"), ShotCount));
	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	UE_LOG(LogRoomHost, Display, TEXT("Screenshot with UI requested: %s"), *FPaths::ConvertRelativePathToFull(Path));
}
