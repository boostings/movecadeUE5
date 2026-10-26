# The cosmetics store (Steam, App Store, Google Play)

Movecade Kart is a paid game. The store sells **optional cosmetics only**: kart paints, driver colourways,
headwear and party-game skins (bowling ball, katana, stacker blocks). Nothing in the store changes handling,
scores, items, hit boxes or any rule a game runs. Every player gets the whole game with the purchase; a handful of
cosmetics are **free unlocks** earned by playing, and a few **gift drops** arrive for free while you play.

There is no own server. On PC, Steam is the store, the payment provider, the receipt, the refund desk and the record
of who owns what (the Steam Inventory Service). On iPhone and iPad the App Store plays that part (StoreKit 2), on
Android Google Play (Play Billing); see "Mobile" below. The game asks its platform, and keeps a local cache only so
owned items still show when the store is offline.

| Build | Store backend |
|---|---|
| Desktop Shipping | Steam Inventory (always compiled in) |
| iOS | App Store, StoreKit 2 (`Store/IOS`) |
| Android | Google Play Billing (`Store/Android`) |
| Editor, tests, Development without `MOVECADE_STEAM` | the mock (it can play any of the three: `-KartStoreFront`) |

## Code map

| Piece | Where |
|---|---|
| Catalog (item ids, names, prices, unlock rules, looks) | `Source/MovecadeKart/{Public,Private}/Store/KartStoreCatalog.*` |
| Backend interface, offline mock | `Store/KartStoreBackend.h`, `Private/Store/MockStoreBackend.cpp` |
| Steam backend (`ISteamInventory`) | `Private/Store/SteamInventoryBackend.cpp` (only with `MOVECADE_STEAM=1`) |
| App Store backend (StoreKit 2) | `Private/Store/IOS/AppStoreBackend.cpp`, `KartStoreKit.swift` (iOS builds only) |
| Google Play backend (Play Billing) | `Private/Store/Android/GooglePlayBackend.cpp` (Android builds only) |
| Phone stores' shared logic (product ids to def ids, prices, free items on the device) | `Private/Store/MobileStore.*` |
| Google Play signature check | `Private/Store/GooglePlayReceipt.*` |
| Product ids, mobile tiers | `KartStoreCatalog` (`ProductId`, `MakeMobileProductsJson`) |
| Store logic (ownership, purchase flow, restore, equip, unlocks, save) | `Store/KartStore.h/.cpp` |
| Save file (checked header, validation, quarantine) | `Private/Store/KartStoreSaveFile.h/.cpp` |
| Game-instance owner, play stats in | `Store/KartStoreSubsystem.*` |
| What games read | `Store/KartCosmetics.h` (`FKartCosmetics`) |
| Store screen | `Private/UI/Screens/KartStoreScreen.cpp` (main menu > Store) |
| Tests | `Private/Tests/KartStoreTests.cpp`, `KartMobileStoreTests.cpp` (`MovecadeKart.Store.*`) |
| Steam item definitions | `Docs/Store/itemdefs.json` |
| App Store / Google Play products | `Docs/Store/mobile-products.json` |

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
on the next inventory refresh. On phones there are no achievements or promo rules behind them: the device keeps
its free items (see "Mobile").

The **playtime generator** (990, "Arcade Gift") drops one of three gift items (160, 460, 660) at most once a day,
about every two hours of play, three in total. The game calls `TriggerItemDrop` every 10 minutes of play; Steam
decides. These are free, never sold, and the pool is small and known up front: there is no paid randomness anywhere.

## Running without Steam (default)

The default build has no Steam dependency. The store runs on `FMockStoreBackend`, a pretend Steam: prices in USD,
checkout that "opens" for ~1.8 s, then approves. Its inventory is a separate save slot (`KartStoreMock.sav`), so
restore behaves like the real thing. The screen says "Demo store (offline test mode)".

* `-KartStoreMock=succeed|cancel|fail|pending` picks what checkout does (default `succeed`; `pending` is Ask to Buy);
  `-KartStoreMock=offline` plays the store as a Shipping build without Steam sees it.
* `-KartStoreFront=appstore|googleplay|steam` makes the mock play that storefront: its words, its price tiers ($0.99
  instead of $1.00) and the store's own price text. Screenshots of the phone store on PC:
  `.\Tools\ui\shots.ps1 -Steps 'store,storebuy,storedone' -Extra '-KartStoreFront=appstore'`.
* `-KartStoreBackend=mock` forces the mock in a Steam build (development builds only; compiled out of Shipping).
* Delete `Saved/SaveGames/KartStore*.sav` to start over.
* **Shipping builds never sell through the mock**: without Steam the store is read-only (offline), and only free
  items are worn (see "Trust model" below).
* **Desktop Shipping builds always include Steamworks**: `MovecadeKart.Build.cs` turns it on for every desktop
  Shipping game target, and `SteamInventoryBackend.cpp` refuses to compile a desktop Shipping build without it. iOS
  and Android Shipping builds use their own store instead.

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

## Mobile: App Store and Google Play

The same catalog sells on iPhone, iPad and Android. Nothing about the items changes: same def ids in the save, same
slots, same free unlocks. What changes is who sells them and who keeps the record.

### Products

Every sellable item (paid, not *coming soon*) is one product, with the **same product id on both stores**:
`movecade.kart.<catalog key>`. All are **non-consumable** (App Store: *Non-Consumable*; Play: a one-time product
that the game never consumes). Bundles are products of their own; owning one counts as owning its contents, exactly
like Steam's bundle items. Free unlocks, gifts and the *coming soon* emotes are not products.

`Docs/Store/mobile-products.json` is generated from the catalog (`MakeMobileProductsJson`) with each product's id,
def id, type, USD price, Steam tier, name, 45-character store description and the full description. The
`MovecadeKart.Store.MobileProducts` test fails when it is out of step and writes the fresh copy to
`Saved/mobile-products.generated.json`. **Never rename, delete or reuse a product id**: neither store lets an id be
used again, and purchases are stored against it forever (that is also why the catalog keys of paid items are frozen).

| Product id | Def | Item | USD | Steam tier |
|---|---|---|---|---|
| `movecade.kart.paint.chrome` | 101 | Chrome Flash | $0.99 | `1;VLV100` |
| `movecade.kart.paint.volt` | 102 | Neon Volt | $0.99 | `1;VLV100` |
| `movecade.kart.paint.velvet` | 103 | Royal Velvet | $0.99 | `1;VLV100` |
| `movecade.kart.paint.carbon` | 104 | Carbon Black | $0.99 | `1;VLV100` |
| `movecade.kart.paint.gold` | 105 | Trophy Gold | $0.99 | `1;VLV100` |
| `movecade.kart.paint.coral` | 106 | Coral Reef | $0.99 | `1;VLV100` |
| `movecade.kart.way.sunset` | 201 | Team Sunset | $0.99 | `1;VLV100` |
| `movecade.kart.way.arctic` | 202 | Arctic Rally | $0.99 | `1;VLV100` |
| `movecade.kart.way.midnight` | 203 | Midnight Neon | $0.99 | `1;VLV100` |
| `movecade.kart.hat.cap` | 301 | Pit Crew Cap | $1.49 | `1;VLV150` |
| `movecade.kart.hat.shades` | 302 | Cool Shades | $1.49 | `1;VLV150` |
| `movecade.kart.ball.galaxy` | 401 | Galaxy Ball | $0.99 | `1;VLV100` |
| `movecade.kart.ball.lava` | 402 | Lava Lamp | $0.99 | `1;VLV100` |
| `movecade.kart.ball.pearl` | 403 | Pearl Pro | $0.99 | `1;VLV100` |
| `movecade.kart.katana.sakura` | 501 | Sakura Blade | $0.99 | `1;VLV100` |
| `movecade.kart.katana.neon` | 502 | Neon Edge | $0.99 | `1;VLV100` |
| `movecade.kart.katana.obsidian` | 503 | Obsidian | $0.99 | `1;VLV100` |
| `movecade.kart.block.gold` | 601 | Gold Bars | $0.99 | `1;VLV100` |
| `movecade.kart.block.ice` | 602 | Ice Cubes | $0.99 | `1;VLV100` |
| `movecade.kart.bundle.paints` | 901 | Paint Shop Bundle (the six paints) | $3.99 | `1;VLV400` |
| `movecade.kart.bundle.party` | 902 | Party Skins Bundle (3 balls, 3 katanas, 2 blocks) | $4.99 | `1;VLV500` |

**Prices.** Each Steam tier has a matching phone price point: $1.00 is $0.99, $1.50 is $1.49, $4.00 is $3.99, $5.00
is $4.99 (`FTier` in `KartStoreCatalog.cpp`, `FItem::MobilePriceCents`). Both stores derive the other currencies
from the USD price; the game shows the store's own formatted price (StoreKit's `displayPrice`, Play's
`formattedPrice`, kept in `FPrice::Display`), never one it computed, so the tile, the confirmation and the store's
sheet always agree. A bundle is a third cheaper than its parts on every store, and its price doesn't drop when you
own some of the parts (the confirmation says so).

### How it works on a phone

* **Ownership is the store's word.** iOS: StoreKit 2's *current entitlements*: transactions Apple signed and StoreKit
  verified on the device (`VerificationResult.verified`), not refunded, not revoked. Android: Play's purchase list
  (`queryPurchasesAsync`, through OnlineSubsystemGooglePlay), and a purchase counts only after the game verifies
  Google's signature on it (below), for this package, in the PURCHASED state. Both answer offline from the platform's
  own signed cache, so paid items stay worn on a plane.
* **At launch the store refreshes quietly**: StoreKit's entitlements and Play's purchase list never show a sign-in
  sheet. Nothing else is needed after a reinstall or on a new phone: the items come back on their own.
* **Restore purchases** (the chip under the status, R / Y) is on every platform, and App Review requires it on iOS.
  On iOS it runs `AppStore.sync()` first (it can ask the player to sign in, which is why only the button does it); on
  Android it is the same purchase query (Google has no separate restore). It also puts back on sale anything still
  *waiting for approval*.
* **Pending purchases.** Ask to Buy (a child's purchase waiting for a parent) and Play's pending payments (cash at a
  shop, slow banks) end the checkout as *Pending*: nothing is charged, the item shows **Waiting for approval**, and it
  can't be bought twice. If it goes through, it arrives by itself (StoreKit's `Transaction.updates`, Play's purchase
  update) with an "Approved: ... is yours" toast. If a parent says no, neither store says anything; Restore purchases
  puts the item back on sale.
* **Refunds.** Apple: the refund arrives through `Transaction.updates` and the entitlement disappears. Google: a
  refunded or voided purchase drops out of the purchase list, which the game reads at launch, when it returns to the
  foreground, and on Restore. Either way the item leaves the owned list and comes off, like on Steam.
* **Acknowledging (Android).** Google refunds a purchase that isn't acknowledged within three days. The game
  acknowledges every verified purchase (`AndroidThunkCpp_Iap_AcknowledgePurchase`) and **never consumes** one: the
  engine's `FinalizePurchase` consumes anything that isn't a subscription, which would delete a cosmetic, so the game
  never calls it and the config switches it off (`bDisableLocalAcknowledgeAndConsume=True`).
* **Free unlocks without Steam.** The App Store and Google Play hold only paid products, so a phone keeps its free
  items itself (`Mobile::FLocalFreeItems`, save slot `KartStoreFree.sav` with the store save's checked header). Earned
  items unlock on the same goals; gift drops follow the Steam generator's rules on the device: one gift per about two
  hours of play, at most one per 24 hours, the next one you don't have, three in total, never random, never sold. The
  backends add these to the inventory they report, so the store treats them exactly as on Steam. Free items stay on
  the device (a new phone starts them over). Game Center / Play Games achievements for the six goals are optional
  and not wired.

### Receipt validation: on the device or on a server

| | On the device (the default) | On a server |
|---|---|---|
| iOS | StoreKit 2 verifies Apple's JWS signature on every transaction and ties it to the device; the game counts only `.verified`, unrevoked entitlements. | App Store Server API (`Get Transaction Info`, signed transactions) plus App Store Server Notifications V2 for refunds. |
| Android | The game checks Google's SHA1withRSA signature on the purchase JSON with the app's license key (`GooglePlayReceipt.cpp`, OpenSSL), the package name and the state. No key, no ownership. | Play Developer API `purchases.products.get` with the purchase token, plus Real-time Developer Notifications for refunds and voids. |
| Stops | Forged or edited saves; billing emulators and patched-billing tools that answer without Apple's or Google's key; replayed receipts from another app or package. | All of that, plus a fully compromised device lying to the game about everything. |
| Costs | Nothing; works offline. | A server with Apple and Google credentials, uptime, and the game must reach it to wear what it bought. |

**The default is on-device validation**, for the same reason the desktop store trusts the Steam client (F-14): the
items are cosmetic, local to the device, non-tradable and invisible to other players, and the game has no server.
Both checks fail closed: an unverifiable purchase is never worn (on Android, Google then refunds it automatically
because it was never acknowledged). Move to server validation if items ever become tradable, cross-platform
(bought on a phone, worn on PC) or visible to others; the backends already keep the transaction ids and purchase
tokens such a server would need.

### Setting up App Store Connect (owner)

1. **Agreements.** *Business > Agreements*: accept the **Paid Apps Agreement** and complete banking and tax. In-app
   purchases can't be sold or even sandbox-tested reliably without it.
2. **The app record** with the bundle id the iOS build uses (the Mac agent sets it in `Config/IOS`).
3. **In-app purchases.** *App > Monetization > In-App Purchases > +*, one per row of `Docs/Store/mobile-products.json`:
   type **Non-Consumable**, *Reference Name* = the name, *Product ID* = `productId` exactly, price = `usdPrice` (the
   USD price point; let Apple derive the other storefronts, then review them), *Display Name* = `name` (30
   characters), *Description* = `shortDescription` (45 characters), a review screenshot (the store screen with that
   item selected), and *Review Notes*: "Cosmetic only. Store: main menu > Store." Availability: all storefronts the
   game ships in.
4. **Family Sharing**: leave it off for now (if turned on, every member's purchases show as entitlements; it can't
   be turned off again for a product).
5. **Sandbox testers.** *Users and Access > Sandbox > Test Accounts*: create one or two, sign in on a device under
   *Settings > Developer > Sandbox Apple Account*.
6. **Submit the first version with the in-app purchases attached** (the version page's *In-App Purchases* section);
   Apple reviews them together with the build.
7. The App Store page lists "In-App Purchases" automatically; the privacy label says purchases go through Apple.

### Setting up Play Console (owner)

1. **Payments profile** linked to the developer account (*Settings > Payments profile*), merchant account active.
2. **Upload a build first.** Play Console only allows products once an AAB that includes Play Billing is on a track:
   upload the Shipping AAB to **Internal testing**.
3. **License key.** *Monetize with Play > Monetization setup* > *Licensing*: copy the base64 public key into the
   Android runtime settings as `GooglePlayLicenseKey` (see the config below). It is public (not a secret), but it must
   be the key of this app.
4. **One-time products.** *Monetize with Play > Products > One-time products > Create*, one per row of
   `mobile-products.json`: *Product ID* = `productId` exactly, name = `name`, description = `description`, default
   price = `usdPrice` in USD (let Play convert, then review), *Activate*. They are consumed by nothing: the game never
   consumes them.
5. **License testers.** *Settings > License testing*: add the testers' Google accounts (they get the test cards: always
   approves, always declines, slow test card for pending purchases) and add them to the internal testing track.
6. **Store listing.** "Contains in-app purchases" is set automatically; fill the *Data safety* form (purchases are
   processed by Google Play).
7. Optional, later: **Real-time Developer Notifications** (a Pub/Sub topic) only matter with server validation.

### Engine configuration (for the iOS and Android builds)

The store code builds on its own; the platform config files belong to the iOS and Android ports:

`Config/IOS/IOSEngine.ini`:
```ini
[/Script/IOSRuntimeSettings.IOSRuntimeSettings]
; StoreKit framework and UE_WITH_STORE_KIT (the engine default, stated so nobody turns it off)
bEnableStoreKitSupport=True

[OnlineSubsystemIOS.Store]
; The store talks to StoreKit 2 itself (Store/IOS/KartStoreKit.swift); the engine's StoreKit 1 observer stays out
bSupportsInAppPurchasing=False
```

`Config/Android/AndroidEngine.ini`:
```ini
[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]
bEnableGooglePlaySupport=True
; Play Console > Monetize with Play > Monetization setup > Licensing (base64). Purchases don't count without it.
GooglePlayLicenseKey=<base64 key>
; Play Games app id: required by bEnableGooglePlaySupport (Play Console > Play Games Services > Configuration)
GamesAppID=<numeric id>

[OnlineSubsystemGooglePlay.Store]
bSupportsInAppPurchasing=True
; Never let the engine consume (delete) a cosmetic; the store acknowledges purchases itself
bDisableLocalAcknowledgeAndConsume=True
```

`MovecadeKart.Build.cs` already does the code side: on iOS it sets the Swift bridging header
(`Store/IOS/KartStoreKitInterop.h`) and links StoreKit; on Android it adds `OnlineSubsystem` and `CoreOnline`; on
Android and Win64 it links OpenSSL for the signature check. `OnlineSubsystem` and `OnlineSubsystemGooglePlay` are
enabled by default in the engine; nothing is added to `MyProject.uproject`.

### Testing on devices

* **iOS, without App Store Connect:** in Xcode, add a StoreKit Configuration file to the scheme with the 21 products
  (type Non-Consumable, ids from `mobile-products.json`); purchases, Ask to Buy, refunds and revocations then run
  locally from Xcode's transaction manager. **With App Store Connect:** a Sandbox Apple Account (purchases are free,
  Ask to Buy and refunds can be triggered from the device's Developer settings or TestFlight).
* **Android:** an internal testing track install with a license tester account; the test cards cover approve,
  decline and pending ("slow test card"). Refund from *Order management* with *revoke entitlement* and bring the game
  back to the foreground.
* The log lines: `Store backend: appstore` / `googleplay`, `Entitlements: ok, N products`,
  `Purchases: N receipts, N verified products`, `Purchase <def>: approved`, and on Android every refused purchase
  with its reason (`refused: bad-signature`, `no-license-key`, ...).

## Trust model

Steam (or, on phones, the App Store or Google Play) is the record of ownership; this device is not. The rules the
code follows (the Steam wording below applies to the phone stores in the same way; "Mobile" above has their details):

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
* **The phone is trusted the same way (mobile).** StoreKit's verification and the Google signature check stop forged
  saves and billing emulators, but a jailbroken or rooted device can hook the game itself. Same reasoning as F-14:
  local cosmetics, no server. Server validation is the fix ("Receipt validation" above).
* **Free items on a phone are kept on the phone (mobile).** `KartStoreFree.sav` and the play stats can be edited
  (like F-04), which gets the six earned items and the three gifts early, nothing paid. They don't follow the player
  to a new phone.

## Pricing suggestions

Fair and small next to the price of the game:

| What | Steam | Tier | App Store / Google Play |
|---|---|---|---|
| Kart paint, colourway, ball, katana, block skin | $1.00 each | `1;VLV100` | $0.99 |
| Headwear | $1.50 | `1;VLV150` | $1.49 |
| Paint Shop Bundle (6 paints) | $4.00 (save a third) | `1;VLV400` | $3.99 |
| Party Skins Bundle (8 skins) | $5.00 | `1;VLV500` | $4.99 |

Keep everything in the store buyable outright for one clear price, shown on every tile and in the confirmation
before checkout. No currencies, no timers, no "limited time" pressure, no item that is only in a bundle.

## Compliance notes

* **Steam rules.** Purchases of in-game items in a Steam game must use Steam's microtransaction system (we do:
  `StartPurchase` / overlay checkout). Prices come from Steam in the player's currency; the game never shows a price
  Steam didn't give it ("Price unavailable" disables buying). Store pages must say the game has in-game purchases.
* **App Store rules.** Digital items sold in an iOS app must use In-App Purchase (we do: StoreKit 2), the app must
  offer Restore purchases for non-consumables (the chip on the store screen), prices come from StoreKit for the
  player's storefront, and no wording points to buying elsewhere (the store never mentions Steam on iOS:
  `MovecadeKart.Store.MobileWording` checks every string). Ask to Buy and Screen Time limits are honoured by StoreKit.
* **Google Play rules.** Digital items in a Play app use Google Play Billing (we do: Play Billing 8 through the
  engine), purchases are acknowledged within three days, prices come from Play, and no Steam wording on Android.
  Pending purchases are never treated as paid.
* **No gambling mechanics.** No loot boxes, no paid random rewards, no paid gacha, no currency that buys chances.
  The only random grant is the free playtime gift (no purchase, fixed small pool). Items are `tradable: false`
  and `marketable: false`, so nothing has a cash-out value. This also keeps the game clear of loot-box regulation
  (Belgium, Netherlands, and similar rules elsewhere) and of PEGI/ESRB "in-game purchases (includes random items)".
* **No pay-to-win.** Cosmetic only, and the store says so on every item. Handling, ball physics, blade reach and
  block sizes are identical; the deterministic game rules (and their golden tests) never read cosmetics.
* **Refunds.** Steam handles refunds for in-game purchases (generally within 48 hours if the item wasn't consumed or
  modified); Apple and Google handle theirs (reportaproblem.apple.com, Google Play's refund flow). A refund removes the
  item from the inventory; the game drops it from the owned list and takes it off on the next inventory update or
  restore. The game never "keeps" a refunded item.
* **Kids and households.** Purchases need the Steam account's wallet and Steam's confirmation in the overlay; the
  game adds its own confirmation first. Family View / parental limits are honoured by Steam's checkout.
* **Privacy.** No personal data leaves the game for the store; only the platform's own APIs are called (Steam,
  StoreKit, Play Billing). The Google signature check runs on the device.
