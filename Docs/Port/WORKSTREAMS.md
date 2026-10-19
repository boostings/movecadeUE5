# Workstreams and shared conventions

Several agents work on the port at once, each in its own git worktree and branch. This page is the contract
between them. Read it with [00-PLAN.md](00-PLAN.md) and the spec for your area.

## Ground rules

- **Never modify the movecade-v2 checkout** (`C:\Users\Jimmy\Documents\Projects\movecade\movecade-v2`). It is
  read-only source. For anything that needs `npm ci` or a dev server, make a local clone first:
  `git clone --local <movecade-v2> <your worktree>/Tools/.cache/movecade` (ignored by git).
- **Room server: local only.** Run PartyKit from that clone. Never change, or deploy to, staging or production.
- **Fab:** free assets only, under Fab's standard licence. Log every asset in [ASSETS.md](ASSETS.md)
  (name, Fab URL, licence, where it's used). Never buy anything.
- Ask the user nothing. Pick sensible defaults and record them in your report.
- Parity first: port the shipped behaviour with the spec's constants. Fixes follow the bug table in 00-PLAN.md.
- Commit on your own branch with clear messages, ending with the
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` line. Don't push (there is no remote).

## Build, test, run

All paths are relative to your worktree root, which holds `MyProject.uproject`.

```powershell
# Build (UBT -WaitMutex queues behind any other agent's build)
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MyProjectEditor Win64 Development "-Project=<root>\MyProject.uproject" -WaitMutex

# Automation tests, headless
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "<root>\MyProject.uproject" "-ExecCmds=Automation RunTests MovecadeKart; Quit" -unattended -nullrhi -nosplash -nosound "-abslog=<log>"

# Run the game windowed with the test hooks: autopilot, screenshots at those seconds, quit
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "<root>\MyProject.uproject" /Game/Maps/L_SunsetHarbor -game -windowed -ResX=1280 -ResY=720 -nosound -KartAutoDrive "-KartShots=4,12" -KartQuitAfter=20 -log=KartGame.log
#   screenshots: Saved/Screenshots/WindowsEditor/HighresScreenshotNNNNN.png   log: Saved/Logs/KartGame.log

# Editor Python, headless (full editor; the script quits it)
.\Tools\ue\run.ps1 <script.py> @{ VAR = 'value' }
```

- If a build fails with "Live Coding is active", an editor for **that** project is open. Close only an editor you started.
- Look at your screenshots (Read the PNG) before you call visual work done.
- Port 8000 errors in logs come from a second editor's MCP server and are harmless.

## Code layout (module `MovecadeKart`)

| Area | Files | Owner |
|---|---|---|
| Sim (web units, no UObjects) | `Public/Sim/*`, `Private/Sim/*` | race (lead) |
| Kart actor, input, HUD, game mode | `KartPawn`, `KartMovementComponent`, `KartPlayerController`, `KartHUD`, `KartGameMode` | race (lead) |
| Race flow | `Public/Race/*`, `Private/Race/*` | race (lead) |
| Tracks | `TrackBuilder`, `Tools/track-export`, `Data/Tracks`, `Content/Tracks` | tracks agent |
| Audio | `Public/Audio/*`, `Private/Audio/*`, `Content/Audio` | audio agent |
| Phone host | `Public/Net/*`, `Private/Net/*`, `Content/UI/Lobby` | network agent |
| Items | `Public/Items/*`, `Private/Items/*` | items agent (later) |

Stay inside your area. If you need a small change in someone else's file, keep it minimal and say so in your
report; the lead merges it. Units: the sim is in web units, and `KartCoords.h` converts to UE (cm, Z up).

## Contracts

**Kart state.** `UKartMovementComponent::SimState()` returns a `MovecadeKart::FKartState` (Speed in SU, Yaw,
DriftDirection/DriftPower, Turbo, BoostMultiplier/ActiveBoost, HopTime/HopOffset, bOnDirt, bHitWallThisStep,
LapsCompleted, NextCheckpoint, ...). Treat it as read-only outside the sim.

**Seat input.** A kart is driven by a `MovecadeKart::FKartInput` (SteeringX positive = LEFT, the web
convention; bLeft/bRight keys; bForward gas; bBackward brake/reverse; bJump drift). The race layer gives each
seat (0..3) one kart. The network agent produces inputs per seat from phone frames (§3.1 of spec 01: gamma ->
clamp(d/90) -> 2° deadzone rescale -> invert). It exposes them as
`bool URoomHostSubsystem::GetSeatInput(int32 Seat, MovecadeKart::FKartInput& Out) const`; the lead wires this into the race.

**Room host API** (network agent; `UGameInstanceSubsystem`): connect to a local PartyKit, create a room, expose
`JoinCode`, `JoinUrl`, a QR `UTexture2D`, seats (player id, name, avatar/paint, connected), and delegates
(seats changed, start requested with a track id, pause changed). Outbound: publish race phase and countdown,
send haptics `{seat, intensity, collisionType, sustained}`, and submit results (finishers only, ascending
times, never a DNF or a 0 ms score).

**Audio API** (audio agent): `UKartAudioComponent` on the kart reads the sim state each tick (engine layers,
drift, boost, surfaces, walls), plus `PlayCue(FName)` for one-shots that other systems (race, items) trigger.

**Track API** (tracks agent): `ATrackBuilder::GetTrack()` returns the parsed `MovecadeKart::FTrack`. A level for
each track: `L_SunsetHarbor`, `L_VioletLoop`. Mesh tracks give the sim 2D wall segments in the JSON's
`collision.walls`, so the sim stays analytic and deterministic.
