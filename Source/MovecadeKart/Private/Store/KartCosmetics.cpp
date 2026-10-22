#include "Store/KartCosmetics.h"

#include "Art/KartMaterials.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Store/KartStore.h"
#include "Store/KartStoreSubsystem.h"

using namespace MovecadeKart::Store;

const FItem* FKartCosmetics::Equipped(ESlot Slot)
{
	const FKartStore* Store = UKartStoreSubsystem::Active();
	return Store ? Store->GetEquippedItem(Slot) : nullptr;
}

void FKartCosmetics::ApplyToLook(FKartLook& Look, int32 LocalPlayer)
{
	if (LocalPlayer != 0)
	{
		return;
	}
	if (const FItem* Paint = Equipped(ESlot::KartPaint))
	{
		Look.Paint = Paint->Visual.Primary;
	}
	// The stock clothes only show with the Classic Jersey (an outfit overlay covers them).
	if (const FItem* Way = Equipped(ESlot::Colourway); Way && Look.Outfit == EKartOutfit::Classic)
	{
		Look.bJerseyMatchesPaint = false;
		Look.Jersey = Way->Visual.Primary;
		Look.Pants = Way->Visual.Secondary;
		Look.Shoes = Way->Visual.Tertiary;
	}
	if (const FItem* Hat = Equipped(ESlot::Headwear))
	{
		Look.Accessory = Hat->Visual.Accessory;
	}
}

FText FKartCosmetics::PaintName(int32 LocalPlayer, const FText& Fallback)
{
	const FItem* Paint = LocalPlayer == 0 ? Equipped(ESlot::KartPaint) : nullptr;
	return Paint ? Paint->Name : Fallback;
}

FLinearColor FKartCosmetics::PaintColor(int32 LocalPlayer, const FLinearColor& Fallback)
{
	const FItem* Paint = LocalPlayer == 0 ? Equipped(ESlot::KartPaint) : nullptr;
	return Paint ? Paint->Visual.Primary : Fallback;
}

void FKartCosmetics::ApplyBowlingBall(UMaterialInstanceDynamic* BallMaterial, bool bLocalBowler, const FLinearColor& DefaultColor)
{
	if (!BallMaterial)
	{
		return;
	}
	// The ball is one material shared by every bowler: set every parameter each turn so a skin never sticks to a bot.
	// The stock finish is whatever the game built the material with, read once before any skin touches it.
	struct FStock { float Roughness = 0.f, Metallic = 0.f, Emissive = 0.f; };
	static TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, FStock> Stocks;
	FStock* Stock = Stocks.Find(BallMaterial);
	if (!Stock)
	{
		for (auto It = Stocks.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}
		Stock = &Stocks.Add(BallMaterial);
		BallMaterial->GetScalarParameterValue(FName(TEXT("Roughness")), Stock->Roughness);
		BallMaterial->GetScalarParameterValue(FName(TEXT("Metallic")), Stock->Metallic);
		BallMaterial->GetScalarParameterValue(FName(TEXT("Emissive")), Stock->Emissive);
	}
	const FItem* Skin = bLocalBowler ? Equipped(ESlot::BowlingBall) : nullptr;
	BallMaterial->SetVectorParameterValue(TEXT("Color"), Skin ? Skin->Visual.Primary : DefaultColor);
	BallMaterial->SetScalarParameterValue(TEXT("Roughness"), Skin ? Skin->Visual.Roughness : Stock->Roughness);
	BallMaterial->SetScalarParameterValue(TEXT("Metallic"), Skin ? Skin->Visual.Metallic : Stock->Metallic);
	BallMaterial->SetScalarParameterValue(TEXT("Emissive"), Skin ? Skin->Visual.Emissive : Stock->Emissive);
}

TArray<UMaterialInterface*> FKartCosmetics::KatanaMaterials(UObject* Outer, bool bLocal, const TArray<UMaterialInterface*>& Default)
{
	const FItem* Skin = bLocal ? Equipped(ESlot::Katana) : nullptr;
	// The katana's slots (SamuraiArt KatanaMaterials): blade, edge, guard, collar, wrap, ray skin.
	if (!Skin || Default.Num() < 6)
	{
		return Default;
	}
	using namespace MovecadeKart::Art;
	const FVisual& V = Skin->Visual;
	TArray<UMaterialInterface*> Out = Default;
	Out[0] = Plain(Outer, V.Primary, V.Roughness, V.Metallic);
	Out[1] = V.Emissive > 0.f ? static_cast<UMaterialInterface*>(Glow(Outer, V.Secondary * V.Emissive)) : Plain(Outer, V.Secondary, V.Roughness * 0.5, V.Metallic);
	Out[2] = Plain(Outer, V.Tertiary, 0.3, 1.0);
	Out[4] = Plain(Outer, V.Tertiary * 0.55f, 0.85);
	return Out;
}

FLinearColor FKartCosmetics::StackerBlockColor(bool bLocal, const FLinearColor& Default)
{
	const FItem* Skin = bLocal ? Equipped(ESlot::StackerBlock) : nullptr;
	return Skin ? Skin->Visual.Primary : Default;
}

FName FKartCosmetics::VictoryEmote()
{
	const FItem* Emote = Equipped(ESlot::VictoryEmote);
	return Emote ? Emote->Key : NAME_None;
}
