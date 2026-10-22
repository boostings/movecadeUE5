#include "Games/ArcadeTestHooks.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

FArcadeTestHooks FArcadeTestHooks::Parse(const TCHAR* CommandLine)
{
	FArcadeTestHooks Hooks;
#if !UE_BUILD_SHIPPING
	if (!CommandLine)
	{
		return Hooks;
	}
	FString Shots;
	if (FParse::Value(CommandLine, TEXT("KartShots="), Shots, /*bShouldStopOnSeparator*/ false))
	{
		TArray<FString> Times;
		Shots.ParseIntoArray(Times, TEXT(","));
		for (const FString& Time : Times)
		{
			Hooks.ShotTimes.Add(FCString::Atof(*Time));
		}
	}
	FParse::Value(CommandLine, TEXT("KartQuitAfter="), Hooks.QuitAfterSeconds);
#endif
	return Hooks;
}

void FArcadeTestHooks::Schedule(AActor& Owner, TArray<FTimerHandle>& OutHandles) const
{
	UWorld* World = Owner.GetWorld();
	if (!World || IsEmpty())
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	for (const float Time : ShotTimes)
	{
		FTimerHandle& Handle = OutHandles.AddDefaulted_GetRef();
		Timers.SetTimer(Handle, FTimerDelegate::CreateWeakLambda(&Owner, [&Owner]()
		{
			// Saved/Screenshots/<platform>/ScreenShotNNNNN.png, with the UI.
			if (APlayerController* PC = Owner.GetWorld()->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("Shot showui"));
			}
		}), Time, /*bLoop*/ false);
	}
	if (QuitAfterSeconds > 0.f)
	{
		FTimerHandle& Handle = OutHandles.AddDefaulted_GetRef();
		Timers.SetTimer(Handle, FTimerDelegate::CreateWeakLambda(&Owner, [&Owner]()
		{
			UKismetSystemLibrary::QuitGame(&Owner, nullptr, EQuitPreference::Quit, false);
		}), QuitAfterSeconds, /*bLoop*/ false);
	}
}
