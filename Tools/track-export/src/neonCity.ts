/**
 * Neon City: an original third track ("better maps"), a night street circuit through a neon downtown.
 *
 * It is an analytic spline like Sunset Harbor and goes through the same code paths: a closed uniform
 * Catmull-Rom (catmullPoint is harborPoint's formula over NEON_CONTROL_POINTS), road width 28, walls at
 * +-16.3, the same gameplay ribbons (road, curbs, dirt shoulder, walls), the shared start line, spawn grid,
 * 9 checkpoints (radius 24, start index 1), item box rows, 12 x 6 boost pads, static cameras, minimap and
 * navigation. Every bend is at least 26 m in radius, so no wall offset folds back over the road (B14).
 *
 * Scenery is generated here, seeded so every export is identical: city blocks with lit windows and neon
 * tubes, a distant skyline, sidewalks, sponsor arches, lamps (with real point lights), palms, tyre stacks
 * and Movecade billboards. Binaries go to out/neon (ribbons + scenery GLBs, the window texture).
 *
 * Local play only: the room server knows violet-loop and sunset-harbor; run it with L_NeonCity or -KartTrack=neon-city.
 */
import { createCanvas } from '@napi-rs/canvas';
import path from 'node:path';
import * as THREE from 'three';
import { curvatures, type PathSample } from '@app/games/kart/scene/trackside';
import { kartTrackLook } from '@app/games/kart/scene/kartLook';
import { SPAWN_POSITIONS } from '@legacy/Experience/kart/engine/spawnGrid';
import {
  BOOST_CONFIG, BOOST_PAD_LOOK, CHECKPOINT_ORDER, CHECKPOINT_RADIUS, ITEM_BOX_CONFIG, RACE_RULES, RECOVERY,
  START_CHECKPOINT_INDEX, SURFACES, SWEEP, closedLength, r, rr, sideOut,
} from './export';
import { BRAND, exportMovecadeProps } from './props';
import { GlbFile, savePng, type MaterialHint, type MeshEntry } from './scenery';

type XZ = [number, number];
const ID = 'neon-city';
const UE_DIR = '/Game/Tracks/NeonCity';

// ---- Layout ----------------------------------------------------------------------------------------

/** Clockwise from the start straight (x = 3, heading -Z, like both shipped tracks, so the shared grid fits). */
export const NEON_CONTROL_POINTS: readonly XZ[] = [
  [3, -7], [3, -75], [10, -145], [45, -200], [105, -225], [175, -215], [228, -175], [250, -115],
  [240, -60], [200, -26], [176, 24], [188, 74], [234, 106], [276, 146], [278, 200], [226, 230],
  [165, 212], [120, 178], [70, 176], [20, 198], [-30, 198], [-60, 180], [-72, 150], [-64, 118],
  [-44, 92], [-20, 72], [-3, 50], [3, 22],
];
const N = NEON_CONTROL_POINTS.length;
const ROAD_WIDTH = 28;
const WALL_OFFSET = 16.3;
/** Render and wall rows, uniform in t (28 per control point, ~1.85 m apart like Harbor's 870). */
const ROWS = N * 28;
/** Sectors: t = k / N lands on control point k (Catmull-Rom passes through its points). */
const SECTORS = [
  { cp: 0.4, name: 'Neon Boulevard' },
  { cp: 4, name: 'Skyline Sweep' },
  { cp: 7, name: 'Tower Bend' },
  { cp: 9, name: 'Arcade Chicane' },
  { cp: 12, name: 'Metro Esses' },
  { cp: 14, name: 'Dockside Hairpin' },
  { cp: 17, name: 'Night Market' },
  { cp: 21, name: 'Lantern Corner' },
  { cp: 25, name: 'Final Flick' },
];
const ITEM_ROWS = [2, 10.5, 16.5, 23.5];
const BOOST_PADS = [5.4, 11.3, 18.6, 26.2];
const ARCHES = [{ at: 'finish', variant: 0 }, { cp: 10.2, variant: 1 }, { cp: 19.4, variant: 1 }];

/** harborPoint's closed uniform Catmull-Rom over NEON_CONTROL_POINTS. */
export function neonPoint(t: number, offset = 0) {
  const wrapped = (((t % 1) + 1) % 1) * N;
  const index = Math.floor(wrapped);
  const u = wrapped - index;
  const pts = [-1, 0, 1, 2].map((d) => NEON_CONTROL_POINTS[(index + d + N) % N]!);
  const sample = (axis: 0 | 1) => {
    const [a, b, c, d] = pts.map((p) => p[axis]) as [number, number, number, number];
    const p = -a + c, q = 2 * a - 5 * b + 4 * c - d, rr2 = -a + 3 * b - 3 * c + d;
    return { value: (2 * b + p * u + q * u * u + rr2 * u * u * u) / 2, tangent: (p + 2 * q * u + 3 * rr2 * u * u) / 2 };
  };
  const x = sample(0), z = sample(1);
  const length = Math.hypot(x.tangent, z.tangent);
  const nx = -z.tangent / length, nz = x.tangent / length;
  return { x: x.value + nx * offset, z: z.value + nz * offset, nx, nz, yaw: Math.atan2(-x.tangent, -z.tangent) };
}
const atCp = (cp: number, offset = 0) => neonPoint(cp / N, offset);

/** Gameplay ribbons as Harbor's (same names, bands and surfaces, so the sim handles identically), plus city dressing. */
const RIBBONS = [
  { name: 'road', inner: -ROAD_WIDTH / 2, outer: ROAD_WIDTH / 2, innerY: 0, outerY: 0, material: 'asphalt', collision: 'ground', surface: 'asphalt' },
  { name: 'leftEdge', inner: -13.7, outer: -13.3, innerY: 0.02, outerY: 0.02, material: 'edgeLine', collision: 'none' },
  { name: 'rightEdge', inner: 13.3, outer: 13.7, innerY: 0.02, outerY: 0.02, material: 'edgeLine', collision: 'none' },
  { name: 'shoulder', inner: -17, outer: 17, innerY: -0.04, outerY: -0.04, material: 'dirtShoulder', collision: 'ground', surface: 'dirt' },
  { name: 'leftCurb', inner: -15.5, outer: -14, innerY: 0.035, outerY: 0.035, material: 'curb', collision: 'ground', surface: 'asphalt', stripePeriodSamples: 4 },
  { name: 'rightCurb', inner: 14, outer: 15.5, innerY: 0.035, outerY: 0.035, material: 'curb', collision: 'ground', surface: 'asphalt', stripePeriodSamples: 4 },
  { name: 'leftWall', inner: -WALL_OFFSET, outer: -WALL_OFFSET, innerY: 0, outerY: 1.3, material: 'barrier', collision: 'wall' },
  { name: 'rightWall', inner: WALL_OFFSET, outer: WALL_OFFSET, innerY: 1.3, outerY: 0, material: 'barrier', collision: 'wall' },
  { name: 'leftCap', inner: -16.7, outer: -15.9, innerY: 1.3, outerY: 1.3, material: 'wallCap', collision: 'none' },
  { name: 'rightCap', inner: 15.9, outer: 16.7, innerY: 1.3, outerY: 1.3, material: 'wallCap', collision: 'none' },
  { name: 'leftSidewalk', inner: -25, outer: -16.8, innerY: 0.14, outerY: 0.14, material: 'sidewalk', collision: 'none' },
  { name: 'rightSidewalk', inner: 16.8, outer: 25, innerY: 0.14, outerY: 0.14, material: 'sidewalk', collision: 'none' },
  { name: 'leftKerbFace', inner: -25, outer: -25, innerY: 0.14, outerY: -0.06, material: 'sidewalk', collision: 'none' },
  { name: 'rightKerbFace', inner: 25, outer: 25, innerY: -0.06, outerY: 0.14, material: 'sidewalk', collision: 'none' },
];

/** The night look, in kartLook.ts's schema: indigo dome, a cool moon, warm sodium lamps, strong bloom for the neon. */
const NEON_LOOK = (() => {
  const night = kartTrackLook('violet-loop', true) as Record<string, unknown>;
  return {
    ...night,
    mood: {
      skyTop: '#07061a', skyHorizon: '#3b1a5e', skyGround: '#0d0a1c', ground: '#1a1726',
      sun: '#cdd9ff', fill: '#8ea7ff', rim: '#ff4fd8', fog: [60, 520],
      sky: { sunDirection: [-0.45, 0.38, 0.81], sunSize: 0.028, sunGlow: 0.2, clouds: 0.2, cloudColor: '#e8eeff' },
    },
    sunDirection: [-0.45, 0.62, 0.64],
    sunColor: '#9fb4ff',
    sunIntensity: 0.8,
    hemisphere: { sky: '#5a4f9e', ground: '#1b1630', intensity: 0.45 },
    fill: { color: '#ff7ad9', intensity: 0.2 },
    fog: { near: 140, far: 1300 },
    grade: { exposure: 1.0, contrast: 1.1, saturation: 1.15, vibrance: 0.25, lift: [0.01, 0.0, 0.03], gain: [1.02, 1.0, 1.04], dither: 1 },
    bloom: { strength: 0.7, radius: 0.45, threshold: 1 },
    vignette: 0.35,
    // UE-only tuning read by ATrackBuilder::ApplyLook.
    ue: { exposureBias: 1.2, glowPower: 90, skyLightScale: 1.4, fogBrightness: 0.6, fogDensity: 0.0045, toneCurve: 0.75, skyBrightness: 1.0 },
  };
})();

// ---- Geometry helpers ------------------------------------------------------------------------------

function rng(seed: number) {
  let s = seed >>> 0;
  return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
}

/** A raw mesh builder: positions, normals, UVs (three.js convention, v up) and vertex colours. */
class Builder {
  pos: number[] = []; nrm: number[] = []; uv: number[] = []; col: number[] = []; idx: number[] = [];
  quad(a: THREE.Vector3, b: THREE.Vector3, c: THREE.Vector3, d: THREE.Vector3, color: THREE.Color, uvs: [number, number][] = [[0, 0], [1, 0], [1, 1], [0, 1]]) {
    // a-b-c-d counter-clockwise seen from the front.
    const n = new THREE.Vector3().subVectors(b, a).cross(new THREE.Vector3().subVectors(d, a)).normalize();
    const base = this.pos.length / 3;
    [a, b, c, d].forEach((p, i) => {
      this.pos.push(p.x, p.y, p.z); this.nrm.push(n.x, n.y, n.z);
      this.uv.push(...uvs[i]!); this.col.push(color.r, color.g, color.b);
    });
    this.idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  /** An oriented box: centre (x, z), base y0 to y1, half extents along `along` (unit XZ) and its normal. */
  box(cx: number, cz: number, y0: number, y1: number, alongX: number, alongZ: number, halfW: number, halfD: number, color: THREE.Color, uvScale = 0) {
    const ax = alongX, az = alongZ, nx = -az, nz = ax; // along, across
    const corner = (sa: number, sn: number, y: number) => new THREE.Vector3(cx + ax * halfW * sa + nx * halfD * sn, y, cz + az * halfW * sa + nz * halfD * sn);
    const faces: Array<[number, number, number, number, number]> = [
      // [sa0, sn0, sa1, sn1, widthM] walls, counter-clockwise from outside
      [-1, -1, 1, -1, halfW * 2], [1, -1, 1, 1, halfD * 2], [1, 1, -1, 1, halfW * 2], [-1, 1, -1, -1, halfD * 2],
    ];
    const h = y1 - y0;
    const centre = new THREE.Vector3(cx, (y0 + y1) / 2, cz);
    // Wind each face so its normal points out of the box.
    const outward = (a: THREE.Vector3, b: THREE.Vector3, c: THREE.Vector3, d: THREE.Vector3, uvs: [number, number][]) => {
      const n = new THREE.Vector3().subVectors(b, a).cross(new THREE.Vector3().subVectors(d, a));
      const mid = new THREE.Vector3().add(a).add(b).add(c).add(d).multiplyScalar(0.25).sub(centre);
      if (n.dot(mid) >= 0) this.quad(a, b, c, d, color, uvs);
      else this.quad(b, a, d, c, color, [uvs[1]!, uvs[0]!, uvs[3]!, uvs[2]!]);
    };
    for (const [sa0, sn0, sa1, sn1, w] of faces) {
      const u1 = uvScale ? w / uvScale : 1, v1 = uvScale ? h / uvScale : 1;
      outward(corner(sa0, sn0, y0), corner(sa1, sn1, y0), corner(sa1, sn1, y1), corner(sa0, sn0, y1), [[0, 0], [u1, 0], [u1, v1], [0, v1]]);
    }
    outward(corner(-1, -1, y1), corner(1, -1, y1), corner(1, 1, y1), corner(-1, 1, y1), [[0, 0], [0, 0], [0, 0], [0, 0]]);
  }
  geometry() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nrm, 3));
    g.setAttribute('uv', new THREE.Float32BufferAttribute(this.uv, 2));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.col, 3));
    g.setIndex(this.idx);
    return g;
  }
}

/** harborRibbon over neonPoint: rows at uniform t, u = offset / 8, v = along / 8 (one tile = 8 m). */
function ribbon(inner: number, outer: number, innerY: number, outerY: number, kind: 'asphalt' | 'curb' | 'plain') {
  const pos: number[] = [], uv: number[] = [], col: number[] = [], idx: number[] = [];
  let along = 0;
  let prev = neonPoint(0);
  const tone = rng(41);
  for (let i = 0; i <= ROWS; i++) {
    const t = i / ROWS;
    const c = neonPoint(t);
    along += Math.hypot(c.x - prev.x, c.z - prev.z);
    prev = c;
    const a = neonPoint(t, inner), b = neonPoint(t, outer);
    pos.push(a.x, innerY, a.z, b.x, outerY, b.z);
    uv.push(inner / 8, along / 8, outer / 8, along / 8);
    let rgb: [number, number, number] = [1, 1, 1];
    if (kind === 'asphalt') { const k = 0.95 + tone() * 0.1; rgb = [k, k, k]; }
    if (kind === 'curb') rgb = Math.floor(i / 4) % 2 ? [0.93, 0.91, 0.86] : [0.84, 0.05, 0.42]; // cream / neon magenta
    col.push(...rgb, ...rgb);
    // harborRibbon's winding: ground faces up, walls face the road.
    if (i < ROWS) { const k = i * 2; idx.push(k, k + 1, k + 2, k + 1, k + 3, k + 2); }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

function ribbonMaterial(material: string): MaterialHint {
  switch (material) {
    case 'asphalt': return { shader: 'surface', color: '#3b3f4a', detail: 'asphalt_track', tileM: 4.5, albedo: 0.65, normal: 0.6, rough: 0.7, roughness: 0.58, desaturate: 0.4 };
    case 'dirtShoulder': return { shader: 'surface', color: '#4a4650', detail: 'gravelly_sand', tileM: 3, albedo: 0.5, normal: 0.6, rough: 0.5, roughness: 0.9, desaturate: 0.7 };
    case 'barrier': return { shader: 'surface', color: '#d9d4e8', detail: 'painted_concrete', triplanar: true, tileM: 2.4, albedo: 0.45, normal: 0.6, rough: 0.5, roughness: 0.6 };
    case 'curb': return { shader: 'surface', roughness: 0.5, detail: 'painted_concrete', tileM: 2, albedo: 0.25, normal: 0.4, rough: 0.4 };
    case 'sidewalk': return { shader: 'surface', color: '#8a8794', detail: 'painted_concrete', triplanar: true, tileM: 3, albedo: 0.55, normal: 0.7, rough: 0.5, roughness: 0.7, desaturate: 0.8 };
    case 'edgeLine': return { shader: 'vertexColor', color: '#f4f1ea', roughness: 0.5, emissive: 0.15 };
    case 'wallCap': return { shader: 'vertexColor', color: '#ff4fd8', roughness: 0.4, emissive: 2.5 };
    default: return { shader: 'vertexColor', color: '#808080' };
  }
}

// ---- Textures ---------------------------------------------------------------------------------------

/** 24 x 24 m of facade (6 floors x 12 bays): dark panels, mullions, a scatter of warm and cool lit windows. */
function windowsTexture() {
  const s = 1024, c = createCanvas(s, s), ctx = c.getContext('2d');
  const rand = rng(77);
  ctx.fillStyle = '#10121c'; ctx.fillRect(0, 0, s, s);
  const bays = 12, floors = 6, bw = s / bays, fh = s / floors;
  for (let f = 0; f < floors; f++) {
    ctx.fillStyle = '#1b1e2b'; ctx.fillRect(0, f * fh + fh - 10, s, 10); // floor slab
    for (let b = 0; b < bays; b++) {
      const x = b * bw + 9, y = f * fh + 22, w = bw - 18, h = fh - 46;
      const k = rand();
      const lit = k < 0.42 ? (rand() < 0.65 ? '#ffd38a' : '#9ad8ff') : k < 0.5 ? '#ff7ad9' : '#1d2433';
      const g = ctx.createLinearGradient(0, y, 0, y + h);
      g.addColorStop(0, lit); g.addColorStop(1, k < 0.5 ? '#2a2236' : '#151b28');
      ctx.fillStyle = g; ctx.fillRect(x, y, w, h);
      ctx.fillStyle = '#0b0d14'; ctx.fillRect(x + w / 2 - 2, y, 4, h);
    }
  }
  return c;
}

/** Street level, 16 x 16 m per tile with the 4.2 m shop band at the bottom: lit glass under awnings in brand colours. */
function shopfrontTexture() {
  const c = createCanvas(1024, 1024), ctx = c.getContext('2d');
  ctx.fillStyle = '#0d0f17'; ctx.fillRect(0, 0, 1024, 1024);
  const band = Math.round((1024 * 4.2) / 16), top = 1024 - band;
  const colors = [BRAND.pink, BRAND.cyan, BRAND.gold, BRAND.violet];
  for (let i = 0; i < 4; i++) {
    const x = i * 256;
    ctx.fillStyle = '#ffe2b8'; ctx.globalAlpha = 0.85; ctx.fillRect(x + 16, top + 96, 224, band - 100); ctx.globalAlpha = 1;
    ctx.fillStyle = colors[i]!; ctx.fillRect(x + 8, top + 36, 240, 48);
    ctx.fillStyle = '#0d0f17'; ctx.font = '800 30px sans-serif'; ctx.textAlign = 'center';
    ctx.fillText(['ARCADE', 'NOODLES', 'KARTS', 'MOVECADE'][i]!, x + 128, top + 71);
    ctx.fillStyle = '#20242f'; ctx.fillRect(x + 124, top + 96, 8, band - 100);
  }
  return c;
}

// ---- The export --------------------------------------------------------------------------------------

export async function exportNeonCity(commit: string, outRoot?: string, movecadeRoot?: string) {
  // Dense samples (render rows) and arc length.
  const raw: (PathSample & { t: number; yaw: number })[] = [];
  for (let i = 0; i < ROWS; i++) {
    const t = i / ROWS, p = neonPoint(t);
    raw.push({ x: p.x, z: p.z, tx: p.nz, tz: -p.nx, s: 0, t, yaw: p.yaw });
  }
  for (let i = 1; i < raw.length; i++) raw[i]!.s = raw[i - 1]!.s + Math.hypot(raw[i]!.x - raw[i - 1]!.x, raw[i]!.z - raw[i - 1]!.z);
  const lapLengthM = closedLength(Array.from({ length: ROWS * 10 }, (_, i) => neonPoint(i / (ROWS * 10))));
  const curv = curvatures(raw, 3);
  const tightest = Math.min(...curv.map((k) => 1 / Math.max(Math.abs(k), 1e-9)));
  if (tightest <= WALL_OFFSET + 5) throw new Error(`Neon City: a bend of ${tightest.toFixed(1)} m is too tight for walls at ${WALL_OFFSET} m (B14)`);
  if (lapLengthM < 1300 || lapLengthM > 1800) throw new Error(`Neon City: lap ${lapLengthM.toFixed(0)} m is outside 1300-1800 m`);

  const nearestCentre = (x: number, z: number) => raw.reduce((m, s) => Math.min(m, Math.hypot(s.x - x, s.z - z)), Infinity);

  // Gameplay data in the shared schema.
  const landmarks = SECTORS.map((s) => atCp(s.cp));
  const checkpoints = CHECKPOINT_ORDER.map(([cid, key], index) => {
    const p = index === 0 ? { x: 3, z: -30.32 } : landmarks[index]!;
    const cp: Record<string, unknown> = { index, id: cid, landmark: key, x: r(p.x), z: r(p.z), radius: CHECKPOINT_RADIUS };
    if (index === 0) cp.isFinish = true;
    else cp.sectorName = SECTORS[index]!.name;
    return cp;
  });
  const waypoints = Array.from({ length: N * 3 }, (_, i) => { const p = neonPoint(i / (N * 3)); return { x: r(p.x), z: r(p.z) }; });
  const itemBoxes = ITEM_ROWS.flatMap((cp, row) => [-8, 0, 8].map((o, k) => {
    const p = atCp(cp, o);
    return { id: `${ID}-box-${row * 3 + k}`, x: r(p.x), y: 1, z: r(p.z), row };
  }));
  const boostPads = BOOST_PADS.map((cp) => { const p = atCp(cp); return { x: r(p.x), z: r(p.z), yaw: r(p.yaw), width: 12, length: 6 }; });
  const staticCameras = SECTORS.map((s, i) => {
    const pos = atCp(s.cp, i % 2 ? -26 : 26), at = atCp(s.cp);
    return { id: `cam_${i}`, name: s.name, position: rr([pos.x, 16, pos.z]), lookAt: rr([at.x, 0, at.z]) };
  });
  const xs = raw.map((s) => s.x), zs = raw.map((s) => s.z);
  const bounds = { minX: Math.floor(Math.min(...xs) - 30), maxX: Math.ceil(Math.max(...xs) + 30), minZ: Math.floor(Math.min(...zs) - 30), maxZ: Math.ceil(Math.max(...zs) + 30) };
  const svg = (() => {
    const sx = bounds.maxX - bounds.minX, sz = bounds.maxZ - bounds.minZ;
    const pts = [...waypoints, waypoints[0]!].map((p) => ({ x: ((p.x - bounds.minX) / sx) * 100, y: ((p.z - bounds.minZ) / sz) * 100 }));
    let d = `M ${pts[0]!.x.toFixed(1)} ${pts[0]!.y.toFixed(1)}`;
    for (let i = 1; i < pts.length; i++) {
      const cur = pts[i]!, next = pts[(i + 1) % pts.length]!;
      d += i < pts.length - 1 ? ` Q ${cur.x.toFixed(1)} ${cur.y.toFixed(1)} ${((cur.x + next.x) / 2).toFixed(1)} ${((cur.y + next.y) / 2).toFixed(1)}` : ` L ${cur.x.toFixed(1)} ${cur.y.toFixed(1)}`;
    }
    return `${d} Z`;
  })();
  const side = { edge: ROAD_WIDTH / 2, curbIn: 14, curbOut: 15.5, wall: WALL_OFFSET, wallTop: 1.3, shoulder: 17, back: 17, backY: 0 };
  const wall = (s: 1 | -1) => ({
    side: s, height: 1.3, closed: true,
    polyline: Array.from({ length: ROWS }, (_, i) => { const p = neonPoint(i / ROWS, s * WALL_OFFSET); return [r(p.x), r(p.z)] as XZ; }),
  });

  // ---- Scenery
  const art = outRoot && movecadeRoot ? await exportArt(outRoot, raw, nearestCentre) : undefined;
  const props = outRoot && movecadeRoot ? await exportMovecadeProps(outRoot, movecadeRoot) : undefined;

  return {
    schemaVersion: 1,
    coordinateSystem: 'three-rh-yup-m',
    source: { repo: 'MyProject (original track)', commit, exportedAt: new Date().toISOString(), files: ['Tools/track-export/src/neonCity.ts'] },
    track: {
      id: ID, name: 'Neon City', description: 'Night streets · neon arches · skyline sweep', uiColor: '#ff4fd8',
      laps: RACE_RULES.laps, groundY: 0, suggestedRebaseY: 0, closedLoop: true, lapLengthM: r(lapLengthM, 2),
      trackType: 'analytic-spline', default: false, localOnly: true, tightestRadiusM: r(tightest, 1),
    },
    raceRules: RACE_RULES,
    spline: { kind: 'catmull-rom-uniform-closed', controlPoints: NEON_CONTROL_POINTS.map(([x, z]) => [x, z] as XZ) },
    samples: {
      spacingM: r(lapLengthM / ROWS), uniformIn: 't',
      points: raw.map((p, i) => ({
        i, s: r(p.s, 3), t: r(p.t, 10), x: r(p.x), y: 0, z: r(p.z),
        tx: r(p.tx, 6), tz: r(p.tz, 6), yaw: r(p.yaw, 6), curvature: r(curv[i]!, 6),
        bankDeg: 0, grade: 0, surface: 'asphalt', right: sideOut({ ...side }), left: sideOut({ ...side }),
      })),
    },
    navigation: { waypoints, lookAhead: 2 },
    checkpoints,
    startCheckpointIndex: START_CHECKPOINT_INDEX,
    startLine: { x: 3, z: -30.32, yaw: 0, width: ROAD_WIDTH },
    spawnGrid: SPAWN_POSITIONS.map((s, slot) => ({ slot, x: s.position[0], y: s.position[1], z: s.position[2], yaw: s.rotation })),
    itemBoxes,
    itemBoxConfig: ITEM_BOX_CONFIG,
    boostPads,
    boostConfig: BOOST_CONFIG,
    staticCameras,
    minimap: { bounds, svgPath: svg },
    recovery: RECOVERY,
    surfaces: SURFACES,
    look: NEON_LOOK,
    collision: { ...SWEEP, walls: [wall(1), wall(-1)] },
    ribbons: RIBBONS,
    meshes: art?.meshes ?? [],
    scenery: {
      instances: art?.instances ?? [],
      ...(props && art ? { prefabs: { ...props.prefabs, ...art.prefabs } } : {}),
      lights: art?.lights ?? [],
      animated: [],
      signs: [],
    },
    textures: [...(art?.textures ?? []), ...(props?.textures ?? [])],
    detailSurfaces: props?.surfaces ?? {},
    boostPadLook: { ...BOOST_PAD_LOOK, emissive: 3 },
    exportNotes: [
      'Original track (Tools/track-export/src/neonCity.ts). Local play only: the room server lists violet-loop and sunset-harbor.',
      `Tightest bend ${tightest.toFixed(1)} m (walls at ${WALL_OFFSET} m): no folded wall offsets (B14).`,
    ],
  };
}

type Sample = PathSample & { t: number };

async function exportArt(outRoot: string, raw: readonly Sample[], nearestCentre: (x: number, z: number) => number) {
  const outDir = path.join(outRoot, 'neon');
  const rand = rng(2026);

  // Ribbons (gameplay strips + sidewalks) and the city ground.
  const ribbonFile = new GlbFile('neon_ribbons.glb', UE_DIR);
  const ribbonParts: MeshEntry[] = RIBBONS.map((rb) =>
    ribbonFile.add(`ribbon_${rb.name}`, ribbon(rb.inner, rb.outer, rb.innerY, rb.outerY, rb.material === 'asphalt' ? 'asphalt' : rb.material === 'curb' ? 'curb' : 'plain'), ribbonMaterial(rb.material)));
  const ground = new THREE.PlaneGeometry(3200, 3200, 1, 1);
  ground.rotateX(-Math.PI / 2);
  ground.translate(110, -0.08, 0);
  const guv = ground.getAttribute('uv');
  for (let i = 0; i < guv.count; i++) guv.setXY(i, guv.getX(i) * 400, guv.getY(i) * 400); // 8 m tiles
  ribbonParts.push(ribbonFile.add('city_ground', ground, { shader: 'surface', color: '#2a2833', detail: 'asphalt_track', tileM: 6, albedo: 0.6, normal: 0.5, rough: 0.6, roughness: 0.55, desaturate: 0.6 }));
  await ribbonFile.write(outDir);

  // City blocks: lots every ~22 m on both sides, 34 m+ out, clear of every part of the track and each other.
  const facade = new Builder(), roofs = new Builder(), neon = new Builder(), shops = new Builder();
  const neonColors = [BRAND.pink, BRAND.cyan, BRAND.violet, BRAND.gold, '#ff6a3d'].map((c) => new THREE.Color(c));
  const tints = ['#ffffff', '#e6e2ff', '#dff2ff', '#ffe9f6'].map((c) => new THREE.Color(c));
  const placed: Array<{ x: number; z: number; r: number }> = [];
  const instances: Record<string, unknown>[] = [];
  const lights: Record<string, unknown>[] = [];
  let billboard = 0;
  const lapLength = raw[raw.length - 1]!.s;
  for (const sideSign of [1, -1] as const) {
    for (let s = 8; s < lapLength; s += 20 + rand() * 8) {
      const i = raw.findIndex((p) => p.s >= s);
      const t = raw[i]!.t;
      const c = neonPoint(t);
      const w = 12 + rand() * 10, d = 14 + rand() * 12;
      const off = sideSign * (34 + d / 2 + rand() * 6);
      const o = neonPoint(t, off);
      const ax = -c.nz, az = c.nx; // along the track (tangent), as (x, z)
      const ok = [[-1, -1], [1, -1], [1, 1], [-1, 1], [0, 0]].every(([sa, sn]) =>
        nearestCentre(o.x + ax * (w / 2) * sa! + c.nx * (d / 2) * sn!, o.z + az * (w / 2) * sa! + c.nz * (d / 2) * sn!) > 31);
      const radius = Math.hypot(w, d) / 2;
      if (!ok || placed.some((p) => Math.hypot(p.x - o.x, p.z - o.z) < (p.r + radius) * 0.85)) continue;
      placed.push({ x: o.x, z: o.z, r: radius });
      const height = 16 + Math.pow(rand(), 1.6) * 64;
      const tint = tints[Math.floor(rand() * tints.length)]!;
      facade.box(o.x, o.z, 4.2, height, ax, az, w / 2, d / 2, tint, 24);
      shops.box(o.x, o.z, -0.1, 4.2, ax, az, w / 2 + 0.05, d / 2 + 0.05, new THREE.Color(1, 1, 1), 16);
      roofs.box(o.x, o.z, height, height + 0.6, ax, az, w / 2 + 0.3, d / 2 + 0.3, new THREE.Color('#1c1b26'));
      // Rooftop plant.
      if (rand() < 0.7) roofs.box(o.x + ax * (rand() - 0.5) * w * 0.4, o.z + az * (rand() - 0.5) * w * 0.4, height + 0.6, height + 2.4 + rand() * 2, ax, az, 1.5 + rand() * 2, 1.5 + rand() * 2, new THREE.Color('#2a2936'));
      // Neon on the face toward the road: a vertical tube sign and a roofline tube.
      const toRoad = { x: -sideSign * c.nx, z: -sideSign * c.nz };
      const faceX = o.x + toRoad.x * (d / 2 + 0.25), faceZ = o.z + toRoad.z * (d / 2 + 0.25);
      const color = neonColors[Math.floor(rand() * neonColors.length)]!;
      neon.box(faceX, faceZ, height - 0.5, height - 0.15, ax, az, w / 2, 0.15, color);
      if (rand() < 0.75) {
        const along = (rand() - 0.5) * (w - 3);
        const y0 = 6 + rand() * 6, y1 = Math.min(height - 3, y0 + 6 + rand() * 12);
        const second = neonColors[Math.floor(rand() * neonColors.length)]!;
        neon.box(faceX + ax * along, faceZ + az * along, y0, y1, ax, az, 0.9, 0.2, second);
        neon.box(faceX + ax * (along + 1.4), faceZ + az * (along + 1.4), y0 + 1, y1 - 1, ax, az, 0.15, 0.25, color);
      }
      // Some rooftops carry a Movecade billboard facing the road.
      if (height < 45 && rand() < 0.3) {
        const yaw = Math.atan2(toRoad.x, toRoad.z);
        const v = billboard++ % 4;
        instances.push({ prefab: 'billboard_frame', x: r(o.x), y: r(height + 0.6 - 9), z: r(o.z), yaw: r(yaw, 6), scale: [1, 1, 1] });
        instances.push({ prefab: 'billboard_face', x: r(o.x), y: r(height + 0.6 - 9), z: r(o.z), yaw: r(yaw, 6), variant: v });
      }
    }
  }

  // The skyline: a ring of towers well beyond the circuit, read as silhouettes with lit windows.
  for (let k = 0; k < 64; k++) {
    const a = (k / 64) * Math.PI * 2 + rand() * 0.05;
    const R = 620 + rand() * 320;
    const x = 110 + Math.cos(a) * R, z = Math.sin(a) * R * 0.9;
    const w = 30 + rand() * 40, h = 60 + Math.pow(rand(), 1.3) * 190;
    facade.box(x, z, 0, h, Math.cos(a + 1.3), Math.sin(a + 1.3), w / 2, w / 2 * (0.6 + rand() * 0.6), tints[k % tints.length]!, 24);
    roofs.box(x, z, h, h + 1, Math.cos(a + 1.3), Math.sin(a + 1.3), w / 2 + 0.5, w / 2, new THREE.Color('#15141d'));
    if (rand() < 0.5) neon.box(x, z, h + 1, h + 1.6, Math.cos(a + 1.3), Math.sin(a + 1.3), w / 2 + 0.6, w / 2 * 0.6 + 0.6, neonColors[k % neonColors.length]!);
  }

  // Start line chequers and the grid boxes (paint, y 0.03).
  const paint = new Builder();
  const black = new THREE.Color('#14141a'), white = new THREE.Color('#f4f1ea');
  for (let col = 0; col < 14; col++) for (let row = 0; row < 2; row++) {
    const x0 = 3 - 14 + col * 2, z0 = -30.32 - 1 + row;
    paint.quad(new THREE.Vector3(x0, 0.03, z0 + 1), new THREE.Vector3(x0 + 2, 0.03, z0 + 1), new THREE.Vector3(x0 + 2, 0.03, z0), new THREE.Vector3(x0, 0.03, z0), (col + row) % 2 ? black : white);
  }
  for (const g of SPAWN_POSITIONS) {
    const [gx, , gz] = g.position;
    const line = (x0: number, z0: number, x1: number, z1: number) => paint.quad(new THREE.Vector3(x0, 0.03, z1), new THREE.Vector3(x1, 0.03, z1), new THREE.Vector3(x1, 0.03, z0), new THREE.Vector3(x0, 0.03, z0), white);
    line(gx - 1.4, gz - 1.6, gx + 1.4, gz - 1.35); // the box's front bar
    line(gx - 1.4, gz - 1.6, gx - 1.15, gz + 1.2);
    line(gx + 1.15, gz - 1.6, gx + 1.4, gz + 1.2);
  }

  // Street furniture along the sidewalks, clear of the arches.
  const arches = ARCHES.map((a) => (a.at === 'finish' ? { x: 3, z: -30.32, yaw: 0, variant: a.variant } : (() => { const p = atCp(a.cp!); return { x: p.x, z: p.z, yaw: p.yaw, variant: a.variant }; })()));
  const nearArch = (x: number, z: number) => arches.some((a) => Math.hypot(a.x - x, a.z - z) < 26);
  let lampSide: 1 | -1 = 1;
  for (let s = 10; s < lapLength; s += 30) {
    const t = raw.find((p) => p.s >= s)!.t;
    const p = neonPoint(t, lampSide * 19.5), c = neonPoint(t);
    lampSide = lampSide === 1 ? -1 : 1;
    if (nearArch(p.x, p.z) || nearestCentre(p.x, p.z) < 18.5) continue;
    const yaw = Math.atan2(c.x - p.x, c.z - p.z);
    instances.push({ prefab: 'lamp_post', x: r(p.x), y: 0.14, z: r(p.z), yaw: r(yaw, 6) });
    instances.push({ prefab: 'lamp_head_glow', x: r(p.x), y: 0.14, z: r(p.z), yaw: r(yaw, 6) });
    const hx = p.x + Math.sin(yaw) * 2.05, hz = p.z + Math.cos(yaw) * 2.05;
    lights.push({ x: r(hx), y: 8.4, z: r(hz), color: '#ffc98a', candela: 1600, radius: 32 });
  }
  // Palms on the Dockside and Night Market sidewalks; tyre stacks on the outside of the tightest bends.
  for (let s = 0; s < lapLength; s += 16) {
    const i = raw.findIndex((p) => p.s >= s), t = raw[i]!.t, cp = t * N;
    if (!((cp > 13 && cp < 18.5) || (cp > 0.8 && cp < 2.6))) continue;
    for (const sd of [1, -1] as const) {
      const p = neonPoint(t, sd * 22.5);
      if (nearArch(p.x, p.z) || nearestCentre(p.x, p.z) < 21) continue;
      instances.push({ prefab: 'palm', x: r(p.x), y: 0.14, z: r(p.z), yaw: r(rand() * Math.PI * 2, 6), scale: rr([0.85 + rand() * 0.3, 0.85 + rand() * 0.3, 0.85 + rand() * 0.3]) });
    }
  }
  const curv = curvatures(raw, 3);
  for (let i = 0; i < raw.length; i += 14) {
    if (Math.abs(curv[i]!) < 1 / 45) continue;
    const outside = curv[i]! > 0 ? -1 : 1; // positive curvature turns toward side +1, so the outside is -1
    const p = neonPoint(raw[i]!.t, outside * 18.6), c = neonPoint(raw[i]!.t);
    if (nearArch(p.x, p.z) || nearestCentre(p.x, p.z) < 17.8) continue;
    instances.push({ prefab: 'tyre_stack', x: r(p.x), y: 0.14, z: r(p.z), yaw: r(Math.atan2(c.x - p.x, c.z - p.z), 6) });
  }
  for (const a of arches) {
    instances.push({ prefab: 'arch_frame', x: r(a.x), y: 0, z: r(a.z), yaw: r(a.yaw, 6) });
    instances.push({ prefab: 'arch_banner', x: r(a.x), y: 0, z: r(a.z), yaw: r(a.yaw, 6), variant: a.variant });
    for (const sx of [-1, 1]) {
      // Uplights under each tower.
      const lx = a.x + Math.cos(a.yaw) * sx * 20, lz = a.z - Math.sin(a.yaw) * sx * 20;
      lights.push({ x: r(lx), y: 3, z: r(lz), color: '#ff4fd8', candela: 650, radius: 22 });
    }
  }

  const sceneryFile = new GlbFile('neon_scenery.glb', UE_DIR);
  const parts = [
    sceneryFile.add('neon_facades', facade.geometry(), { shader: 'textured', texture: 'neon_windows', roughness: 0.35, emissive: 1.3, tileM: 24 }),
    sceneryFile.add('neon_shopfronts', shops.geometry(), { shader: 'textured', texture: 'neon_shopfront', roughness: 0.3, emissive: 1.6, tileM: 16 }),
    sceneryFile.add('neon_roofs', roofs.geometry(), { shader: 'vertexColor', roughness: 0.8 }),
    sceneryFile.add('neon_tubes', neon.geometry(), { shader: 'vertexColor', roughness: 0.3, emissive: 9 }),
    sceneryFile.add('neon_paint', paint.geometry(), { shader: 'vertexColor', roughness: 0.6 }),
  ];
  await sceneryFile.write(outDir);

  const texDir = path.join(outDir, 'textures');
  const textures = [savePng(texDir, UE_DIR, 'neon_windows', windowsTexture(), 24), savePng(texDir, UE_DIR, 'neon_shopfront', shopfrontTexture(), 16)];
  return {
    meshes: [
      { file: 'neon_ribbons.glb', decoded: 'Tools/track-export/out/neon/neon_ribbons.glb', worldSpace: true, replacesRibbons: true, collision: false, nanite: false, ueAssets: ribbonParts.map((p) => p.ueAsset), parts: ribbonParts },
      { file: 'neon_scenery.glb', decoded: 'Tools/track-export/out/neon/neon_scenery.glb', worldSpace: true, collision: false, nanite: true, ueAssets: parts.map((p) => p.ueAsset), parts },
    ],
    instances,
    lights,
    textures,
    // The Violet lamps, lit for the night (VioletTrackside.tsx:87-94: emissive 4 at night).
    prefabs: {
      lamp_post: { node: 'lamp_post', ueAsset: '/Game/Tracks/Shared/violet_prefabs/StaticMeshes/lamp_post', material: { shader: 'vertexColor', roughness: 0.45, metalness: 0.6 } },
      lamp_head_glow: { node: 'lamp_head', ueAsset: '/Game/Tracks/Shared/violet_prefabs/StaticMeshes/lamp_head', material: { shader: 'vertexColor', color: '#ffd9a0', roughness: 0.3, emissive: 8 } },
    },
  };
}
