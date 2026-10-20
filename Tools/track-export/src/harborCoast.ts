/**
 * Sunset Harbor's UE upgrade (Docs/Port/AAA-PLAN.md): the coast as a shipped kart racer would draw it.
 * Gameplay is untouched (the JSON ribbons and walls are the web's); only visuals change:
 *
 * - a sculpted seawall on each wall line (±16.3 m): a chamfered concrete body with a rounded cream rail,
 *   replacing the flat wall and cap ribbons; its road face sits exactly on the sim's wall;
 * - rumble kerbs (raised crown, ridges, red and white) over the web's flat curb stripes;
 * - real water: a single layer water sea over a sloping seabed (turquoise shallows, deep blue offshore),
 *   replacing the flat textured sea and the translucent shallows; the surf ribbon stays as foam;
 * - per-batch PBR surfaces for the town (plaster, timber, sandstone rock) instead of one grain texture;
 * - palms and dune grass on the free stretches of beach;
 * - the sunset look (look.ue): a low gold sun, a blue-to-peach sky with lit clouds, warm haze.
 */
import path from 'node:path';
import * as THREE from 'three';
import { harborPoint } from '@app/games/kart/tracks/harbor';
import { palmFrond } from '@app/games/kart/tracks/harborGeometry';
import { flipWinding, merge, paint, place, rng, rumbleKerb, shade, smoothNormals, smoothstep, sweep, trunk, type EdgePoint } from './envKit';
import { GlbFile, type MaterialHint, type MeshEntry } from './scenery';

const UE_DIR = '/Game/Tracks/SunsetHarbor';
/** HarborTrack.tsx `SAMPLES`: ribbon rows per lap, uniform in spline t. */
const ROWS = 870;
const WALL = 16.3;
const KERB_IN = 14;
/** The sea surface (the web's plane sits at -7; the beach meets it at offset 40, y -6.9). */
export const HARBOR_SEA_Y = -6.96;

/** Web ribbons the coast pass draws its own way (visual only; the JSON ribbons stay the web's). */
export const HARBOR_REDRAWN_RIBBONS = new Set(['leftWall', 'rightWall', 'leftCap', 'rightCap', 'leftCurb', 'rightCurb', 'leftShallows', 'rightShallows']);

/** The sea: single layer water with the generated wave normal map (M_TrackWater parameters). */
export const HARBOR_SEA_MATERIAL: MaterialHint & { params: Record<string, number | string | number[]> } = {
  shader: 'water',
  color: '#0a1416',
  roughness: 0.035,
  params: {
    WaveNormal: 'env_waves_normal',
    WaveStrength: 0.45,
    WaveTileA: 7,
    WaveTileB: 19,
    WaveTileC: 63,
    Scattering: [0.07, 0.34, 0.3],
    Absorption: [0.32, 0.06, 0.04],
    PhaseG: 0.25,
  },
};

/** The town's batches by colour (HarborTrack.tsx buildHarborScenery): which CC0 set dresses each. */
const ROCKS = new Set(['#829278', '#9aa89b', '#8b968b', '#96aaa2']);
const TIMBER = new Set(['#b48d69', '#cba77c', '#a87a51', '#ba9168', '#b69a5c', '#a2874f']);
const LEAVES = new Set(['#63926a', '#417b64']);
/** Thin boxes on the old flat sea that faked glints; the real water reflects the sun itself. */
export const HARBOR_DROPPED_BATCHES = new Set(['#71b9b7']);

export function harborBatchMaterial(color: string): MaterialHint {
  if (ROCKS.has(color)) {
    // Warm sandstone in the low sun rather than grey lumps.
    return { shader: 'surface', color: '#ffffff', detail: 'rock_face', triplanar: true, tileM: color === '#96aaa2' ? 14 : 4, albedo: 0.75, normal: 1, rough: 0.4, roughness: 0.92 };
  }
  if (TIMBER.has(color)) return { shader: 'surface', color, detail: 'dark_wooden_planks', triplanar: true, tileM: 1.8, albedo: 0.35, normal: 0.7, rough: 0.4, roughness: 0.8 };
  if (LEAVES.has(color)) return { shader: 'foliage', color, detail: 'leafy_grass', triplanar: true, tileM: 0.9, albedo: 0.4, normal: 0.7, rough: 0.3, roughness: 0.75 };
  if (color === '#e6cc9e' || color === '#efcf99' || color === '#d9c39a') return { shader: 'surface', color, detail: 'gravelly_sand', triplanar: true, tileM: 2, albedo: 0.45, normal: 0.6, rough: 0.4, roughness: 0.95 };
  if (color === '#2f3a3d' || color === '#325e68') return { shader: 'surface', color, detail: 'painted_concrete', triplanar: true, tileM: 1.5, albedo: 0.2, normal: 0.3, rough: 0.4, roughness: 0.55 };
  // Painted plaster and stucco for the facades, roofs, awnings and street furniture.
  return { shader: 'surface', color, detail: 'plastered_wall_02', triplanar: true, tileM: 2.4, albedo: 0.3, normal: 0.55, rough: 0.45, roughness: 0.82 };
}

/**
 * The rock batches as stylised islets: smooth-shaded, sandstone on the flanks and grass on the tops
 * (vertex colours by facing; the material's Color stays white).
 */
export function dressRock(geometry: THREE.BufferGeometry, color: string): THREE.BufferGeometry {
  if (!ROCKS.has(color)) return geometry;
  const g = smoothNormals(paint(geometry));
  const stone = new THREE.Color(color === '#96aaa2' ? '#d8b48c' : '#c9a882'), grass = new THREE.Color('#7fb35a'), wet = new THREE.Color('#8d7a66');
  return shade(g, (p, n, c) => {
    c.copy(stone).lerp(wet, smoothstep(-4, -6.8, p.y));
    c.lerp(grass, smoothstep(0.55, 0.8, n.y) * smoothstep(-6, -3, p.y));
  });
}

/** One side's edge path at `offset` (outward normal = away from the road on that side). */
function edge(side: 1 | -1, offset: number, y: number): EdgePoint[] {
  return Array.from({ length: ROWS + 1 }, (_, i) => {
    const p = harborPoint(i / ROWS, side * offset);
    return { x: p.x, z: p.z, nx: side * p.nx, nz: side * p.nz, y };
  });
}

/** HarborMotion.tsx:15-18: the beach's height at an offset from the centreline. */
function beachY(offset: number) {
  const o = Math.abs(offset);
  return o <= 17 ? 0 : o >= 40 ? -6.9 : -0.06 - ((o - 17) / 23) * 6.84;
}

function seawall(side: 1 | -1) {
  // (out from the wall line, up): the road face on the wall line, a chamfer, a rounded rail, the sea face.
  const profile: [number, number][] = [
    [0, -0.35], [0, 0.82], [0.05, 0.95], [0.12, 1.06], [0.22, 1.2], [0.36, 1.3], [0.52, 1.32], [0.66, 1.28], [0.76, 1.16], [0.8, 1.0], [0.82, -0.6],
  ];
  const body = new THREE.Color('#d8e3dc'), rail = new THREE.Color('#fff2d8'), band = new THREE.Color('#5aa49c');
  const g = sweep(edge(side, WALL, 0), profile, {
    closed: false,
    color: (_along, k) => (k >= 4 && k <= 7 ? rail : k === 1 ? band : body),
  });
  // The left side's path runs mirrored, so its sweep winds the other way: flip it so both face out.
  return paint(side === 1 ? g : flipWinding(g));
}

function seabed(side: 1 | -1) {
  const path = edge(side, 39.5, 0);
  const profile: [number, number][] = [[0, -7.02], [15, -9.5], [40, -13.5], [85, -19]];
  const sand = new THREE.Color('#c9b48a');
  const g = sweep(path, profile, { closed: false, color: (_a, k) => sand.clone().multiplyScalar(1 - k * 0.12) });
  return side === 1 ? g : flipWinding(g);
}

/** A palm: a ringed, curving trunk and a crown of the web's fronds (harborGeometry palmFrond). */
function palm(seed: number) {
  const rand = rng(seed);
  const height = 8 + rand() * 2;
  const stem = trunk(height, 0.38, 0.22, 1.6, seed);
  shade(stem, (p, _n, c) => c.set(Math.floor(p.y / 0.45) % 2 ? '#a87a51' : '#c49a6c'));
  const tip = new THREE.Vector3();
  const pos = stem.getAttribute('position');
  for (let i = 0; i < pos.count; i++) if (pos.getY(i) > tip.y) tip.fromBufferAttribute(pos, i);
  const fronds = merge(Array.from({ length: 11 }, (_, k) => {
    const f = paint(palmFrond(4.6 + (k % 3) * 0.7, 1.0), k % 2 ? '#4f8a52' : '#6aa055');
    return place(f, [tip.x, tip.y - 0.1, tip.z], [0, (k / 11) * Math.PI * 2 + rand() * 0.3, 0]);
  }));
  const coconuts = merge([0, 1, 2].map((k) => place(paint(new THREE.SphereGeometry(0.2, 10, 8), '#6b4b2a'), [tip.x + Math.cos(k * 2.1) * 0.3, tip.y - 0.35, tip.z + Math.sin(k * 2.1) * 0.3])));
  return { trunk: merge([stem, coconuts]), fronds };
}

/** Dune grass: a clump of thin, arching blades. */
function duneGrass(seed: number) {
  const rand = rng(seed);
  const blades: THREE.BufferGeometry[] = [];
  for (let i = 0; i < 40; i++) {
    const a = rand() * Math.PI * 2, r = Math.sqrt(rand()) * 0.5, h = 0.6 + rand() * 0.7, lean = 0.2 + rand() * 0.5;
    const blade = new THREE.PlaneGeometry(0.05, h, 1, 4);
    const p = blade.getAttribute('position');
    for (let k = 0; k < p.count; k++) {
      const t = (p.getY(k) + h / 2) / h;
      p.setXYZ(k, p.getX(k) * (1 - t * 0.8), t * h, t * t * lean);
    }
    blades.push(place(paint(blade), [Math.cos(a) * r, 0, Math.sin(a) * r], [0, a, 0]));
  }
  return shade(merge(blades), (p, _n, c) => c.set('#8f9a55').lerp(new THREE.Color('#d8c88a'), smoothstep(0.2, 1.1, p.y)));
}

/**
 * Free beach spots: every few metres along both beaches, between offsets 20 and 34, away from anything the
 * web's scenery already stands on (`occupied(x, z)`).
 */
function beachSpots(occupied: (x: number, z: number) => boolean, seed: number) {
  const rand = rng(seed);
  const spots: { x: number; y: number; z: number; offset: number }[] = [];
  for (let i = 0; i < ROWS; i += 3) {
    for (const side of [1, -1] as const) {
      if (rand() > 0.55) continue;
      const offset = 20 + rand() * 14;
      const p = harborPoint((i + rand()) / ROWS, side * offset);
      if (occupied(p.x, p.z) || spots.some((s) => Math.hypot(s.x - p.x, s.z - p.z) < 5)) continue;
      spots.push({ x: p.x, y: beachY(offset), z: p.z, offset });
    }
  }
  return spots;
}

/** Writes harbor_coast.glb (world meshes and prefabs) and returns the JSON pieces. */
export async function exportHarborCoast(outDir: string, occupied: (x: number, z: number) => boolean) {
  const file = new GlbFile('harbor_coast.glb', UE_DIR);
  const parts: MeshEntry[] = [];
  const wallHint: MaterialHint = { shader: 'surface', detail: 'painted_concrete', triplanar: true, tileM: 2.2, albedo: 0.4, normal: 0.6, rough: 0.45, roughness: 0.7 };
  const kerbHint: MaterialHint = { shader: 'surface', detail: 'painted_concrete', tileM: 1.5, albedo: 0.2, normal: 0.35, rough: 0.35, roughness: 0.55 };
  for (const side of [1, -1] as const) {
    const name = side === 1 ? 'right' : 'left';
    parts.push(file.add(`seawall_${name}`, seawall(side), wallHint));
    const kerbPath = edge(side, KERB_IN, 0.012);
    parts.push(file.add(`kerb_${name}`, rumbleKerb(kerbPath, ['#d7312a', '#f7f4ec'], { inset: 0, outset: 1.5, stripe: 1.86, height: 0.055 }), kerbHint));
    parts.push(file.add(`seabed_${name}`, seabed(side), { shader: 'surface', detail: 'gravelly_sand', tileM: 3, albedo: 0.6, normal: 0.7, rough: 0.4, roughness: 1 }));
  }
  const prefabs: Record<string, MeshEntry> = {};
  const palms = [palm(3), palm(8)];
  palms.forEach((p, v) => {
    prefabs[`coast_palm_trunk_${v}`] = file.add(`coast_palm_trunk_${v}`, p.trunk, { shader: 'surface', detail: 'rock_face', triplanar: true, tileM: 0.9, albedo: 0.3, normal: 0.8, rough: 0.3, roughness: 0.9 });
    prefabs[`coast_palm_fronds_${v}`] = file.add(`coast_palm_fronds_${v}`, p.fronds, { shader: 'foliage', detail: 'leafy_grass', triplanar: true, tileM: 0.6, albedo: 0.3, normal: 0.6, rough: 0.3, roughness: 0.7 });
  });
  prefabs.dune_grass = file.add('dune_grass', duneGrass(5), { shader: 'foliage', detail: 'leafy_grass', triplanar: true, tileM: 0.5, albedo: 0.2, normal: 0.4, rough: 0.3, roughness: 0.8 });
  await file.write(outDir);

  const rand = rng(1610);
  const instances: Record<string, unknown>[] = [];
  const r4 = (n: number) => Math.round(n * 10000) / 10000;
  for (const s of beachSpots(occupied, 42)) {
    if (rand() < 0.45) {
      const v = Math.floor(rand() * 2), yaw = r4(rand() * 6.28), scale = r4(0.85 + rand() * 0.35);
      for (const part of ['trunk', 'fronds']) instances.push({ prefab: `coast_palm_${part}_${v}`, x: r4(s.x), y: r4(s.y - 0.1), z: r4(s.z), yaw, scale: [scale, scale, scale] });
    } else {
      instances.push({ prefab: 'dune_grass', x: r4(s.x), y: r4(s.y - 0.05), z: r4(s.z), yaw: r4(rand() * 6.28), scale: [1, 0.8 + rand() * 0.5, 1].map(r4) });
    }
  }
  return {
    mesh: { file: 'harbor_coast.glb', decoded: 'Tools/track-export/out/harbor/harbor_coast.glb', worldSpace: true, transformBaked: true, collision: false, nanite: true, ueAssets: parts.map((p) => p.ueAsset), parts },
    prefabs,
    instances,
  };
}

/**
 * Sunset Harbor's look: the sun a hand above the sea, gold light raking the town, a blue-to-peach sky with
 * sunlit clouds, warm haze over the water. Keys are read by ATrackBuilder::ApplyLook (look.ue).
 */
export const HARBOR_LOOK = {
  sunDirection: [0.62, 0.3, -0.72],
  sunColor: '#ffd29a',
  sunScale: 1.3,
  skyLightScale: 1.2,
  skyLightTint: '#a9c8ff',
  skyTop: '#2a64d6',
  skyMid: '#6fa2ec',
  skyHorizon: '#ffd7a6',
  skyBelow: '#f2c9a0',
  sunDisc: '#fff4d8',
  glowPower: 12,
  sunGlow: 0.9,
  horizonGlow: 0.7,
  clouds: 0.5,
  cloudScale: 0.3,
  cloudLit: '#fff1dc',
  cloudShade: '#a9b7d8',
  fogColor: '#e9e4dc',
  fogDensity: 0.0009,
  fogFalloff: 0.008,
  fogStart: 160,
  fogMaxOpacity: 0.55,
  fogSunScatter: 0.35,
};
