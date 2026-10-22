#include "Store/KartStoreCatalog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "KartStore"

namespace MovecadeKart::Store
{
	namespace Stat
	{
		const FName ArcadeGames(TEXT("arcade.games"));
		const FName RacesFinished(TEXT("race.finished"));
		const FName RacesWon(TEXT("race.won"));
		const FName BowlingGames(TEXT("arcade.bowling"));
		const FName SamuraiGames(TEXT("arcade.samurai"));
		const FName StackerGames(TEXT("arcade.stacker"));
	}

	namespace
	{
		FLinearColor Hex(const TCHAR* Code)
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(Code));
		}

		/** Valve's price tiers: Steam localises each tier per currency. Cents are the US price (the mock's). */
		struct FTier
		{
			const TCHAR* Category;
			int32 Cents;
		};
		const FTier Tier100{ TEXT("1;VLV100"), 100 };
		const FTier Tier150{ TEXT("1;VLV150"), 150 };
		const FTier Tier400{ TEXT("1;VLV400"), 400 };
		const FTier Tier500{ TEXT("1;VLV500"), 500 };

		FItem Paid(int32 DefId, const TCHAR* Key, FText Name, FText Description, ESlot Slot, const FTier& Price, const FVisual& Visual)
		{
			FItem Item;
			Item.DefId = DefId;
			Item.Key = Key;
			Item.Name = MoveTemp(Name);
			Item.Description = MoveTemp(Description);
			Item.Slot = Slot;
			Item.Source = ESource::Paid;
			Item.PriceCents = Price.Cents;
			Item.PriceCategory = Price.Category;
			Item.Visual = Visual;
			return Item;
		}

		FItem Earned(int32 DefId, const TCHAR* Key, FText Name, FText Description, ESlot Slot, FName StatName, int32 Target, const TCHAR* Achievement, const FVisual& Visual)
		{
			FItem Item = Paid(DefId, Key, MoveTemp(Name), MoveTemp(Description), Slot, FTier{ TEXT(""), 0 }, Visual);
			Item.Source = ESource::Earned;
			Item.PriceCategory.Reset();
			Item.Unlock = { StatName, Target, FName(Achievement) };
			return Item;
		}

		FItem Drop(int32 DefId, const TCHAR* Key, FText Name, FText Description, ESlot Slot, const FVisual& Visual)
		{
			FItem Item = Paid(DefId, Key, MoveTemp(Name), MoveTemp(Description), Slot, FTier{ TEXT(""), 0 }, Visual);
			Item.Source = ESource::Drop;
			Item.PriceCategory.Reset();
			return Item;
		}

		FVisual Colour(const TCHAR* A, float Roughness = 0.4f, float Metallic = 0.f, float Emissive = 0.f)
		{
			FVisual V;
			V.Primary = Hex(A);
			V.Secondary = V.Primary;
			V.Tertiary = V.Primary;
			V.Roughness = Roughness;
			V.Metallic = Metallic;
			V.Emissive = Emissive;
			return V;
		}

		FVisual Three(const TCHAR* A, const TCHAR* B, const TCHAR* C, float Roughness = 0.4f, float Metallic = 0.f, float Emissive = 0.f)
		{
			FVisual V = Colour(A, Roughness, Metallic, Emissive);
			V.Secondary = Hex(B);
			V.Tertiary = Hex(C);
			return V;
		}

		FVisual Wear(EKartAccessory Accessory, const TCHAR* Tint)
		{
			FVisual V = Colour(Tint);
			V.Accessory = Accessory;
			return V;
		}

		TArray<FItem> Build()
		{
			TArray<FItem> L;
			// Kart paints -----------------------------------------------------------------------------------------
			const FText PaintNote = LOCTEXT("PaintNote", "A paint job for Player 1's kart. Looks only: every kart handles the same.");
			L.Add(Paid(101, TEXT("paint.chrome"), LOCTEXT("Chrome", "Chrome Flash"), PaintNote, ESlot::KartPaint, Tier100, Colour(TEXT("c9d3dc"), 0.15f, 0.9f)));
			L.Add(Paid(102, TEXT("paint.volt"), LOCTEXT("Volt", "Neon Volt"), PaintNote, ESlot::KartPaint, Tier100, Colour(TEXT("c6ff3a"))));
			L.Add(Paid(103, TEXT("paint.velvet"), LOCTEXT("Velvet", "Royal Velvet"), PaintNote, ESlot::KartPaint, Tier100, Colour(TEXT("6a2c91"))));
			L.Add(Paid(104, TEXT("paint.carbon"), LOCTEXT("Carbon", "Carbon Black"), PaintNote, ESlot::KartPaint, Tier100, Colour(TEXT("23262c"), 0.3f)));
			L.Add(Paid(105, TEXT("paint.gold"), LOCTEXT("Gold", "Trophy Gold"), PaintNote, ESlot::KartPaint, Tier100, Colour(TEXT("e2b13c"), 0.25f, 0.8f)));
			L.Add(Paid(106, TEXT("paint.coral"), LOCTEXT("Coral", "Coral Reef"), PaintNote, ESlot::KartPaint, Tier100, Colour(TEXT("ff6f61"))));
			L.Add(Earned(150, TEXT("paint.confetti"), LOCTEXT("Confetti", "Confetti Pop"), LOCTEXT("ConfettiDesc", "Free for party people: play 5 party games."),
				ESlot::KartPaint, Stat::ArcadeGames, 5, TEXT("STORE_PARTY_5"), Colour(TEXT("ff7bc4"))));
			L.Add(Drop(160, TEXT("paint.giftwrap"), LOCTEXT("GiftWrap", "Gift Wrap Teal"), LOCTEXT("GiftWrapDesc", "A free gift that drops while you play."),
				ESlot::KartPaint, Colour(TEXT("1fb5a8"))));

			// Driver colourways -----------------------------------------------------------------------------------
			const FText WayNote = LOCTEXT("WayNote", "Jersey, pants and shoes for Player 1's driver in the Classic Jersey outfit.");
			L.Add(Paid(201, TEXT("way.sunset"), LOCTEXT("Sunset", "Team Sunset"), WayNote, ESlot::Colourway, Tier100, Three(TEXT("ff7a45"), TEXT("3b2c5e"), TEXT("ffd166"))));
			L.Add(Paid(202, TEXT("way.arctic"), LOCTEXT("Arctic", "Arctic Rally"), WayNote, ESlot::Colourway, Tier100, Three(TEXT("f2f7fb"), TEXT("5f8fb4"), TEXT("6fe3ff"))));
			L.Add(Paid(203, TEXT("way.midnight"), LOCTEXT("MidnightNeon", "Midnight Neon"), WayNote, ESlot::Colourway, Tier100, Three(TEXT("22202e"), TEXT("ff3fa4"), TEXT("3ff2ff"))));
			L.Add(Earned(250, TEXT("way.rookie"), LOCTEXT("Rookie", "Rookie Stripes"), LOCTEXT("RookieDesc", "Free: finish your first race."),
				ESlot::Colourway, Stat::RacesFinished, 1, TEXT("STORE_FIRST_RACE"), Three(TEXT("2f80ed"), TEXT("f4f1ec"), TEXT("f2c94c"))));

			// Headwear --------------------------------------------------------------------------------------------
			L.Add(Paid(301, TEXT("hat.cap"), LOCTEXT("Cap", "Pit Crew Cap"), LOCTEXT("CapDesc", "A cap in your jersey colour for Player 1's driver."),
				ESlot::Headwear, Tier150, Wear(EKartAccessory::Cap, TEXT("f0508e"))));
			L.Add(Paid(302, TEXT("hat.shades"), LOCTEXT("Shades", "Cool Shades"), LOCTEXT("ShadesDesc", "Sunglasses for Player 1's driver. Too cool to look behind."),
				ESlot::Headwear, Tier150, Wear(EKartAccessory::Glasses, TEXT("23262c"))));
			L.Add(Earned(350, TEXT("hat.sweatband"), LOCTEXT("Sweatband", "Winner's Sweatband"), LOCTEXT("SweatbandDesc", "Free: win a race against anyone."),
				ESlot::Headwear, Stat::RacesWon, 1, TEXT("STORE_FIRST_WIN"), Wear(EKartAccessory::Headband, TEXT("f7c948"))));

			// Bowling balls ---------------------------------------------------------------------------------------
			const FText BallNote = LOCTEXT("BallNote", "The ball every player at this PC bowls with. It rolls exactly like the house ball.");
			L.Add(Paid(401, TEXT("ball.galaxy"), LOCTEXT("Galaxy", "Galaxy Ball"), BallNote, ESlot::BowlingBall, Tier100, Three(TEXT("1b2a6b"), TEXT("8f6bff"), TEXT("ffffff"), 0.06f, 0.3f)));
			L.Add(Paid(402, TEXT("ball.lava"), LOCTEXT("Lava", "Lava Lamp"), BallNote, ESlot::BowlingBall, Tier100, Three(TEXT("ff4d1a"), TEXT("ffb000"), TEXT("ffe066"), 0.2f, 0.f, 0.6f)));
			L.Add(Paid(403, TEXT("ball.pearl"), LOCTEXT("Pearl", "Pearl Pro"), BallNote, ESlot::BowlingBall, Tier100, Three(TEXT("f2efe9"), TEXT("cfd8f0"), TEXT("ffffff"), 0.08f, 0.6f)));
			L.Add(Earned(450, TEXT("ball.alleycat"), LOCTEXT("AlleyCat", "Alley Cat"), LOCTEXT("AlleyCatDesc", "Free: bowl 3 games."),
				ESlot::BowlingBall, Stat::BowlingGames, 3, TEXT("STORE_BOWL_3"), Three(TEXT("ff8a3d"), TEXT("2b1d14"), TEXT("ffffff"), 0.15f)));
			L.Add(Drop(460, TEXT("ball.gumball"), LOCTEXT("Gumball", "Gumball"), LOCTEXT("GumballDesc", "A free gift that drops while you play."),
				ESlot::BowlingBall, Three(TEXT("4dd2ff"), TEXT("ff5fa2"), TEXT("ffffff"), 0.1f)));

			// Katanas: Primary blade, Secondary edge, Tertiary guard and wrap -------------------------------------
			const FText KatanaNote = LOCTEXT("KatanaNote", "The blade every player at this PC swings in Samurai Slice. Same reach, same cut.");
			L.Add(Paid(501, TEXT("katana.sakura"), LOCTEXT("Sakura", "Sakura Blade"), KatanaNote, ESlot::Katana, Tier100, Three(TEXT("f7b6cf"), TEXT("fff0f6"), TEXT("c2185b"), 0.2f, 0.8f)));
			L.Add(Paid(502, TEXT("katana.neon"), LOCTEXT("NeonEdge", "Neon Edge"), KatanaNote, ESlot::Katana, Tier100, Three(TEXT("2b2f3a"), TEXT("3ff2ff"), TEXT("1b1b1f"), 0.2f, 0.9f, 3.f)));
			L.Add(Paid(503, TEXT("katana.obsidian"), LOCTEXT("Obsidian", "Obsidian"), KatanaNote, ESlot::Katana, Tier100, Three(TEXT("1a1a20"), TEXT("6b6f7a"), TEXT("d4a017"), 0.1f, 0.9f)));
			L.Add(Earned(550, TEXT("katana.bamboo"), LOCTEXT("Bamboo", "Bamboo Practice"), LOCTEXT("BambooDesc", "Free: play Samurai Slice twice."),
				ESlot::Katana, Stat::SamuraiGames, 2, TEXT("STORE_SAMURAI_2"), Three(TEXT("c9b26b"), TEXT("e6d79a"), TEXT("5b7a2e"), 0.7f, 0.f)));

			// Stacker blocks --------------------------------------------------------------------------------------
			const FText BlockNote = LOCTEXT("BlockNote", "Block colour for every builder at this PC in Sky Stacker. Same size, same weight.");
			L.Add(Paid(601, TEXT("block.gold"), LOCTEXT("GoldBars", "Gold Bars"), BlockNote, ESlot::StackerBlock, Tier100, Colour(TEXT("e2b13c"))));
			L.Add(Paid(602, TEXT("block.ice"), LOCTEXT("Ice", "Ice Cubes"), BlockNote, ESlot::StackerBlock, Tier100, Colour(TEXT("a8e6ff"))));
			L.Add(Earned(650, TEXT("block.toy"), LOCTEXT("Toy", "Toy Bricks"), LOCTEXT("ToyDesc", "Free: play Sky Stacker twice."),
				ESlot::StackerBlock, Stat::StackerGames, 2, TEXT("STORE_STACKER_2"), Colour(TEXT("e8394d"))));
			L.Add(Drop(660, TEXT("block.candy"), LOCTEXT("Candy", "Candy Blocks"), LOCTEXT("CandyDesc", "A free gift that drops while you play."),
				ESlot::StackerBlock, Colour(TEXT("ff9ad5"))));

			// Victory emotes: not wired into the results yet, so not for sale. ------------------------------------
			FItem Moon = Paid(701, TEXT("emote.moonwalk"), LOCTEXT("Moonwalk", "Moonwalk"), LOCTEXT("MoonwalkDesc", "A victory dance on the podium."), ESlot::VictoryEmote, Tier100, Colour(TEXT("a67cf0")));
			Moon.bComingSoon = true;
			L.Add(Moon);
			FItem Guitar = Paid(702, TEXT("emote.airguitar"), LOCTEXT("AirGuitar", "Air Guitar"), LOCTEXT("AirGuitarDesc", "Shred a solo when you win."), ESlot::VictoryEmote, Tier100, Colour(TEXT("ff8a3d")));
			Guitar.bComingSoon = true;
			L.Add(Guitar);

			// Bundles: cheaper than the parts, never random. ------------------------------------------------------
			FItem Paints = Paid(901, TEXT("bundle.paints"), LOCTEXT("PaintBundle", "Paint Shop Bundle"),
				LOCTEXT("PaintBundleDesc", "All six paid kart paints for the price of four."), ESlot::Bundle, Tier400, Colour(TEXT("f0508e")));
			Paints.Contents = { 101, 102, 103, 104, 105, 106 };
			L.Add(Paints);
			FItem Skins = Paid(902, TEXT("bundle.party"), LOCTEXT("PartyBundle", "Party Skins Bundle"),
				LOCTEXT("PartyBundleDesc", "Every paid ball, katana and block skin: eight skins for the price of five."), ESlot::Bundle, Tier500, Colour(TEXT("5ec8e5")));
			Skins.Contents = { 401, 402, 403, 501, 502, 503, 601, 602 };
			L.Add(Skins);
			return L;
		}
	}

	const TCHAR* SlotId(ESlot Slot)
	{
		switch (Slot)
		{
		case ESlot::KartPaint: return TEXT("kart-paint");
		case ESlot::Colourway: return TEXT("colourway");
		case ESlot::Headwear: return TEXT("headwear");
		case ESlot::BowlingBall: return TEXT("bowling-ball");
		case ESlot::Katana: return TEXT("katana");
		case ESlot::StackerBlock: return TEXT("stacker-block");
		case ESlot::VictoryEmote: return TEXT("victory-emote");
		case ESlot::Bundle: return TEXT("bundle");
		default: return TEXT("none");
		}
	}

	FText SlotName(ESlot Slot)
	{
		switch (Slot)
		{
		case ESlot::KartPaint: return LOCTEXT("SlotPaint", "Kart paint");
		case ESlot::Colourway: return LOCTEXT("SlotWay", "Driver colourway");
		case ESlot::Headwear: return LOCTEXT("SlotHat", "Headwear");
		case ESlot::BowlingBall: return LOCTEXT("SlotBall", "Bowling ball");
		case ESlot::Katana: return LOCTEXT("SlotKatana", "Katana");
		case ESlot::StackerBlock: return LOCTEXT("SlotBlock", "Stacker blocks");
		case ESlot::VictoryEmote: return LOCTEXT("SlotEmote", "Victory emote");
		case ESlot::Bundle: return LOCTEXT("SlotBundle", "Bundle");
		default: return FText::GetEmpty();
		}
	}

	const TCHAR* ResultId(EPurchaseResult Result)
	{
		switch (Result)
		{
		case EPurchaseResult::Success: return TEXT("success");
		case EPurchaseResult::Cancelled: return TEXT("cancelled");
		case EPurchaseResult::Failed: return TEXT("failed");
		case EPurchaseResult::AlreadyOwned: return TEXT("already-owned");
		default: return TEXT("unavailable");
		}
	}

	FText FormatPrice(const FPrice& Price)
	{
		const FString Currency = Price.Currency.IsEmpty() ? FString(TEXT("USD")) : Price.Currency;
		// Steam reports the price in the currency's base (minor) units; the culture formats symbol and decimals.
		return FText::AsCurrencyBase(static_cast<int64>(Price.Amount), Currency);
	}

	const TArray<FItem>& Catalog()
	{
		static const TArray<FItem> Items = Build();
		return Items;
	}

	const FItem* FindItem(int32 DefId)
	{
		return Catalog().FindByPredicate([DefId](const FItem& I) { return I.DefId == DefId; });
	}

	const FItem* FindItem(FName Key)
	{
		return Catalog().FindByPredicate([Key](const FItem& I) { return I.Key == Key; });
	}

	FName GameStat(const FString& GameId)
	{
		if (GameId == TEXT("bowling")) return Stat::BowlingGames;
		if (GameId == TEXT("samurai")) return Stat::SamuraiGames;
		if (GameId == TEXT("stacker")) return Stat::StackerGames;
		return NAME_None;
	}

	TArray<int32> DropPool()
	{
		TArray<int32> Out;
		for (const FItem& I : Catalog())
		{
			if (I.Source == ESource::Drop)
			{
				Out.Add(I.DefId);
			}
		}
		return Out;
	}

	FString MakeItemDefsJson(int32 AppId)
	{
		// The Steamworks item schema (partner.steamgames.com/doc/features/inventory/schema). Upload this file
		// under Steamworks > Community > Inventory Service > Item Definitions.
		TArray<TSharedPtr<FJsonValue>> Items;
		auto Hex6 = [](const FLinearColor& C) { return C.ToFColorSRGB().ToHex().Left(6).ToLower(); };
		for (const FItem& I : Catalog())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("itemdefid"), I.DefId);
			O->SetStringField(TEXT("type"), I.Slot == ESlot::Bundle ? TEXT("bundle") : TEXT("item"));
			O->SetStringField(TEXT("name"), I.Name.ToString());
			O->SetStringField(TEXT("description"), I.Description.ToString());
			O->SetStringField(TEXT("display_type"), SlotName(I.Slot).ToString());
			O->SetStringField(TEXT("background_color"), Hex6(I.Visual.Primary));
			O->SetStringField(TEXT("name_color"), TEXT("2f3857"));
			O->SetStringField(TEXT("icon_url"), FString::Printf(TEXT("https://cdn.example.com/movecade/store/%d.png"), I.DefId));
			O->SetStringField(TEXT("icon_url_large"), FString::Printf(TEXT("https://cdn.example.com/movecade/store/%d_large.png"), I.DefId));
			O->SetBoolField(TEXT("tradable"), false);
			O->SetBoolField(TEXT("marketable"), false);
			O->SetBoolField(TEXT("game_only"), false);
			O->SetStringField(TEXT("tags"), FString::Printf(TEXT("slot:%s;key:%s"), SlotId(I.Slot), *I.Key.ToString()));
			if (I.Slot == ESlot::Bundle)
			{
				TArray<FString> Parts;
				for (int32 Def : I.Contents)
				{
					Parts.Add(FString::Printf(TEXT("%dx1"), Def));
				}
				O->SetStringField(TEXT("bundle"), FString::Join(Parts, TEXT(";")));
			}
			if (I.Source == ESource::Paid && !I.bComingSoon)
			{
				O->SetStringField(TEXT("price_category"), I.PriceCategory);
				O->SetStringField(TEXT("store_tags"), SlotId(I.Slot));
			}
			else
			{
				// Free and unreleased items are never sold in the Steam item store.
				O->SetBoolField(TEXT("store_hidden"), true);
			}
			if (I.Source == ESource::Earned)
			{
				O->SetStringField(TEXT("promo"), FString::Printf(TEXT("ach:%s"), *I.Unlock.Achievement.ToString()));
			}
			Items.Add(MakeShared<FJsonValueObject>(O));
		}
		// The playtime generator: drops one of the gift items now and then while the game runs (free, no purchase).
		{
			TSharedRef<FJsonObject> G = MakeShared<FJsonObject>();
			G->SetNumberField(TEXT("itemdefid"), PlaytimeGeneratorDef);
			G->SetStringField(TEXT("type"), TEXT("playtimegenerator"));
			G->SetStringField(TEXT("name"), TEXT("Arcade Gift"));
			G->SetStringField(TEXT("description"), TEXT("Free gifts while you play. Never sold."));
			TArray<FString> Parts;
			for (int32 Def : DropPool())
			{
				Parts.Add(FString::Printf(TEXT("%dx1"), Def));
			}
			G->SetStringField(TEXT("bundle"), FString::Join(Parts, TEXT(";")));
			G->SetNumberField(TEXT("drop_interval"), 120);
			G->SetNumberField(TEXT("drop_window"), 1440);
			G->SetNumberField(TEXT("drop_max_per_window"), 1);
			G->SetBoolField(TEXT("use_drop_limit"), true);
			G->SetNumberField(TEXT("drop_limit"), DropPool().Num());
			G->SetBoolField(TEXT("store_hidden"), true);
			Items.Add(MakeShared<FJsonValueObject>(G));
		}
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("appid"), AppId);
		Root->SetArrayField(TEXT("items"), Items);
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		Out.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		return Out + TEXT("\n");
	}
}

#undef LOCTEXT_NAMESPACE
