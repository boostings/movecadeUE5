# Release checklist

Run through sections 1 to 4 for every build that goes to a Steam depot, including betas; section 5 for every iOS
build sent to TestFlight or App Review, section 6 for every Android build uploaded to a Play testing track or
production. Each item exists because getting it wrong either gives paid cosmetics away, takes them from players who
paid, or ships development tools to players (security assessment, branch `pentest-store-fix`; mobile store,
branch `mobile-store`). Store setup: `Docs/Store/README.md`.

## 1. Build configuration

- [ ] **Shipping, never Development.** `Config/DefaultGame.ini` packages Shipping (`BuildConfiguration=PPBC_Shipping`).
  A Development build has the demo store online (every checkout "succeeds" for free), `-KartStoreEquip`, the
  console, `-ExecCmds` and logging. When packaging from the command line, pass `-clientconfig=Shipping`:
  ```powershell
  & "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
      "-project=$p\MyProject.uproject" -platform=Win64 -clientconfig=Shipping `
      -build -cook -stage -pak -iostore -archive "-archivedirectory=$p\Saved\Release"
  ```
- [ ] **Steamworks is in.** Shipping always builds with it (`MovecadeKart.Build.cs`); the build fails if it can't.
  The packaged log (run once with `-log` from the depot folder in a test install) shows `Store backend: steam` and
  `Steam Inventory ready (app <real id>)`.
- [ ] The executable is `MyProject-Win64-Shipping.exe` (the name says the configuration).

## 2. Steamworks

- [ ] **App ID.** `UKartStoreSubsystem::DevAppId` and `appid` in `Docs/Store/itemdefs.json` are the real app, not
  480. No `steam_appid.txt` in the depot.
- [ ] **Item definitions** uploaded from `Docs/Store/itemdefs.json` (the `MovecadeKart.Store.Catalog` test keeps it in
  step with the code), with real icon URLs. Paid items have a `price_category` and no `promo`; free unlocks have
  only `promo: ach:<name>`; gifts have neither. Everything `tradable: false`, `marketable: false`.
- [ ] The six `STORE_*` achievements exist with the same API names.
- [ ] Microtransactions are approved and a test purchase, a refund and **Restore purchases** work on a dev account
  (`Docs/Store/README.md`, "Testing with the Steam client").

## 3. What a player can reach

- [ ] Test flags do nothing in the Shipping build. Launch it with
  `-KartStoreBackend=mock -KartStoreEquip=paint.gold -KartStoreMock=succeed -KartBots=0 -KartAutoDrive -KartLaps=1 -KartCapture=C:\Temp\x -KartTrack=../../x`:
  the store still says Steam (or "Steam offline"), nothing is granted or worn, the menus open normally, and no
  `frame_*.png` appears anywhere.
- [ ] Offline check: quit Steam, start the game. The store shows owned paid items as **Owned: needs Steam** and the
  kart wears none of them; free unlocks are still worn. Start Steam, restart: the paid items are worn again.
- [ ] A damaged save doesn't stop the game: replace `%LOCALAPPDATA%\MyProject\Saved\SaveGames\KartStore.sav` with any
  junk file; the game starts, the junk is moved to `KartStore.bad.sav`, and Restore brings the purchases back.
- [ ] The automation tests pass on the release commit (`.\Tools\ue\test.ps1`), including `MovecadeKart.Pentest.*`.

## 4. After release

- [ ] Watch for `Store save ... is unreadable` and `Saved Steam inventory: rejected` in player-reported logs: either
  means a damaged or edited save, not a store fault.
- [ ] Re-read the accepted risks in `Docs/Store/README.md` ("Trust model") before making any item tradable,
  marketable, or visible to other players: those decisions rest on cosmetics staying local.

## 5. iOS (App Store)

- [ ] **Shipping** (`-clientconfig=Shipping`, distribution signing). The device log shows `Store backend: appstore`
  and `App Store ready (StoreKit 2, payments allowed)`; never `mock`.
- [ ] **In-app purchase is off in OnlineSubsystemIOS** (`[OnlineSubsystemIOS.Store] bSupportsInAppPurchasing=False` in
  `Config/IOS/IOSEngine.ini`). The store uses StoreKit 2 directly; the engine's StoreKit 1 observer would also see
  every transaction.
- [ ] **Products** in App Store Connect match `Docs/Store/mobile-products.json` exactly: 21 Non-Consumables, same
  product ids, the listed USD price points, display name and short description, a review screenshot each, status
  *Ready to Submit*. The first version that sells them is submitted **with** them attached.
- [ ] **Paid Apps Agreement** active (Business), with banking and tax.
- [ ] **Sandbox pass on a device** (a Sandbox Apple Account, Settings > Developer > Sandbox Account): buy an item and a
  bundle; cancel a purchase; Ask to Buy (StoreKit testing in Xcode with *Editor > Enable Ask To Buy* on the
  `.storekit` file, then approve in the transaction manager): the item shows *Waiting for approval*, then arrives
  with the *Approved* toast; delete and reinstall, launch
  (no sign-in sheet; items are back), press **Restore purchases** (it syncs); refund a test purchase (Xcode StoreKit
  transaction manager, or *Request a Refund* in the sandbox): it comes off. No string in the store says Steam.
- [ ] **Screen Time** with in-app purchases off: the store opens, owned items still show and are worn, buying says
  purchases are turned off.
- [ ] Test flags do nothing in Shipping: `-KartStoreBackend=mock -KartStoreMock=succeed -KartStoreFront=steam
  -KartStoreEquip=paint.gold` (Xcode scheme arguments) change nothing.
- [ ] App Privacy answers: purchases are handled by Apple; the game sends nothing of its own.

## 6. Android (Google Play)

- [ ] **Shipping AAB**, signed with the upload key; Play App Signing on. The logcat shows `Store backend: googleplay`
  and `Google Play ready (<package>)`; never `mock`.
- [ ] **License key set**: `GooglePlayLicenseKey` (Play Console > Monetize with Play > Monetization setup) in the
  Android runtime settings. Without it every purchase reads as unverified (`refused: no-license-key` in the log) and
  nothing paid is worn: the build must not ship like that.
- [ ] **Config**: `bEnableGooglePlaySupport=True`, `[OnlineSubsystemGooglePlay.Store] bSupportsInAppPurchasing=True`
  and `bDisableLocalAcknowledgeAndConsume=True` (`Config/Android/AndroidEngine.ini`).
- [ ] **Products** in Play Console match `Docs/Store/mobile-products.json`: 21 one-time products, same ids, active,
  priced from the listed USD prices (with Play's local conversions reviewed).
- [ ] **License testers pass** on an internal testing track install: buy an item and a bundle (test card *always
  approves*); cancel; *slow test card* (approves after a few minutes): the item shows *Waiting for approval* and
  arrives by itself; uninstall, reinstall: items are back without pressing anything; refund one in Play Console
  (Order management > Refund, with *revoke entitlement*), return to the game: it comes off. Three days later, no test
  purchase was auto-refunded (they were acknowledged). No string in the store says Steam.
- [ ] Data safety form: purchases are handled by Google Play; the game sends nothing of its own.
