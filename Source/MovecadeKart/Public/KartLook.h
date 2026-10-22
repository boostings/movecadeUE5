#pragma once

#include "CoreMinimal.h"
#include "KartLook.generated.h"

/** Cosmetic mount (protocol avatar.vehicle). Handling is identical on every vehicle (spec 03 §6.1). */
UENUM(BlueprintType)
enum class EKartVehicle : uint8
{
	Kart,
	Moto,
	Coupe,
	Truck,
};

/** avatar.face: the six avatar4 identities. */
UENUM(BlueprintType)
enum class EKartFace : uint8
{
	Kai,
	Leo,
	Mira,
	Nia,
	Omar,
	Rose,
};

UENUM(BlueprintType)
enum class EKartBody : uint8
{
	Female,
	Male,
};

/** avatar.clothing: Classic is the stock jersey and pants; the others are the paid overlays. */
UENUM(BlueprintType)
enum class EKartOutfit : uint8
{
	Classic,
	ArcadeVarsity,
	TurboRacer,
	MidnightBowler,
	DojoGi,
	OcheAce,
	SkylineCrew,
	GlowHoodie,
};

/** avatar.hair: the eight in-GLB hair slots. */
UENUM(BlueprintType)
enum class EKartHair : uint8
{
	Short,
	Bangs,
	Puff,
	Tail,
	Crop,
	Curls,
	Bob,
	Bun,
};

UENUM(BlueprintType)
enum class EKartAccessory : uint8
{
	None,
	Glasses,
	Headband,
	/** The cap hides the hair (spec 03 §6.2). */
	Cap,
	// Modelled in code (Avatar/AvatarProps.h), worn on the head bone.
	Crown,
	PartyHat,
	Headphones,
	/** Hides the hair, like the cap. */
	Beanie,
};

/**
 * Everything that makes one kart look like its player: the vehicle and its paint, the driver
 * (face, body, outfit, colours) and the name on the plate. Colours are linear (sRGB-decoded).
 */
USTRUCT(BlueprintType)
struct MOVECADEKART_API FKartLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	FLinearColor Paint = FLinearColor(0.871f, 0.084f, 0.270f); // #f0508e

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	EKartVehicle Vehicle = EKartVehicle::Kart;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	EKartFace Face = EKartFace::Mira;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	EKartBody Body = EKartBody::Female;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	EKartOutfit Outfit = EKartOutfit::Classic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	EKartHair Hair = EKartHair::Short;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	EKartAccessory Accessory = EKartAccessory::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	FLinearColor Skin = FLinearColor::FromSRGBColor(FColor(0xd0, 0x8a, 0x68)); // tan

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	FLinearColor HairColor = FLinearColor::FromSRGBColor(FColor(0x3a, 0x24, 0x1c)); // brown

	/** The jersey; the web default is "seat", the kart paint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	bool bJerseyMatchesPaint = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	FLinearColor Jersey = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	FLinearColor Pants = FLinearColor::FromSRGBColor(FColor(0x2f, 0x38, 0x57)); // navy

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	FLinearColor Shoes = FLinearColor::FromSRGBColor(FColor(0xff, 0xf6, 0xf2)); // white

	/** Shown on the name plate; empty hides the plate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kart")
	FString Name;

	FLinearColor JerseyColor() const { return bJerseyMatchesPaint ? Paint : Jersey; }

	/**
	 * A deterministic, varied look for seat SeatIndex (0-based): bots and seats without an avatar get
	 * distinct faces, bodies, hair and colours, and the same seat always looks the same.
	 */
	static FKartLook MakeVaried(int32 SeatIndex);

	/**
	 * The look for a room seat: the protocol's avatar strings (kart|moto|coupe|truck, a face id,
	 * female|male) over MakeVaried(SeatIndex). Unknown or empty strings keep the varied choice; a face
	 * without a body takes the face's own body, as protocol/avatar.ts does.
	 */
	static FKartLook FromRoomAvatar(const FString& Vehicle, const FString& Face, const FString& Body, int32 SeatIndex);

	static bool ParseVehicle(const FString& Id, EKartVehicle& Out);
	static bool ParseFace(const FString& Id, EKartFace& Out);
	static bool ParseBody(const FString& Id, EKartBody& Out);
	static bool ParseOutfit(const FString& Id, EKartOutfit& Out);

	/** Asset ids: "mira", "female", "turbo-racer", "bob". */
	static const TCHAR* FaceId(EKartFace Face);
	static const TCHAR* BodyId(EKartBody Body);
	static const TCHAR* OutfitId(EKartOutfit Outfit);
	static const TCHAR* HairId(EKartHair Hair);
	static const TCHAR* VehicleId(EKartVehicle Vehicle);
	/** leo, kai and omar default to the male body (protocol/avatar.ts). */
	static EKartBody DefaultBodyFor(EKartFace Face);
};
