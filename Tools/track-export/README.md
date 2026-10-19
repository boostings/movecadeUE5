# track-export

Exports the shipped Movecade kart tracks from movecade-v2 to `Data/Tracks/<trackId>.track.json`
(schema: [Docs/Port/03 §4.3](../../Docs/Port/03-tracks-scenery-assets.md)). It reads the movecade repo and
never writes to it. It also doesn't need movecade's `node_modules`.

```bash
npm install
npm run export   # bundles src/export.ts against the movecade sources and writes the JSON
npm run check    # compares the output with the values measured in Docs/Port/03
```

Set `MOVECADE_ROOT` if movecade-v2 isn't at `../../../../Projects/movecade/movecade-v2`. The commit hash in
each file's `source.commit` gets `-dirty` appended when movecade's `app/src` has uncommitted changes.

All values are in source units: metres, three.js axes, and `yaw` = three.js rotation.y. The UE importer
converts once: `UE.X = -z*100, UE.Y = x*100, UE.Z = (y + rebaseY)*100, UE.Yaw = -yaw` in degrees.

Not exported yet (phase 1b, listed in each file's `exportNotes`): Harbor props, tunnel, animated actors and
signs; prefab meshes; canvas textures; the decoded Violet GLB.
