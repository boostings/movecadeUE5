# AAA pass: plan

The port baseline is done (00-PLAN.md). This pass turns it into a portfolio piece: AAA-quality graphics, code and UX.

## Jimmy's decisions (2026-10-19)

| Topic | Decision |
|---|---|
| Art direction | **Polished Nintendo-like**: clean, saturated, soft materials and lighting (the Mario Kart 8 Deluxe *feel*, with no Nintendo IP). Realistic rendering (GI, ray-traced shadows and reflections, PBR), cartoony shapes and palette. |
| Violet Loop | **Rebuilt original.** The racing line, walls, checkpoints and gameplay stay exactly the same; all the art is new and IP-safe. No pipes, bricks or anything that reads as Mario. |
| Performance | **Max visuals at 60 fps in single view** at 1440p on an RTX 4070 SUPER: hardware Lumen, ray-traced shadows and reflections, Nanite, TSR. Split screen scales down automatically to hold 60. |
| Game feel | **Improve it**: visual suspension, chassis lean and bounce, squash and stretch, camera polish, drift and boost juice. The web's core numbers (speeds, drift tiers, items) stay; `-KartLegacyWalls` and any parity switches stay available. |

## Workstreams (one owner each, so parallel work doesn't collide)

| Stream | Owner | Owns | Done when |
|---|---|---|---|
| Rendering | render agent | `Config/DefaultEngine.ini` rendering/RT settings, `Source/.../Render/*` (quality subsystem: solo vs split scalability, settings apply), `Content/Rendering` (global post-process and grade material, auto exposure, motion blur, AO, lens) | Hardware RT Lumen + RT shadows/reflections + VSM + TSR on; 60 fps in solo view at 1440p; split screen scales; Nintendo-like grade |
| Environments | maps agent | `TrackBuilder*`, `TrackScenery*`, `Tools/track-export`, `Data/Tracks`, `Content/Tracks`, `Content/Maps`, per-track sky, sun, fog and water | Harbor upgraded (materials, bevelled geometry, water, foliage); Violet rebuilt original on the same line; gameplay data unchanged; tests pass |
| VFX | VFX agent | `Race/KartItemPresenter*`, `Source/.../FX/*`, `Content/FX`, item meshes | Niagara for items, boosts, drift sparks, skid marks, dust, explosions, hits; item meshes and materials |
| UX and UI | UX agent | `Source/.../UI/*` (new), `Content/UI`, the HUD (`KartHUD*`, `SKartRaceOverlay*`), the lobby widget's look | Title and main menu, local race setup (track, vehicle, character, bots, laps), settings (graphics, audio, controls), pause and results menus, redesigned in-race HUD, transitions, full gamepad and keyboard navigation |
| Feel and camera | lead | `KartPawn*`, `KartMovementComponent*`, `KartDriverAnimInstance*`, camera | Visual suspension and chassis dynamics, camera polish (FOV kick, boost shake, drift framing), tuned against video captures |
| Code quality and portfolio | lead, last | reviews, docs | Code review pass, architecture doc, README, captured screenshots and a short trailer (Movie Render Queue), `Docs/Portfolio/` case study |

The rules from WORKSTREAMS.md still apply. In particular:
- Never write to movecade-v2; free Fab assets only, logged in ASSETS.md.
- Build with `-NoHotReloadFromIDE`; close any editor or game you start.
- Gameplay stays deterministic; every test passes.
