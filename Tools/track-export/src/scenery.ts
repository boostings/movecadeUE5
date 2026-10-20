/**
 * Scenery binaries for the UE port (Docs/Port/03 §4.2, §5): prefab and scenery meshes as GLB, and the
 * canvas-drawn textures as PNG, all under Tools/track-export/out (git-ignored, rebuilt by `npm run export`).
 * Tools/ue/import_track_assets.py imports them; the track JSON says where each lands in /Game.
 *
 * Geometry comes from the live movecade modules (tracksideGeometry, violetTrees, harborRibbon,
 * buildHarborScenery via the build's stub plugin). HarborMotion.tsx's animated actors are JSX, so their
 * parts are rebuilt here as three.js scene graphs that mirror the JSX one to one (refs per actor).
 *
 * Every mesh stays in three.js axes and metres. UE's glTF import maps (x, y, z) to UE (x, z, y), so the
 * builder adds a +90 degree yaw to land on the port's convention (UE.X = -z, UE.Y = x, UE.Z = y).
 */
import { Document, NodeIO, type Node as GNode } from '@gltf-transform/core';
import { createCanvas } from '@napi-rs/canvas';
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { bannerGeometry, grandstandGeometry, lampHeadGeometry, lampPostGeometry } from '@app/games/kart/scene/tracksideGeometry';
import { softCanopyGeometry, trunkGeometry, TREE_BARK } from '@app/games/kart/scene/violetTrees';
import { drawVioletLoopBanner } from '@app/games/kart/tracks/violetBanner';
// @ts-expect-error appended to the in-memory source by build.mjs's stub plugin
import { BANNER_FACES, bannerTexture } from '@app/games/kart/tracks/VioletTrackside';
// @ts-expect-error appended to the in-memory source by build.mjs's stub plugin
import { buildHarborScenery, harborRibbon } from '@app/games/kart/tracks/HarborTrack';
import { harborPoint } from '@app/games/kart/tracks/harbor';
import { asphaltTexture, cobbleTexture, grainTexture, sandTexture, stoneTexture, waterTexture } from '@app/games/kart/tracks/harborTextures';
import { HARBOR_DROPPED_BATCHES, HARBOR_REDRAWN_RIBBONS, dressRock, exportHarborCoast, harborBatchMaterial } from './harborCoast';

type V3 = [number, number, number];

/** Material hints for ATrackBuilder (Content/Tracks/Shared/Materials masters). */
export interface MaterialHint {
  /** surface: M_TrackSurface, Map (UV0) x Color x vertex colour x world-space CC0 detail (props.ts SURFACE_SETS). */
  shader: 'vertexColor' | 'textured' | 'unlitTexture' | 'translucent' | 'additive' | 'surface' | 'boostPad';
  /** surface: the CC0 set (top-level detailSurfaces[] key), its tile in metres, strengths, and triplanar (walls, props) vs top-down. */
  detail?: string;
  triplanar?: boolean;
  albedo?: number;
  normal?: number;
  rough?: number;
  /** surface: desaturates the detail colour (0..1) before it multiplies the base. */
  desaturate?: number;
  /** surface: desaturates the base (palette or canvas) colour. */
  baseDesaturate?: number;
  /** A texture asset for Map given by its /Game path (an imported GLB palette) instead of a textures[] name. */
  textureAsset?: string;
  color?: string;
  roughness?: number;
  metalness?: number;
  emissive?: number;
  opacity?: number;
  texture?: string;
  /** World metres per texture tile, for textures on world-space UVs. */
  tileM?: number;
}

export interface MeshEntry {
  node: string;
  ueAsset: string;
  material: MaterialHint;
  /** false: the instances cast no shadow (sky dressing far above the road). */
  castShadow?: boolean;
}

const ue = (dir: string, file: string, node: string) => `${dir}/${path.basename(file, '.glb')}/StaticMeshes/${node}`;

// ---- GLB writer ---------------------------------------------------------------------------------

export class GlbFile {
  doc = new Document();
  buffer = this.doc.createBuffer();
  scene = this.doc.createScene();
  nodes: GNode[] = [];
  constructor(readonly file: string, readonly ueDir: string) {}

  /** Adds `geometry` as a node named `name`; vertex colours, UVs and normals are kept. */
  add(name: string, geometry: THREE.BufferGeometry, material: MaterialHint): MeshEntry {
    const g = geometry;
    if (!g.getAttribute('normal')) g.computeVertexNormals();
    const acc = (type: 'VEC2' | 'VEC3' | 'SCALAR', array: Float32Array | Uint32Array) =>
      this.doc.createAccessor().setType(type).setArray(array).setBuffer(this.buffer);
    const prim = this.doc.createPrimitive()
      .setAttribute('POSITION', acc('VEC3', Float32Array.from(g.getAttribute('position').array)))
      .setAttribute('NORMAL', acc('VEC3', Float32Array.from(g.getAttribute('normal').array)));
    const uv = g.getAttribute('uv');
    if (uv) {
      // three.js textures default to flipY (v = 1 at the image top); glTF puts v = 0 at the top.
      const flipped = Float32Array.from(uv.array);
      for (let i = 1; i < flipped.length; i += 2) flipped[i] = 1 - flipped[i]!;
      prim.setAttribute('TEXCOORD_0', acc('VEC2', flipped));
    }
    // Always write vertex colours (white when the source has none): the masters multiply by them, and UE
    // gives a mesh without a colour channel black ones.
    const color = g.getAttribute('color');
    const count = g.getAttribute('position').count;
    const c = color && color.itemSize === 3 ? Float32Array.from(color.array) : new Float32Array(count * 3).fill(1);
    prim.setAttribute('COLOR_0', acc('VEC3', c));
    if (g.index) prim.setIndices(acc('SCALAR', Uint32Array.from(g.index.array)));
    prim.setMaterial(this.doc.createMaterial(name).setRoughnessFactor(material.roughness ?? 0.8).setMetallicFactor(0).setDoubleSided(true));
    const mesh = this.doc.createMesh(name).addPrimitive(prim);
    const node = this.doc.createNode(name).setMesh(mesh);
    this.scene.addChild(node);
    this.nodes.push(node);
    return { node: name, ueAsset: ue(this.ueDir, this.file, name), material };
  }

  async write(outDir: string) {
    mkdirSync(outDir, { recursive: true });
    const bytes = await new NodeIO().writeBinary(this.doc);
    writeFileSync(path.join(outDir, this.file), bytes);
  }
}

/** Vertex colours from each mesh's material colour, every mesh baked into `root`'s frame and merged. */
export function bake(root: THREE.Object3D): THREE.BufferGeometry {
  root.updateMatrixWorld(true);
  const inverse = root.matrixWorld.clone().invert();
  const parts: THREE.BufferGeometry[] = [];
  root.traverse((o) => {
    const mesh = o as THREE.Mesh;
    if (!mesh.isMesh) return;
    let g = mesh.geometry.clone();
    g = g.index ? g.toNonIndexed() : g;
    g.applyMatrix4(new THREE.Matrix4().multiplyMatrices(inverse, mesh.matrixWorld));
    const c = (mesh.material as THREE.MeshBasicMaterial).color ?? new THREE.Color(1, 1, 1);
    const colors = new Float32Array(g.getAttribute('position').count * 3);
    for (let i = 0; i < colors.length; i += 3) { colors[i] = c.r; colors[i + 1] = c.g; colors[i + 2] = c.b; }
    g.setAttribute('color', new THREE.BufferAttribute(colors, 3));
    for (const key of Object.keys(g.attributes)) if (!['position', 'normal', 'color'].includes(key)) g.deleteAttribute(key);
    parts.push(g);
  });
  const merged = mergeGeometries(parts, false)!;
  merged.computeVertexNormals();
  return merged;
}

export const mat = (color: string) => new THREE.MeshBasicMaterial({ color });
export function mesh(geometry: THREE.BufferGeometry, color: string, position: V3 = [0, 0, 0], rotation: V3 = [0, 0, 0]) {
  const m = new THREE.Mesh(geometry, mat(color));
  m.position.set(...position);
  m.rotation.set(...rotation);
  return m;
}
export function group(children: THREE.Object3D[], position: V3 = [0, 0, 0], rotation: V3 = [0, 0, 0]) {
  const g = new THREE.Group();
  g.position.set(...position);
  g.rotation.set(...rotation);
  children.forEach((c) => g.add(c));
  return g;
}
const box = (w: number, h: number, d: number) => new THREE.BoxGeometry(w, h, d);
const cyl = (...a: ConstructorParameters<typeof THREE.CylinderGeometry>) => new THREE.CylinderGeometry(...a);
const sphere = (...a: ConstructorParameters<typeof THREE.SphereGeometry>) => new THREE.SphereGeometry(...a);

// ---- Textures -----------------------------------------------------------------------------------

export interface TextureEntry { name: string; file: string; ueAsset: string; srgb: true; tileM?: number; width: number; height: number }

export function savePng(outDir: string, ueDir: string, name: string, canvas: { toBuffer(mime: 'image/png'): Buffer; width: number; height: number }, tileM?: number): TextureEntry {
  mkdirSync(outDir, { recursive: true });
  const file = `${name}.png`;
  writeFileSync(path.join(outDir, file), canvas.toBuffer('image/png'));
  return { name, file, ueAsset: `${ueDir}/Textures/${name}`, srgb: true, width: canvas.width, height: canvas.height, ...(tileM ? { tileM } : {}) };
}
// The stubbed canvasTexture keeps the @napi-rs canvas as `image`.
export type StubTexture = { image: { toBuffer(mime: 'image/png'): Buffer; width: number; height: number } };

// ---- Violet ------------------------------------------------------------------------------------

const VIOLET_DIR = '/Game/Tracks/VioletLoop';
const SHARED_DIR = '/Game/Tracks/Shared';

/** violetBanner.tsx:28-33: the start sign plane, facing +Z, unlit. */
const VIOLET_SIGN = { position: [0.86, 13.94, -30.82] as V3, size: [26.4, 4.4] as [number, number] };

export async function exportVioletScenery(outRoot: string) {
  const outDir = path.join(outRoot, 'violet');
  // Prefabs (local +Z toward the road, base at y=0): tracksideGeometry.ts, violetTrees.ts;
  // materials from VioletTrackside.tsx:82-90 and VioletTrees.tsx:26-28.
  const prefabs = new GlbFile('violet_prefabs.glb', SHARED_DIR);
  const prefab: Record<string, MeshEntry> = {
    grandstand: prefabs.add('grandstand', grandstandGeometry(), { shader: 'vertexColor', roughness: 0.78 }),
    lamp_post: prefabs.add('lamp_post', lampPostGeometry(), { shader: 'vertexColor', roughness: 0.45, metalness: 0.6 }),
    lamp_head: prefabs.add('lamp_head', lampHeadGeometry(), { shader: 'vertexColor', color: '#fff4d6', roughness: 0.3, emissive: 0.4 }),
    banner_board: prefabs.add('banner_board', bannerGeometry(), { shader: 'textured', roughness: 0.6, texture: 'violet_banner_{variant}' }),
    tree_canopy: prefabs.add('tree_canopy', softCanopyGeometry(2), { shader: 'vertexColor', roughness: 0.88 }),
    tree_trunk: prefabs.add('tree_trunk', trunkGeometry(), { shader: 'vertexColor', color: TREE_BARK, roughness: 0.95 }),
    // A 1 x 1 m plane facing +Z (UV 0..1, v down the image) for signs: scale by the sign's size.
    sign_plane: prefabs.add('sign_plane', new THREE.PlaneGeometry(1, 1), { shader: 'unlitTexture' }),
  };
  await prefabs.write(path.join(outRoot, 'shared'));

  // Banner faces 512x102 (VioletTrackside.tsx:17-46) and the start sign 1920x320 (violetBanner.tsx:35, :50-81).
  const texDir = path.join(outDir, 'textures');
  const textures: TextureEntry[] = (BANNER_FACES as unknown[]).map((face, i) =>
    savePng(texDir, VIOLET_DIR, `violet_banner_${i}`, (bannerTexture(face) as StubTexture).image));
  const sign = createCanvas(1920, 320);
  drawVioletLoopBanner(sign.getContext('2d') as unknown as Parameters<typeof drawVioletLoopBanner>[0], 1920, 320);
  textures.push(savePng(texDir, VIOLET_DIR, 'violet_start_banner', sign));

  return {
    prefabs: prefab,
    signs: [{ name: 'violet-start-banner', position: VIOLET_SIGN.position, size: VIOLET_SIGN.size, yaw: 0, texture: 'violet_start_banner', unlit: true }],
    textures,
  };
}

// ---- Sunset Harbor -----------------------------------------------------------------------------

const HARBOR_DIR = '/Game/Tracks/SunsetHarbor';
const HARBOR_PALETTE = ['#e88c68', '#6da9a0', '#efcf99', '#98b5b5', '#e3ba68'];

/** HarborMotion.tsx:15-18. */
function beachY(offset: number) {
  const o = Math.abs(offset);
  return o <= 17 ? 0 : o >= 40 ? -6.9 : -0.06 - (o - 17) / 23 * 6.84;
}

/** HarborTrack.tsx:821-841, the start sign (the canvas is drawn inline in the component). */
function drawHarborSign() {
  const canvas = createCanvas(1536, 192);
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = '#254d57';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.strokeStyle = '#f3c898';
  ctx.lineWidth = 6;
  ctx.strokeRect(12, 12, 1512, 168);
  ctx.textAlign = 'center';
  ctx.fillStyle = '#fff0cc';
  ctx.font = '800 94px sans-serif';
  ctx.fillText('SUNSET HARBOR', 768, 114);
  ctx.font = '500 25px sans-serif';
  ctx.fillStyle = '#f3c898';
  ctx.fillText('M O V E C A D E   /   C O A S T A L   G R A N D   P R I X', 768, 159);
  return canvas;
}

export interface HarborRibbonSpec { name: string; inner: number; outer: number; innerY: number; outerY: number; material: string; stripePeriodSamples?: number }

/** The Harbor ribbon materials (HarborTrack.tsx:890-946); textures tile on 8 m (harborRibbon UVs are metres / 8). */
function ribbonMaterial(material: string): MaterialHint {
  switch (material) {
    // The canvas texture on UV0 plus the CC0 detail the web layers on top (HarborTrack.tsx:884-905).
    case 'asphalt': return { shader: 'surface', color: '#5c6a6b', texture: 'harbor_asphalt', roughness: 0.86, detail: 'asphalt_track', tileM: 4.5, albedo: 0.6, normal: 0.55, rough: 0.6, desaturate: 0.35 };
    case 'dirtShoulder': return { shader: 'surface', color: '#d5b58a', texture: 'harbor_cobble', roughness: 1, detail: 'gravelly_sand', tileM: 3, albedo: 0.5, normal: 0.6, rough: 0.5 };
    case 'barrier': return { shader: 'surface', color: '#8fb0ad', texture: 'harbor_stone', roughness: 0.8, detail: 'painted_concrete', triplanar: true, tileM: 2.4, albedo: 0.45, normal: 0.6, rough: 0.5 };
    case 'sand': return { shader: 'surface', color: '#e6cc9e', texture: 'harbor_sand', roughness: 1, detail: 'gravelly_sand', tileM: 4, albedo: 0.5, normal: 0.6, rough: 0.4 };
    case 'curb': return { shader: 'surface', roughness: 0.7, detail: 'painted_concrete', tileM: 2, albedo: 0.25, normal: 0.4, rough: 0.4 };
    case 'edgeLine': return { shader: 'vertexColor', color: '#fff0cc', roughness: 0.9 };
    case 'wallCap': return { shader: 'vertexColor', color: '#fff0cc', roughness: 0.8 };
    case 'surf': return { shader: 'translucent', color: '#f4f7ec', opacity: 0.85 };
    case 'shallows': return { shader: 'translucent', color: '#7fd0cc', opacity: 0.7 };
    default: return { shader: 'vertexColor', color: '#808080' };
  }
}

export async function exportHarborScenery(outRoot: string, ribbons: readonly HarborRibbonSpec[]) {
  const outDir = path.join(outRoot, 'harbor');

  // Ribbons: harborRibbon() itself, so UVs (metres / 8), asphalt tones and curb stripes match.
  const ribbonFile = new GlbFile('harbor_ribbons.glb', HARBOR_DIR);
  // The coast pass (harborCoast.ts) draws the walls, caps, curbs and shallows its own way.
  const ribbonParts = ribbons.filter((r) => !HARBOR_REDRAWN_RIBBONS.has(r.name)).map((r) => {
    const striped = r.material === 'asphalt' ? 'asphalt' : r.material === 'curb';
    return ribbonFile.add(`ribbon_${r.name}`, harborRibbon(r.inner, r.outer, r.innerY, r.outerY, striped), ribbonMaterial(r.material));
  });
  await ribbonFile.write(outDir);

  // Static props: buildHarborScenery's colour batches (HarborTrack.tsx:67-818), material :961-965.
  const sceneryFile = new GlbFile('harbor_scenery.glb', HARBOR_DIR);
  // Each colour batch gets the CC0 surface its colour stands for (plaster, timber, sandstone, leaves).
  const batches = (buildHarborScenery() as { color: string; geometry: THREE.BufferGeometry }[]).filter((b) => !HARBOR_DROPPED_BATCHES.has(b.color));
  const sceneryParts = batches.map(({ color, geometry }) => sceneryFile.add(`hs_${color.replace('#', '')}`, dressRock(geometry, color), harborBatchMaterial(color)));
  await sceneryFile.write(outDir);

  // Where the web's scenery stands (2 m cells), so the coast pass dresses only free beach.
  const taken = new Set<string>();
  const cell = (x: number, z: number) => `${Math.floor(x / 2)},${Math.floor(z / 2)}`;
  for (const { geometry } of batches) {
    const pos = geometry.getAttribute('position');
    for (let i = 0; i < pos.count; i++) if (pos.getY(i) > -6.5) taken.add(cell(pos.getX(i), pos.getZ(i)));
  }
  const occupied = (x: number, z: number) => {
    for (let dx = -3; dx <= 3; dx += 1.5) for (let dz = -3; dz <= 3; dz += 1.5) if (taken.has(cell(x + dx, z + dz))) return true;
    return false;
  };
  const coast = await exportHarborCoast(outDir, occupied);

  // Animated actors (HarborMotion.tsx), one node per moving part, each in its own pivot's frame.
  const motion = new GlbFile('harbor_motion.glb', HARBOR_DIR);
  const parts: Record<string, MeshEntry> = {};
  // Baked in the frame of a fresh pivot, so each part's own offset and rotation inside its JSX group stay.
  const add = (name: string, root: THREE.Object3D, material: MaterialHint) => { parts[name] = motion.add(name, bake(group([root])), material); };
  const lit = (roughness = 0.7): MaterialHint => ({ shader: 'vertexColor', roughness });
  // :21-42 lighthouse beams (additive) and lamp.
  // The beams are soft light shafts (M_TrackAdditive fades their silhouette edges), fainter than the web's.
  add('lighthouse_beams', group([0, Math.PI].map((a) => group([mesh(cyl(9, 0.8, 140, 24, 1, true), '#ffe9a8', [70, 0, 0], [0, 0, -Math.PI / 2])], [0, 0, 0], [0, a, 0]))),
    { shader: 'additive', color: '#ffe9a8', opacity: 0.07 });
  add('lighthouse_lamp', mesh(sphere(1.5, 12, 8), '#fff6cc'), { shader: 'translucent', color: '#fff6cc', opacity: 1 });
  // :45-113 Ferris wheel: frame in the base frame, wheel about the hub (y 11.6), gondolas about their pivots.
  const legs = [-1, 1].flatMap((s) => [mesh(box(0.5, 13, 0.5), '#325e68', [s * 4, 6, 1.6], [0, 0, s * 0.36]), mesh(box(0.5, 13, 0.5), '#325e68', [s * 4, 6, -1.6], [0, 0, s * 0.36])]);
  add('ferris_frame', group([...legs, mesh(cyl(0.6, 0.6, 4.2, 12), '#e88c68', [0, 11.6, 0], [Math.PI / 2, 0, 0])]), lit(0.65));
  const radius = 9;
  const spokes: THREE.Object3D[] = [];
  for (let i = 0; i < 10; i++) {
    const angle = (i / 10) * Math.PI * 2;
    for (const z of [-1.4, 1.4]) spokes.push(mesh(box(0.16, radius, 0.16), '#e3ba68', [Math.cos(angle) * radius / 2, Math.sin(angle) * radius / 2, z], [0, 0, angle + Math.PI / 2]));
  }
  add('ferris_wheel', group([...[-1.4, 1.4].map((z) => mesh(new THREE.TorusGeometry(radius, 0.28, 8, 48), '#fff0cc', [0, 0, z])), ...spokes]), lit(0.6));
  HARBOR_PALETTE.forEach((c, k) => add(`ferris_gondola_${k}`, group([mesh(box(1.5, 1.2, 1.6), c, [0, -0.9, 0]), mesh(box(1.9, 0.12, 2), '#fff0cc', [0, 0.1, 0])]), lit()));
  // :116-157 ferry and cargo ship.
  add('ferry', group([
    mesh(box(60, 9, 14), '#fff0cc'), mesh(box(60, 1.4, 14.2), '#d86f50', [0, -4, 0]), mesh(box(46, 7, 12), '#fff0cc', [0, 8.5, 0]),
    mesh(box(30, 4, 10), '#6da9a0', [0, 13.5, 0]), mesh(cyl(1.3, 1.6, 5, 12), '#d86f50', [-6, 18, 0]),
  ]), lit(0.8));
  add('ferry_smoke', mesh(sphere(2.6, 8, 6), '#b8c4c4', [-6, 21.5, 0]), { shader: 'translucent', color: '#b8c4c4', opacity: 0.55 });
  add('cargo_ship', group([
    mesh(box(90, 9, 18), '#325e68'), mesh(box(90, 1.4, 18.2), '#d86f50', [0, -4, 0]),
    ...['#e88c68', '#e3ba68', '#6da9a0', '#efcf99', '#98b5b5', '#e88c68'].map((c, s) => mesh(box(9, 6 + (s % 2) * 3, 14), c, [-32 + s * 11, 7.5 + (s % 2) * 1.5, 0])),
    mesh(box(10, 12, 15), '#fff0cc', [36, 10.5, 0]), mesh(cyl(1.6, 2, 5, 12), '#d86f50', [40, 18.5, 0]),
  ]), lit(0.8));
  // :159-196 jet skis and wakes.
  ['#e88c68', '#6da9a0', '#e3ba68'].forEach((c, i) => add(`jet_ski_${i}`, group([
    mesh(box(1.4, 0.6, 3), c), mesh(box(0.7, 0.5, 1.6), '#325e68', [0, 0.55, 0.3]), mesh(sphere(0.32, 8, 6), '#fff0cc', [0, 1.1, 0.2]),
  ]), lit(0.55)));
  add('jet_wake', mesh(new THREE.PlaneGeometry(3.5, 9), '#f4f7ec', [0, 0, 0], [-Math.PI / 2, 0, 0]), { shader: 'translucent', color: '#f4f7ec', opacity: 0.45 });
  // :199-237 gulls: a body and two wings about the body's pivot.
  add('gull_body', mesh(box(0.3, 0.2, 0.7), '#fff0cc'), lit(0.9));
  add('gull_wing_l', mesh(box(1.6, 0.04, 0.4), '#fff0cc', [-0.8, 0, 0]), lit(0.9));
  add('gull_wing_r', mesh(box(1.6, 0.04, 0.4), '#fff0cc', [0.8, 0, 0]), lit(0.9));
  // :240-268 fountain jet and spray ring.
  add('fountain_jet', mesh(cyl(0.18, 0.32, 2.8, 8), '#e6fbff'), { shader: 'translucent', color: '#e6fbff', opacity: 0.75 });
  add('fountain_spray', mesh(new THREE.TorusGeometry(0.9, 0.22, 6, 16), '#f4f7ec'), { shader: 'translucent', color: '#f4f7ec', opacity: 0.5 });
  // :271-304 buoys (colour by i % 2) and their blinking lamps.
  ['#6da9a0', '#e88c68'].forEach((c, k) => add(`buoy_${k}`, group([mesh(cyl(0.7, 0.9, 1.2, 10), c), mesh(cyl(0.12, 0.12, 1.4, 6), '#325e68', [0, 1.2, 0])]), lit(0.6)));
  add('buoy_lamp', mesh(sphere(0.3, 8, 6), '#ffe9a8', [0, 2.05, 0]), { shader: 'translucent', color: '#ffe9a8', opacity: 1 });
  await motion.write(outDir);

  const ferris = harborPoint(12.2 / 29, 38);
  const gulls = harborPoint(27.6 / 29, 26);
  const fountain = harborPoint(24.4 / 29, -37);
  const r4 = (n: number) => Math.round(n * 10000) / 10000;
  const animated = [
    { type: 'lighthouseBeam', position: [-11, 33, -190], params: { yawRadPerS: 0.7 }, ref: 'HarborMotion.tsx:12, :21-42' },
    { type: 'ferrisWheel', position: [r4(ferris.x), r4(beachY(38)), r4(ferris.z)], yaw: r4(ferris.yaw), params: { hubY: 11.6, radius: 9, seats: 10, rollRadPerS: 0.25 }, ref: 'HarborMotion.tsx:45-113' },
    { type: 'ferry', position: [130, -3.5, -35], params: { radiusX: 330, radiusZ: 300, angularRadPerS: 0.02 }, ref: 'HarborMotion.tsx:116-141' },
    { type: 'cargoShip', position: [130, -3.5, -35], params: { radiusX: 420, radiusZ: 400, angularRadPerS: -0.012, phase: 2.2 }, ref: 'HarborMotion.tsx:126-153' },
    { type: 'jetSki', position: [110, -6.6, -45], params: { count: 3, radius: 24, angularRadPerS: 0.55 }, ref: 'HarborMotion.tsx:159-196' },
    { type: 'gullFlock', position: [r4(gulls.x), 0, r4(gulls.z)], params: { count: 9 }, ref: 'HarborMotion.tsx:199-237' },
    { type: 'fountain', position: [r4(fountain.x), 0, r4(fountain.z)], params: { pulseRadPerS: 1.6, sprayRadPerS: 0.8 }, ref: 'HarborMotion.tsx:240-268' },
    ...[3.0, 3.8, 4.6, 5.4].map((t, i) => {
      const p = harborPoint(t / 29, 50 + (i % 2) * 4);
      return { type: 'buoy', position: [r4(p.x), -6.6, r4(p.z)], variant: i % 2, params: { phase: r4(i * 1.7) }, ref: 'HarborMotion.tsx:271-304' };
    }),
  ];

  // Canvas textures (harborTextures.ts, all tile on 8 m) and the start sign.
  const texDir = path.join(outDir, 'textures');
  const tex = (name: string, t: unknown) => savePng(texDir, HARBOR_DIR, name, (t as StubTexture).image, 8);
  const textures = [
    tex('harbor_asphalt', asphaltTexture()), tex('harbor_sand', sandTexture()), tex('harbor_water', waterTexture()),
    tex('harbor_stone', stoneTexture()), tex('harbor_grain', grainTexture()), tex('harbor_cobble', cobbleTexture()),
    savePng(texDir, HARBOR_DIR, 'harbor_start_sign', drawHarborSign()),
  ];

  return {
    meshes: [
      { file: 'harbor_ribbons.glb', decoded: 'Tools/track-export/out/harbor/harbor_ribbons.glb', worldSpace: true, replacesRibbons: true, collision: false, nanite: false, ueAssets: ribbonParts.map((p) => p.ueAsset), parts: ribbonParts },
      { file: 'harbor_scenery.glb', decoded: 'Tools/track-export/out/harbor/harbor_scenery.glb', worldSpace: true, collision: false, nanite: true, ueAssets: sceneryParts.map((p) => p.ueAsset), parts: sceneryParts },
      coast.mesh,
    ],
    coastPrefabs: coast.prefabs,
    coastInstances: coast.instances,
    motionParts: parts,
    animated,
    // HarborTrack.tsx:948-951: plane 32 x 4 at (3, 13, -29.48), facing +Z, unlit.
    signs: [{ name: 'harbor-start-banner', position: [3, 13, -29.48], size: [32, 4], yaw: 0, texture: 'harbor_start_sign', unlit: true }],
    // The sea (HarborTrack.tsx:957-960): water texture repeated 160 times over 5000 m, UV scroll (0.012 t, 0.006 t + 0.01 sin 0.3 t).
    sea: { texture: 'harbor_water', repeat: 160, roughness: 0.34, metalness: 0.15, scroll: [0.012, 0.006] },
    textures,
  };
}
