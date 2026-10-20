# Movecade Kart Racer (UE 5.8)

The Movecade kart racer, ported from the web build (movecade-v2) to Unreal Engine 5.8. The race plays exactly
like the web version, with a short list of deliberate fixes and upgrades (see [Docs/Port/00-PLAN.md](Docs/Port/00-PLAN.md)).

## Play

Open `MyProject.uproject` and press **Play**. The editor opens on Sunset Harbor; you race three bots.

| | Keyboard | Gamepad |
|---|---|---|
| Gas | W / Up | A or Right Trigger |
| Brake / reverse | S / Down | B or Left Trigger |
| Steer | A D / Left Right | Left stick, D-pad |
| Hop / drift (hold) | Space | RB / LB |
| Use item | E / Left Shift | X / Y |
| Look behind (hold) | C | Right stick click |
| Skip intro, race again | Enter | Start |
| Pause (resume, R restart, Q quit) | Esc or P | Back |

Hold drift through a corner and let go for a mini-turbo (longer holds give super and ultra boosts). Hold gas
just before GO for a rocket start.

### Phones as controllers (room mode)

1. Start the local room server: `.\Tools\net-sim\run-party.ps1` (it clones movecade-v2 into `Tools/.cache` the
   first time; nothing in your checkout changes, and nothing is deployed).
2. Run the movecade web app so phones can open the join link: `npm run dev` in movecade-v2 (port 5173).
3. Launch the game with `-KartRoom`. The lobby shows a QR code and a room code. Phones join, ready up, and
   the race starts; each phone steers its kart by tilting. "Race again" sends everyone back to the lobby.

No phone handy? `.\Tools\net-sim\e2e.ps1` runs a simulated phone against bots.

## Command line (game or `-game` runs)

| Flag | Effect |
|---|---|
| `-KartBots=N` | Bots in the race (default 3 locally, 0 in room mode) |
| `-KartBotSkill=easy\|normal\|hard` | Bot difficulty (normal is the web build's bot) |
| `-KartWebFeel` | The web build's presentation: no chassis springs or camera juice |
| `-KartLocalPlayers=M` | Extra local players on gamepads 2..M |
| `-KartSoloView` | Only your kart on screen (default: one view per kart, like the TV host) |
| `-KartLaps=N` | Shorter races |
| `-KartSkipIntro` | Straight to the countdown |
| `-KartRoom` | Phones join through the lobby |
| `-KartLegacyWalls` | The web build's sticky walls (B11), for comparison |
| `-KartAutoDrive`, `-KartShots=4,10`, `-KartQuitAfter=30` | Automated checks: self-driving, screenshots, quit |

## Develop

- C++ lives in `Source/MovecadeKart` (`Sim/` is the deterministic 60 Hz kart and item simulation in web units).
  Build: `Engine/Build/BatchFiles/Build.bat MyProjectEditor Win64 Development -Project=<path>\MyProject.uproject`.
- Tests: `UnrealEditor-Cmd.exe MyProject.uproject "-ExecCmds=Automation RunTests MovecadeKart; Quit" -unattended -nullrhi`.
- Track data: `Tools/track-export` turns the web game's track modules into `Data/Tracks/*.track.json`
  (`npm run export`, `npm run check`).
- Specs for every system: `Docs/Port/01`–`04`. Plan, bug decisions and status: `Docs/Port/00-PLAN.md`.
- Live Coding is off for this project (`Config/DefaultEditorPerProjectUserSettings.ini`), so close the editor
  before building from the command line.

## Tracks

| Track | Level | Look |
|---|---|---|
| Violet Loop | `L_VioletLoop` | The Violet Gardens at twilight: hedged islands, crystals, lavender rows, a glass conservatory |
| Sunset Harbor | `L_SunsetHarbor` | A coastal town at golden hour: seawalls, real water, sandstone islets, palms |

Both are built from `Data/Tracks/*.track.json` by `ATrackBuilder` (levels rebuilt by `Tools/ue/make_track_map.py`);
the art comes from `Tools/track-export` (see its README). Look checks: `-KartTrackCam=x,y,z,lookX,lookY,lookZ` holds
the first view on a fixed camera (three.js metres, as in the track JSON), and `-KartLookUE="key=value,..."`
overrides a track's `look.ue` numbers (sun, sky, clouds, fog) without a rebuild.
