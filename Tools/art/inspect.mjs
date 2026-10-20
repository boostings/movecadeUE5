// Prints the structure of a GLB (nodes, meshes, materials, skins, morphs, bounds) for the import scripts.
//   node inspect.mjs <file.glb> [--verbose]
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { getBounds } from '@gltf-transform/core';
import draco3d from 'draco3dgltf';

const file = process.argv[2];
const verbose = process.argv.includes('--verbose');
const io = new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({ 'draco3d.decoder': await draco3d.createDecoderModule() });
const doc = await io.read(file);
const root = doc.getRoot();
const r = (v) => v.map((x) => +x.toFixed(3));

console.log('extensions used:', root.listExtensionsUsed().map((e) => e.extensionName).join(', '));
console.log('scenes', root.listScenes().length, 'nodes', root.listNodes().length, 'meshes', root.listMeshes().length,
  'materials', root.listMaterials().length, 'textures', root.listTextures().length, 'skins', root.listSkins().length,
  'animations', root.listAnimations().length);

function walk(node, depth) {
  const mesh = node.getMesh();
  const skin = node.getSkin();
  let extra = '';
  if (mesh) {
    const prims = mesh.listPrimitives();
    const tris = prims.reduce((n, p) => n + (p.getIndices() ? p.getIndices().getCount() / 3 : p.getAttribute('POSITION').getCount() / 3), 0);
    const mats = [...new Set(prims.map((p) => p.getMaterial()?.getName()))].join('|');
    const morphs = prims[0]?.listTargets().length ?? 0;
    const names = mesh.getExtras()?.targetNames;
    extra = ` mesh=${mesh.getName()} prims=${prims.length} tris=${tris} mats=${mats}` + (morphs ? ` morphs=${morphs}${names ? '(' + names.join(',') + ')' : ''}` : '');
  }
  if (skin) extra += ` skin=${skin.listJoints().length}j`;
  const t = node.getTranslation(), q = node.getRotation(), s = node.getScale();
  if (verbose || depth < 4 || mesh) {
    console.log(`${'  '.repeat(depth)}${node.getName()} t=${r(t)} r=${r(q)} s=${r(s)}${extra}`);
  }
  for (const c of node.listChildren()) walk(c, depth + 1);
}
for (const scene of root.listScenes()) {
  for (const n of scene.listChildren()) walk(n, 0);
  const b = getBounds(scene);
  console.log('scene bounds min', r(b.min), 'max', r(b.max));
}
for (const m of root.listMaterials()) {
  const tex = ['getBaseColorTexture', 'getMetallicRoughnessTexture', 'getNormalTexture', 'getEmissiveTexture', 'getOcclusionTexture']
    .map((f) => m[f]() ? `${f.slice(3, -7)}=${m[f]().getName() || m[f]().getURI()}(${m[f]().getMimeType()})` : null).filter(Boolean).join(' ');
  console.log(`material ${m.getName()} base=${r(m.getBaseColorFactor())} metal=${m.getMetallicFactor()} rough=${m.getRoughnessFactor()} ${m.getAlphaMode()} ${tex} ext=${m.listExtensions().map((e) => e.extensionName).join(',')}`);
}
for (const s of root.listSkins()) {
  console.log('skin joints:', s.listJoints().map((j) => j.getName()).join(', '));
}
