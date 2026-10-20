// Writes every texture of a GLB to a folder, named <material>_<slot>.<ext>, for review.
//   node textures.mjs <file.glb> <outDir>
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import draco3d from 'draco3dgltf';
import fs from 'node:fs';
import path from 'node:path';

const [file, dir] = process.argv.slice(2);
const io = new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({ 'draco3d.decoder': await draco3d.createDecoderModule() });
const doc = await io.read(file);
fs.mkdirSync(dir, { recursive: true });
const seen = new Set();
for (const m of doc.getRoot().listMaterials()) {
  for (const slot of ['BaseColor', 'MetallicRoughness', 'Normal', 'Emissive', 'Occlusion']) {
    const tex = m[`get${slot}Texture`]();
    if (!tex) continue;
    const ext = tex.getMimeType() === 'image/png' ? 'png' : tex.getMimeType() === 'image/webp' ? 'webp' : 'jpg';
    const name = `${tex.getName() || m.getName() + '_' + slot}.${ext}`;
    if (seen.has(name)) continue;
    seen.add(name);
    const size = tex.getSize();
    fs.writeFileSync(path.join(dir, name), tex.getImage());
    console.log(`${m.getName()} ${slot} -> ${name} ${size?.join('x')}`);
  }
}
