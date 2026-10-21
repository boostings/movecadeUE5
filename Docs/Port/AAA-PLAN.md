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

## Status (2026-10-20)

Every workstream is merged on master; 51 automation tests pass, and the packaged Win64 build
(`Build/Package/Windows/MyProject.exe`) runs both tracks, every menu and split screen with no errors.

| Stream | Result |
|---|---|
| Rendering | Hardware RT Lumen (hit-lit reflections at Ultra), VSM, TSR, dynamic resolution; presets, RT and motion blur settings; split-screen budget and frame-time guard; a global grade. About 95 fps solo at a 1440p-equivalent load, 60+ in four-way split. |
| Environments | Violet Loop rebuilt as the original Violet Gardens (gameplay data fingerprinted unchanged); Sunset Harbor's coast, water, kerbs and sky upgraded. Neon City is parked on branch `tracks-neon-city-wip`. |
| VFX | Niagara boost flames, tiered drift sparks, tyre marks, dust, impact sparks, a stylised explosion, lightning, ink, confetti; item meshes modelled in code with IP-safe looks (prize orbs, buzz discs, drone, cone, toy rocket). |
| UX and UI | Slate front end (title, menus, racer and track select, settings, credits), HUD, pause, results, lobby restyle, multi-pad navigation, UI sounds and music. |
| Feel and camera | FKartFeel chassis and camera springs, look-behind, driver reach, item throw and finish cheer, intro dolly, results orbit; bot skills; determinism test. |
| Code quality and portfolio | Review fixes (sim step split into phases, loader validation, per-event impacts, race clock), [Architecture.md](../Architecture.md), [the case study](../Portfolio/README.md) with a trailer and gameplay video. |

Open: key remapping (the controls page is read-only), Neon City's art pass, and phones acting on haptics (that
needs a change in movecade-v2's phone code).

## Status (2026-10-20, later): the living track and the drivers

- **Track life** (`ATrackLife`): Violet's lawn verges carry about 400k geometry grass tufts and flowers on a baked
  lawn height grid (`Data/Tracks/violet-loop.lawn.json`). The grass sways in a rolling wind and lies down in a
  lighter trail wherever a kart crosses it: two ping-ponged 1024² render targets, decayed and stamped per frame,
  sampled by the grass material. The grandstands are rebuilt with about 370 spectators who track the nearest kart,
  jump and wave when one passes, and cheer at GO, laps and the finish. Also butterflies that scatter, daylight
  fireworks over the gate, and wind on every instanced plant. It costs about 1.4 ms GPU and 1.5 ms render thread
  in solo view (110 fps at 1080p). `-KartNoLife` turns it off.
- **Drivers:** softer skin (the 512 px AO atlas at a third of its strength, a lighter albedo bake, a warm rim),
  stylised hair sheen and rim, eye catchlights and per-face iris colours, and spring-driven hair and hem
  swing in the native driver pose, and cartoon proportions (head x1.14, hands x1.2).

