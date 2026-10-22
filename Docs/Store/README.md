# The cosmetics store (Steam Inventory Service)

Movecade Kart is a paid game. The store sells **optional cosmetics only**: kart paints, driver colourways,
headwear and party-game skins (bowling ball, katana, stacker blocks). Nothing in the store changes handling,
scores, items, hit boxes or any rule a game runs. Every player gets the whole game with the purchase; a handful of
cosmetics are **free unlocks** earned by playing, and a few **gift drops** arrive for free while you play.

There is no own server. Steam is the store, the payment provider, the receipt, the refund desk and the record
of who owns what (the Steam Inventory Service). The game asks Steam, and keeps a local cache only so owned items
still show when Steam is offline.

## Code map

| Piece | Where |
|---|---|
| Catalog (item ids, names, prices, unlock rules, looks) | `Source/MovecadeKart/{Public,Private}/Store/KartStoreCatalog.*` |
| Backend interface, offline mock | `Store/KartStoreBackend.h`, `Private/Store/MockStoreBackend.cpp` |
| Steam backend (`ISteamInventory`) | `Private/Store/SteamInventoryBackend.cpp` (only with `MOVECADE_STEAM=1`) |
| Store logic (ownership, purchase flow, restore, equip, unlocks, save) | `Store/KartStore.h/.cpp` |
| Save file (checked header, validation, quarantine) | `Private/Store/KartStoreSaveFile.h/.cpp` |
| Game-instance owner, play stats in | `Store/KartStoreSubsystem.*` |
| What games read | `Store/KartCosmetics.h` (`FKartCosmetics`) |
| Store screen | `Private/UI/Screens/KartStoreScreen.cpp` (main menu > Store) |
| Tests | `Private/Tests/KartStoreTests.cpp` (`MovecadeKart.Store.*`) |
| Steam item definitions | `Docs/Store/itemdefs.json` |

### Who wears what

* **Kart paint, colourway, headwear** are Player 1's (the person whose Steam account owns them): applied in the
  race seat setup and the front-end showroom. Colourways show with the Classic Jersey outfit (an outfit overlay
  covers the stock clothes). Other local players, bots and phones keep the stock look.
* **Ball, katana and block skins** are worn by every local player at this PC (turn-based bowling shares the
  controls anyway). Phones and house bots keep the stock skins, so a bot's ball never looks like yours.
* **Victory emotes** are in the catalog as *coming soon* and are **not sold** until the results screens play them.
  Never sell something the game doesn't use.

### Free unlocks

| Item | Goal | Steam achievement (promo rule) |
|---|---|---|
| Confetti Pop paint (150) | play 5 party games | `STORE_PARTY_5` |
| Rookie Stripes colourway (250) | finish a race | `STORE_FIRST_RACE` |
| Winner's Sweatband (350) | win a race | `STORE_FIRST_WIN` |
| Alley Cat ball (450) | bowl 3 games | `STORE_BOWL_3` |
| Bamboo Practice katana (550) | play Samurai Slice twice | `STORE_SAMURAI_2` |
| Toy Bricks blocks (650) | play Sky Stacker twice | `STORE_STACKER_2` |

Stats are counted by the game (`UArcadeSession::RecordResult`, `AKartRaceManager::EndRace`) and saved in
`Saved/SaveGames/KartStore.sav`. When a goal is reached the Steam backend unlocks the achievement and calls
`AddPromoItem`; the item's promo rule (`ach:<name>`) makes Steam grant it. A grant that fails (offline) is retried
on the next inventory refresh.

The **playtime generator** (990, "Arcade Gift") drops one of three gift items (160, 460, 660) at most once a day,
about every two hours of play, three in total. The game calls `TriggerItemDrop` every 10 minutes of play; Steam
decides. These are free, never sold, and the pool is small and known up front: there is no paid randomness anywhere.

## Running without Steam (default)

The default build has no Steam dependency. The store runs on `FMockStoreBackend`, a pretend Steam: prices in USD,
checkout that "opens" for ~1.8 s, then approves. Its inventory is a separate save slot (`KartStoreMock.sav`), so
restore behaves like the real thing. The screen says "Demo store (offline test mode)".

* `-KartStoreMock=succeed|cancel|fail` picks what checkout does (default `succeed`); `-KartStoreMock=offline` plays
  the store as a Shipping build without Steam sees it.
* `-KartStoreBackend=mock` forces the mock in a Steam build (development builds only; compiled out of Shipping).
* Delete `Saved/SaveGames/KartStore*.sav` to start over.
* **Shipping builds never sell through the mock**: without Steam the store is read-only (offline), and only free
  items are worn (see "Trust model" below).
* **Shipping builds always include Steamworks**: `MovecadeKart.Build.cs` turns it on for every desktop Shipping game
  target, and `SteamInventoryBackend.cpp` refuses to compile a Shipping build without it.

Screenshots of the store: `.\Tools\ui\shots.ps1 -Steps 'store,storeparty,storefree,storebuy,storedone'`.

## Setting up Steamworks

1. **App ID.** Development uses **480** (Spacewar, Valve's public test app). In a non-shipping build the backend
   sets `SteamAppId=480` for the process when Steam didn't launch the game. Once the real app exists, change
   `UKartStoreSubsystem::DevAppId`, put the id in `itemdefs.json` (`appid`), and ship a `steam_appid.txt` only in
   development (never in the depot).
2. **Build with Steam.** Shipping game builds always have it. For a Development build (the editor, `-game`), set the
   environment variable and rebuild:
   ```powershell
   $env:MOVECADE_STEAM = '1'
   & "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MyProjectEditor Win64 Development "-Project=$p\MyProject.uproject" -NoMutex -NoHotReloadFromIDE
   ```
   UBT caches the module rules, so after switching the variable on or off, touch `Source/MovecadeKart/MovecadeKart.Build.cs`
   (any edit or a new timestamp) or the build stays "up to date" in the old mode.
   `MovecadeKart.Build.cs` then adds the engine's `Steamworks` module (SDK 1.64, in
   `Engine/Source/ThirdParty/Steamworks`) and defines `MOVECADE_WITH_STEAM=1`. No plugin is required: the backend
   loads `steam_api64.dll` from `Engine/Binaries/ThirdParty/Steamworks/Steamv164/Win64` (it is staged with packaged
   builds by the Steamworks module), and calls `SteamAPI_InitEx` itself. If `OnlineSubsystemSteam` is enabled later
   (for friends, invites or achievements UI), the backend sees the API already initialised and shares it: enable the
   `OnlineSubsystemSteam` and `SteamShared` plugins in `MyProject.uproject` and add to `Config/DefaultEngine.ini`:
   ```ini
   [/Script/Engine.GameEngine]
   +NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="OnlineSubsystemSteam.SteamNetDriver",DriverClassNameFallback="OnlineSubsystemUtils.IpNetDriver")
   [OnlineSubsystem]
   DefaultPlatformService=Steam
   [OnlineSubsystemSteam]
   bEnabled=true
   SteamDevAppId=480
   ```
   Without Steam running, a Steam build falls back to the offline store; nothing requires Steam to start.
3. **Item definitions.** In Steamworks: *App Admin > Community > Inventory Service*: enable the Inventory Service,
   then *Item Definitions > Upload* `Docs/Store/itemdefs.json`. Before uploading the real thing:
   * Replace the placeholder `icon_url` / `icon_url_large` (`cdn.example.com`) with real hosted PNGs
     (200x200 and 512x512 renders of each item). Steam shows these in the overlay checkout and inventory.
   * Check `price_category` tiers (`1;VLV100` is Valve's ~$1 tier, localised per currency by Steam).
   * The file is generated from the catalog: the `MovecadeKart.Store.Catalog` test fails if they differ and writes
     the fresh copy to `Saved/itemdefs.generated.json`. Copy it over `Docs/Store/itemdefs.json`.
   * Never renumber or reuse an `itemdefid`: owned items are stored by id forever.
4. **Achievements** for the free unlocks: create the six achievements above (*Stats & Achievements*), same API
   names. Their promo rules grant the items.
5. **Microtransactions.** Inventory purchases go through Steam's in-game checkout, so the app needs the
   microtransaction setup in Steamworks (*Inventory Service > enable item store / purchases*, and the partner payment
   info). Until it is approved, test with Steam's test purchase flow on your own dev account.

## Testing with the Steam client

1. Log in to Steam with an account that owns 480 (everyone does) or the real app, with the **in-game overlay on**
   (Steam > Settings > In Game). The overlay must work for checkout (`IsOverlayEnabled`).
2. Run the Steam build (`-game` from the editor binaries or a packaged build). The log shows
   `Store backend: steam` and `Steam Inventory ready (app 480)`.
3. Main menu > Store: prices come from `RequestPrices` in your wallet currency. Buy something: `StartPurchase`
   opens the overlay checkout; approving sends `MicroTxnAuthorizationResponse_t`, then the item lands in
   `GetAllItems`, and the dialog says "It's yours!". Closing the overlay cancels (no charge).
4. Inspect / revoke test items in Steamworks (*Inventory Service > Player Inventory*) or with the
   `IInventoryService` Web API on the partner site. Revoke one, press **Restore purchases** (R / Y): it disappears and
   is taken off.
5. Spacewar (480) has Valve's own item definitions, not ours: with 480 the real purchase flow can't sell Movecade
   items. Use 480 to check init, inventory and overlay; use the real App ID (or a dev app) for item purchases.

## Trust model

Steam is the record of ownership; this PC is not. The rules the code follows:

* **Paid items are worn only once Steam confirms them this session.** `KartStore.sav` keeps an owned list so the
  store can show what you own offline, but anyone can edit that file (`Tools/pentest/forge_store_save.py` shows
  how). Until an inventory refresh or a checkout confirms an item, it shows as **Owned: needs Steam**
  (`EItemState::OwnedUnconfirmed`), the store won't equip it, and `FKartCosmetics` (what the games draw) returns
  nothing for it. The player's equip choice is kept and comes back the moment Steam answers. Free items (earned,
  dropped) are worn from the cache.
* **Offline with Steam running**, `GetAllItems` answers from Steam's own signed cache (`k_EResultExpired`), which
  counts as confirmed. If even that fails, the backend uses the last full inventory it saved with `SerializeResult`
  (`Saved/SaveGames/KartStoreSteam_<steamid>.proof`), and accepts it only if `DeserializeResult` verifies Steam's
  signature and `CheckResultSteamID` says it belongs to the signed-in account. Every live result passes the same
  account check. **Without Steam at all**, nothing is confirmed: paid cosmetics stay off until Steam is back.
* **The save file** has a 16-byte header (magic `MKS1`, format version, payload size, CRC32) in front of the engine's
  GVAS payload. Before the engine deserialises anything the size, the CRC, the save class and every container's
  element count are checked (a negative or huge count used to crash the game at launch). A file that fails is moved
  to `KartStore.bad.sav` and the store starts clean. Saves from before the header are still read, with the same
  checks, and rewritten with it. The CRC catches corruption, not tampering; ownership never depends on it.

### Accepted risks

* **Free unlocks are counted on this PC (F-04).** The play stats behind the earned items are kept in the save, and
  the client unlocks the matching Steam achievement itself before `AddPromoItem`, so Steam's `ach:` promo rule
  only proves the client asked. Editing the stats (or a tool like Steam Achievement Manager) gets the six free items
  and their achievements early. No paid item has an unlock rule, so no money is at stake. A fix would need a game
  server to set the achievements (Steamworks "official game server" stats), which this game doesn't have.
* **The Steam client is trusted (F-14).** Inventory, prices and checkout results come from the local Steam client.
  A modified client or a Steam emulator can report any inventory, and with no game server nothing can check it.
  Items are cosmetic, shown only on this PC, non-tradable and non-marketable, so a fake inventory gains nothing
  anyone else can see or sell. Revisit this if items ever become tradable or visible to other players.

## Pricing suggestions

Fair and small next to the price of the game:

| What | Suggested | Tier |
|---|---|---|
| Kart paint, colourway, ball, katana, block skin | $0.99-1.00 each | `1;VLV100` |
| Headwear | $1.49 | `1;VLV150` |
| Paint Shop Bundle (6 paints) | $3.99 (save a third) | `1;VLV400` |
| Party Skins Bundle (8 skins) | $4.99 | `1;VLV500` |

Keep everything in the store buyable outright for one clear price, shown on every tile and in the confirmation
before checkout. No currencies, no timers, no "limited time" pressure, no item that is only in a bundle.

## Compliance notes

* **Steam rules.** Purchases of in-game items in a Steam game must use Steam's microtransaction system (we do:
  `StartPurchase` / overlay checkout). Prices come from Steam in the player's currency; the game never shows a price
  Steam didn't give it ("Price unavailable" disables buying). Store pages must say the game has in-game purchases.
* **No gambling mechanics.** No loot boxes, no paid random rewards, no paid gacha, no currency that buys chances.
  The only random grant is the free playtime gift (no purchase, fixed small pool). Items are `tradable: false`
  and `marketable: false`, so nothing has a cash-out value. This also keeps the game clear of loot-box regulation
  (Belgium, Netherlands, and similar rules elsewhere) and of PEGI/ESRB "in-game purchases (includes random items)".
* **No pay-to-win.** Cosmetic only, and the store says so on every item. Handling, ball physics, blade reach and
  block sizes are identical; the deterministic game rules (and their golden tests) never read cosmetics.
* **Refunds.** Steam handles refunds for in-game purchases (generally within 48 hours if the item wasn't consumed or
  modified). A refund removes the item from the inventory; the game drops it from the owned list and takes it off on
  the next inventory update or restore. The game never "keeps" a refunded item.
* **Kids and households.** Purchases need the Steam account's wallet and Steam's confirmation in the overlay; the
  game adds its own confirmation first. Family View / parental limits are honoured by Steam's checkout.
* **Privacy.** No personal data leaves the game for the store; only Steam's own APIs are called.
