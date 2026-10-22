# Release checklist (Steam builds)

Run through this for every build that goes to a Steam depot, including betas. Each item exists because getting it
wrong either gives paid cosmetics away or ships development tools to players (security assessment, branch
`pentest-store-fix`).

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
