#include "KartLook.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartLookVariedTest, "MovecadeKart.Art.VariedLooks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartLookVariedTest::RunTest(const FString& Parameters)
{
	// Deterministic per seat, and the four grid seats never share a face.
	TSet<EKartFace> Faces;
	for (int32 Seat = 0; Seat < 4; ++Seat)
	{
		const FKartLook A = FKartLook::MakeVaried(Seat);
		const FKartLook B = FKartLook::MakeVaried(Seat);
		TestTrue(TEXT("same seat, same face"), A.Face == B.Face && A.Body == B.Body && A.Hair == B.Hair && A.Vehicle == B.Vehicle);
		TestTrue(TEXT("same seat, same skin"), A.Skin.Equals(B.Skin));
		Faces.Add(A.Face);
	}
	TestEqual(TEXT("four distinct faces"), Faces.Num(), 4);
	TestTrue(TEXT("seat 0 rides the stock kart"), FKartLook::MakeVaried(0).Vehicle == EKartVehicle::Kart);
	TestTrue(TEXT("negative seats wrap"), FKartLook::MakeVaried(-1).Face == FKartLook::MakeVaried(7).Face);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartLookRoomTest, "MovecadeKart.Art.RoomAvatar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartLookRoomTest::RunTest(const FString& Parameters)
{
	const FKartLook Truck = FKartLook::FromRoomAvatar(TEXT("truck"), TEXT("omar"), FString(), 2);
	TestTrue(TEXT("vehicle"), Truck.Vehicle == EKartVehicle::Truck);
	TestTrue(TEXT("face"), Truck.Face == EKartFace::Omar);
	TestTrue(TEXT("omar's own body"), Truck.Body == EKartBody::Male);

	const FKartLook Chosen = FKartLook::FromRoomAvatar(TEXT("moto"), TEXT("leo"), TEXT("female"), 0);
	TestTrue(TEXT("explicit body wins"), Chosen.Body == EKartBody::Female && Chosen.Face == EKartFace::Leo);

	// protocol/avatar.ts: an absent vehicle rides the kart; garbage keeps the seat's varied driver.
	const FKartLook Empty = FKartLook::FromRoomAvatar(FString(), TEXT("nobody"), TEXT("robot"), 3);
	const FKartLook Varied = FKartLook::MakeVaried(3);
	TestTrue(TEXT("absent vehicle = kart"), Empty.Vehicle == EKartVehicle::Kart);
	TestTrue(TEXT("unknown face keeps varied"), Empty.Face == Varied.Face && Empty.Body == Varied.Body);

	EKartOutfit Outfit = EKartOutfit::Classic;
	TestTrue(TEXT("outfit id"), FKartLook::ParseOutfit(TEXT("turbo-racer"), Outfit) && Outfit == EKartOutfit::TurboRacer);
	TestEqual(TEXT("asset ids"), FString(FKartLook::OutfitId(EKartOutfit::GlowHoodie)), FString(TEXT("glow-hoodie")));
	return true;
}

#endif
