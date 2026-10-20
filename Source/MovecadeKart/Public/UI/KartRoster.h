#pragma once

#include "CoreMinimal.h"
#include "KartLook.h"
#include "KartRoster.generated.h"

/** One local player's picks on the racers screen. Indices are into FKartRoster's tables. */
USTRUCT(BlueprintType)
struct MOVECADEKART_API FKartRacerChoice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	int32 Character = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	EKartOutfit Outfit = EKartOutfit::Classic;

	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	EKartVehicle Vehicle = EKartVehicle::Kart;

	UPROPERTY(EditAnywhere, Config, Category = "Kart")
	int32 Paint = 0;

	bool operator==(const FKartRacerChoice& Other) const = default;
};

struct MOVECADEKART_API FKartCharacterInfo
{
	FText Name;
	/** The FKartLook::MakeVaried row this character's driver comes from. */
	int32 LookRow = 0;
};

struct MOVECADEKART_API FKartPaintInfo
{
	FText Name;
	FLinearColor Color;
};

/**
 * What the racers screen offers: the six avatar4 drivers (styled like their FKartLook::MakeVaried rows),
 * the outfits, the four vehicles and a paint palette whose first four entries are the v2 seat paints.
 */
class MOVECADEKART_API FKartRoster
{
public:
	static const TArray<FKartCharacterInfo>& Characters();
	static const TArray<FKartPaintInfo>& Paints();
	static int32 NumOutfits() { return static_cast<int32>(EKartOutfit::GlowHoodie) + 1; }
	static int32 NumVehicles() { return static_cast<int32>(EKartVehicle::Truck) + 1; }
	static FText OutfitName(EKartOutfit Outfit);
	static FText VehicleName(EKartVehicle Vehicle);

	/** The default picks for local player Index (0..3): a different driver and the seat's v2 paint. */
	static FKartRacerChoice DefaultChoice(int32 PlayerIndex);
	/** Clamps every index into range. */
	static FKartRacerChoice Sanitize(const FKartRacerChoice& Choice);
	/** The full kart look for a choice (driver styling from the character's MakeVaried row). */
	static FKartLook MakeLook(const FKartRacerChoice& Choice, const FString& Name);
	static FLinearColor PaintColor(int32 PaintIndex);
	/** "Mira", for result rows: the face of a look. */
	static FText CharacterNameOf(const FKartLook& Look);
};
