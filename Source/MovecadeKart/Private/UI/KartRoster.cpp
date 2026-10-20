#include "UI/KartRoster.h"

#define LOCTEXT_NAMESPACE "KartRoster"

namespace
{
	FLinearColor Hex(const TCHAR* Code)
	{
		return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
	}
}

const TArray<FKartCharacterInfo>& FKartRoster::Characters()
{
	// MakeVaried rows 0..5 are the six faces, each once (KartLook.cpp VariedSeats).
	static const TArray<FKartCharacterInfo> List = {
		{ LOCTEXT("Mira", "Mira"), 0 },
		{ LOCTEXT("Leo", "Leo"), 1 },
		{ LOCTEXT("Nia", "Nia"), 2 },
		{ LOCTEXT("Kai", "Kai"), 3 },
		{ LOCTEXT("Rose", "Rose"), 4 },
		{ LOCTEXT("Omar", "Omar"), 5 },
	};
	return List;
}

const TArray<FKartPaintInfo>& FKartRoster::Paints()
{
	// The first four are the v2 seat paints (games/kart/kartPaint.ts), in seat order.
	static const TArray<FKartPaintInfo> List = {
		{ LOCTEXT("Pink", "Bubblegum"), Hex(TEXT("f0508e")) },
		{ LOCTEXT("Cyan", "Lagoon"), Hex(TEXT("5ec8e5")) },
		{ LOCTEXT("Gold", "Sunshine"), Hex(TEXT("f7c948")) },
		{ LOCTEXT("Violet", "Grape"), Hex(TEXT("a67cf0")) },
		{ LOCTEXT("Lime", "Lime"), Hex(TEXT("7ed957")) },
		{ LOCTEXT("Tangerine", "Tangerine"), Hex(TEXT("ff8a3d")) },
		{ LOCTEXT("Cherry", "Cherry"), Hex(TEXT("e8394d")) },
		{ LOCTEXT("Snow", "Snow"), Hex(TEXT("f4f1ec")) },
		{ LOCTEXT("Midnight", "Midnight"), Hex(TEXT("2e3a6e")) },
	};
	return List;
}

FText FKartRoster::OutfitName(EKartOutfit Outfit)
{
	switch (Outfit)
	{
	case EKartOutfit::ArcadeVarsity: return LOCTEXT("ArcadeVarsity", "Arcade Varsity");
	case EKartOutfit::TurboRacer: return LOCTEXT("TurboRacer", "Turbo Racer");
	case EKartOutfit::MidnightBowler: return LOCTEXT("MidnightBowler", "Midnight Bowler");
	case EKartOutfit::DojoGi: return LOCTEXT("DojoGi", "Dojo Gi");
	case EKartOutfit::OcheAce: return LOCTEXT("OcheAce", "Oche Ace");
	case EKartOutfit::SkylineCrew: return LOCTEXT("SkylineCrew", "Skyline Crew");
	case EKartOutfit::GlowHoodie: return LOCTEXT("GlowHoodie", "Glow Hoodie");
	default: return LOCTEXT("Classic", "Classic Jersey");
	}
}

FText FKartRoster::VehicleName(EKartVehicle Vehicle)
{
	switch (Vehicle)
	{
	case EKartVehicle::Moto: return LOCTEXT("Moto", "Moto");
	case EKartVehicle::Coupe: return LOCTEXT("Coupe", "Coupe");
	case EKartVehicle::Truck: return LOCTEXT("Truck", "Truck");
	default: return LOCTEXT("Kart", "Kart");
	}
}

FKartRacerChoice FKartRoster::DefaultChoice(int32 PlayerIndex)
{
	FKartRacerChoice Choice;
	const FKartLook Row = FKartLook::MakeVaried(PlayerIndex);
	Choice.Character = PlayerIndex % Characters().Num();
	Choice.Outfit = Row.Outfit;
	Choice.Vehicle = Row.Vehicle;
	Choice.Paint = PlayerIndex % 4;
	return Choice;
}

FKartRacerChoice FKartRoster::Sanitize(const FKartRacerChoice& Choice)
{
	FKartRacerChoice Out = Choice;
	Out.Character = FMath::Clamp(Choice.Character, 0, Characters().Num() - 1);
	Out.Paint = FMath::Clamp(Choice.Paint, 0, Paints().Num() - 1);
	Out.Outfit = static_cast<EKartOutfit>(FMath::Clamp(static_cast<int32>(Choice.Outfit), 0, NumOutfits() - 1));
	Out.Vehicle = static_cast<EKartVehicle>(FMath::Clamp(static_cast<int32>(Choice.Vehicle), 0, NumVehicles() - 1));
	return Out;
}

FLinearColor FKartRoster::PaintColor(int32 PaintIndex)
{
	const TArray<FKartPaintInfo>& List = Paints();
	return List[((PaintIndex % List.Num()) + List.Num()) % List.Num()].Color;
}

FKartLook FKartRoster::MakeLook(const FKartRacerChoice& InChoice, const FString& Name)
{
	const FKartRacerChoice Choice = Sanitize(InChoice);
	FKartLook Look = FKartLook::MakeVaried(Characters()[Choice.Character].LookRow);
	Look.Outfit = Choice.Outfit;
	Look.Vehicle = Choice.Vehicle;
	Look.Paint = PaintColor(Choice.Paint);
	Look.Name = Name;
	return Look;
}

FText FKartRoster::CharacterNameOf(const FKartLook& Look)
{
	for (const FKartCharacterInfo& Character : Characters())
	{
		if (FKartLook::MakeVaried(Character.LookRow).Face == Look.Face)
		{
			return Character.Name;
		}
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
