# Third-party assets

Every asset that didn't originate in this repo or movecade-v2 is listed here with its source and licence.

| Asset | Source | Licence | Used for |
|---|---|---|---|
| UE BasicShapes (cube, cylinder, sphere, plane, BasicShapeMaterial) | Unreal Engine 5.8 engine content | UE EULA | Graybox meshes and colours |
| `WhiteSquareTexture` | Unreal Engine 5.8 engine content | UE EULA | Default `Map` of the track master materials |

Assets ported from movecade-v2 (owned by the project): `kart.glb`, `violet-loop.glb` (confirmed usable by Jimmy
on 2026-10-19), the `kart-*.wav` audio set, and the track data in `Data/Tracks`. `Content/Tracks` holds the
decoded Violet circuit and the scenery, prefab meshes and canvas textures generated from movecade-v2 code by
`Tools/track-export` (no Fab or other third-party assets).

### Vehicles and drivers (art workstream)

All from movecade-v2 `app/public` (project-owned art, ported unchanged apart from decoding):

| Source (`app/public/...`) | UE location | Notes |
|---|---|---|
| `models/kart.glb` | `/Game/Karts/Kart/SM_Kart_{body,booster,d_wheel,wheel_0..3}` | Draco decoded; the root's x10 scale and the unused `shape` mesh dropped |
| `game-assets/vehicles/{moto,coupe,truck}.glb` | `/Game/Karts/{Moto,Coupe,Truck}/SM_<V>_*` | Draco decoded; node names kept (`body`, `trim`, `d_wheel`/`handlebar`, `wheel_N`); `seat` read into `AKartPawn`'s mount table |
| `game-assets/avatar4/{kai,leo,mira,nia,omar,rose}-{female,male}.glb` | `/Game/Avatars/Drivers/<face>_<body>/SK_Driver_<face>_<body>` on `/Game/Avatars/SK_Avatar4_Skeleton` | Draco decoded; the 65 skinned parts merged into one mesh (one section per material), 26 joint names and 13 morph targets kept; shared materials split per toggle (`hair_<style>`, `acc_*`, `skin_body`, `waist`) |
| `game-assets/clothing/{7 outfits}-{female,male}.glb` | `/Game/Avatars/Outfits/<outfit>_<body>/SK_Outfit_<outfit>_<body>` | parts merged into one mesh, same skeleton |

Pipeline: `Tools/art/decode.mjs` (Node, @gltf-transform 4.2.1 + draco3dgltf, raw output in the git-ignored
`Tools/art/.cache/out`), then `Tools/ue/import_karts.py` (Interchange import, master materials and instances).
Materials are new: `M_KartPaint` (clear coat), `M_KartTyre`, `M_VehiclePart`, `M_NamePlateBack`,
`M_AvatarSkin` (subsurface profile), `M_AvatarHair`, `M_AvatarCloth` (cloth shading), `M_AvatarSurface`.

### VFX (VFX workstream)

No third-party assets: everything under `/Game/FX` is generated from this repo's scripts (`Tools/fx/build_fx.ps1`).

| Asset | Made by | Notes |
|---|---|---|
| `T_FX_Star`, `T_FX_Noise`, `T_FX_Chevron`, `T_ItemBox_Question` | `Tools/fx/make_fx_textures.py` (numpy, procedural) | twinkle star, tileable value noise, boost-pad chevrons, the item box "?" glyph; PNGs in `Tools/fx/textures` |
| `M_FX_*`, `M_KartItem`, `M_ItemPrism`, `M_ItemBoxGlass`, `M_ItemBoxLabel`, `M_PP_KartScreen` | `Tools/ue/make_fx_materials.py` | particle, glow, fireball, bolt, pad, skid, item and per-view post-process materials |
| `NS_FX_{Smoke,Fire,Spark,Ring,Flare,Confetti,SpeedLines}` | `Tools/ue/make_fx_niagara.py` (Niagara toolset) | parameterised CPU particle fields |
| Item meshes (shells, banana, bomb, nitro, prism, lightning, ink, rocket, box core and glyphs, lightning strikes) | `Source/MovecadeKart/Private/FX/KartItemMeshes.cpp` | modelled in code at runtime, no asset files |

Engine content used: `/Engine/BasicShapes/{Sphere,Plane,Cube}` (glows, pad flash, skid quads, box shell) and the
Niagara plugin's `DirectionalBurst` / `Minimal` templates the systems are created from.
