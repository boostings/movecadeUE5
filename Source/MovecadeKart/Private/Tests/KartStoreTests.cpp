#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Store/KartCosmetics.h"
#include "Store/KartStore.h"
#include "Store/KartStoreSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Store;

namespace
{
	constexpr EAutomationTestFlags StoreTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A store on the mock, in memory unless slots are given; keeps a handle on the mock for its controls. */
	struct FHarness
	{
		FMockStoreBackend* Mock = nullptr;
		TUniquePtr<FKartStore> Store;
		TArray<TTuple<int32, EPurchaseResult, FString>> Finished;
		TArray<int32> Unlocked;

		explicit FHarness(const FString& Slot = FString(), const FString& MockSlot = FString(), bool bStart = true)
		{
			TUniquePtr<FMockStoreBackend> Backend = MakeUnique<FMockStoreBackend>(MockSlot);
			Mock = Backend.Get();
			Mock->SetNextOutcome(EMockOutcome::Succeed);
			Store = MakeUnique<FKartStore>(MoveTemp(Backend), Slot);
			Store->OnPurchaseFinished.AddLambda([this](int32 Def, EPurchaseResult R, const FString& D) { Finished.Add({ Def, R, D }); });
			Store->OnUnlocked.AddLambda([this](int32 Def) { Unlocked.Add(Def); });
			if (bStart)
			{
				Store->Start();
				Pump(1.0);
			}
		}

		/** Steps the mock's clock in frames. */
		void Pump(double Seconds)
		{
			for (double T = 0.0; T < Seconds; T += 0.05)
			{
				Store->Tick(0.05);
			}
		}

		bool LastIs(int32 Def, EPurchaseResult R) const
		{
			return Finished.Num() > 0 && Finished.Last().Get<0>() == Def && Finished.Last().Get<1>() == R;
		}
	};

	void DeleteSlot(const FString& Slot)
	{
		if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(Slot, 0);
		}
	}

	/** Installs a store for FKartCosmetics for the scope of a test. */
	struct FActiveStore
	{
		explicit FActiveStore(FKartStore* Store) { UKartStoreSubsystem::SetActiveForTests(Store); }
		~FActiveStore() { UKartStoreSubsystem::SetActiveForTests(nullptr); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartStoreCatalogTest, "MovecadeKart.Store.Catalog", StoreTestFlags)

bool FKartStoreCatalogTest::RunTest(const FString& Parameters)
{
	TSet<int32> Ids;
	TSet<FName> Keys;
	for (const FItem& I : Catalog())
	{
		TestFalse(*FString::Printf(TEXT("%d unique"), I.DefId), Ids.Contains(I.DefId));
		TestFalse(*FString::Printf(TEXT("%s unique"), *I.Key.ToString()), Keys.Contains(I.Key));
		Ids.Add(I.DefId);
		Keys.Add(I.Key);
		TestTrue(TEXT("Steam reserves itemdefids under 100 for nothing; we use 100..999"), I.DefId >= 100 && I.DefId < 1000);
		TestFalse(TEXT("named"), I.Name.IsEmpty());
		TestFalse(TEXT("described"), I.Description.IsEmpty());
		if (I.Source == ESource::Paid)
		{
			TestTrue(*FString::Printf(TEXT("%d has a US price"), I.DefId), I.PriceCents > 0 && I.PriceCents <= 500);
			TestTrue(*FString::Printf(TEXT("%d has a Steam price tier"), I.DefId), I.PriceCategory.StartsWith(TEXT("1;VLV")));
		}
		else
		{
			TestEqual(TEXT("free items cost nothing"), I.PriceCents, 0);
		}
		if (I.Source == ESource::Earned)
		{
			TestTrue(TEXT("earned items have a goal"), !I.Unlock.Stat.IsNone() && I.Unlock.Target > 0 && !I.Unlock.Achievement.IsNone());
		}
		if (I.Slot == ESlot::Bundle)
		{
			int32 Parts = 0;
			for (int32 Def : I.Contents)
			{
				const FItem* Part = FindItem(Def);
				TestTrue(TEXT("bundle parts are paid items"), Part && Part->Source == ESource::Paid && Part->Slot != ESlot::Bundle);
				Parts += Part ? Part->PriceCents : 0;
			}
			TestTrue(TEXT("a bundle is cheaper than its parts"), I.PriceCents < Parts);
		}
	}
	TestTrue(TEXT("a free unlock in each kind of party game"), FindItem(FName(TEXT("ball.alleycat"))) && FindItem(FName(TEXT("katana.bamboo"))) && FindItem(FName(TEXT("block.toy"))));
	TestEqual(TEXT("game stats"), GameStat(TEXT("bowling")), Stat::BowlingGames);
	TestEqual(TEXT("unknown game"), GameStat(TEXT("darts")), FName(NAME_None));
	TestEqual(TEXT("three gifts in the drop pool"), DropPool().Num(), 3);

	// The checked-in itemdefs.json is exactly what the catalog generates (regenerate: see Docs/Store/README.md).
	const FString Json = MakeItemDefsJson(UKartStoreSubsystem::DevAppId);
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("itemdefs json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
	TestEqual(TEXT("one entry per item plus the generator"), Root.IsValid() ? Root->GetArrayField(TEXT("items")).Num() : 0, Catalog().Num() + 1);
	FString OnDisk;
	const FString DocPath = FPaths::ProjectDir() / TEXT("Docs/Store/itemdefs.json");
	FFileHelper::LoadFileToString(OnDisk, *DocPath);
	OnDisk.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	if (OnDisk != Json)
	{
		FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("itemdefs.generated.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	TestTrue(TEXT("Docs/Store/itemdefs.json matches the catalog (new copy in Saved/itemdefs.generated.json)"), OnDisk == Json);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartStorePurchaseTest, "MovecadeKart.Store.Purchase", StoreTestFlags)

bool FKartStorePurchaseTest::RunTest(const FString& Parameters)
{
	FHarness H;
	const int32 Chrome = 101;
	TestTrue(TEXT("inventory arrived"), H.Store->HasInventory());
	TestTrue(TEXT("for sale"), H.Store->GetState(Chrome) == EItemState::ForSale);
	TestTrue(TEXT("priced"), H.Store->CanPurchase(Chrome));
	TestEqual(TEXT("price text"), H.Store->GetPriceText(Chrome).ToString(), FText::AsCurrencyBase(100, TEXT("USD")).ToString());
	TestEqual(TEXT("free reads Free"), H.Store->GetPriceText(150).ToString(), FString(TEXT("Free")));

	TestTrue(TEXT("checkout starts"), H.Store->Purchase(Chrome));
	TestTrue(TEXT("pending while the overlay is open"), H.Store->GetState(Chrome) == EItemState::Pending);
	TestFalse(TEXT("one checkout at a time"), H.Store->CanPurchase(102));
	H.Pump(1.0);
	TestTrue(TEXT("still pending after a second"), H.Store->GetState(Chrome) == EItemState::Pending);
	TestEqual(TEXT("no result yet"), H.Finished.Num(), 0);
	H.Pump(1.5);
	TestTrue(TEXT("success"), H.LastIs(Chrome, EPurchaseResult::Success));
	TestTrue(TEXT("owned"), H.Store->GetState(Chrome) == EItemState::Owned);
	TestTrue(TEXT("the backend has it"), H.Mock->GetServerInventory().Contains(Chrome));

	TestFalse(TEXT("can't buy twice"), H.Store->Purchase(Chrome));
	TestTrue(TEXT("already owned"), H.LastIs(Chrome, EPurchaseResult::AlreadyOwned));
	TestFalse(TEXT("free items aren't sold"), H.Store->Purchase(150));
	TestTrue(TEXT("unavailable"), H.LastIs(150, EPurchaseResult::Unavailable));
	TestFalse(TEXT("coming soon isn't sold"), H.Store->Purchase(701));
	TestTrue(TEXT("unavailable (soon)"), H.LastIs(701, EPurchaseResult::Unavailable));

	// A bundle grants everything inside.
	TestTrue(TEXT("bundle checkout"), H.Store->Purchase(901));
	H.Pump(2.5);
	TestTrue(TEXT("bundle success"), H.LastIs(901, EPurchaseResult::Success));
	TestTrue(TEXT("bundle owned"), H.Store->IsOwned(901));
	TestTrue(TEXT("its paints owned"), H.Store->IsOwned(102) && H.Store->IsOwned(106));
	TestEqual(TEXT("contents"), H.Store->NumOwnedContents(901), 6);
	TestFalse(TEXT("bundles aren't equipped"), H.Store->Equip(901));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartStoreCancelFailTest, "MovecadeKart.Store.CancelAndFailure", StoreTestFlags)

bool FKartStoreCancelFailTest::RunTest(const FString& Parameters)
{
	FHarness H;
	H.Mock->SetNextOutcome(EMockOutcome::Cancel);
	TestTrue(TEXT("checkout starts"), H.Store->Purchase(401));
	H.Pump(2.5);
	TestTrue(TEXT("cancelled"), H.LastIs(401, EPurchaseResult::Cancelled));
	TestTrue(TEXT("still for sale"), H.Store->GetState(401) == EItemState::ForSale);
	TestFalse(TEXT("nothing granted"), H.Mock->GetServerInventory().Contains(401));

	H.Mock->SetNextOutcome(EMockOutcome::Fail);
	TestTrue(TEXT("checkout starts again"), H.Store->Purchase(401));
	H.Pump(2.5);
	TestTrue(TEXT("failed"), H.LastIs(401, EPurchaseResult::Failed));
	TestFalse(TEXT("not owned"), H.Store->IsOwned(401));
	TestFalse(TEXT("not pending"), H.Store->IsPurchasing());

	// Offline: refused at once, with a reason, and nothing changes.
	H.Mock->SetOnline(false);
	H.Mock->SetNextOutcome(EMockOutcome::Succeed);
	TestFalse(TEXT("offline refuses"), H.Store->Purchase(401));
	TestTrue(TEXT("offline fails"), H.LastIs(401, EPurchaseResult::Failed));
	TestFalse(TEXT("can't purchase offline"), H.Store->CanPurchase(401));
	H.Mock->SetOnline(true);
	TestTrue(TEXT("back online"), H.Store->Purchase(401));
	H.Pump(2.5);
	TestTrue(TEXT("then it works"), H.Store->IsOwned(401));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartStoreEquipTest, "MovecadeKart.Store.Equip", StoreTestFlags)

bool FKartStoreEquipTest::RunTest(const FString& Parameters)
{
	FHarness H;
	const FActiveStore Active(H.Store.Get());
	TestFalse(TEXT("can't wear what you don't own"), H.Store->Equip(101));
	FKartLook Before = FKartLook::MakeVaried(0);
	Before.Outfit = EKartOutfit::Classic;
	FKartLook Look = Before;
	FKartCosmetics::ApplyToLook(Look, 0);
	TestTrue(TEXT("nothing equipped leaves the look alone"), Look.Paint.Equals(Before.Paint) && Look.Accessory == Before.Accessory);

	for (int32 Def : { 101, 102, 201, 302, 401 })
	{
		TestTrue(TEXT("buy"), H.Store->Purchase(Def));
		H.Pump(2.5);
	}
	TestTrue(TEXT("equip paint"), H.Store->Equip(101));
	TestTrue(TEXT("equipped"), H.Store->GetState(101) == EItemState::Equipped);
	TestTrue(TEXT("equip another paint"), H.Store->Equip(102));
	TestEqual(TEXT("one per slot"), H.Store->GetEquipped(ESlot::KartPaint), 102);
	TestTrue(TEXT("the first is just owned"), H.Store->GetState(101) == EItemState::Owned);
	TestTrue(TEXT("colourway"), H.Store->Equip(201));
	TestTrue(TEXT("shades"), H.Store->Equip(302));

	Look = Before;
	FKartCosmetics::ApplyToLook(Look, 0);
	TestTrue(TEXT("P1 wears the paint"), Look.Paint.Equals(FindItem(102)->Visual.Primary));
	TestTrue(TEXT("P1 wears the colourway"), !Look.bJerseyMatchesPaint && Look.Jersey.Equals(FindItem(201)->Visual.Primary) && Look.Shoes.Equals(FindItem(201)->Visual.Tertiary));
	TestTrue(TEXT("P1 wears the shades"), Look.Accessory == EKartAccessory::Glasses);
	FKartLook Other = Before;
	FKartCosmetics::ApplyToLook(Other, 1);
	TestTrue(TEXT("P2 keeps their own look"), Other.Paint.Equals(Before.Paint) && Other.Accessory == Before.Accessory);
	FKartLook Outfitted = Before;
	Outfitted.Outfit = EKartOutfit::TurboRacer;
	FKartCosmetics::ApplyToLook(Outfitted, 0);
	TestTrue(TEXT("an outfit overlay keeps its own clothes"), Outfitted.bJerseyMatchesPaint == Before.bJerseyMatchesPaint);
	TestEqual(TEXT("paint name"), FKartCosmetics::PaintName(0, FText::FromString(TEXT("x"))).ToString(), FindItem(102)->Name.ToString());
	TestEqual(TEXT("P2 paint name"), FKartCosmetics::PaintName(1, FText::FromString(TEXT("x"))).ToString(), FString(TEXT("x")));

	// Party skins: locals only.
	const FLinearColor Lane(0.1f, 0.2f, 0.3f);
	TestTrue(TEXT("no block skin"), FKartCosmetics::StackerBlockColor(true, Lane).Equals(Lane));
	TestTrue(TEXT("ball equip"), H.Store->Equip(401));
	TestTrue(TEXT("ball slot"), FKartCosmetics::Equipped(ESlot::BowlingBall) == FindItem(401));
	TestEqual(TEXT("no emote"), FKartCosmetics::VictoryEmote(), FName(NAME_None));
	TestTrue(TEXT("katana defaults pass through"), FKartCosmetics::KatanaMaterials(nullptr, true, {}).Num() == 0);

	H.Store->Unequip(ESlot::KartPaint);
	TestEqual(TEXT("unequipped"), H.Store->GetEquipped(ESlot::KartPaint), 0);
	Look = Before;
	FKartCosmetics::ApplyToLook(Look, 0);
	TestTrue(TEXT("stock paint again"), Look.Paint.Equals(Before.Paint));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartStoreUnlockTest, "MovecadeKart.Store.Unlocks", StoreTestFlags)

bool FKartStoreUnlockTest::RunTest(const FString& Parameters)
{
	FHarness H;
	const int32 Confetti = 150; // play 5 party games
	int32 Cur = -1, Target = -1;
	TestTrue(TEXT("earned items have progress"), H.Store->GetProgress(Confetti, Cur, Target));
	TestTrue(TEXT("0 of 5"), Cur == 0 && Target == 5);
	TestFalse(TEXT("paid items don't"), H.Store->GetProgress(101, Cur, Target));
	TestTrue(TEXT("locked"), H.Store->GetState(Confetti) == EItemState::Locked);
	TestFalse(TEXT("can't equip a locked item"), H.Store->Equip(Confetti));

	for (int32 i = 0; i < 4; ++i)
	{
		H.Store->AddStat(Stat::ArcadeGames);
	}
	H.Pump(1.0);
	H.Store->GetProgress(Confetti, Cur, Target);
	TestEqual(TEXT("4 of 5"), Cur, 4);
	TestTrue(TEXT("still locked"), H.Store->GetState(Confetti) == EItemState::Locked);
	H.Store->AddStat(Stat::ArcadeGames);
	TestTrue(TEXT("granting"), H.Store->GetState(Confetti) == EItemState::Pending);
	H.Pump(1.0);
	TestTrue(TEXT("unlocked"), H.Store->GetState(Confetti) == EItemState::Owned);
	TestTrue(TEXT("announced once"), H.Unlocked.Num() == 1 && H.Unlocked[0] == Confetti);
	H.Store->GetProgress(Confetti, Cur, Target);
	TestEqual(TEXT("progress caps at the goal"), Cur, 5);
	H.Store->AddStat(Stat::ArcadeGames);
	H.Pump(1.0);
	TestEqual(TEXT("no second grant"), H.Unlocked.Num(), 1);
	TestTrue(TEXT("equip the free paint"), H.Store->Equip(Confetti));

	// Offline grants wait, and land on the next refresh.
	H.Mock->SetOnline(false);
	H.Store->AddStat(Stat::RacesFinished);
	H.Pump(1.0);
	TestTrue(TEXT("offline: not yet"), H.Store->GetState(250) == EItemState::Locked);
	H.Mock->SetOnline(true);
	H.Store->Restore();
	H.Pump(2.0);
	TestTrue(TEXT("granted after the refresh"), H.Store->GetState(250) == EItemState::Owned);

	// Playtime drops: one gift per check while there are gifts left.
	H.Store->AddPlaytime(FKartStore::DropCheckSeconds + 1.0);
	H.Pump(1.0);
	TestTrue(TEXT("first gift"), H.Store->IsOwned(DropPool()[0]));
	TestTrue(TEXT("gift announced"), H.Unlocked.Contains(DropPool()[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartStoreRestoreTest, "MovecadeKart.Store.RestoreAndPersistence", StoreTestFlags)

bool FKartStoreRestoreTest::RunTest(const FString& Parameters)
{
	const FString Slot = TEXT("KartStoreAutomationTest");
	const FString MockSlot = TEXT("KartStoreAutomationTestMock");
	DeleteSlot(Slot);
	DeleteSlot(MockSlot);
	{
		FHarness H(Slot, MockSlot);
		TestTrue(TEXT("buy"), H.Store->Purchase(501));
		H.Pump(2.5);
		TestTrue(TEXT("buy 2"), H.Store->Purchase(103));
		H.Pump(2.5);
		TestTrue(TEXT("equip"), H.Store->Equip(501));
		H.Store->AddStat(Stat::BowlingGames, 2);
		H.Pump(0.5);
	}
	{
		// A fresh launch: everything comes back from disk before Steam (the mock) answers.
		FHarness H(Slot, MockSlot, false);
		H.Store->Start();
		TestTrue(TEXT("owned from the cache"), H.Store->IsOwned(501) && H.Store->IsOwned(103));
		TestTrue(TEXT("still equipped"), H.Store->IsEquipped(501));
		TestEqual(TEXT("stats kept"), H.Store->GetStat(Stat::BowlingGames), 2);
		H.Pump(1.0);
		TestTrue(TEXT("and after the refresh"), H.Store->IsOwned(501));

		// Lose the local save entirely (new PC): restore brings the purchases back.
		H.Store->ResetLocal();
		TestFalse(TEXT("forgotten locally"), H.Store->IsOwned(501));
		bool bRestored = false;
		H.Store->OnRestored.AddLambda([&bRestored](bool bOk) { bRestored = bOk; });
		H.Store->Restore();
		TestTrue(TEXT("restoring"), H.Store->IsRestoring());
		H.Pump(1.0);
		TestTrue(TEXT("restored"), bRestored && H.Store->IsOwned(501) && H.Store->IsOwned(103));

		// A refund takes the item away and takes it off.
		TestTrue(TEXT("equip again"), H.Store->Equip(103));
		H.Mock->Revoke(103);
		H.Pump(0.1);
		TestFalse(TEXT("refunded"), H.Store->IsOwned(103));
		TestEqual(TEXT("not worn after a refund"), H.Store->GetEquipped(ESlot::KartPaint), 0);
	}
	{
		FHarness H(Slot, MockSlot, false);
		H.Store->Start();
		TestFalse(TEXT("the refund persisted"), H.Store->IsOwned(103));
		TestTrue(TEXT("the rest persisted"), H.Store->IsOwned(501));
		H.Mock->Wipe();
	}
	DeleteSlot(Slot);
	DeleteSlot(MockSlot);
	return true;
}

#endif
