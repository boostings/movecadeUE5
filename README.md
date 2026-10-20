# Movecade Kart Racer (UE 5.8)

A split-screen party kart racer in Unreal Engine 5.8, where phones can be the controllers. It is a port of the
Movecade web build (movecade-v2): the race plays exactly like the web version, with a short list of deliberate fixes
(see [Docs/Port/00-PLAN.md](Docs/Port/00-PLAN.md)). Around it everything is new: ray-traced lighting, original
environments, a game-feel layer, effects, and a full console-style front end.

- **Case study, screenshots and trailer:** [Docs/Portfolio](Docs/Portfolio/README.md)
- **How the code fits together:** [Docs/Architecture.md](Docs/Architecture.md)

## Play

Open `MyProject.uproject` and press **Play** (or run with `-game`). The title screen comes up over a flyover of the
track; press Enter or Start. **Race** sets up a local race (racers for up to four players: pads 2 to 4 join with
Start, then bots, bot skill, laps and screens, then the track), **Party Mode** opens the phone lobby, and
**Settings** has graphics, audio and the controls. Menus work with the keyboard, any gamepad and the mouse.
Set `Kart.FrontEnd 0` (console variable) to have Play go straight into a race.

| | Keyboard | Gamepad |
|---|---|---|
| Gas | W / Up | A or Right Trigger |
| Brake / reverse | S / Down | B or Left Trigger |
| Steer | A D / Left Right | Left stick, D-pad |
| Hop / drift (hold) | Space | RB / LB |
| Use item | E / Left Shift | X / Y |
| Look behind (hold) | C | Right stick click |
| Skip intro | Enter | Start |
| Pause menu (resume, restart, settings, quit to menu) | Esc or P | Start or View |

Hold drift through a corner and let go for a mini-turbo (longer holds give super and ultra boosts). Hold gas
just before GO for a rocket start.

### Phones as controllers (Party Mode)

Pick **Party Mode** in the main menu (or launch with `-KartRoom`). The game hosts a room on the Movecade room server
(`party.movecade.com`), and phones join with the existing Movecade controller: scan the QR (it opens
`movecade.com/join/<CODE>`), or scan it from the Movecade app. Players pick a name and look, ready up, and the race
starts. Each phone steers its kart by tilting, or with touch steering. "Race again" sends everyone back to the lobby.

The production server must admit the Unreal host first: a one-file server change, with its reasons, tests and
deploy steps in [Docs/Port/server](Docs/Port/server/README.md). Until it is deployed, the lobby says the server
doesn't accept this game as a host yet.

**Local server instead** (`-RoomLocal`, nothing deployed):
1. Start the room server: `.\Tools\net-sim\run-party.ps1`. It clones movecade-v2 into `Tools/.cache` the first
   time; your checkout is never touched.
2. Serve the controller from that clone: `npx vite --port 5173` in `Tools/.cache/movecade`.
3. Launch the game with `-KartRoom -RoomLocal`.

No phone handy? `.\Tools\net-sim\e2e.ps1` races a simulated phone against bots on the local server.

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
| `-KartRoom` | Phones join through the lobby (production room server; add `-RoomLocal` for `npm run dev:party` on this PC) |
| `-KartLegacyWalls` | The web build's sticky walls (B11), for comparison |
| `-KartAutoDrive`, `-KartShots=4,10`, `-KartQuitAfter=30` | Automated checks: self-driving, screenshots, quit |
| `-KartNoFrontEnd` | Skip the menus and race at once (any flag above does the same) |
| `-KartUiShots=title,menu,racers,rules,tracks,settings,audio,controls,credits,quit,race,pause,results,lobby` | Screenshot tour: steps through those screens, captures `Saved/Screenshots/WindowsEditor/KartUi_<step>_<W>x<H>.png`, quits (`.\Tools\ui\shots.ps1`). Race flags still apply to its race (`-KartLaps=1 -KartAutoDrive`) |
| `-KartUiPad` | Show gamepad button hints regardless of input (for screenshots) |
| `-KartCinematic` | Trailer camera: cuts every 4.5 s between the chase view and tracking shots round your kart |
| `-KartCapture=<dir>`, `-KartCaptureFrom=s`, `-KartCaptureFor=s`, `-KartCaptureNoUI` | Write every frame of a time window (use with `-benchmark -fps=60`; `Tools/video/capture.ps1` does this and encodes the MP4) |
| `-KartFxShowcase[=explosion\|lightning\|prism\|rocket\|ink\|hit\|confetti\|box\|items]` | Plays effects on your kart for screenshots, without touching the race |

## Develop

- C++ lives in `Source/MovecadeKart` (`Sim/` is the deterministic 60 Hz kart and item simulation in web units).
  Build: `Engine/Build/BatchFiles/Build.bat MyProjectEditor Win64 Development -Project=<path>\MyProject.uproject`.
- Tests: `.\Tools\ue\test.ps1` builds and runs all 52 automation tests headless (`-NoBuild`, `-Filter MovecadeKart.Sim`).
  `.\Tools\ue\play.ps1` runs the game with the autopilot and takes screenshots.
- Video: `.\Tools\video\capture.ps1` records a clip and `.\Tools\video\trailer.ps1` builds the trailer. Both need the
  tools venv once: `py -3.13 -m venv Tools\.cache\venv`, then `Tools\.cache\venv\Scripts\pip install imageio-ffmpeg`.
- Effects: `Source/MovecadeKart/.../FX` and `Content/FX`. Regenerate the content only with `Tools/fx/build_fx.ps1`,
  with no editor open on the project.
- Track data: `Tools/track-export` turns the web game's track modules into `Data/Tracks/*.track.json`
  (`npm run export`, `npm run check`).
- UI: `Source/MovecadeKart/.../UI` (Slate, drawn in code; see the header comments of `KartScreen.h` and
  `KartUISubsystem.h`). Fonts and UI sounds: `Tools/ui/fetch_fonts.ps1`, `Tools/ui/make_ui_sounds.py`, then
  `.\Tools\ue\run.ps1 import_ui_assets.py -NullRhi`.
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
