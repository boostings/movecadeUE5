// The phone stores' shared logic and the mock's mobile flows (MovecadeKart.Store.Mobile*): product ids and tiers,
// pending / cancelled / restored / refunded purchases, the free items a phone keeps, the Google Play signature check,
// and the words the store shows on each storefront.
#include "Algo/AllOf.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Store/GooglePlayReceipt.h"
#include "Store/KartStore.h"
#include "Store/KartStoreCatalog.h"
#include "Store/MobileStore.h"
#include "Store/MockStoreBackend.h"
#include "UI/Screens/Store/KartStorePresenter.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace MovecadeKart::Store;

namespace
{
	constexpr EAutomationTestFlags MobileTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A store on the mock playing a phone storefront, in memory unless slots are given. */
	struct FMobileHarness
	{
		FMockStoreBackend* Mock = nullptr;
		TUniquePtr<FKartStore> Store;
		TArray<TTuple<int32, EPurchaseResult, FString>> Finished;
		TArray<int32> Approved;

		explicit FMobileHarness(EStorefront Front, const FString& Slot = FString(), const FString& MockSlot = FString())
		{
			TUniquePtr<FMockStoreBackend> Backend = MakeUnique<FMockStoreBackend>(MockSlot);
			Mock = Backend.Get();
			Mock->SetNextOutcome(EMockOutcome::Succeed);
			Mock->SetOnline(true);
			Mock->SetStorefront(Front);
			Store = MakeUnique<FKartStore>(MoveTemp(Backend), Slot);
			Store->OnPurchaseFinished.AddLambda([this](int32 Def, EPurchaseResult R, const FString& D) { Finished.Add({ Def, R, D }); });
			Store->OnApproved.AddLambda([this](int32 Def) { Approved.Add(Def); });
			Store->Start();
			Pump(1.0);
		}

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

		void Buy(int32 Def, EMockOutcome Outcome)
		{
			Mock->SetNextOutcome(Outcome);
			Store->Purchase(Def);
			Pump(2.5);
		}
	};

	void DeleteSlot(const FString& Slot)
	{
		if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
		{
			UGameplayStatics::DeleteGameInSlot(Slot, 0);
		}
	}

	bool IsProductIdChar(TCHAR C)
	{
		return (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('.') || C == TEXT('_');
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMobileProductsTest, "MovecadeKart.Store.MobileProducts", MobileTestFlags)

bool FKartMobileProductsTest::RunTest(const FString& Parameters)
{
	TSet<FString> Ids;
	for (const FItem& I : Catalog())
	{
		const FString Id = ProductId(I);
		if (!IsSellable(I))
		{
			TestTrue(*FString::Printf(TEXT("%s: free and coming-soon items have no product"), *I.Key.ToString()), Id.IsEmpty());
			if (I.IsFree())
			{
				TestEqual(TEXT("free on phones too"), I.MobilePriceCents, 0);
			}
			continue;
		}
		TestFalse(*FString::Printf(TEXT("%s unique"), *Id), Ids.Contains(Id));
		Ids.Add(Id);
		// Both stores: lowercase letters, digits, dots and underscores, starting with a letter; Apple allows 100 characters.
		TestTrue(*FString::Printf(TEXT("%s is a valid product id"), *Id), Id.Len() <= 100 && FChar::IsAlpha(Id[0]) && Algo::AllOf(Id, IsProductIdChar));
		TestTrue(TEXT("round trip"), FindItemByProductId(Id) == &I);
		// App Store Connect: display name up to 30 characters, description up to 45.
		TestTrue(*FString::Printf(TEXT("%s name fits the App Store"), *Id), I.Name.ToString().Len() <= 30);
		TestTrue(*FString::Printf(TEXT("%s description fits the App Store"), *Id), StoreShortDescription(I).ToString().Len() <= 45);
		// The tier matches Steam's: $0.99 next to $1.00, $1.49 next to $1.50 and so on.
		TestEqual(*FString::Printf(TEXT("%s mobile tier"), *Id), I.MobilePriceCents, I.PriceCents - 1);
		if (I.Slot == ESlot::Bundle)
		{
			int32 Parts = 0;
			for (int32 Def : I.Contents)
			{
				Parts += FindItem(Def)->MobilePriceCents;
			}
			TestTrue(TEXT("a bundle is cheaper than its parts on phones too"), I.MobilePriceCents < Parts);
		}
	}
	TestEqual(TEXT("the shipping product list"), SellableProductIds().Num(), Ids.Num());
	TestNull(TEXT("unknown product"), FindItemByProductId(TEXT("movecade.kart.paint.nope")));
	TestNull(TEXT("free items aren't products"), FindItemByProductId(TEXT("movecade.kart.paint.confetti")));
	TestNull(TEXT("another app's id"), FindItemByProductId(TEXT("paint.chrome")));

	// What the stores say you own becomes def ids: bundles as their contents, unknown ids dropped.
	const TSet<int32> Defs = Mobile::DefsForProducts({ TEXT("movecade.kart.paint.chrome"), TEXT("movecade.kart.bundle.party"), TEXT("com.someone.else") });
	TestTrue(TEXT("an item is itself"), Defs.Contains(101));
	TestTrue(TEXT("a bundle is its contents"), Defs.Contains(401) && Defs.Contains(602) && !Defs.Contains(902));
	TestEqual(TEXT("1 + 8 defs"), Defs.Num(), 9);

	// The store's own price text wins over our formatting (it is what the purchase sheet shows).
	const FPrice Euro = Mobile::MakeStorePrice(109, TEXT("EUR"), FText::FromString(TEXT("1,09 €")));
	TestEqual(TEXT("store text shown as is"), FormatPrice(Euro).ToString(), FString(TEXT("1,09 €")));
	TestEqual(TEXT("without text, the amount"), FormatPrice(Mobile::MakeStorePrice(99, TEXT("USD"), FText::GetEmpty())).ToString(), FText::AsCurrencyBase(99, TEXT("USD")).ToString());

	// The checked-in product table is exactly what the catalog generates.
	const FString Json = MakeMobileProductsJson();
	TSharedPtr<FJsonObject> Root;
	TestTrue(TEXT("products json parses"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid());
	TestEqual(TEXT("one entry per product"), Root.IsValid() ? Root->GetArrayField(TEXT("products")).Num() : 0, Ids.Num());
	FString OnDisk;
	FFileHelper::LoadFileToString(OnDisk, *(FPaths::ProjectDir() / TEXT("Docs/Store/mobile-products.json")));
	OnDisk.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	if (OnDisk != Json)
	{
		FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / TEXT("mobile-products.generated.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	TestTrue(TEXT("Docs/Store/mobile-products.json matches the catalog (new copy in Saved/mobile-products.generated.json)"), OnDisk == Json);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMobileFlowsTest, "MovecadeKart.Store.MobileFlows", MobileTestFlags)

bool FKartMobileFlowsTest::RunTest(const FString& Parameters)
{
	for (const EStorefront Front : { EStorefront::AppStore, EStorefront::GooglePlay })
	{
		const FString Name = StorefrontId(Front);
		FMobileHarness H(Front);
		TestEqual(*(Name + TEXT(": the store plays the phone storefront")), H.Store->GetStorefront(), Front);
		TestEqual(*(Name + TEXT(": launch never syncs (no sign-in sheet)")), H.Mock->NumRestoreSyncs(), 0);
		TestEqual(*(Name + TEXT(": mobile price tier")), H.Store->GetPriceText(101).ToString(), FText::AsCurrencyBase(99, TEXT("USD")).ToString());
		TestEqual(*(Name + TEXT(": headwear tier")), H.Store->GetPriceText(301).ToString(), FText::AsCurrencyBase(149, TEXT("USD")).ToString());

		// Cancelled: nothing charged, still for sale.
		H.Buy(401, EMockOutcome::Cancel);
		TestTrue(*(Name + TEXT(": cancelled")), H.LastIs(401, EPurchaseResult::Cancelled));
		TestTrue(*(Name + TEXT(": still for sale")), H.Store->GetState(401) == EItemState::ForSale);

		// Pending (Ask to Buy): not owned, can't be bought again meanwhile, then approved and announced.
		H.Buy(101, EMockOutcome::Pending);
		TestTrue(*(Name + TEXT(": pending")), H.LastIs(101, EPurchaseResult::Pending));
		TestTrue(*(Name + TEXT(": awaiting approval")), H.Store->GetState(101) == EItemState::AwaitingApproval);
		TestFalse(*(Name + TEXT(": not owned while pending")), H.Store->IsOwned(101) || H.Store->IsConfirmed(101));
		TestFalse(*(Name + TEXT(": can't wear a pending item")), H.Store->Equip(101));
		TestFalse(*(Name + TEXT(": no second checkout while waiting")), H.Store->Purchase(101));
		TestTrue(*(Name + TEXT(": refused as pending")), H.LastIs(101, EPurchaseResult::Pending));
		TestTrue(*(Name + TEXT(": approved")), H.Mock->ResolvePending(101, true));
		TestTrue(*(Name + TEXT(": owned once approved")), H.Store->GetState(101) == EItemState::Owned && H.Store->IsConfirmed(101));
		TestTrue(*(Name + TEXT(": approval announced")), H.Approved.Num() == 1 && H.Approved[0] == 101);

		// Pending bundle approved: it counts once all its contents arrived.
		H.Buy(902, EMockOutcome::Pending);
		TestTrue(*(Name + TEXT(": bundle waiting")), H.Store->GetState(902) == EItemState::AwaitingApproval);
		H.Mock->ResolvePending(902, true);
		TestTrue(*(Name + TEXT(": bundle contents owned")), H.Store->IsOwned(902) && H.Store->IsOwned(503));
		TestTrue(*(Name + TEXT(": bundle approval announced")), H.Approved.Contains(902));

		// Pending then declined: the stores say nothing; Restore purchases checks again and puts it back on sale.
		H.Buy(201, EMockOutcome::Pending);
		H.Mock->ResolvePending(201, false);
		TestTrue(*(Name + TEXT(": still waiting (nobody told us)")), H.Store->GetState(201) == EItemState::AwaitingApproval);
		H.Store->Restore();
		H.Pump(1.0);
		TestEqual(*(Name + TEXT(": the player's restore syncs")), H.Mock->NumRestoreSyncs(), 1);
		TestTrue(*(Name + TEXT(": declined goes back on sale")), H.Store->GetState(201) == EItemState::ForSale);

		// Restored: a new phone (local save gone) gets the purchases back from the store.
		H.Store->ResetLocal();
		TestFalse(*(Name + TEXT(": forgotten locally")), H.Store->IsOwned(101));
		H.Store->Restore();
		H.Pump(1.0);
		TestTrue(*(Name + TEXT(": restored")), H.Store->IsConfirmed(101) && H.Store->IsConfirmed(503));

		// Refunded: the store takes it away (Transaction.updates / the purchase list), and it comes off.
		TestTrue(*(Name + TEXT(": equip")), H.Store->Equip(101));
		H.Mock->Revoke(101);
		TestFalse(*(Name + TEXT(": refunded")), H.Store->IsOwned(101));
		TestEqual(*(Name + TEXT(": not worn after a refund")), H.Store->GetEquipped(ESlot::KartPaint), 0);
		TestTrue(*(Name + TEXT(": the rest stays")), H.Store->IsOwned(503));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMobileFreeItemsTest, "MovecadeKart.Store.MobileFreeItems", MobileTestFlags)

bool FKartMobileFreeItemsTest::RunTest(const FString& Parameters)
{
	const FString Slot = TEXT("KartStoreFreeAutomationTest");
	DeleteSlot(Slot);
	FDateTime Now(2027, 3, 1, 12, 0, 0);
	auto Clock = [&Now]() { return Now; };
	{
		Mobile::FLocalFreeItems Free(Slot, Clock);
		TestTrue(TEXT("an earned item is granted"), Free.GrantEarned(150));
		TestFalse(TEXT("a paid item never is"), Free.GrantEarned(101));
		TestFalse(TEXT("nor a gift"), Free.GrantEarned(DropPool()[0]));
		TestTrue(TEXT("owned"), Free.GetOwned().Contains(150) && Free.GetOwned().Num() == 1);

		// About two hours of play (12 checks of 10 minutes) per gift.
		for (int32 i = 1; i < Mobile::FLocalFreeItems::ChecksPerDrop; ++i)
		{
			TestEqual(TEXT("no gift before two hours"), Free.PlaytimeCheck().Num(), 0);
		}
		const TArray<int32> First = Free.PlaytimeCheck();
		TestTrue(TEXT("the first gift"), First.Num() == 1 && First[0] == DropPool()[0]);

		// At most one a day, however long you play.
		for (int32 i = 0; i < 3 * Mobile::FLocalFreeItems::ChecksPerDrop; ++i)
		{
			TestEqual(TEXT("one gift per day"), Free.PlaytimeCheck().Num(), 0);
		}
		Now += FTimespan::FromHours(24.0);
		TestTrue(TEXT("the next day: the second gift"), Free.PlaytimeCheck() == TArray<int32>{ DropPool()[1] });
	}
	{
		// A fresh launch reads it back; three gifts in total, then no more.
		Mobile::FLocalFreeItems Free(Slot, Clock);
		TestTrue(TEXT("kept on disk"), Free.GetOwned().Contains(150) && Free.GetOwned().Contains(DropPool()[1]));
		Now += FTimespan::FromHours(48.0);
		for (int32 i = 0; i < Mobile::FLocalFreeItems::ChecksPerDrop; ++i)
		{
			Free.PlaytimeCheck();
		}
		TestTrue(TEXT("the third gift"), Free.GetOwned().Contains(DropPool()[2]));
		Now += FTimespan::FromDays(30.0);
		for (int32 i = 0; i < 4 * Mobile::FLocalFreeItems::ChecksPerDrop; ++i)
		{
			TestEqual(TEXT("the pool is small and known: no fourth"), Free.PlaytimeCheck().Num(), 0);
		}
		Free.Wipe();
		TestEqual(TEXT("wiped"), Free.GetOwned().Num(), 0);
	}
	DeleteSlot(Slot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartPlayReceiptTest, "MovecadeKart.Store.PlayReceipt", MobileTestFlags)

bool FKartPlayReceiptTest::RunTest(const FString& Parameters)
{
	using namespace GooglePlay;
	if (!IsVerifierAvailable())
	{
		AddInfo(TEXT("This platform builds without the Google Play verifier: skipped."));
		return true;
	}
	// A throwaway RSA-2048 test key and purchases signed with it (openssl dgst -sha1 -sign), like Google signs them.
	const FString Key = TEXT("MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA0me+4+2J3YB13OCQxmhzJN2FZElsmrBJjEUX38pYY+P+CYT+om0CtzgntSU4XLP/W8e8yHcckDuDOfzo8NK+zktOa/n45Kre1NT7UPJ9O7cfZV1FKnzVXKyW1ePZFskVhCVxJb1kkE3HL+l2Gj4f9PoITktriamp6VrNEG+3ep+7qwoWANzn/ymGoaHWmAx1pBUZgpl0eTg4RuAhfFPBzoamdLsZhiZcRTvuQmSMnM6lD486sEmpBQK8RAmPtgLxOY9izKZ7To8lywhnJuj0XAiqxUkf9bfNn2cw+1GvgQbP8OLfTUqX1I0+8YpFLP2bxBznAjwsLAATGRi7YeUcQwIDAQAB");
	const FString OtherKey = TEXT("MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuifr5dD+8CJPhwNNiYkiRflPQhQaRCpx/GMu4NaBREdkYl2dNlen1zQyEw0BJe7udjoohMNHWOG6BhfwEx34txlPZtyTohMh+vFjiZ+RFLXo+Aq/OoYNq88ZJs5a40uAaadeFW8nXltnUO+kp6sWOLWWYCUuY7gB8E6hYdCnqy+6SWlsfOD/kKIgdMFhSvCSyOCrLC8Fxy0HVM+vk36GhwTYRlZjiuvafZNaKg9nWTF2xv/FeaFlmUQ67DphVbuNCvuGb3LZPrd1FiR4sXsnujx26wUXtkwezHshDUJApOXeXXWA38iabgTqen4pyxey+yepRfhulZG+5tuLSdgL+wIDAQAB");
	// paint.chrome, purchased, not acknowledged yet.
	const FString Chrome = TEXT("eyJvcmRlcklkIjoiR1BBLjAwMDAtMTExMS0yMjIyLTMzMzMzIiwicGFja2FnZU5hbWUiOiJjb20ubW92ZWNhZGUua2FydCIsInByb2R1Y3RJZCI6Im1vdmVjYWRlLmthcnQucGFpbnQuY2hyb21lIiwicHVyY2hhc2VUaW1lIjoxNzY3MjI1NjAwMDAwLCJwdXJjaGFzZVN0YXRlIjowLCJwdXJjaGFzZVRva2VuIjoidG9rLWNocm9tZS0xIiwicXVhbnRpdHkiOjEsImFja25vd2xlZGdlZCI6ZmFsc2V9");
	const FString ChromeSig = TEXT("yuFy8pi+iPDswjBKZ0LvZQ25AAqOQAwAjKdCKuZzmpgiz1tF23r20+4Ws0QMymCaxzM5L2gNHyjlbbJguQF6dBtLMDVWaCCp9mcWxCgEcQaFzkuAOkSiI3AVvW1+VeV24QB821h2UXEksH4JHli2ENOeknp8ByLxvd/SgfIQQIfY8bFUecCEOqP4TrdAVUduZeZZWUXhRV4rQpGY7gcNS+M5ixn90w+1GXQY5V+0n7Cl8tEEh+YVA0qoonvnkeGFIBdvOw9CCQXIk1HTBHfBAs0B9y9U49Dn+lh1A326VP3Tzgn0HWcVXsmOrRprqzdQXcORFMu0fByYtpSJ3SKs5w==");
	// bundle.paints in the newer "productIds" list, already acknowledged.
	const FString Bundle = TEXT("eyJvcmRlcklkIjoiR1BBLjAwMDAtMTExMS0yMjIyLTQ0NDQ0IiwicGFja2FnZU5hbWUiOiJjb20ubW92ZWNhZGUua2FydCIsInByb2R1Y3RJZHMiOlsibW92ZWNhZGUua2FydC5idW5kbGUucGFpbnRzIl0sInB1cmNoYXNlVGltZSI6MTc2NzIyNTYwMDAwMCwicHVyY2hhc2VTdGF0ZSI6MCwicHVyY2hhc2VUb2tlbiI6InRvay1idW5kbGUtMSIsInF1YW50aXR5IjoxLCJhY2tub3dsZWRnZWQiOnRydWV9");
	const FString BundleSig = TEXT("yxJQn8BLb9lNsq/hHXlEYysKf5iw9vQmPxHz4bmodEbVoski4LF2P0o/M1L2GkSg6a7Nt8qDH7MLesYi+uDQK7qBMKa3nKLzOUVbVQZJdra+z/rq6/Hvr4et74Qe9CScEl0sik5wBxQ6oyhoo3WNj9K1XIBYyUTwGDbQGQVnt1fVxRCxA7EJFKDhitVoUJy8/9Qpcdk1ZyqsFTQLfbCj8F6je6SNv/grG4IkzWiop3cJAQkK405L8zijRbgyz6deer3/IOgNrXYz7Im2dD3mBUnmVCzUjmRBkf5kbIEn0NcOF5mr+AocYik2WuCBI9h7S+RNu0v5W2kCD/XMrVGMrg==");
	// hat.cap, PENDING (purchaseState 4).
	const FString Cap = TEXT("eyJvcmRlcklkIjoiR1BBLjAwMDAtMTExMS0yMjIyLTU1NTU1IiwicGFja2FnZU5hbWUiOiJjb20ubW92ZWNhZGUua2FydCIsInByb2R1Y3RJZCI6Im1vdmVjYWRlLmthcnQuaGF0LmNhcCIsInB1cmNoYXNlVGltZSI6MTc2NzIyNTYwMDAwMCwicHVyY2hhc2VTdGF0ZSI6NCwicHVyY2hhc2VUb2tlbiI6InRvay1jYXAtMSIsInF1YW50aXR5IjoxLCJhY2tub3dsZWRnZWQiOmZhbHNlfQ==");
	const FString CapSig = TEXT("bdGfAoXQGLQY/AO2Na5FQ1YN/rIH0pjGu8GaOsj3MIQE6+BJkbyxNSDtIZG3c/qe39vZf3ZHLpyTdgEzgQzJEXX+FG7IMygUZSD5DaLMP0Tyx/+MLlVzIwXo8b3JToPHXK7M/WtRBBv2N9xyj471OVmwNcz2Ul7e4go85UOFMMi5ku3HcCWWmY0r80ylIY3ATXYF5230ISRUnuc/N1SSnmOhJ98lonfUqVO5QOd+3AvSRAE4bGca7vxwrsk6kAIB03KkztrKJ8dUyf6a8pao+EwN7HjTtbKOmVqHkutthBCeake/15G9xvdVped+JTNUU0pvaknkKVNCgFjAog40SQ==");
	const FString Package = TEXT("com.movecade.kart");
	auto Bytes = [](const FString& Base64) { TArray<uint8> Out; FBase64::Decode(Base64, Out); return Out; };

	FPurchase P;
	TestEqual(TEXT("a signed purchase is valid"), CheckPurchase(Bytes(Chrome), ChromeSig, Key, Package, P), EReceiptCheck::Valid);
	TestTrue(TEXT("its fields"), P.ProductIds == TArray<FString>{ TEXT("movecade.kart.paint.chrome") } && P.PurchaseToken == TEXT("tok-chrome-1") && !P.bAcknowledged);
	TestEqual(TEXT("the productIds list"), CheckPurchase(Bytes(Bundle), BundleSig, Key, Package, P), EReceiptCheck::Valid);
	TestTrue(TEXT("bundle id and acknowledged"), P.ProductIds == TArray<FString>{ TEXT("movecade.kart.bundle.paints") } && P.bAcknowledged);
	TestEqual(TEXT("a pending purchase is not owned yet"), CheckPurchase(Bytes(Cap), CapSig, Key, Package, P), EReceiptCheck::Pending);

	// Forgeries: another key, another purchase's signature, one edited byte, no signature, no key.
	TestEqual(TEXT("signed with another key"), CheckPurchase(Bytes(Chrome), ChromeSig, OtherKey, Package, P), EReceiptCheck::BadSignature);
	TestEqual(TEXT("another purchase's signature"), CheckPurchase(Bytes(Chrome), BundleSig, Key, Package, P), EReceiptCheck::BadSignature);
	// One byte changed (a digit of the order id): any edit must break the signature.
	TArray<uint8> Edited = Bytes(Chrome);
	const int32 At = Edited.IndexOfByKey(static_cast<uint8>('0'));
	TestTrue(TEXT("found a byte to edit"), At != INDEX_NONE);
	Edited[At] = '1';
	TestEqual(TEXT("an edited purchase"), CheckPurchase(Edited, ChromeSig, Key, Package, P), EReceiptCheck::BadSignature);
	TestEqual(TEXT("no signature"), CheckPurchase(Bytes(Chrome), FString(), Key, Package, P), EReceiptCheck::BadSignature);
	TestEqual(TEXT("garbage signature"), CheckPurchase(Bytes(Chrome), TEXT("not base64 at all!"), Key, Package, P), EReceiptCheck::BadSignature);
	TestEqual(TEXT("no license key: fail closed"), CheckPurchase(Bytes(Chrome), ChromeSig, FString(), Package, P), EReceiptCheck::NoKey);
	TestEqual(TEXT("a broken key"), CheckPurchase(Bytes(Chrome), ChromeSig, TEXT("AAAA"), Package, P), EReceiptCheck::BadSignature);
	TestEqual(TEXT("signed for another app"), CheckPurchase(Bytes(Chrome), ChromeSig, Key, TEXT("com.other.game"), P), EReceiptCheck::WrongPackage);

	// The engine's wrapper: {"receiptData": base64(purchase JSON), "signature": ...}.
	const FString Info = FString::Printf(TEXT("{\"receiptData\":\"%s\",\"signature\":\"%s\"}"), *Chrome, *ChromeSig);
	TestEqual(TEXT("ValidationInfo verifies"), CheckValidationInfo(Info, Key, Package, P), EReceiptCheck::Valid);
	TestEqual(TEXT("empty ValidationInfo (pending in the engine)"), CheckValidationInfo(FString(), Key, Package, P), EReceiptCheck::Malformed);
	TestEqual(TEXT("not JSON"), CheckValidationInfo(TEXT("{{{"), Key, Package, P), EReceiptCheck::Malformed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartMobileWordingTest, "MovecadeKart.Store.MobileWording", MobileTestFlags)

bool FKartMobileWordingTest::RunTest(const FString& Parameters)
{
	using namespace KartStoreUI;
	const FItem& Chrome = *FindItem(FName(TEXT("paint.chrome")));
	const EDialogPhase Phases[] = { EDialogPhase::Confirm, EDialogPhase::Waiting, EDialogPhase::Success, EDialogPhase::Cancelled, EDialogPhase::Failed, EDialogPhase::Pending };

	// Everything a phone player can read in the store, in every state the mock can reach.
	auto AllWords = [&](FMobileHarness& H)
	{
		TArray<FString> Words;
		const FKartStore* Store = H.Store.Get();
		Words.Add(StatusFor(Store).Text.ToString());
		Words.Add(RestoreStartedText(Store).ToString());
		Words.Add(RestoreDoneText(Store, true).ToString());
		Words.Add(RestoreDoneText(Store, false).ToString());
		Words.Add(UnconfirmedChip(Store).ToString());
		Words.Add(UnconfirmedNote(Store).ToString());
		for (const FItem& I : Catalog())
		{
			Words.Add(PrimaryFor(Store, I).Label.ToString());
		}
		for (const EDialogPhase Phase : Phases)
		{
			Words.Add(DialogTitle(Phase, Chrome, StorefrontOf(Store)).ToString());
			for (const FText& Line : DialogLines(Phase, Store, Chrome, TEXT("network error")))
			{
				Words.Add(Line.ToString());
			}
			for (const FText& Button : DialogButtons(Phase, Store, Chrome))
			{
				Words.Add(Button.ToString());
			}
		}
		return Words;
	};

	for (const EStorefront Front : { EStorefront::AppStore, EStorefront::GooglePlay })
	{
		FMobileHarness H(Front);
		TArray<FString> Words = AllWords(H);
		H.Mock->SetNextOutcome(EMockOutcome::Pending);
		H.Store->Purchase(102);
		Words.Append(AllWords(H)); // while the checkout is open
		H.Pump(2.5);
		Words.Append(AllWords(H)); // awaiting approval
		H.Mock->SetOnline(false);
		Words.Append(AllWords(H)); // offline
		for (const FString& W : Words)
		{
			TestFalse(*FString::Printf(TEXT("%s: no Steam on a phone: \"%s\""), StorefrontId(Front), *W), W.Contains(TEXT("Steam")));
		}
		TestTrue(*FString::Printf(TEXT("%s: the store is named"), StorefrontId(Front)), Words.ContainsByPredicate([Front](const FString& W)
		{
			return W.Contains(Front == EStorefront::AppStore ? TEXT("App Store") : TEXT("Google Play"));
		}));
		TestEqual(TEXT("waiting for approval reads so"), PrimaryFor(H.Store.Get(), *FindItem(102)).Label.ToString(), FString(TEXT("Waiting for approval")));
	}

	// Desktop keeps its Steam words.
	FMobileHarness Desk(EStorefront::Steam);
	TestEqual(TEXT("Steam restore toast"), RestoreStartedText(Desk.Store.Get()).ToString(), FString(TEXT("Checking Steam for your items...")));
	TestEqual(TEXT("Steam chip"), UnconfirmedChip(Desk.Store.Get()).ToString(), FString(TEXT("NEEDS STEAM")));
	TestTrue(TEXT("Steam overlay line"), DialogLines(EDialogPhase::Waiting, Desk.Store.Get(), Chrome, FString())[0].ToString().Contains(TEXT("Steam overlay")));
	TestTrue(TEXT("a pending checkout lands on its own phase"), PhaseAfter(EPurchaseResult::Pending) == EDialogPhase::Pending);
	TestEqual(TEXT("pending has one button"), NumDialogButtons(EDialogPhase::Pending, true), 1);
	return true;
}

#endif
