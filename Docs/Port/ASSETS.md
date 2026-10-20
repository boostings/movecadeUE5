# Third-party assets

Every asset that didn't originate in this repo or movecade-v2 is listed here with its source and licence.

| Asset | Source | Licence | Used for |
|---|---|---|---|
| UE BasicShapes (cube, cylinder, sphere, plane, BasicShapeMaterial) | Unreal Engine 5.8 engine content | UE EULA | Graybox meshes and colours |
| `WhiteSquareTexture` | Unreal Engine 5.8 engine content | UE EULA | Default `Map` of the track master materials |
| Poly Haven `asphalt_track` 1k (colour, normal GL, roughness) | https://polyhaven.com/a/asphalt_track, via movecade-v2 `app/public/textures/surfaces` (`LEDGER.json`) | CC0-1.0 | Road detail on all tracks (`/Game/Tracks/Shared/Surfaces/T_asphalt_track_*`) |
| Poly Haven `painted_concrete` 1k | https://polyhaven.com/a/painted_concrete, same ledger | CC0-1.0 | Barriers, walls, kerbs, plinths, gantries |
| Poly Haven `leafy_grass` 1k | https://polyhaven.com/a/leafy_grass, same ledger | CC0-1.0 | Violet's hills, Neon City's verges |
| Poly Haven `gravelly_sand` 1k | https://polyhaven.com/a/gravelly_sand, same ledger | CC0-1.0 | Violet's sand verge, Harbor's beach and shoulder |
| Poly Haven `rock_face` 1k | https://polyhaven.com/a/rock_face, same ledger | CC0-1.0 | Rock outcrops (the Violet brick-stack replacements) |
| Poly Haven `dark_wooden_planks` 1k | https://polyhaven.com/a/dark_wooden_planks, same ledger | CC0-1.0 | Imported for boardwalk/deck surfaces (Neon City's waterfront deck) |

Assets ported from movecade-v2 (owned by the project): `kart.glb`, `violet-loop.glb` (confirmed usable by Jimmy
on 2026-10-19), the `kart-*.wav` audio set, and the track data in `Data/Tracks`. `Content/Tracks` holds the
decoded Violet circuit and the scenery, prefab meshes and canvas textures generated from movecade-v2 code by
`Tools/track-export` (no Fab or other third-party assets).

The Poly Haven sets above are copied from movecade-v2's own ledger (project-owned copies of CC0 assets); the
exporter converts WebP to PNG and flips the normal maps' green channel (OpenGL to DirectX). Everything else the
maps pass added is original: the Movecade props (`Tools/track-export/src/props.ts`: advertising columns,
billboards, sponsor arches, rock outcrops, plinths, flags, tyre stacks, palms, boost pad), their canvas-drawn
Movecade artwork (`mc_*` textures), and Neon City's track, buildings and signs (`src/neonCity.ts`).

**Violet Loop IP cleanup.** The decoded Violet GLB no longer draws the old "mario-circuit" motifs: the six warp
pipes (`Object_12`), the brick blocks (`Object_47`: stair stacks, floating rows, the gantry feet) and the puffy
clouds are cut from the visual mesh, and the four-colour studded barrier blocks are flattened and repainted
violet/cream. Collision is unchanged (the sim's walls come from the JSON, measured before the cut).
