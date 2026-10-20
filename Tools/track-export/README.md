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
.\Tools\ue\run.ps1 make_track_map.py @{ KART_TRACK = 'neon-city'; KART_MAP = 'L_NeonCity' }
```

`Data/Tracks/neon-city.track.json` is not exported from movecade: `src/neonCity.ts` generates it, an original
track on Harbor's schema and code paths (closed Catmull-Rom, road 28 m, walls at ±16.3, tightest bend 26.9 m, so
no wall offset folds; Docs/Port/00-PLAN.md B14), with its scenery in `out/neon`.

Set `MOVECADE_ROOT` if movecade-v2 isn't at `<Documents>/Projects/movecade/movecade-v2`. The commit hash in
each file's `source.commit` gets `-dirty` appended when movecade's `app/src` has uncommitted changes.

All values are in source units: metres, three.js axes, and `yaw` = three.js rotation.y. The UE importer
converts once: `UE.X = -z*100, UE.Y = x*100, UE.Z = (y + rebaseY)*100, UE.Yaw = -yaw` in degrees. UE's glTF
import maps (x, y, z) to UE (x, z, y), so ATrackBuilder puts every imported mesh under a +90 degree yaw.

## What `out/` holds

| File | From | Used as |
|---|---|---|
| `violet/violet_loop.glb` | `public/models/violet-loop.glb`, Draco + WebP decoded, mount `[155,-28,15] x0.08` and the +1.605 m rebase baked in, Object_22 dropped, the 53 voxel trees cut from Object_18; IP cleanup: warp pipes (Object_12), brick blocks (Object_47) and clouds cut, barrier studs flattened and the barrier palette recoloured (`material_8_recoloured`) | the circuit (Nanite), with its own palette materials |
| `shared/movecade_props.glb`, `shared/textures/mc_*.png` | `src/props.ts` (original) | Movecade props and their branded faces; the props that replace Violet's motifs (`scenery.instances[].replaces`) |
| `shared/surfaces/T_<set>_{color,normal,rough}.png` | movecade-v2 `public/textures/surfaces` (Poly Haven CC0), normals flipped to DirectX | world-space detail on `M_TrackSurface*` (`detailSurfaces` in the JSON) |
| `violet/violet_kerbs.glb` | `kerbGeometry(samples, plan.kerbs)` | world-space kerbs |
| `shared/violet_prefabs.glb` | `tracksideGeometry.ts`, `violetTrees.ts`, plus a 1 m sign plane | instanced grandstands, lamps, banners, trees; signs and the sea |
| `harbor/harbor_ribbons.glb` | `harborRibbon()` for the 16 ribbons | replaces the procedural graybox ribbons |
| `harbor/harbor_scenery.glb` | `buildHarborScenery()` colour batches (incl. the coral tunnel) | static props (Nanite) |
| `harbor/harbor_motion.glb` | `HarborMotion.tsx`, mirrored part by part | the 8 animated actors |
| `*/textures/*.png` | canvas drawings (`harborTextures.ts`, banner faces, start signs) via `@napi-rs/canvas` | textures |

`src/violetMesh.mjs` also measures the decoded GLB for the sim: `collision.walls` (the wall faces the web's
sweep and probe rays meet, as 2D segments with face normals; rules in `collision.wallRules`) and
`collision.dirtGrid` (where the terrain ray's first floor hit is the sand verge, Object_11).

The scenery modules are React components; `build.mjs` stubs React, r3f, drei and the render helpers, and
appends `export { buildHarborScenery }` / `export { BANNER_FACES, bannerTexture }` to those files' source in
memory only.
