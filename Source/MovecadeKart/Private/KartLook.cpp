#include "KartLook.h"

namespace
{
	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
	}

	// appearanceColors.ts
	namespace Skin
	{
		const TCHAR* Porcelain = TEXT("f9e4d2");
		const TCHAR* Fair = TEXT("f3d0b8");
		const TCHAR* Light = TEXT("e0a888");
		const TCHAR* Olive = TEXT("c9a27c");
		const TCHAR* Tan = TEXT("d08a68");
		const TCHAR* Warm = TEXT("a86a48");
		const TCHAR* Brown = TEXT("8a5236");
		const TCHAR* Deep = TEXT("6b3d28");
	}
	namespace HairHex
	{
		const TCHAR* Black = TEXT("1a1214");
		const TCHAR* Brown = TEXT("3a241c");
		const TCHAR* Auburn = TEXT("7a3a1e");
		const TCHAR* Blonde = TEXT("d4b06a");
		const TCHAR* Silver = TEXT("c9c9d2");
		const TCHAR* Red = TEXT("a33c28");
		const TCHAR* Pink = TEXT("f070a8");
		const TCHAR* Blue = TEXT("3a6aa8");
		const TCHAR* Teal = TEXT("2a9a94");
	}
	namespace PantsHex
	{
		const TCHAR* Navy = TEXT("2f3857");
		const TCHAR* Black = TEXT("1c1c24");
		const TCHAR* Denim = TEXT("4a6a8a");
		const TCHAR* Cream = TEXT("e8dcc8");
	}
	namespace ShoeHex
	{
		const TCHAR* White = TEXT("fff6f2");
		const TCHAR* Black = TEXT("22222c");
		const TCHAR* Pink = TEXT("f070a8");
		const TCHAR* Cyan = TEXT("5ec8e5");
	}

	struct FVariedSeat
	{
		EKartFace Face;
		EKartBody Body;
		EKartHair Hair;
		const TCHAR* Skin;
		const TCHAR* HairColor;
		const TCHAR* Pants;
		const TCHAR* Shoes;
		EKartAccessory Accessory;
		EKartOutfit Outfit;
		EKartVehicle Vehicle;
	};

	// One row per grid seat (and beyond, cycling): every face once before any repeats, both bodies,
	// a spread of skin tones and hair, two of the paid outfits and every vehicle but the truck (it hides
	// the kart ahead of it from the chase camera).
	const FVariedSeat VariedSeats[] = {
		{ EKartFace::Mira, EKartBody::Female, EKartHair::Short, Skin::Tan, HairHex::Brown, PantsHex::Navy, ShoeHex::White, EKartAccessory::None, EKartOutfit::Classic, EKartVehicle::Kart },
		{ EKartFace::Leo, EKartBody::Male, EKartHair::Crop, Skin::Brown, HairHex::Black, PantsHex::Black, ShoeHex::Black, EKartAccessory::None, EKartOutfit::TurboRacer, EKartVehicle::Kart },
		{ EKartFace::Nia, EKartBody::Female, EKartHair::Puff, Skin::Deep, HairHex::Black, PantsHex::Denim, ShoeHex::Cyan, EKartAccessory::None, EKartOutfit::Classic, EKartVehicle::Coupe },
		{ EKartFace::Kai, EKartBody::Male, EKartHair::Short, Skin::Fair, HairHex::Blonde, PantsHex::Navy, ShoeHex::White, EKartAccessory::Glasses, EKartOutfit::Classic, EKartVehicle::Moto },
		{ EKartFace::Rose, EKartBody::Female, EKartHair::Tail, Skin::Porcelain, HairHex::Red, PantsHex::Cream, ShoeHex::Pink, EKartAccessory::None, EKartOutfit::SkylineCrew, EKartVehicle::Kart },
		{ EKartFace::Omar, EKartBody::Male, EKartHair::Curls, Skin::Warm, HairHex::Black, PantsHex::Black, ShoeHex::White, EKartAccessory::Headband, EKartOutfit::Classic, EKartVehicle::Coupe },
		{ EKartFace::Mira, EKartBody::Female, EKartHair::Bob, Skin::Olive, HairHex::Teal, PantsHex::Navy, ShoeHex::Black, EKartAccessory::None, EKartOutfit::GlowHoodie, EKartVehicle::Kart },
		{ EKartFace::Leo, EKartBody::Male, EKartHair::Bangs, Skin::Light, HairHex::Auburn, PantsHex::Denim, ShoeHex::White, EKartAccessory::Cap, EKartOutfit::Classic, EKartVehicle::Kart },
	};
}

FKartLook FKartLook::MakeVaried(int32 SeatIndex)
{
	const int32 Count = UE_ARRAY_COUNT(VariedSeats);
	const FVariedSeat& Row = VariedSeats[((SeatIndex % Count) + Count) % Count];
	FKartLook Look;
	Look.Face = Row.Face;
	Look.Body = Row.Body;
	Look.Hair = Row.Hair;
	Look.Skin = Hex(Row.Skin);
	Look.HairColor = Hex(Row.HairColor);
	Look.Pants = Hex(Row.Pants);
	Look.Shoes = Hex(Row.Shoes);
	Look.Accessory = Row.Accessory;
	Look.Outfit = Row.Outfit;
	Look.Vehicle = Row.Vehicle;
	return Look;
}

FKartLook FKartLook::FromRoomAvatar(const FString& Vehicle, const FString& Face, const FString& Body, int32 SeatIndex)
{
	FKartLook Look = MakeVaried(SeatIndex);
	// A phone that sent no vehicle rides the stock kart (protocol: absent = kart).
	Look.Vehicle = EKartVehicle::Kart;
	ParseVehicle(Vehicle, Look.Vehicle);
	if (ParseFace(Face, Look.Face))
	{
		Look.Body = DefaultBodyFor(Look.Face);
	}
	ParseBody(Body, Look.Body);
	return Look;
}

bool FKartLook::ParseVehicle(const FString& Id, EKartVehicle& Out)
{
	for (EKartVehicle V : { EKartVehicle::Kart, EKartVehicle::Moto, EKartVehicle::Coupe, EKartVehicle::Truck })
	{
		if (Id.Equals(VehicleId(V), ESearchCase::IgnoreCase))
		{
			Out = V;
			return true;
		}
	}
	return false;
}

bool FKartLook::ParseFace(const FString& Id, EKartFace& Out)
{
	for (EKartFace F : { EKartFace::Kai, EKartFace::Leo, EKartFace::Mira, EKartFace::Nia, EKartFace::Omar, EKartFace::Rose })
	{
		if (Id.Equals(FaceId(F), ESearchCase::IgnoreCase))
		{
			Out = F;
			return true;
		}
	}
	return false;
}

bool FKartLook::ParseBody(const FString& Id, EKartBody& Out)
{
	if (Id.Equals(TEXT("female"), ESearchCase::IgnoreCase)) { Out = EKartBody::Female; return true; }
	if (Id.Equals(TEXT("male"), ESearchCase::IgnoreCase)) { Out = EKartBody::Male; return true; }
	return false;
}

bool FKartLook::ParseOutfit(const FString& Id, EKartOutfit& Out)
{
	for (uint8 i = 0; i <= static_cast<uint8>(EKartOutfit::GlowHoodie); ++i)
	{
		if (Id.Equals(OutfitId(static_cast<EKartOutfit>(i)), ESearchCase::IgnoreCase))
		{
			Out = static_cast<EKartOutfit>(i);
			return true;
		}
	}
	return false;
}

const TCHAR* FKartLook::FaceId(EKartFace Face)
{
	switch (Face)
	{
	case EKartFace::Kai: return TEXT("kai");
	case EKartFace::Leo: return TEXT("leo");
	case EKartFace::Nia: return TEXT("nia");
	case EKartFace::Omar: return TEXT("omar");
	case EKartFace::Rose: return TEXT("rose");
	default: return TEXT("mira");
	}
}

const TCHAR* FKartLook::BodyId(EKartBody Body)
{
	return Body == EKartBody::Male ? TEXT("male") : TEXT("female");
}

const TCHAR* FKartLook::OutfitId(EKartOutfit Outfit)
{
	switch (Outfit)
	{
	case EKartOutfit::ArcadeVarsity: return TEXT("arcade-varsity");
	case EKartOutfit::TurboRacer: return TEXT("turbo-racer");
	case EKartOutfit::MidnightBowler: return TEXT("midnight-bowler");
	case EKartOutfit::DojoGi: return TEXT("dojo-gi");
	case EKartOutfit::OcheAce: return TEXT("oche-ace");
	case EKartOutfit::SkylineCrew: return TEXT("skyline-crew");
	case EKartOutfit::GlowHoodie: return TEXT("glow-hoodie");
	default: return TEXT("classic");
	}
}

const TCHAR* FKartLook::HairId(EKartHair Hair)
{
	switch (Hair)
	{
	case EKartHair::Bangs: return TEXT("bangs");
	case EKartHair::Puff: return TEXT("puff");
	case EKartHair::Tail: return TEXT("tail");
	case EKartHair::Crop: return TEXT("crop");
	case EKartHair::Curls: return TEXT("curls");
	case EKartHair::Bob: return TEXT("bob");
	case EKartHair::Bun: return TEXT("bun");
	default: return TEXT("short");
	}
}

const TCHAR* FKartLook::VehicleId(EKartVehicle Vehicle)
{
	switch (Vehicle)
	{
	case EKartVehicle::Moto: return TEXT("moto");
	case EKartVehicle::Coupe: return TEXT("coupe");
	case EKartVehicle::Truck: return TEXT("truck");
	default: return TEXT("kart");
	}
}

EKartBody FKartLook::DefaultBodyFor(EKartFace Face)
{
	return Face == EKartFace::Leo || Face == EKartFace::Kai || Face == EKartFace::Omar ? EKartBody::Male : EKartBody::Female;
}
