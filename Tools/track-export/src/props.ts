/**
 * Movecade's own trackside props and the shared surface textures (the maps pass, Docs/Port/00-PLAN.md phase 8).
 *
 * - out/shared/movecade_props.glb: original low-poly prefabs in the style of Harbor's scenery (flat-shaded,
 *   vertex-coloured, local +Z toward the road, base at y = 0, metres): advertising columns, billboards,
 *   sponsor arches, rock formations, painted plinths, pennant flags, tyre stacks, palms.
 * - out/shared/textures/mc_*.png: their Movecade-branded faces (canvas), and the boost pad chevrons.
 * - out/shared/surfaces/<set>_{color,normal,rough}.png: the project-owned CC0 Poly Haven sets from
 *   movecade-v2 app/public/textures/surfaces (LEDGER.json), WebP -> PNG; normal maps are OpenGL (nor_gl),
 *   so their green channel is flipped here for UE's DirectX convention.
 *
 * Tools/ue/import_track_assets.py imports all three into /Game/Tracks/Shared.
 */
import { createCanvas, type SKRSContext2D } from '@napi-rs/canvas';
import { mkdirSync } from 'node:fs';
import path from 'node:path';
import sharp from 'sharp';
import * as THREE from 'three';
import { palmFrond } from '@app/games/kart/tracks/harborGeometry';
import { GlbFile, bake, group, mesh, savePng, type MaterialHint, type MeshEntry, type TextureEntry } from './scenery';

type V3 = [number, number, number];
const SHARED_DIR = '/Game/Tracks/Shared';

/** Movecade's palette (kartPaint.ts, the banner faces). */
export const BRAND = {
  violet: '#6d4bd8',
  deep: '#2b1553',
  ink: '#16102b',
  cream: '#f4f1ea',
  pink: '#f0508e',
  cyan: '#5ec8e5',
  gold: '#f7c948',
  steel: '#3a3f4b',
  rubber: '#1b1b1d',
};

// ---- Geometry helpers ---------------------------------------------------------------------------

const box = (w: number, h: number, d: number) => new THREE.BoxGeometry(w, h, d);
const cyl = (...a: ConstructorParameters<typeof THREE.CylinderGeometry>) => new THREE.CylinderGeometry(...a);

/** A seeded LCG, so every export is identical. */
function rng(seed: number) {
  let s = seed >>> 0;
  return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
}

/** A lumpy boulder: a low icosahedron with seeded radial noise. */
function boulder(radius: number, seed: number) {
  const g = new THREE.IcosahedronGeometry(radius, 1);
  const p = g.getAttribute('position');
  const rand = rng(seed);
  const bumps = new Map<string, number>();
  for (let i = 0; i < p.count; i++) {
    const k = `${p.getX(i).toFixed(3)},${p.getY(i).toFixed(3)},${p.getZ(i).toFixed(3)}`;
    if (!bumps.has(k)) bumps.set(k, 0.78 + rand() * 0.4);
    const s = bumps.get(k)!;
    p.setXYZ(i, p.getX(i) * s, p.getY(i) * s * 0.85, p.getZ(i) * s);
  }
  return g;
}

/** A box with chamfered vertical edges (an octagonal prism), base at y = 0. */
function chamferBox(w: number, h: number, d: number, c: number) {
  const shape = new THREE.Shape();
  const x = w / 2, z = d / 2;
  shape.moveTo(-x + c, -z);
  shape.lineTo(x - c, -z); shape.lineTo(x, -z + c); shape.lineTo(x, z - c); shape.lineTo(x - c, z);
  shape.lineTo(-x + c, z); shape.lineTo(-x, z - c); shape.lineTo(-x, -z + c); shape.closePath();
  const g = new THREE.ExtrudeGeometry(shape, { depth: h, bevelEnabled: false });
  g.rotateX(-Math.PI / 2);
  return g;
}

// ---- Prefabs ------------------------------------------------------------------------------------

/** Advertising column: a 2.6 m-radius poster drum (textured) on a plinth, under a violet dome. */
function adColumnTrim() {
  return bake(group([
    mesh(cyl(2.95, 3.05, 0.9, 24), BRAND.steel, [0, 0.45, 0]),
    mesh(cyl(2.8, 2.8, 0.25, 24), BRAND.cream, [0, 1.0, 0]),
    mesh(cyl(2.8, 2.8, 0.35, 24), BRAND.cream, [0, 8.25, 0]),
    mesh(cyl(3.05, 2.85, 0.45, 24), BRAND.violet, [0, 8.6, 0]),
    mesh(new THREE.SphereGeometry(2.7, 24, 8, 0, Math.PI * 2, 0, Math.PI / 2), BRAND.violet, [0, 8.8, 0]),
    mesh(cyl(0.25, 0.35, 1.4, 8), BRAND.gold, [0, 11.9, 0]),
    mesh(new THREE.SphereGeometry(0.45, 12, 8), BRAND.gold, [0, 12.8, 0]),
  ]));
}
function adColumnBody() {
  const g = cyl(2.6, 2.6, 7, 32, 1, true);
  g.translate(0, 1.1 + 3.5, 0);
  return g;
}

/** Billboard: 12 x 4 m face (textured) 9-13 m up on twin lattice legs, with a lit catwalk. */
const BILLBOARD = { width: 12, height: 4, bottom: 9 };
function billboardFrame() {
  const { width: w, height: h, bottom: b } = BILLBOARD;
  const parts: THREE.Object3D[] = [];
  for (const sx of [-1, 1]) {
    const x = sx * (w / 2 - 2);
    parts.push(mesh(box(0.7, b + h, 0.7), BRAND.steel, [x, (b + h) / 2, -0.6]));
    parts.push(mesh(box(1.6, 0.6, 1.6), BRAND.steel, [x, 0.3, -0.6]));
    for (let y = 1.5; y < b; y += 2.4) parts.push(mesh(box(0.18, 2.9, 0.18), '#555b68', [x, y + 1.2, -0.6], [0, 0, sx * 0.5]));
  }
  parts.push(mesh(box(w + 0.6, h + 0.6, 0.35), BRAND.deep, [0, b + h / 2, -0.25]));
  parts.push(mesh(box(w + 0.8, 0.25, 0.5), BRAND.violet, [0, b + h + 0.35, 0]));
  parts.push(mesh(box(w + 0.8, 0.25, 0.5), BRAND.violet, [0, b - 0.35, 0]));
  parts.push(mesh(box(w, 0.12, 1.4), '#555b68', [0, b - 0.6, 0.6]));
  for (let i = 0; i < 4; i++) parts.push(mesh(box(0.5, 0.3, 0.6), '#fff4d6', [(-1.5 + i) * w / 4, b - 0.45, 1.1], [0.5, 0, 0]));
  return bake(group(parts));
}
function billboardFace() {
  const g = new THREE.PlaneGeometry(BILLBOARD.width, BILLBOARD.height);
  g.translate(0, BILLBOARD.bottom + BILLBOARD.height / 2, 0.0);
  return g;
}

/** Sponsor arch over the road: towers at +-20 m, a 9 m-high box truss, a 32 x 3 m banner (both faces). */
const ARCH = { halfSpan: 20, top: 11, bannerWidth: 32, bannerHeight: 3 };
function archFrame() {
  const parts: THREE.Object3D[] = [];
  for (const sx of [-1, 1]) {
    const x = sx * ARCH.halfSpan;
    parts.push(mesh(box(2.2, ARCH.top, 2.2), BRAND.violet, [x, ARCH.top / 2, 0]));
    parts.push(mesh(box(2.8, 1.2, 2.8), BRAND.steel, [x, 0.6, 0]));
    parts.push(mesh(box(2.5, 0.4, 2.5), BRAND.cream, [x, ARCH.top - 0.2, 0]));
    for (let y = 2.5; y < ARCH.top - 1; y += 2.5) parts.push(mesh(box(2.3, 0.35, 2.3), BRAND.cream, [x, y, 0]));
  }
  parts.push(mesh(box(ARCH.halfSpan * 2 + 2.2, 1.4, 1.6), BRAND.deep, [0, ARCH.top - 0.7, 0]));
  parts.push(mesh(box(ARCH.halfSpan * 2 + 2.2, 0.3, 1.8), BRAND.pink, [0, ARCH.top - 1.5, 0]));
  return bake(group(parts));
}
function archBanner() {
  // Two faces back to back, so the banner reads from both directions.
  const front = new THREE.PlaneGeometry(ARCH.bannerWidth, ARCH.bannerHeight);
  front.translate(0, ARCH.top - 1.65 - ARCH.bannerHeight / 2, 0.82);
  const back = new THREE.PlaneGeometry(ARCH.bannerWidth, ARCH.bannerHeight);
  back.rotateY(Math.PI);
  back.translate(0, ARCH.top - 1.65 - ARCH.bannerHeight / 2, -0.82);
  const merged = new THREE.BufferGeometry();
  const pos: number[] = [], uv: number[] = [], idx: number[] = [];
  for (const g of [front, back]) {
    const base = pos.length / 3;
    pos.push(...g.getAttribute('position').array);
    uv.push(...g.getAttribute('uv').array);
    idx.push(...Array.from(g.index!.array, (i) => i + base));
  }
  merged.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  merged.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  merged.setIndex(idx);
  merged.computeVertexNormals();
  return merged;
}

/** A rock outcrop in a unit box (x, z in -0.5..0.5, y 0..1): seven boulders, scaled per instance. */
function rockFormation() {
  const rand = rng(97);
  const parts: THREE.Object3D[] = [];
  const tones = ['#b39a7b', '#a48b6c', '#c2a985', '#977f62'];
  const spots: Array<[number, number, number, number]> = [
    [0, 0.42, 0, 0.5], [-0.22, 0.25, 0.18, 0.34], [0.24, 0.22, -0.16, 0.32], [0.1, 0.7, 0.05, 0.3],
    [-0.18, 0.62, -0.12, 0.26], [0.28, 0.12, 0.26, 0.22], [-0.3, 0.1, -0.28, 0.2],
  ];
  spots.forEach(([x, y, z, r], i) => {
    const m = mesh(boulder(r, 11 + i * 7), tones[i % tones.length]!, [x, y, z], [rand() * 3, rand() * 3, rand() * 3]);
    parts.push(m);
  });
  return bake(group(parts));
}

/** A painted concrete plinth in a unit box (base at 0, 1 m tall), violet with a cream band. */
function plinth() {
  return bake(group([
    mesh(chamferBox(1, 0.82, 1, 0.08), BRAND.violet),
    mesh(chamferBox(1.02, 0.1, 1.02, 0.08), BRAND.cream, [0, 0.82, 0]),
    mesh(chamferBox(0.96, 0.08, 0.96, 0.08), BRAND.deep, [0, 0.92, 0]),
  ]));
}

/** A pennant flag hung from its top-left corner at the origin, flying toward +X, 3 x 1.8 m, gently waved. */
function flagCloth(colors: [string, string]) {
  const g = new THREE.PlaneGeometry(3, 1.8, 12, 4);
  const p = g.getAttribute('position');
  for (let i = 0; i < p.count; i++) {
    const x = p.getX(i) + 1.5;
    p.setXYZ(i, x, p.getY(i) - 0.9, Math.sin(x * 1.7) * 0.18 * (x / 3));
  }
  g.computeVertexNormals();
  const top = g.clone();
  const n = top.getAttribute('position').count;
  const c = new Float32Array(n * 3);
  const a = new THREE.Color(colors[0]), b = new THREE.Color(colors[1]);
  for (let i = 0; i < n; i++) {
    const y = top.getAttribute('position').getY(i);
    const x = top.getAttribute('position').getX(i);
    const band = y > -0.55 || (y < -1.25 && x < 2.2) ? a : b;
    c.set([band.r, band.g, band.b], i * 3);
  }
  top.setAttribute('color', new THREE.BufferAttribute(c, 3));
  top.deleteAttribute('uv');
  return top.toNonIndexed();
}

/** Three stacks of four tyres with a violet band, 2.4 m wide, for corner run-off. */
function tyreStacks() {
  const parts: THREE.Object3D[] = [];
  const stacks: Array<[number, number]> = [[-0.85, 0], [0.85, 0], [0, -0.9]];
  for (const [x, z] of stacks) {
    for (let k = 0; k < 4; k++) {
      parts.push(mesh(new THREE.TorusGeometry(0.36, 0.17, 6, 14), BRAND.rubber, [x, 0.17 + k * 0.33, z], [Math.PI / 2, 0, 0]));
      parts.push(mesh(cyl(0.53, 0.53, 0.08, 14, 1, true), k % 2 ? BRAND.cream : BRAND.violet, [x, 0.17 + k * 0.33, z]));
    }
  }
  return bake(group(parts));
}

/** A curved palm (Harbor's frond, original trunk), 9 m. */
function palm() {
  const parts: THREE.Object3D[] = [];
  const height = 9;
  for (let s = 0; s < 10; s++) {
    const t = s / 10, n = (s + 1) / 10;
    const a = new THREE.Vector3(t * t * 1.6, t * height - 0.3, 0);
    const b = new THREE.Vector3(n * n * 1.6, n * height, 0);
    const dir = b.clone().sub(a);
    const g = cyl(0.42 - n * 0.14, 0.48 - t * 0.14, dir.length(), 9);
    g.applyMatrix4(new THREE.Matrix4().compose(a.clone().add(b).multiplyScalar(0.5), new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize()), new THREE.Vector3(1, 1, 1)));
    parts.push(mesh(g, s % 2 ? '#a87a51' : '#ba9168'));
  }
  for (let leaf = 0; leaf < 9; leaf++) {
    parts.push(mesh(palmFrond(5.2 + (leaf % 3) * 0.6, 1.05), leaf % 2 ? '#417b64' : '#63926a', [1.6, height, 0], [0, (leaf * Math.PI * 2) / 9, 0]));
  }
  return bake(group(parts));
}

// ---- Textures ------------------------------------------------------------------------------------

function chevrons(ctx: SKRSContext2D, x: number, y: number, size: number, count: number, color: string) {
  ctx.fillStyle = color;
  for (let i = 0; i < count; i++) {
    const cx = x + i * size * 0.75;
    ctx.beginPath();
    ctx.moveTo(cx, y - size / 2); ctx.lineTo(cx + size * 0.4, y); ctx.lineTo(cx, y + size / 2);
    ctx.lineTo(cx + size * 0.22 - size * 0.4, y + size / 2); ctx.lineTo(cx + size * 0.22 - size * 0.4 + size * 0.4, y);
    ctx.lineTo(cx + size * 0.22 - size * 0.4, y - size / 2); ctx.closePath(); ctx.fill();
  }
}

/** The Movecade wordmark: bold caps with a pink-to-violet underline. */
function wordmark(ctx: SKRSContext2D, text: string, cx: number, y: number, px: number, ink: string) {
  ctx.textAlign = 'center';
  ctx.fillStyle = ink;
  ctx.font = `900 ${px}px sans-serif`;
  ctx.fillText(text, cx, y);
  const w = ctx.measureText(text).width;
  const grad = ctx.createLinearGradient(cx - w / 2, 0, cx + w / 2, 0);
  grad.addColorStop(0, BRAND.pink); grad.addColorStop(0.5, BRAND.violet); grad.addColorStop(1, BRAND.cyan);
  ctx.fillStyle = grad;
  ctx.fillRect(cx - w / 2, y + px * 0.12, w, px * 0.09);
}

const BILLBOARD_FACES = [
  (ctx: SKRSContext2D, w: number, h: number) => {
    const g = ctx.createLinearGradient(0, 0, w, h);
    g.addColorStop(0, BRAND.deep); g.addColorStop(1, '#4a2a9a');
    ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
    wordmark(ctx, 'MOVECADE', w / 2, h * 0.56, 150, BRAND.cream);
    ctx.font = '700 40px sans-serif'; ctx.fillStyle = BRAND.gold;
    ctx.fillText('PARTY GAMES ON ONE SCREEN', w / 2, h * 0.86);
  },
  (ctx: SKRSContext2D, w: number, h: number) => {
    ctx.fillStyle = BRAND.cream; ctx.fillRect(0, 0, w, h);
    ctx.fillStyle = BRAND.pink; ctx.fillRect(0, 0, w * 0.3, h);
    chevrons(ctx, w * 0.06, h / 2, 120, 3, BRAND.cream);
    ctx.textAlign = 'center'; ctx.fillStyle = BRAND.deep;
    ctx.font = '900 104px sans-serif'; ctx.fillText('DRIFT · BOOST', w * 0.65, h * 0.5);
    ctx.font = '800 64px sans-serif'; ctx.fillStyle = BRAND.violet; ctx.fillText('WIN THE CUP', w * 0.65, h * 0.8);
  },
  (ctx: SKRSContext2D, w: number, h: number) => {
    ctx.fillStyle = '#101826'; ctx.fillRect(0, 0, w, h);
    ctx.strokeStyle = BRAND.cyan; ctx.lineWidth = 10; ctx.strokeRect(14, 14, w - 28, h - 28);
    ctx.textAlign = 'center'; ctx.fillStyle = BRAND.cyan;
    ctx.font = '900 92px sans-serif'; ctx.fillText('YOUR PHONE IS THE WHEEL', w / 2, h * 0.47);
    ctx.font = '700 48px sans-serif'; ctx.fillStyle = BRAND.cream; ctx.fillText('SCAN · JOIN · RACE  —  MOVECADE', w / 2, h * 0.78);
  },
  (ctx: SKRSContext2D, w: number, h: number) => {
    const g = ctx.createLinearGradient(0, 0, w, 0);
    g.addColorStop(0, BRAND.gold); g.addColorStop(1, '#ff9a3c');
    ctx.fillStyle = g; ctx.fillRect(0, 0, w, h);
    wordmark(ctx, 'MOVECADE KART', w * 0.5, h * 0.52, 118, BRAND.deep);
    ctx.font = '800 42px sans-serif'; ctx.fillStyle = BRAND.deep; ctx.textAlign = 'center';
    ctx.fillText('4 PLAYERS · 1 SCREEN · 0 CONTROLLERS', w / 2, h * 0.84);
  },
];

function columnPoster(variant: number) {
  // Wraps once around the drum (u) over its 7 m height (v). Two poster panels and a band of logos.
  const c = createCanvas(1024, 512);
  const ctx = c.getContext('2d');
  const w = 1024, h = 512;
  ctx.fillStyle = variant ? BRAND.deep : BRAND.cream; ctx.fillRect(0, 0, w, h);
  const ink = variant ? BRAND.cream : BRAND.deep;
  for (let k = 0; k < 2; k++) {
    const x0 = k * w / 2;
    ctx.fillStyle = k === variant ? BRAND.violet : BRAND.pink;
    ctx.fillRect(x0 + 24, 40, w / 2 - 48, h - 150);
    ctx.save();
    ctx.textAlign = 'center'; ctx.fillStyle = BRAND.cream;
    ctx.font = '900 64px sans-serif'; ctx.fillText('MOVECADE', x0 + w / 4, 180);
    ctx.font = '700 30px sans-serif'; ctx.fillText(k ? 'KART GRAND PRIX' : 'PLAY ON ONE SCREEN', x0 + w / 4, 240);
    chevrons(ctx, x0 + w / 4 - 70, 320, 60, 3, BRAND.gold);
    ctx.restore();
  }
  ctx.textAlign = 'center'; ctx.fillStyle = ink; ctx.font = '800 34px sans-serif';
  for (let k = 0; k < 4; k++) ctx.fillText('MOVECADE', (k + 0.5) * w / 4, h - 50);
  return c;
}

function archBannerArt(title: string) {
  const c = createCanvas(2048, 192);
  const ctx = c.getContext('2d');
  const g = ctx.createLinearGradient(0, 0, 2048, 0);
  g.addColorStop(0, BRAND.deep); g.addColorStop(0.5, '#3d1f86'); g.addColorStop(1, BRAND.deep);
  ctx.fillStyle = g; ctx.fillRect(0, 0, 2048, 192);
  ctx.strokeStyle = BRAND.pink; ctx.lineWidth = 8; ctx.strokeRect(8, 8, 2032, 176);
  chevrons(ctx, 60, 96, 90, 3, BRAND.cyan);
  chevrons(ctx, 1820, 96, 90, 3, BRAND.cyan);
  ctx.textAlign = 'center'; ctx.fillStyle = BRAND.cream; ctx.font = '900 104px sans-serif';
  ctx.fillText(title, 1024, 132);
  return c;
}

/** BoostPads.jsx:42-100: orange-to-red gradient, light diagonal bands, yellow rails and forward arrows;
 *  drawn at 256 px with three chevrons that tile seamlessly up the image, so the material can scroll
 *  them forward. Up (v = 1 in three.js) = forward. */
function boostPadArt() {
  const s = 256;
  const c = createCanvas(s, s);
  const ctx = c.getContext('2d');
  const bg = ctx.createLinearGradient(0, 0, s, 0);
  bg.addColorStop(0, '#dd4400'); bg.addColorStop(0.5, '#ff9a00'); bg.addColorStop(1, '#dd4400');
  ctx.fillStyle = bg; ctx.fillRect(0, 0, s, s);
  ctx.fillStyle = 'rgba(255, 220, 120, 0.18)';
  for (let i = -s; i < s; i += 32) {
    ctx.beginPath(); ctx.moveTo(i, 0); ctx.lineTo(i + 14, 0); ctx.lineTo(i + s + 14, s); ctx.lineTo(i + s, s); ctx.closePath(); ctx.fill();
  }
  const period = s / 3;
  for (let k = -1; k <= 3; k++) {
    const y = 14 + k * period;
    const arrow = ctx.createLinearGradient(0, y, 0, y + 62);
    arrow.addColorStop(0, '#fff6c8'); arrow.addColorStop(1, '#ffcc00');
    ctx.fillStyle = arrow;
    ctx.beginPath();
    ctx.moveTo(128, y); ctx.lineTo(220, y + 58); ctx.lineTo(180, y + 58); ctx.lineTo(128, y + 26);
    ctx.lineTo(76, y + 58); ctx.lineTo(36, y + 58); ctx.closePath(); ctx.fill();
  }
  ctx.fillStyle = '#ffcc00';
  ctx.fillRect(0, 0, 12, s); ctx.fillRect(s - 12, 0, 12, s);
  return c;
}

/** The boost pad: a 1 x 1 m plane on the ground (scaled to width x length), texture up = local -Z = forward. */
function boostPadPlane() {
  const g = new THREE.PlaneGeometry(1, 1);
  g.rotateX(-Math.PI / 2);
  return g;
}

// ---- Surfaces --------------------------------------------------------------------------------------

/** The CC0 sets and how the circuits use them (tile and strengths from trackSurfaces.ts / HarborTrack.tsx). */
export const SURFACE_SETS = ['asphalt_track', 'painted_concrete', 'leafy_grass', 'gravelly_sand', 'rock_face', 'dark_wooden_planks'] as const;
export type SurfaceSet = (typeof SURFACE_SETS)[number];
export interface SurfaceEntry { set: SurfaceSet; color: string; normal: string; rough: string; mean: V3; licence: 'CC0-1.0'; source: string }

const toLinear = (c: number) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);

async function exportSurfaces(outRoot: string, movecadeRoot: string) {
  const dir = path.join(outRoot, 'shared', 'surfaces');
  mkdirSync(dir, { recursive: true });
  const out: Record<string, SurfaceEntry> = {};
  for (const set of SURFACE_SETS) {
    const src = path.join(movecadeRoot, 'app/public/textures/surfaces', set);
    const ue = (map: string) => `${SHARED_DIR}/Surfaces/T_${set}_${map}`;
    await sharp(path.join(src, 'color.webp')).png().toFile(path.join(dir, `T_${set}_color.png`));
    await sharp(path.join(src, 'rough.webp')).greyscale().png().toFile(path.join(dir, `T_${set}_rough.png`));
    // OpenGL -> DirectX normal: flip green.
    const { data, info } = await sharp(path.join(src, 'normal.webp')).removeAlpha().raw().toBuffer({ resolveWithObject: true });
    for (let i = 1; i < data.length; i += info.channels) data[i] = 255 - data[i]!;
    await sharp(data, { raw: { width: info.width, height: info.height, channels: info.channels } }).png().toFile(path.join(dir, `T_${set}_normal.png`));
    // Linear mean of the colour map, so detail can add texture without shifting a palette colour (trackSurfaces.ts:6-18).
    const px = await sharp(path.join(src, 'color.webp')).removeAlpha().raw().toBuffer();
    const sum = [0, 0, 0];
    for (let i = 0; i < px.length; i += 3) for (let c = 0; c < 3; c++) sum[c] += toLinear(px[i + c]! / 255);
    const n = px.length / 3;
    out[set] = {
      set, color: ue('color'), normal: ue('normal'), rough: ue('rough'),
      mean: sum.map((s) => Math.round((s / n) * 10000) / 10000) as V3,
      licence: 'CC0-1.0', source: `https://polyhaven.com/a/${set} (movecade-v2 app/public/textures/surfaces/${set}, LEDGER.json)`,
    };
  }
  return out;
}

// ---- Export ---------------------------------------------------------------------------------------

export interface PropsExport {
  prefabs: Record<string, MeshEntry>;
  textures: TextureEntry[];
  surfaces: Record<string, SurfaceEntry>;
}

let cached: Promise<PropsExport> | null = null;

/** Writes the shared props, textures and surfaces once per export run. */
export function exportMovecadeProps(outRoot: string, movecadeRoot: string): Promise<PropsExport> {
  cached ??= build(outRoot, movecadeRoot);
  return cached;
}

async function build(outRoot: string, movecadeRoot: string): Promise<PropsExport> {
  const file = new GlbFile('movecade_props.glb', SHARED_DIR);
  const lit = (roughness = 0.7, extra: Partial<MaterialHint> = {}): MaterialHint => ({ shader: 'vertexColor', roughness, ...extra });
  const prefabs: Record<string, MeshEntry> = {
    ad_column_trim: file.add('ad_column_trim', adColumnTrim(), lit(0.55)),
    ad_column_body: file.add('ad_column_body', adColumnBody(), { shader: 'textured', roughness: 0.6, texture: 'mc_column_{variant}', emissive: 0.15 }),
    billboard_frame: file.add('billboard_frame', billboardFrame(), lit(0.6)),
    billboard_face: file.add('billboard_face', billboardFace(), { shader: 'textured', roughness: 0.5, texture: 'mc_billboard_{variant}', emissive: 0.25 }),
    arch_frame: file.add('arch_frame', archFrame(), lit(0.55)),
    arch_banner: file.add('arch_banner', archBanner(), { shader: 'textured', roughness: 0.5, texture: 'mc_arch_{variant}', emissive: 0.35 }),
    rock_formation: file.add('rock_formation', rockFormation(), { shader: 'surface', detail: 'rock_face', triplanar: true, tileM: 5, albedo: 0.6, normal: 1, rough: 0.5, roughness: 0.9 } as MaterialHint),
    plinth: file.add('plinth', plinth(), { shader: 'surface', detail: 'painted_concrete', triplanar: true, tileM: 2.6, albedo: 0.4, normal: 0.6, rough: 0.5, roughness: 0.75 } as MaterialHint),
    flag_violet: file.add('flag_violet', flagCloth([BRAND.violet, BRAND.cream]), lit(0.9)),
    flag_pink: file.add('flag_pink', flagCloth([BRAND.pink, BRAND.cream]), lit(0.9)),
    flag_cyan: file.add('flag_cyan', flagCloth([BRAND.cyan, BRAND.deep]), lit(0.9)),
    tyre_stack: file.add('tyre_stack', tyreStacks(), lit(0.85)),
    palm: file.add('palm', palm(), lit(0.85)),
    boost_pad: file.add('boost_pad', boostPadPlane(), { shader: 'boostPad', texture: 'mc_boost_pad', roughness: 0.35 }),
  };
  await file.write(path.join(outRoot, 'shared'));

  const texDir = path.join(outRoot, 'shared', 'textures');
  const tex = (name: string, canvas: Parameters<typeof savePng>[3]) => savePng(texDir, SHARED_DIR, name, canvas);
  const textures: TextureEntry[] = [];
  BILLBOARD_FACES.forEach((draw, i) => {
    const c = createCanvas(1536, 512);
    draw(c.getContext('2d'), c.width, c.height);
    textures.push(tex(`mc_billboard_${i}`, c));
  });
  textures.push(tex('mc_column_0', columnPoster(0)), tex('mc_column_1', columnPoster(1)));
  textures.push(tex('mc_arch_0', archBannerArt('MOVECADE  ·  KART GRAND PRIX')), tex('mc_arch_1', archBannerArt('NEON CITY  ·  NIGHT CIRCUIT')));
  textures.push(tex('mc_boost_pad', boostPadArt()));
  return { prefabs, textures, surfaces: await exportSurfaces(outRoot, movecadeRoot) };
}

export { BILLBOARD, ARCH };
export type { V3 };
