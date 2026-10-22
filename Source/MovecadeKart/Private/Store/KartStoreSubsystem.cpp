#include "Store/KartStoreSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartStoreSubsystem, Log, All);

using namespace MovecadeKart::Store;

namespace
{
	FKartStore* GLiveStore = nullptr;
	FKartStore* GTestStore = nullptr;
	bool GTestOverride = false;
}

UKartStoreSubsystem* UKartStoreSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UKartStoreSubsystem>() : nullptr;
}

FKartStore* UKartStoreSubsystem::Active()
{
	return GTestOverride ? GTestStore : GLiveStore;
}

void UKartStoreSubsystem::SetActiveForTests(FKartStore* InStore)
{
	GTestStore = InStore;
	GTestOverride = InStore != nullptr;
}

void UKartStoreSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Choice;
	FParse::Value(FCommandLine::Get(), TEXT("KartStoreBackend="), Choice);
	TUniquePtr<IKartStoreBackend> Backend;
	if (!Choice.Equals(TEXT("mock"), ESearchCase::IgnoreCase))
	{
		Backend = MakeSteamBackend(DevAppId);
	}
	// Development: -KartStoreEquip=paint.chrome,ball.galaxy grants (mock only) and wears items, for screenshots and checks.
	TArray<int32> DevEquip;
#if !UE_BUILD_SHIPPING
	FString EquipList;
	if (FParse::Value(FCommandLine::Get(), TEXT("KartStoreEquip="), EquipList, false))
	{
		TArray<FString> Keys;
		EquipList.ParseIntoArray(Keys, TEXT(","));
		for (const FString& Key : Keys)
		{
			if (const FItem* Item = FindItem(FName(*Key.TrimStartAndEnd())))
			{
				DevEquip.Add(Item->DefId);
			}
		}
	}
#endif
	if (!Backend)
	{
		TUniquePtr<FMockStoreBackend> Mock = MakeUnique<FMockStoreBackend>();
#if UE_BUILD_SHIPPING
		// A shipped game never hands out paid items without Steam: offline shows what the cache says is owned.
		Mock->SetOnline(false);
#else
		Mock->DevGrant(DevEquip);
#endif
		Backend = MoveTemp(Mock);
	}
	UE_LOG(LogKartStoreSubsystem, Display, TEXT("Store backend: %s"), *Backend->GetName().ToString());
	Store = MakeUnique<FKartStore>(MoveTemp(Backend), TEXT("KartStore"));
	Store->Start();
	if (DevEquip.Num() > 0)
	{
		// Wear them once the inventory has them.
		TSharedRef<FDelegateHandle> Once = MakeShared<FDelegateHandle>();
		*Once = Store->OnRestored.AddLambda([this, DevEquip, Once](bool)
		{
			for (int32 Def : DevEquip)
			{
				Store->Equip(Def);
			}
			UE_LOG(LogKartStoreSubsystem, Display, TEXT("KartStoreEquip: %d items worn"), DevEquip.Num());
			Store->OnRestored.Remove(*Once);
		});
	}
	GLiveStore = Store.Get();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UKartStoreSubsystem::Tick));
}

void UKartStoreSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (GLiveStore == Store.Get())
	{
		GLiveStore = nullptr;
	}
	if (Store)
	{
		Store->Save();
	}
	Store.Reset();
	Super::Deinitialize();
}

bool UKartStoreSubsystem::Tick(float DeltaSeconds)
{
	if (Store)
	{
		// Real seconds (the ticker runs paused or not): checkout delays and play time are wall clock.
		const double Dt = FMath::Clamp(static_cast<double>(DeltaSeconds), 0.0, 0.5);
		Store->Tick(Dt);
		Store->AddPlaytime(Dt);
	}
	return true;
}

void UKartStoreSubsystem::NoteArcadeGame(const UObject* WorldContext, const FString& GameId, bool bHumans)
{
	UKartStoreSubsystem* Self = Get(WorldContext);
	if (!Self || !Self->Store || !bHumans)
	{
		return;
	}
	Self->Store->AddStat(Stat::ArcadeGames);
	Self->Store->AddStat(GameStat(GameId));
}

void UKartStoreSubsystem::NoteRaceFinished(const UObject* WorldContext, bool bHumans, bool bHumanWon)
{
	UKartStoreSubsystem* Self = Get(WorldContext);
	if (!Self || !Self->Store || !bHumans)
	{
		return;
	}
	Self->Store->AddStat(Stat::RacesFinished);
	if (bHumanWon)
	{
		Self->Store->AddStat(Stat::RacesWon);
	}
}
