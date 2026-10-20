// Decodes the Movecade vehicle, driver and outfit GLBs for UE import.
//
//   node decode.mjs [<movecade-v2 root>]
//
// Reads movecade-v2/app/public (never writes there) and writes plain, uncompressed GLBs to
// Tools/art/.cache/out/ (git-ignored), plus mounts.json with every vehicle's contract nodes.
//
// What changes on the way through (everything else is byte-for-byte the shipped art):
//  * Draco is decoded (UE's glTF importer does not read KHR_draco_mesh_compression).
//  * kart.glb: the root's x10 scale and the unused `shape` mesh are dropped (Kart.jsx renders the
//    body/wheel geometry at scale 1 and never draws `shape`).
//  * Vehicles: the contract nodes stay; the `seat` empty is kept only in mounts.json.
//  * Avatars and outfits: materials that several parts share are split per part, so the importer makes
//    one material section per toggle. UE can then hide a section the way three.js hides a node:
//      Hair_<style>           hair  -> hair_<style>      (eight hair styles, one shown)
//      Accessory_cap/headband accessoryFabric -> acc_cap / acc_headband;  glasses frame -> acc_glasses
//      Neck, Arm_*, Hand_*, Thumb_*   skin -> skin_body   (hidden under an outfit, clothingAsset.ts)
//      Garment_waist          pants -> waist             (the jersey "detail" band)
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS, KHRDracoMeshCompression } from '@gltf-transform/extensions';
import { prune, sparse } from '@gltf-transform/functions';
import draco3d from 'draco3dgltf';
import fs from 'node:fs';
import zlib from 'node:zlib';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const v2 = process.argv[2] ?? 'C:/Users/Jimmy/Documents/Projects/movecade/movecade-v2';
const pub = path.join(v2, 'app', 'public');
const out = path.join(here, '.cache', 'out');

const io = new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({ 'draco3d.decoder': await draco3d.createDecoderModule() });

async function load(file) {
  const doc = await io.read(file);
  for (const ext of doc.getRoot().listExtensionsUsed()) {
    if (ext.extensionName === KHRDracoMeshCompression.EXTENSION_NAME) ext.dispose();
  }
  return doc;
}

// Per output file: every material's glTF factors and texture names, for the UE material script.
const materialTable = {};
const slots = ['BaseColor', 'MetallicRoughness', 'Normal', 'Emissive', 'Occlusion'];

async function save(doc, rel) {
  const dest = path.join(out, rel);
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  // Dense morph targets: the shipped files store them sparse, which not every importer reads.
  await doc.transform(prune({ keepLeaves: true, keepAttributes: true }), sparse({ ratio: 0 }));
  const stem = path.basename(rel, '.glb');
  const mats = {};
  for (const m of doc.getRoot().listMaterials()) {
    const entry = { base: round(m.getBaseColorFactor()), rough: m.getRoughnessFactor(), metal: m.getMetallicFactor(), tex: {} };
    for (const slot of slots) {
      const tex = m[`get${slot}Texture`]();
      if (!tex) continue;
      // Unnamed images (kart.glb) get a stable name, so the UE script can find the imported Texture2D.
      if (!tex.getName()) tex.setName(`${stem}_${m.getName()}_${slot}`.replace(/[^A-Za-z0-9_]/g, '_'));
      entry.tex[slot] = tex.getName();
    }
    mats[m.getName()] = entry;
  }
  materialTable[rel] = mats;
  await io.write(dest, doc);
  console.log(`wrote ${rel} (${fs.statSync(dest).size} B)`);
}

const round = (v) => v.map((x) => Math.round(x * 1e5) / 1e5);
function nodeInfo(node) {
  return node ? { t: round(node.getTranslation()), r: round(node.getRotation()) } : null;
}

function splitMaterial(doc, nodeName, from, to) {
  const node = doc.getRoot().listNodes().find((n) => n.getName() === nodeName);
  const mesh = node?.getMesh();
  if (!mesh) return false;
  let made = doc.getRoot().listMaterials().find((m) => m.getName() === to);
  for (const prim of mesh.listPrimitives()) {
    const mat = prim.getMaterial();
    if (mat?.getName() !== from) continue;
    if (!made) made = mat.clone().setName(to);
    prim.setMaterial(made);
  }
  return true;
}

// --- vehicles -------------------------------------------------------------------------------------
const mounts = {};
const vehicles = { kart: path.join(pub, 'models', 'kart.glb') };
for (const v of ['moto', 'coupe', 'truck']) vehicles[v] = path.join(pub, 'game-assets', 'vehicles', `${v}.glb`);

for (const [id, file] of Object.entries(vehicles)) {
  const doc = await load(file);
  const root = doc.getRoot();
  const byName = (n) => root.listNodes().find((x) => x.getName() === n);
  if (id === 'kart') {
    byName('shape')?.dispose();
    const top = byName('root');
    top.setScale([1, 1, 1]);
  }
  const mount = {};
  for (const n of ['seat', 'd_wheel', 'handlebar', 'booster', 'trim', 'wheel_0', 'wheel_1', 'wheel_2', 'wheel_3']) {
    const info = nodeInfo(byName(n));
    if (info) mount[n] = info;
  }
  // Wheel radius from the geometry (rest height of the hub above the road is the radius).
  for (const n of ['wheel_0', 'wheel_1', 'wheel_2', 'wheel_3']) {
    const mesh = byName(n)?.getMesh();
    if (!mesh) continue;
    let min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
    for (const p of mesh.listPrimitives()) {
      const a = p.getAttribute('POSITION');
      const lo = a.getMin([]), hi = a.getMax([]);
      for (let i = 0; i < 3; i++) { min[i] = Math.min(min[i], lo[i]); max[i] = Math.max(max[i], hi[i]); }
    }
    mount[n].min = round(min);
    mount[n].max = round(max);
  }
  mounts[id] = mount;
  await save(doc, `vehicles/${id}.glb`);
}
fs.writeFileSync(path.join(out, 'mounts.json'), JSON.stringify(mounts, null, 2));

// --- drivers and outfits --------------------------------------------------------------------------
const hairStyles = ['short', 'bangs', 'puff', 'tail', 'crop', 'curls', 'bob', 'bun'];
const bodyParts = (name) => name === 'Neck' || /^(Arm_|Hand_|Thumb_)/.test(name);

function splitAvatar(doc) {
  for (const style of hairStyles) splitMaterial(doc, `Hair_${style}`, 'hair', `hair_${style}`);
  splitMaterial(doc, 'Accessory_cap', 'accessoryFabric', 'acc_cap');
  splitMaterial(doc, 'Accessory_headband', 'accessoryFabric', 'acc_headband');
  splitMaterial(doc, 'Accessory_glasses', 'frame', 'acc_glasses');
  splitMaterial(doc, 'Garment_waist', 'pants', 'waist');
  for (const node of doc.getRoot().listNodes()) {
    if (bodyParts(node.getName())) splitMaterial(doc, node.getName(), 'skin', 'skin_body');
  }
}

// The avatars ship as 65 skinned nodes (36 for an outfit) on one skin. UE's importer makes a skeletal
// mesh per node, so the parts are merged into one glTF mesh: one primitive per part, keeping its
// material, and the union of the morph targets on every primitive (zeros where a part has none; glTF
// needs the same target list on every primitive of a mesh). Every part node has an identity transform.
function mergeSkinned(doc, name) {
  const root = doc.getRoot();
  const parts = root.listNodes().filter((n) => n.getMesh() && n.getSkin());
  if (parts.length < 2) return;
  const skin = parts[0].getSkin();
  const parent = parts[0].getParentNode();
  const morphs = [];
  for (const n of parts) {
    for (const t of n.getMesh().getExtras()?.targetNames ?? []) if (!morphs.includes(t)) morphs.push(t);
  }
  const merged = doc.createMesh(name);
  const zeros = new Map();
  const buffer = root.listBuffers()[0];
  for (const n of parts) {
    if (n.getSkin() !== skin) throw new Error(`${name}: ${n.getName()} has another skin`);
    const mesh = n.getMesh();
    const names = mesh.getExtras()?.targetNames ?? [];
    for (const prim of mesh.listPrimitives()) {
      const own = prim.listTargets();
      for (const t of own) prim.removeTarget(t);
      const count = prim.getAttribute('POSITION').getCount();
      for (const m of morphs) {
        const i = names.indexOf(m);
        if (i >= 0 && own[i]) {
          // The writer takes targetNames from the targets' own names.
          prim.addTarget(own[i].setName(m));
          continue;
        }
        let zero = zeros.get(count);
        if (!zero) {
          zero = doc.createAccessor().setType('VEC3').setArray(new Float32Array(count * 3)).setBuffer(buffer);
          zeros.set(count, zero);
        }
        prim.addTarget(doc.createPrimitiveTarget(m).setAttribute('POSITION', zero));
      }
      merged.addPrimitive(prim);
    }
    n.dispose();
    mesh.dispose();
  }
  if (morphs.length) {
    merged.setWeights(morphs.map(() => 0));
    merged.setExtras({ targetNames: morphs });
  }
  const node = doc.createNode(name).setMesh(merged).setSkin(skin);
  (parent ?? root.listScenes()[0]).addChild(node);
  console.log(`  ${name}: merged ${parts.length} parts, ${merged.listPrimitives().length} primitives, morphs ${morphs.join(',')}`);
}

const faces = ['kai', 'leo', 'mira', 'nia', 'omar', 'rose'];
for (const face of faces) {
  for (const body of ['female', 'male']) {
    const doc = await load(path.join(pub, 'game-assets', 'avatar4', `${face}-${body}.glb`));
    splitAvatar(doc);
    mergeSkinned(doc, 'Avatar');
    await save(doc, `avatar4/${face}-${body}.glb`);
  }
}

const catalog = JSON.parse(fs.readFileSync(path.join(pub, 'game-assets', 'clothing', 'catalog.json'), 'utf8'));
for (const { outfit, body } of catalog) {
  const doc = await load(path.join(pub, 'game-assets', 'clothing', `${outfit}-${body}.glb`));
  mergeSkinned(doc, 'Outfit');
  await save(doc, `clothing/${outfit}-${body}.glb`);
}
fs.writeFileSync(path.join(out, 'materials.json'), JSON.stringify(materialTable, null, 1));

// Tiny default textures for the master materials' texture parameters (a linear white so a mask/MR
// sampler has a valid default; sRGB white and black for colour samplers).
function png(rgba, size = 4) {
  const crcTable = Array.from({ length: 256 }, (_, n) => {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c >>> 0;
  });
  const crc = (buf) => {
    let c = 0xffffffff;
    for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  };
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]);
    const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
    return Buffer.concat([len, td, c]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(size, 0); ihdr.writeUInt32BE(size, 4);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  const row = Buffer.concat([Buffer.from([0]), Buffer.concat(Array(size).fill(Buffer.from(rgba)))]);
  const raw = Buffer.concat(Array(size).fill(row));
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}
fs.mkdirSync(path.join(out, 'defaults'), { recursive: true });
fs.writeFileSync(path.join(out, 'defaults', 'T_Default_White.png'), png([255, 255, 255, 255]));
fs.writeFileSync(path.join(out, 'defaults', 'T_Default_WhiteLinear.png'), png([255, 255, 255, 255]));
fs.writeFileSync(path.join(out, 'defaults', 'T_Default_Black.png'), png([0, 0, 0, 255]));
console.log('done');
