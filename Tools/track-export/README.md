# track-export

Exports the shipped Movecade kart tracks from movecade-v2 to `Data/Tracks/<trackId>.track.json`
(schema: [Docs/Port/03 §4.3](../../Docs/Port/03-tracks-scenery-assets.md)) and their scenery binaries to
`out/` (git-ignored). It reads the movecade repo and never writes to it, and doesn't need movecade's
`node_modules`.

```bash
npm install
npm run export   # bundles src/export.ts against the movecade sources, writes the JSON and out/
npm run check    # compares the output with the values measured in Docs/Port/03
```

Then import the binaries and rebuild the levels (headless editor, from the project root):

```powershell
.\Tools\ue\run.ps1 import_track_assets.py      # out/ -> Content/Tracks (materials, meshes, textures)
.\Tools\ue\run.ps1 make_track_map.py @{ KART_TRACK = 'violet-loop'; KART_MAP = 'L_VioletLoop' }
.\Tools\ue\run.ps1 make_track_map.py @{ KART_TRACK = 'sunset-harbor'; KART_MAP = 'L_SunsetHarbor' }
```

Set `MOVECADE_ROOT` if movecade-v2 isn't at `<Documents>/Projects/movecade/movecade-v2`. The commit hash in
each file's `source.commit` gets `-dirty` appended when movecade's `app/src` has uncommitted changes.

All values are in source units: metres, three.js axes, and `yaw` = three.js rotation.y. The UE importer
converts once: `UE.X = -z*100, UE.Y = x*100, UE.Z = (y + rebaseY)*100, UE.Yaw = -yaw` in degrees. UE's glTF
import maps (x, y, z) to UE (x, z, y), so ATrackBuilder puts every imported mesh under a +90 degree yaw.

## What `out/` holds

| File | From | Used as |
|---|---|---|
| `violet/violet_loop.glb` | `public/models/violet-loop.glb`, Draco + WebP decoded, mount `[155,-28,15] x0.08` and the +1.605 m rebase baked in; only the road (Object_24), its line paint (Object_25, kerbs recoloured violet: `material_19_violet`) and the verge (Object_11, the sim's dirt) | the road, lines and lawn (Nanite) |
| `violet/violet_garden.glb` | `src/violetGarden.ts` (original): terrain, hedges and plinths on the barrier blocks' outlines, the start gate on the start structure's footprint, garden masts | world meshes (Nanite) |
| `violet/violet_garden_props.glb` | `src/violetGarden.ts`: pillars and lanterns, crystals, trees, lavender, flower beds, topiaries, gazebo, conservatory, balloons | instanced prefabs (Nanite) |
| `harbor/harbor_coast.glb` | `src/harborCoast.ts` (original): seawalls, rumble kerbs, seabed, palms, dune grass | world meshes and prefabs (Nanite) |
| `shared/movecade_props.glb`, `shared/textures/mc_*.png` | `src/props.ts` (original) | Movecade props and their branded faces (billboards, boost pad, pennants) |
| `shared/textures/env_*.png` | `src/envTextures.ts` (generated, seeded) | the sky's cloud layer, the water's wave normals |
| `shared/surfaces/T_<set>_{color,normal,rough}.png` | movecade-v2 `public/textures/surfaces` (Poly Haven CC0), normals flipped to DirectX | world-space detail on the surface and foliage masters (`detailSurfaces` in the JSON) |
| `shared/violet_prefabs.glb` | `tracksideGeometry.ts`, `violetTrees.ts`, plus a 1 m sign plane | grandstands, banners; signs and the sea plane |
| `harbor/harbor_ribbons.glb` | `harborRibbon()` for the web ribbons the coast pass doesn't redraw (road, lines, shoulder, beaches, surf) | the road and beaches |
| `harbor/harbor_scenery.glb` | `buildHarborScenery()` colour batches, each with the CC0 surface its colour stands for; rocks as grass-topped sandstone | static props (Nanite) |
| `harbor/harbor_motion.glb` | `HarborMotion.tsx`, mirrored part by part | the 8 animated actors |
| `*/textures/*.png` | canvas drawings (`harborTextures.ts`, banner faces, start signs) via `@napi-rs/canvas` | textures |

Gameplay never moves: `check.mjs` pins each track's gameplay data (everything the sim reads) by fingerprint,
and checks that every one of Violet's sweep walls has art on it (`meshes[].wallCoverage`).

`src/violetMesh.mjs` also measures the decoded GLB for the sim: `collision.walls` (the wall faces the web's
sweep and probe rays meet, as 2D segments with face normals; rules in `collision.wallRules`) and
`collision.dirtGrid` (where the terrain ray's first floor hit is the sand verge, Object_11), and for the
rebuild: `barriers` (Object_13's blocks as closed outlines: footprint on a 0.25 m grid, morphological closing,
contour tracing, Douglas-Peucker) and `gantry` (Object_10's pieces).

The scenery modules are React components; `build.mjs` stubs React, r3f, drei and the render helpers, and
appends `export { buildHarborScenery }` / `export { BANNER_FACES, bannerTexture }` to those files' source in
memory only.
