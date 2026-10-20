#include "Capture/KartCaptureSubsystem.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartCapture, Log, All);

bool UKartCaptureSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& FCString::Strifind(FCommandLine::Get(), TEXT("-KartCapture")) != nullptr;
}

void UKartCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("KartCapture="), Folder, /*bShouldStopOnSeparator*/ false);
	FParse::Value(Cmd, TEXT("KartCaptureFrom="), From);
	FParse::Value(Cmd, TEXT("KartCaptureFor="), For);
	bShowUI = !FParse::Param(Cmd, TEXT("KartCaptureNoUI"));
	if (Folder.IsEmpty())
	{
		Folder = FPaths::ProjectSavedDir() / TEXT("Capture");
	}
	if (For <= 0.0)
	{
		For = 10.0;
	}
	IFileManager::Get().MakeDirectory(*Folder, /*Tree*/ true);
	UE_LOG(LogKartCapture, Display, TEXT("Capturing %.1f s from %.1f s into %s"), For, From, *Folder);
}

void UKartCaptureSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < From || Now >= From + For)
	{
		return;
	}
	// One request per rendered frame; the viewport resolves it at the end of the frame with the UI drawn.
	const FString File = FString::Printf(TEXT("%s/frame_%05d.png"), *Folder, Frame++);
	FScreenshotRequest::RequestScreenshot(File, bShowUI, /*bAddFilenameSuffix*/ false);
}

TStatId UKartCaptureSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKartCaptureSubsystem, STATGROUP_Tickables);
}
