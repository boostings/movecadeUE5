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

### UI (UX workstream)

| Asset | Source | Licence | Used for |
|---|---|---|---|
| Fredoka (Medium, SemiBold, Bold) | Google Fonts (static instances), Tools/ui/fetch_fonts.ps1; licence text in Tools/ui/fonts/Fredoka-OFL.txt | SIL Open Font License 1.1 | /Game/UI/Fonts/Fredoka-*: titles, buttons, values |
| Nunito (Bold, ExtraBold, Black) | Google Fonts, same script; Tools/ui/fonts/Nunito-OFL.txt | SIL Open Font License 1.1 | /Game/UI/Fonts/Nunito-*: body text and labels |
| Lilita One (Regular) | Google Fonts, same script; Tools/ui/fonts/LilitaOne-OFL.txt | SIL Open Font License 1.1 | /Game/UI/Fonts/LilitaOne-Regular: logo, place numbers, countdown, banners |
| UI sounds ui_* and music_menu_loop | Synthesised in this repo by Tools/ui/make_ui_sounds.py (numpy; no samples) | Project-owned | /Game/UI/Sounds/* on the SC_UI / SC_Music sound classes |
| SC_Music, SC_UI, SM_KartVolumes | Created by Tools/ue/import_ui_assets.py | Project-owned | Volume settings (master / music / SFX) |

The logo, item icons, button glyphs (keyboard key caps; gamepad buttons drawn by position, with no console
artwork) and every UI shape are drawn in code (Source/MovecadeKart/.../UI), so the UI ships no images.
