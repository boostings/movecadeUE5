#include "Capture/KartCaptureSubsystem.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartCapture, Log, All);

namespace
{
	/** How long a capture runs without -KartCaptureFor (seconds of world time). */
	constexpr double DefaultCaptureSeconds = 10.0;
}

FString FKartCaptureSettings::CaptureRoot()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Capture"));
}

FString FKartCaptureSettings::ConfineFolder(const FString& Requested)
{
	const FString Root = CaptureRoot();
	if (Requested.IsEmpty())
	{
		return Root;
	}
	// A name ("harbor_cinematic") lands under the root; a full path must already be inside it.
	FString Folder = FPaths::IsRelative(Requested) ? Root / Requested : Requested;
	Folder = FPaths::ConvertRelativePathToFull(Folder);
	if (!FPaths::CollapseRelativeDirectories(Folder) || !FPaths::IsUnderDirectory(Folder, Root))
	{
		UE_LOG(LogKartCapture, Warning, TEXT("-KartCapture=%s is outside %s: capturing into %s"), *Requested, *Root, *Root);
		return Root;
	}
	return Folder;
}

FKartCaptureSettings FKartCaptureSettings::Parse(const TCHAR* CommandLine)
{
	FKartCaptureSettings Out;
	FString Requested;
	FParse::Value(CommandLine, TEXT("KartCapture="), Requested, /*bShouldStopOnSeparator*/ false);
	Out.Folder = ConfineFolder(Requested);
	FParse::Value(CommandLine, TEXT("KartCaptureFrom="), Out.From);
	FParse::Value(CommandLine, TEXT("KartCaptureFor="), Out.For);
	Out.bShowUI = !FParse::Param(CommandLine, TEXT("KartCaptureNoUI"));
	if (Out.For <= 0.0)
	{
		Out.For = DefaultCaptureSeconds;
	}
	return Out;
}

bool FKartCaptureSettings::IsRequested(const TCHAR* CommandLine)
{
#if UE_BUILD_SHIPPING
	return false; // a development tool: a shipped game never writes frames
#else
	FString Ignored;
	return CommandLine && (FParse::Param(CommandLine, TEXT("KartCapture")) || FParse::Value(CommandLine, TEXT("KartCapture="), Ignored));
#endif
}

FString FKartCaptureSettings::FramePath(int32 Frame) const
{
	return FString::Printf(TEXT("%s/frame_%05d.png"), *Folder, Frame);
}

bool UKartCaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && FKartCaptureSettings::IsRequested(FCommandLine::Get());
}

void UKartCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Settings = FKartCaptureSettings::Parse(FCommandLine::Get());
	if (!IFileManager::Get().MakeDirectory(*Settings.Folder, /*Tree*/ true))
	{
		UE_LOG(LogKartCapture, Error, TEXT("Can't create the capture folder %s"), *Settings.Folder);
	}
	UE_LOG(LogKartCapture, Display, TEXT("Capturing %.1f s from %.1f s into %s"), Settings.For, Settings.From, *Settings.Folder);
}

void UKartCaptureSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!Settings.Covers(GetWorld()->GetTimeSeconds()))
	{
		return;
	}
	// One request per rendered frame; the viewport resolves it at the end of the frame with the UI drawn. (The file
	// name is built per frame, but only while capturing, which writes a PNG per frame anyway.)
	FScreenshotRequest::RequestScreenshot(Settings.FramePath(Frame++), Settings.bShowUI, /*bAddFilenameSuffix*/ false);
}

TStatId UKartCaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKartCaptureSubsystem, STATGROUP_Tickables);
}
