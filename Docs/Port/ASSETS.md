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
