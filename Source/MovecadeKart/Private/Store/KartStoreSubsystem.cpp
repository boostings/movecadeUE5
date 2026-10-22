#include "Store/KartStoreSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Store/MockStoreBackend.h"
#include "Store/SteamInventoryBackend.h"

DEFINE_LOG_CATEGORY_STATIC(LogKartStoreSubsystem, Log, All);

using namespace MovecadeKart::Store;

namespace
{
	// The one store FKartCosmetics reads, so the games ask for cosmetics without a world context. A deliberate
	// session-wide pointer: the subsystem sets it for the game instance's life, the store tests swap in their own.
	FKartStore* GLiveStore = nullptr;
	FKartStore* GTestStore = nullptr;
	bool GTestOverride = false;

	/** The longest tick the store steps (a hitch must not skip a checkout delay or a playtime check). */
	constexpr double MaxTickSeconds = 0.5;

	/** Development: -KartStoreEquip=paint.chrome,ball.galaxy lists items to grant (mock only) and wear, for screenshots. */
	TArray<int32> ParseDevEquip()
	{
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
		return DevEquip;
	}

	/** Development: -KartStoreBackend=mock skips Steam. Shipping always asks Steam first. */
	bool WantsMockBackend()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		FString Choice;
		return FParse::Value(FCommandLine::Get(), TEXT("KartStoreBackend="), Choice) && Choice.Equals(TEXT("mock"), ESearchCase::IgnoreCase);
#endif
	}

	/** Steam Inventory when the build has it and Steam runs, else the offline mock. */
	TUniquePtr<IKartStoreBackend> MakeBackend(const TArray<int32>& DevEquip)
	{
		if (!WantsMockBackend())
		{
			if (TUniquePtr<IKartStoreBackend> Steam = MakeSteamBackend(UKartStoreSubsystem::DevAppId))
			{
				return Steam;
			}
		}
		TUniquePtr<FMockStoreBackend> Mock = MakeUnique<FMockStoreBackend>();
#if UE_BUILD_SHIPPING
		// A shipped game never hands out paid items without Steam: offline, the cache shows what is owned, and only
		// free items are worn (FKartStore wears paid items once a backend confirms them).
		Mock->SetOnline(false);
#else
		Mock->DevGrant(DevEquip);
#endif
		return Mock;
	}
}

UKartStoreSubsystem* UKartStoreSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UKartStoreSubsystem>() : nullptr;
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
	const TArray<int32> DevEquip = ParseDevEquip();
	TUniquePtr<IKartStoreBackend> Backend = MakeBackend(DevEquip);
	UE_LOG(LogKartStoreSubsystem, Display, TEXT("Store backend: %s"), *Backend->GetName().ToString());
	Store = MakeUnique<FKartStore>(MoveTemp(Backend), TEXT("KartStore"));
	Store->Start();
	if (DevEquip.Num() > 0)
	{
		WearOnceRestored(DevEquip);
	}
	GLiveStore = Store.Get();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UKartStoreSubsystem::Tick));
}

void UKartStoreSubsystem::WearOnceRestored(const TArray<int32>& DefIds)
{
	// The first inventory refresh brings the granted items: wear them then, once.
	TSharedRef<FDelegateHandle> Once = MakeShared<FDelegateHandle>();
	*Once = Store->OnRestored.AddWeakLambda(this, [this, DefIds, Once](bool)
	{
		for (int32 Def : DefIds)
		{
			Store->Equip(Def);
		}
		UE_LOG(LogKartStoreSubsystem, Display, TEXT("KartStoreEquip: %d items worn"), DefIds.Num());
		Store->OnRestored.Remove(*Once);
	});
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
		const double Dt = FMath::Clamp(static_cast<double>(DeltaSeconds), 0.0, MaxTickSeconds);
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
