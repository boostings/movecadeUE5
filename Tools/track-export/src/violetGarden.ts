/**
 * Violet Loop, rebuilt original: "the Violet Gardens", a twilight crystal garden (Docs/Port/AAA-PLAN.md).
 *
 * The racing line, walls, checkpoints, boost pads, item boxes and dirt are untouched: they come from the
 * movecade GLB through violetMesh.mjs's collision analysis. Only the road (Object_24), its lines and
 * chequers (Object_25) and the verge (Object_11, the sim's dirt, dressed here as lawn) survive from the
 * GLB's visuals. Everything else is built here, over the same footprints:
 *
 * - the barrier chains (Object_13) become clipped hedges on cream stone plinths, traced from the blocks'
 *   outlines, so every sweep wall has a hedge face on it; stone pillars with lanterns punctuate them;
 * - the start structure (Object_10, the brick feet) becomes a stone gate with a violet arch and crystal
 *   finials on the same footprints, carrying the existing Violet Loop banner;
 * - the hills (Object_18) become a heightfield: raised flower beds inside the hedged islands, rolling
 *   meadows and lavender rows beyond the outer hedge, crystal-topped mountains on the horizon;
 * - set pieces: a glass conservatory, a gazebo, crystal spires, hot-air balloons at dusk;
 * - rumble kerbs in violet and cream along the movecade kerb plan.
 *
 * All geometry is original and procedural (envKit.ts); surfaces use the CC0 sets already in the project.
 * Coordinates: world meshes are written rebased (road at y = 0, transformBaked); instances are in source
 * coordinates (road at groundY) like every other scenery.instances entry.
 */
import path from 'node:path';
import * as THREE from 'three';
import { sideNormal, type KerbRun, type TrackSample } from '@app/games/kart/scene/trackside';
import {
  Noise, PointGrid, canopy, crystal, crystalCluster, displace, lerp, merge, orient, paint, place, rng, roundedBox,
  rumbleKerb, shade, smoothstep, terrain, trunk, type EdgePoint, type V3,
} from './envKit';
import { GlbFile, type MaterialHint, type MeshEntry } from './scenery';

type XZ = [number, number];
const UE_DIR = '/Game/Tracks/VioletLoop';

/** The garden palette: cartoon-saturated, soft, lit by a low pink-gold sun under a violet sky. */
export const GARDEN = {
  hedge: '#3d6b3a',
  hedgeTop: '#5d8f45',
  stone: '#efe4d2',
  stoneShade: '#d9cbb4',
  violet: '#7656d8',
  deep: '#3a2775',
  gold: '#f2c24f',
  lawn: '#7fae4f',
  meadow: '#6f9e45',
  crystal: ['#8c5cff', '#ff5fc8', '#45c8f5'],
  blossom: ['#f0a8de', '#cfa6ff', '#ffc2e6'],
  foliage: ['#4f8a46', '#3f7a52', '#6a9a3f'],
  lavender: '#8a63d2',
  bark: '#7a5a46',
  lantern: '#ffcf8a',
} as const;

/** The hedge cross-section (metres, inset from the block faces / height above the road). */
const HEDGE = { height: 1.75, radius: 0.42, inset: 0.14, plinthTop: 0.42, base: -0.5 };
/** Stone pillars along the hedges: spacing along an edge, footprint, height. */
const PILLAR = { spacing: 17, size: 0.95, height: 2.45 };
/** Lanterns get real point lights only near the road, and only this many: every light costs Lumen
 *  surface-cache lighting across thousands of instances, and the lanterns' emissive glow carries the look. */
const MAX_LANTERN_LIGHTS = 6;

/** What violetMesh.mjs measured (the parts this module needs). */
export interface VioletFootprints {
  barriers: { cellM: number; minY: number; maxY: number; loops: XZ[][] };
  gantry: { node: string; min: V3; max: V3 }[];
  motifs: { kind: 'pipe' | 'bricks'; min: V3; max: V3 }[];
  dirtGrid: { cellM: number; minX: number; minZ: number; cols: number; rows: number; runs: number[] };
  /** The sim's sweep walls, flat [ax, az, bx, bz, nx, nz, ...]: every one must have art on it. */
  sweepWalls: number[];
}

export interface GardenInputs {
  outRoot: string;
  samples: readonly TrackSample[];
  kerbs: readonly KerbRun[];
  footprints: VioletFootprints;
  /** Source y of the drawn road (-1.605); world meshes are written with it at 0. */
  groundY: number;
  /** Gameplay spots dressing must keep clear of (item boxes, boost pads, grid), source xz. */
  keepClear: readonly XZ[];
}

interface Instance { prefab: string; x: number; y: number; z: number; yaw: number; scale?: V3; tint?: string; variant?: number }
interface Light { x: number; y: number; z: number; color: string; candela: number; radius: number }

const r4 = (n: number) => Math.round(n * 10000) / 10000;

// ---- Masks and distance -------------------------------------------------------------------------------

/**
 * Where a kart can be (road or dirt), on a 2 m grid, and the distance from it (a two-pass chamfer
 * transform). Islands inside the hedges and everything beyond the outer hedge get their terrain from it.
 */
class PlayField {
  readonly cell = 2;
  readonly minX = -460; readonly minZ = -580; readonly cols = 620; readonly rows = 580;
  readonly dist: Float32Array;
  constructor(samples: readonly TrackSample[], dirt: VioletFootprints['dirtGrid']) {
    const { cols, rows, cell } = this;
    const play = new Uint8Array(cols * rows);
    const mark = (x: number, z: number) => {
      const i = Math.floor((x - this.minX) / cell), j = Math.floor((z - this.minZ) / cell);
      if (i >= 0 && j >= 0 && i < cols && j < rows) play[j * cols + i] = 1;
    };
    // Dirt runs are [row, firstCol, lastCol] (export.ts dirtGrid), on 0.25 m cells.
    const runs = dirt.runs as unknown as number[][];
    for (const [row, a, b] of runs) {
      const z = dirt.minZ + (row! + 0.5) * dirt.cellM;
      for (let c = a!; c <= b!; c += 4) mark(dirt.minX + (c + 0.5) * dirt.cellM, z);
      mark(dirt.minX + (b! + 0.5) * dirt.cellM, z);
    }
    // The road: every sample's edge-to-edge span, densified.
    for (let i = 0; i < samples.length; i++) {
      const a = samples[i]!, b = samples[(i + 1) % samples.length]!;
      for (let t = 0; t < 1; t += 0.25) {
        const x = lerp(a.x, b.x, t), z = lerp(a.z, b.z, t);
        const nl = sideNormal(a, -1), nr = sideNormal(a, 1);
        for (let o = -a.sides[-1].edge; o <= a.sides[1].edge; o += 1) {
          const n = o < 0 ? nl : nr;
          mark(x + n.x * Math.abs(o), z + n.z * Math.abs(o));
        }
      }
    }
    // Chamfer distance (1, sqrt 2) in metres.
    const INF = 1e9, D = cell, DD = cell * Math.SQRT2;
    const d = new Float32Array(cols * rows).map((_, k) => (play[k] ? 0 : INF));
    for (let j = 0; j < rows; j++) for (let i = 0; i < cols; i++) {
      const k = j * cols + i;
      if (i > 0) d[k] = Math.min(d[k]!, d[k - 1]! + D);
      if (j > 0) d[k] = Math.min(d[k]!, d[k - cols]! + D);
      if (i > 0 && j > 0) d[k] = Math.min(d[k]!, d[k - cols - 1]! + DD);
      if (i < cols - 1 && j > 0) d[k] = Math.min(d[k]!, d[k - cols + 1]! + DD);
    }
    for (let j = rows - 1; j >= 0; j--) for (let i = cols - 1; i >= 0; i--) {
      const k = j * cols + i;
      if (i < cols - 1) d[k] = Math.min(d[k]!, d[k + 1]! + D);
      if (j < rows - 1) d[k] = Math.min(d[k]!, d[k + cols]! + D);
      if (i < cols - 1 && j < rows - 1) d[k] = Math.min(d[k]!, d[k + cols + 1]! + DD);
      if (i > 0 && j < rows - 1) d[k] = Math.min(d[k]!, d[k + cols - 1]! + DD);
    }
    this.dist = d;
  }
  /** Bilinear distance to the play field; beyond the grid, the distance to the grid's edge plus its value there. */
  at(x: number, z: number) {
    const fx = (x - this.minX) / this.cell - 0.5, fz = (z - this.minZ) / this.cell - 0.5;
    const cx = Math.min(this.cols - 1.001, Math.max(0, fx)), cz = Math.min(this.rows - 1.001, Math.max(0, fz));
    const i = Math.floor(cx), j = Math.floor(cz), u = cx - i, v = cz - j;
    const k = j * this.cols + i;
    const val = lerp(lerp(this.dist[k]!, this.dist[k + 1]!, u), lerp(this.dist[k + this.cols]!, this.dist[k + this.cols + 1]!, u), v);
    return val + Math.hypot((fx - cx) * this.cell, (fz - cz) * this.cell);
  }
}

function pointInLoop(x: number, z: number, loop: readonly XZ[]) {
  let inside = false;
  for (let i = 0, j = loop.length - 1; i < loop.length; j = i++) {
    const [xi, zi] = loop[i]!, [xj, zj] = loop[j]!;
    if ((zi > z) !== (zj > z) && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}
const loopArea = (loop: readonly XZ[]) => loop.reduce((a, [x, z], i) => { const [x2, z2] = loop[(i + 1) % loop.length]!; return a + x * z2 - x2 * z; }, 0) / 2;

// ---- Hedges ---------------------------------------------------------------------------------------------

interface LoopPoint { x: number; z: number; nx: number; nz: number; miter: number; corner: boolean }

/**
 * A traced outline (interior on the left of travel) as points with outward normals: every corner, plus
 * points every `step` metres along each edge (kept clear of the corners so inset rings never fold).
 */
function resampleLoop(loop: readonly XZ[], step: number, cornerClear: number): LoopPoint[] {
  const n = loop.length;
  const edgeNormal = (k: number) => {
    const [ax, az] = loop[k]!, [bx, bz] = loop[(k + 1) % n]!;
    const len = Math.hypot(bx - ax, bz - az) || 1;
    return { x: (bz - az) / len, z: -(bx - ax) / len }; // right of travel = outward
  };
  const out: LoopPoint[] = [];
  for (let k = 0; k < n; k++) {
    const prev = edgeNormal((k - 1 + n) % n), next = edgeNormal(k);
    let nx = prev.x + next.x, nz = prev.z + next.z;
    const len = Math.hypot(nx, nz) || 1;
    nx /= len; nz /= len;
    const miter = 1 / Math.max(0.5, nx * next.x + nz * next.z);
    const [ax, az] = loop[k]!, [bx, bz] = loop[(k + 1) % n]!;
    out.push({ x: ax, z: az, nx, nz, miter, corner: true });
    const edgeLen = Math.hypot(bx - ax, bz - az);
    const count = Math.floor((edgeLen - 2 * cornerClear) / step);
    for (let s = 0; s <= count && count >= 0; s++) {
      const t = (cornerClear + (count > 0 ? (s * (edgeLen - 2 * cornerClear)) / count : (edgeLen - 2 * cornerClear) / 2)) / edgeLen;
      if (t <= 0 || t >= 1) continue;
      out.push({ x: lerp(ax, bx, t), z: lerp(az, bz, t), nx: next.x, nz: next.z, miter: 1, corner: false });
    }
  }
  return out;
}

/**
 * Rings of a profile swept around a resampled loop: rows (inset, y), quads between rows. Each quad faces
 * out of the solid: its profile segment's outward normal is (outward x dy, do) in (horizontal, up).
 */
function sweepLoop(points: readonly LoopPoint[], rows: readonly [number, number][]) {
  const pos: number[] = [];
  const P = (p: LoopPoint, [o, y]: [number, number]) => new THREE.Vector3(p.x - p.nx * o * p.miter, y, p.z - p.nz * o * p.miter);
  for (let k = 0; k < points.length; k++) {
    const a = points[k]!, b = points[(k + 1) % points.length]!;
    for (let r = 0; r < rows.length - 1; r++) {
      const [o0, y0] = rows[r]!, [o1, y1] = rows[r + 1]!;
      const hint = new THREE.Vector3((a.nx + b.nx) * (y1 - y0), 2 * (o1 - o0), (a.nz + b.nz) * (y1 - y0));
      const q = [P(a, rows[r]!), P(b, rows[r]!), P(a, rows[r + 1]!), P(b, rows[r + 1]!)];
      const face = new THREE.Vector3().subVectors(q[2]!, q[0]!).cross(new THREE.Vector3().subVectors(q[1]!, q[0]!));
      const tris = face.dot(hint) >= 0 ? [q[0]!, q[2]!, q[1]!, q[1]!, q[2]!, q[3]!] : [q[0]!, q[1]!, q[2]!, q[1]!, q[3]!, q[2]!];
      for (const v of tris) pos.push(v.x, v.y, v.z);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.computeVertexNormals();
  return paint(g);
}

/** A flat cap over the loops inset by `inset`, at height y (outer loops with their holes). */
function capLoops(groups: { outer: LoopPoint[]; holes: LoopPoint[][] }[], inset: number, y: number) {
  const pos: number[] = [];
  const ring = (pts: LoopPoint[]) => pts.map((p) => new THREE.Vector2(p.x - p.nx * inset * p.miter, p.z - p.nz * inset * p.miter));
  for (const { outer, holes } of groups) {
    const contour = ring(outer), holeRings = holes.map(ring);
    const all = [contour, ...holeRings].flat();
    for (const [a, b, c] of THREE.ShapeUtils.triangulateShape(contour, holeRings)) {
      for (const k of [a!, b!, c!]) pos.push(all[k]!.x, y, all[k]!.y);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  return orient(paint(g), () => new THREE.Vector3(0, 1, 0));
}

function hedges(loops: readonly XZ[][], seed: number) {
  const noise = new Noise(seed);
  const outlines = loops.map((loop) => ({ loop, area: loopArea(loop), points: resampleLoop(loop, 0.38, 0.7) }));
  // Outer outlines wind one way, the holes of ring-shaped chains the other: the largest sets the sign.
  const sign = Math.sign(outlines.reduce((a, o) => (Math.abs(o.area) > Math.abs(a.area) ? o : a)).area);
  const outers = outlines.filter((o) => Math.sign(o.area) === sign);
  const groups = outers.map((o) => ({ outer: o.points, holes: [] as LoopPoint[][], loop: o.loop, area: Math.abs(o.area) }));
  for (const hole of outlines.filter((o) => Math.sign(o.area) !== sign)) {
    const owner = groups.filter((g) => pointInLoop(hole.loop[0]![0], hole.loop[0]![1], g.loop)).sort((a, b) => a.area - b.area)[0];
    owner?.holes.push(hole.points);
  }

  const { height: H, radius: R, inset: I, plinthTop: PT, base: B } = HEDGE;
  const plinthRows: [number, number][] = [[0.0, B], [0.0, PT - 0.07], [0.04, PT - 0.02], [0.1, PT], [I + 0.18, PT]];
  const hedgeRows: [number, number][] = [[I + 0.02, PT - 0.02], [I, PT + 0.25], [I, (PT + H - R) / 2], [I, H - R]];
  for (let a = 1; a <= 5; a++) {
    const t = (a / 5) * (Math.PI / 2);
    hedgeRows.push([I + R - R * Math.cos(t), H - R + R * Math.sin(t)]);
  }
  const allLoops = outlines.map((o) => o.points);
  const plinth = merge([...allLoops.map((pts) => sweepLoop(pts, plinthRows)), capLoops(groups, I + 0.18, PT)]);
  const hedgeSides = merge(allLoops.map((pts) => sweepLoop(pts, hedgeRows)));
  const hedgeTop = capLoops(groups, I + R, H);
  const leafy = (p: THREE.Vector3) =>
    0.075 * noise.n3(p.x * 1.6, p.y * 1.6, p.z * 1.6) + 0.04 * noise.n3(p.x * 4.3 + 11, p.y * 4.3, p.z * 4.3);
  const body = merge([hedgeSides, hedgeTop]);
  displace(body, (p) => (p.y < PT + 0.05 ? 0 : leafy(p)));
  shade(body, (p, n, c) => {
    // Darker deep in the base, sun-bleached on top: reads as foliage before any light.
    const t = smoothstep(PT, H, p.y);
    const top = new THREE.Color(GARDEN.hedgeTop), low = new THREE.Color(GARDEN.hedge);
    c.copy(low).lerp(top, t * 0.7 + Math.max(0, n.y) * 0.3);
    c.multiplyScalar(0.92 + 0.16 * noise.n3(p.x * 0.15, 0, p.z * 0.15));
  });
  shade(plinth, (p, _n, c) => c.set(p.y > PT - 0.03 ? GARDEN.stone : GARDEN.stoneShade).lerp(new THREE.Color(GARDEN.stone), smoothstep(B, PT, p.y)));

  // Pillars: on every convex corner and every PILLAR.spacing along long edges, flush with the plinth.
  const pillars: { x: number; z: number; yaw: number }[] = [];
  for (const { loop } of outlines) {
    const n = loop.length;
    for (let k = 0; k < n; k++) {
      const [ax, az] = loop[k]!, [bx, bz] = loop[(k + 1) % n]!;
      const len = Math.hypot(bx - ax, bz - az);
      if (len < 2.2) continue;
      const tx = (bx - ax) / len, tz = (bz - az) / len, nx = tz, nz = -tx;
      const count = Math.max(1, Math.round(len / PILLAR.spacing));
      for (let s = 0; s <= count; s++) {
        const along = lerp(PILLAR.size / 2, len - PILLAR.size / 2, s / count);
        const x = ax + tx * along - nx * PILLAR.size / 2, z = az + tz * along - nz * PILLAR.size / 2;
        if (pillars.some((p) => Math.hypot(p.x - x, p.z - z) < 1.6)) continue;
        pillars.push({ x, z, yaw: Math.atan2(nx, nz) });
      }
    }
  }
  return { plinth, body, pillars, groups };
}

// ---- Prefabs -----------------------------------------------------------------------------------------------

function gardenPillar() {
  const S = PILLAR.size, Hh = PILLAR.height;
  const shaft = roundedBox(S * 0.86, Hh - 0.55, S * 0.86, 0.06, 0.5);
  place(shaft, [0, 0.35, 0]);
  const foot = roundedBox(S, 0.42, S, 0.05, 0.5);
  const cap = roundedBox(S * 1.02, 0.22, S * 1.02, 0.07, 0.5);
  place(cap, [0, Hh - 0.22, 0]);
  const band = roundedBox(S * 0.9, 0.14, S * 0.9, 0.04, 0.5);
  place(band, [0, Hh - 0.62, 0]);
  const lanternBase = roundedBox(0.36, 0.12, 0.36, 0.03, 0.5);
  place(lanternBase, [0, Hh, 0]);
  const posts = [-1, 1].flatMap((sx) => [-1, 1].map((sz) => place(paint(new THREE.CylinderGeometry(0.025, 0.025, 0.5, 6)), [sx * 0.15, Hh + 0.37, sz * 0.15])));
  const roof = place(paint(new THREE.ConeGeometry(0.29, 0.26, 4, 1)), [0, Hh + 0.75, 0], [0, Math.PI / 4, 0]);
  const finial = place(paint(new THREE.SphereGeometry(0.06, 10, 8)), [0, Hh + 0.92, 0]);
  return {
    stone: shade(merge([shaft, foot, cap]), (p, _n, c) => c.set(p.y < 0.4 ? GARDEN.stoneShade : GARDEN.stone)),
    trim: merge([paint(band, GARDEN.violet), paint(lanternBase, '#2c2440'), ...posts.map((g) => paint(g, '#2c2440')), paint(roof, GARDEN.deep), paint(finial, GARDEN.gold)]),
    glow: place(paint(new THREE.SphereGeometry(0.13, 16, 12), GARDEN.lantern), [0, Hh + 0.36, 0]),
  };
}

function startGate(feet: { cx: number; cz: number; w: number; d: number }[], banner: { y0: number; y1: number; z: number; x0: number; x1: number }) {
  const stone: THREE.BufferGeometry[] = [], trim: THREE.BufferGeometry[] = [], crystals: THREE.BufferGeometry[] = [], glow: THREE.BufferGeometry[] = [];
  const towerTop = banner.y1 + 1.6;
  for (const f of feet) {
    const base = roundedBox(f.w + 0.3, 1.2, f.d + 0.3, 0.12, 0.6);
    place(base, [f.cx, -0.5, f.cz]);
    const shaft = roundedBox(f.w, towerTop, f.d, 0.18, 0.8);
    place(shaft, [f.cx, 0, f.cz]);
    stone.push(base, shaft);
    for (const y of [3.2, banner.y0 - 0.6, towerTop - 0.4]) trim.push(place(roundedBox(f.w + 0.18, 0.32, f.d + 0.18, 0.08, 0.6), [f.cx, y, f.cz]));
    // A violet inset panel on the two faces along the road, with a gold crystal emblem.
    for (const sz of [-1, 1]) {
      trim.push(place(roundedBox(f.w * 0.62, 5.2, 0.12, 0.05, 0.4), [f.cx, 4.2, f.cz + sz * (f.d / 2 + 0.02)]));
      glow.push(place(paint(new THREE.OctahedronGeometry(0.42, 0)), [f.cx, 6.8, f.cz + sz * (f.d / 2 + 0.12)], [0, 0, 0], [1, 1.5, 0.35]));
    }
    const cap = place(paint(new THREE.CylinderGeometry(f.w * 0.42, f.w * 0.62, 1.1, 8)), [f.cx, towerTop + 0.55, f.cz]);
    trim.push(cap);
    crystals.push(place(crystalCluster(41 + Math.round(f.cx), 6, 1.15), [f.cx, towerTop + 1.0, f.cz]));
  }
  // The arch: a gently curved violet beam under the banner and a slimmer one over it, both spanning the towers.
  const [L, R] = [feet[0]!, feet[1]!];
  const span = R.cx - L.cx;
  const beam = (y: number, h: number, rise: number) => {
    const steps = 40, pts: THREE.BufferGeometry[] = [];
    for (let s = 0; s < steps; s++) {
      const t0 = s / steps, t1 = (s + 1) / steps;
      const x0 = L.cx + span * t0, x1 = L.cx + span * t1;
      const y0 = y + rise * Math.sin(Math.PI * t0), y1 = y + rise * Math.sin(Math.PI * t1);
      const g = roundedBox(Math.hypot(x1 - x0, y1 - y0) + 0.05, h, 1.1, Math.min(0.2, h / 2 - 0.01), 1.2);
      place(g, [0, -h / 2, 0]);
      g.applyMatrix4(new THREE.Matrix4().makeRotationZ(Math.atan2(y1 - y0, x1 - x0)));
      place(g, [(x0 + x1) / 2, (y0 + y1) / 2, banner.z]);
      pts.push(g);
    }
    return merge(pts);
  };
  trim.push(beam(banner.y0 - 0.15, 0.9, 0.0), beam(banner.y1 + 0.55, 0.55, 1.4));
  // Gold studs along the lower beam.
  for (let s = 1; s < 12; s++) glow.push(place(paint(new THREE.SphereGeometry(0.16, 12, 8)), [L.cx + (span * s) / 12, banner.y0 - 1.1, banner.z - 0.6]));
  return {
    stone: shade(merge(stone), (p, _n, c) => c.set(GARDEN.stone).multiplyScalar(p.y < 1 ? 0.9 : 1)),
    trim: shade(merge(trim), (_p, _n, c) => c.set(GARDEN.violet)),
    crystals: merge(crystals),
    glow: merge(glow),
  };
}

/** A lavender bush: a soft green mound bristling with violet flower spikes. */
function lavenderBush(seed: number) {
  const rand = rng(seed);
  const parts: THREE.BufferGeometry[] = [];
  const mound = paint(new THREE.SphereGeometry(0.55, 14, 8, 0, Math.PI * 2, 0, Math.PI / 2), '#58784a');
  place(mound, [0, -0.05, 0], [0, 0, 0], [1, 0.75, 1]);
  parts.push(mound);
  for (let i = 0; i < 34; i++) {
    const a = rand() * Math.PI * 2, rr = Math.sqrt(rand()) * 0.5;
    const lean = rr * 0.9;
    const h = 0.55 + rand() * 0.35;
    const stem = paint(new THREE.CylinderGeometry(0.012, 0.016, h, 4), '#6b8a52');
    const spike = paint(new THREE.CapsuleGeometry(0.045, 0.2, 3, 6), GARDEN.lavender);
    place(stem, [0, h / 2, 0]);
    place(spike, [0, h + 0.08, 0]);
    const g = merge([stem, spike]);
    g.applyMatrix4(new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(Math.sin(a) * lean, 0, -Math.cos(a) * lean)));
    place(g, [Math.cos(a) * rr * 0.6, 0.1, Math.sin(a) * rr * 0.6]);
    parts.push(g);
  }
  return shade(merge(parts), (p, _n, c) => c.multiplyScalar(0.8 + 0.25 * Math.min(1, p.y / 0.9)));
}

/** A low flower patch for the raised beds: a carpet mound with dots of colour. */
function flowerPatch(seed: number) {
  const rand = rng(seed);
  const parts: THREE.BufferGeometry[] = [paint(place(new THREE.SphereGeometry(1.4, 16, 6, 0, Math.PI * 2, 0, Math.PI / 2), [0, -0.1, 0], [0, 0, 0], [1, 0.22, 1]), '#5c8a44')];
  const colors = ['#ffd1f0', '#ffffff', '#ffe27a', '#c8a2ff', '#ff9fc9'];
  for (let i = 0; i < 46; i++) {
    const a = rand() * Math.PI * 2, rr = Math.sqrt(rand()) * 1.25;
    const y = 0.21 * Math.sqrt(Math.max(0, 1 - (rr / 1.4) ** 2)) - 0.08;
    parts.push(place(paint(new THREE.IcosahedronGeometry(0.07 + rand() * 0.05, 1), colors[i % colors.length]), [Math.cos(a) * rr, y + 0.06, Math.sin(a) * rr]));
  }
  return merge(parts);
}

function treeTrunk() {
  return shade(trunk(4.2, 0.36, 0.2, 0.5, 3), (p, _n, c) => c.set(GARDEN.bark).multiplyScalar(0.8 + 0.2 * Math.min(1, p.y / 4)));
}

/** A blossom tree crown: three overlapping lumpy blobs, base at y = 0 (sits on the trunk top). */
function blossomCrown(seed: number) {
  const a = canopy(seed, 2.4, 0.78, 0.28);
  const b = place(canopy(seed + 1, 1.8, 0.8, 0.3), [1.6, 0.6, 0.4]);
  const c = place(canopy(seed + 2, 1.7, 0.8, 0.3), [-1.3, 0.8, -0.9]);
  return merge([a, b, c]);
}

/** A round garden tree crown (one soft blob), base at y = 0. */
function roundCrown(seed: number) {
  return canopy(seed, 3.0, 0.9, 0.24);
}

function topiary() {
  const ball = place(canopy(77, 0.9, 1, 0.12), [0, 0.9, 0]);
  const pot = place(roundedBox(1.0, 0.7, 1.0, 0.1, 0.4), [0, 0, 0]);
  return { leaf: ball, pot: paint(pot, GARDEN.stone) };
}

function gazebo() {
  const stone: THREE.BufferGeometry[] = [], trim: THREE.BufferGeometry[] = [], roof: THREE.BufferGeometry[] = [];
  stone.push(place(paint(new THREE.CylinderGeometry(5.4, 5.8, 0.7, 8)), [0, 0.0, 0]));
  stone.push(place(paint(new THREE.CylinderGeometry(4.9, 5.2, 0.3, 8)), [0, 0.5, 0]));
  for (let k = 0; k < 8; k++) {
    const a = (k / 8) * Math.PI * 2 + Math.PI / 8;
    trim.push(place(paint(new THREE.CylinderGeometry(0.2, 0.24, 4.2, 12)), [Math.cos(a) * 4.4, 2.7, Math.sin(a) * 4.4]));
  }
  trim.push(place(paint(new THREE.CylinderGeometry(5.0, 5.0, 0.45, 8)), [0, 4.95, 0]));
  const dome = paint(new THREE.SphereGeometry(5.4, 24, 10, 0, Math.PI * 2, 0, Math.PI / 2));
  place(dome, [0, 5.1, 0], [0, 0, 0], [1, 0.75, 1]);
  roof.push(dome);
  trim.push(place(paint(new THREE.CylinderGeometry(0.15, 0.3, 1.4, 8)), [0, 9.6, 0]));
  return { stone: merge(stone), trim: merge(trim), roof: merge(roof), crystal: place(crystal(1.6, 0.35, 6, 0.35, 5), [0, 10.1, 0]) };
}

/** The conservatory: a glass dome on an octagonal stone base, with white ribs and a crystal on top. */
function conservatory() {
  const R = 17;
  const frame: THREE.BufferGeometry[] = [], glass: THREE.BufferGeometry[] = [], stone: THREE.BufferGeometry[] = [];
  stone.push(place(paint(new THREE.CylinderGeometry(R + 2, R + 2.6, 2.2, 16)), [0, 0.2, 0]));
  stone.push(place(paint(new THREE.CylinderGeometry(R + 0.6, R + 0.6, 4.5, 32, 1, true)), [0, 3.5, 0]));
  const dome = paint(new THREE.SphereGeometry(R, 48, 18, 0, Math.PI * 2, 0, Math.PI / 2));
  place(dome, [0, 5.6, 0], [0, 0, 0], [1, 0.95, 1]);
  glass.push(dome);
  for (let k = 0; k < 16; k++) {
    const rib = paint(new THREE.TorusGeometry(R + 0.12, 0.16, 6, 40, Math.PI));
    place(rib, [0, 5.6, 0], [0, (k / 16) * Math.PI, 0], [1, 0.95, 1]);
    frame.push(rib);
  }
  for (const h of [0.35, 0.7, 0.92]) {
    const y = Math.sin(Math.asin(h)) * R * 0.95, rr = Math.cos(Math.asin(h)) * R;
    frame.push(place(paint(new THREE.TorusGeometry(rr + 0.1, 0.14, 6, 64)), [0, 5.6 + y, 0], [Math.PI / 2, 0, 0]));
  }
  frame.push(place(paint(new THREE.TorusGeometry(R + 0.3, 0.3, 8, 64)), [0, 5.7, 0], [Math.PI / 2, 0, 0]));
  return { frame: merge(frame), glass: merge(glass), stone: merge(stone), crystal: place(crystalCluster(9, 5, 1.4), [0, 5.6 + R * 0.95 - 0.3, 0]) };
}

/** A hot-air balloon: a striped lathe envelope, ropes, a wicker basket and a glowing burner. */
function balloon(colors: [string, string]) {
  const pts: THREE.Vector2[] = [];
  for (let i = 0; i <= 24; i++) {
    const t = i / 24, a = t * Math.PI;
    pts.push(new THREE.Vector2(Math.max(0.6, Math.sin(a) * 7.5 * (1 - 0.35 * t * t)), -Math.cos(a) * 8.5 + 1.5));
  }
  const env = paint(new THREE.LatheGeometry(pts, 24));
  // Stripes by angle around the axis.
  shade(env, (p, _n, c) => { const k = Math.floor(((Math.atan2(p.z, p.x) / (Math.PI * 2)) + 1) * 12) % 2; c.set(colors[k]!); });
  place(env, [0, 14, 0]);
  const basket = place(roundedBox(1.6, 1.1, 1.6, 0.12, 0.4), [0, 0, 0]);
  const ropes = [-1, 1].flatMap((sx) => [-1, 1].map((sz) => {
    const g = paint(new THREE.CylinderGeometry(0.03, 0.03, 4.2, 4));
    g.applyMatrix4(new THREE.Matrix4().makeRotationFromEuler(new THREE.Euler(sz * 0.12, 0, -sx * 0.12)));
    return place(g, [sx * 0.95, 3.1, sz * 0.95]);
  }));
  return { envelope: env, basket: merge([paint(basket, '#a37a4c'), ...ropes.map((g) => paint(g, '#5a4636'))]), burner: place(paint(new THREE.SphereGeometry(0.45, 12, 8)), [0, 3.0, 0]) };
}

// ---- Terrain -------------------------------------------------------------------------------------------------

/** The ground height (rebased: road at 0) everywhere: under the play field, in the islands, over the hills. */
function groundHeight(field: PlayField, isIsland: (x: number, z: number) => boolean, seed: number) {
  const noise = new Noise(seed);
  return (x: number, z: number) => {
    const d = field.at(x, z);
    if (d < 2.5) return -0.45;
    // Raised beds: up from under the hedge plinths to ~0.35 m, with soft mounds.
    let h = -0.45 + 0.8 * smoothstep(2.5, 7, d) + 0.45 * noise.fbm2(x / 11, z / 11, 3) * smoothstep(6, 14, d);
    if (!isIsland(x, z)) {
      // Meadows rising to rolling hills, then the mountains on the horizon.
      h += smoothstep(10, 160, d) * (10 + 16 * noise.fbm2(x / 150, z / 150, 4) + 6 * noise.fbm2(x / 45, z / 45, 2));
      h += smoothstep(420, 1300, d) * (160 + 190 * noise.fbm2(x / 520 + 3, z / 520, 4));
    }
    return h;
  };
}

function terrainMesh(field: PlayField, height: (x: number, z: number) => number, centre: XZ, seed: number) {
  const noise = new Noise(seed + 1);
  const color = (x: number, z: number, y: number, slope: number) => {
    const d = field.at(x, z);
    const c = new THREE.Color(GARDEN.meadow);
    const n = noise.fbm2(x / 23, z / 23, 3);
    c.lerp(new THREE.Color('#9dbb57'), smoothstep(-0.2, 0.7, n) * 0.55); // sun-dried patches
    c.lerp(new THREE.Color('#4b7a43'), smoothstep(0.15, 0.6, slope) * 0.6);
    // Far hills drift toward a cool green-grey, the mountains to violet rock.
    c.lerp(new THREE.Color('#7f8f8a'), smoothstep(380, 900, d) * 0.55);
    c.lerp(new THREE.Color('#8d7fb0'), smoothstep(80, 260, y) * 0.6);
    return c;
  };
  return terrain(centre[0], centre[1], 4200, 640, height, color, 2.1);
}

// ---- Export ------------------------------------------------------------------------------------------------------

export async function exportVioletGarden(inp: GardenInputs) {
  const { samples, footprints, groundY } = inp;
  const rand = rng(20261019);
  const outDir = path.join(inp.outRoot, 'violet');
  const world = new GlbFile('violet_garden.glb', UE_DIR);
  const props = new GlbFile('violet_garden_props.glb', UE_DIR);
  const instances: Instance[] = [];
  const lights: Light[] = [];
  /** Source-space instance from rebased (road = 0) coordinates. */
  const put = (prefab: string, x: number, y: number, z: number, yaw = 0, scale?: V3 | number, extra: Partial<Instance> = {}) => {
    const s = scale === undefined ? undefined : (typeof scale === 'number' ? [scale, scale, scale] as V3 : scale);
    instances.push({ prefab, x: r4(x), y: r4(y + groundY), z: r4(z), yaw: r4(yaw), ...(s ? { scale: s.map(r4) as V3 } : {}), ...extra });
  };

  // ---- Materials (hints for ATrackBuilder's masters).
  const stoneHint: MaterialHint = { shader: 'surface', detail: 'plastered_wall_02', triplanar: true, tileM: 2.2, albedo: 0.35, normal: 0.6, rough: 0.5, roughness: 0.82 };
  const trimHint: MaterialHint = { shader: 'surface', detail: 'painted_concrete', triplanar: true, tileM: 1.6, albedo: 0.15, normal: 0.35, rough: 0.4, roughness: 0.5 };
  const foliageHint = (tile = 1.4): MaterialHint => ({ shader: 'foliage', detail: 'leafy_grass', triplanar: true, tileM: tile, albedo: 0.55, normal: 0.9, rough: 0.3, roughness: 0.75 });
  const crystalHint: MaterialHint = { shader: 'crystal', roughness: 0.08, emissive: 1.1 };
  const glowHint: MaterialHint = { shader: 'vertexColor', roughness: 0.4, emissive: 9 };
  const ironHint: MaterialHint = { shader: 'vertexColor', roughness: 0.45, metalness: 0.5 };

  // ---- Field, islands, outline.
  const field = new PlayField(samples, footprints.dirtGrid);
  const loops = footprints.barriers.loops;
  const outerLoop = loops.reduce((a, l) => (Math.abs(loopArea(l)) > Math.abs(loopArea(a)) ? l : a));
  const isIsland = (x: number, z: number) => pointInLoop(x, z, outerLoop) && field.at(x, z) > 2;
  const centre: XZ = [153, -5];
  const centreline = new PointGrid(samples.map((s) => [s.x, s.z] as XZ), 16);
  const clear = new PointGrid(inp.keepClear as XZ[], 16);

  // ---- World meshes.
  const parts: MeshEntry[] = [];
  const terrainY = groundHeight(field, isIsland, 7);
  parts.push(world.add('garden_terrain', terrainMesh(field, terrainY, centre, 7), { shader: 'surface', detail: 'leafy_grass', tileM: 3.2, albedo: 0.6, normal: 0.7, rough: 0.4, roughness: 0.95 }));
  const hedge = hedges(loops, 11);
  parts.push(world.add('garden_hedges', hedge.body, foliageHint(1.2)));
  parts.push(world.add('garden_plinths', hedge.plinth, stoneHint));

  // Kerbs: the GLB's painted kerbs (Object_25) stay, recoloured violet and cream in violetMesh.mjs.

  // The start gate over the GLB's start structure and its brick feet (motifs 6, 7: 3.1 m squares).
  const feet = footprints.motifs.filter((m) => m.kind === 'bricks' && Math.abs(m.min[2] + 31.3) < 4 && m.max[1] < 5)
    .map((m) => ({ cx: (m.min[0] + m.max[0]) / 2, cz: (m.min[2] + m.max[2]) / 2, w: m.max[0] - m.min[0], d: m.max[2] - m.min[2] }))
    .sort((a, b) => a.cx - b.cx);
  if (feet.length === 2) {
    // The start banner (violetBanner.tsx) hangs at y 13.94 + rebase, 4.4 m tall, at z -30.82.
    const gate = startGate(feet, { y0: 13.94 - groundY - 2.2, y1: 13.94 - groundY + 2.2, z: -30.82, x0: feet[0]!.cx, x1: feet[1]!.cx });
    parts.push(world.add('gate_stone', gate.stone, stoneHint));
    parts.push(world.add('gate_trim', gate.trim, trimHint));
    parts.push(world.add('gate_crystals', shade(gate.crystals, (_p, _n, c) => c.set(GARDEN.crystal[0])), crystalHint));
    parts.push(world.add('gate_glow', paint(gate.glow, GARDEN.lantern), glowHint));
    for (const f of feet) lights.push({ x: f.cx, y: 8.2 + groundY, z: f.cz - 2.4, color: GARDEN.lantern, candela: 90, radius: 22 });
  }
  // The flagpoles by the second straight (Object_10's three poles): slim iron masts with gold tips.
  const poles = footprints.gantry.filter((g) => g.node === 'Object_10' && g.max[1] - g.min[1] > 15 && g.max[0] - g.min[0] < 1);
  const mast = merge(poles.flatMap((p) => {
    const cx = (p.min[0] + p.max[0]) / 2, cz = (p.min[2] + p.max[2]) / 2;
    return [
      place(paint(new THREE.CylinderGeometry(0.16, 0.24, 20.4, 12), '#2c2440'), [cx, 10.0, cz]),
      place(paint(new THREE.CylinderGeometry(0.45, 0.55, 0.6, 12), GARDEN.stone), [cx, 0.0, cz]),
      place(paint(new THREE.SphereGeometry(0.3, 12, 8), GARDEN.gold), [cx, 20.4, cz]),
    ];
  }));
  if (poles.length) parts.push(world.add('garden_masts', mast, ironHint));

  // ---- Prefabs.
  const prefab: Record<string, MeshEntry> = {};
  const pil = gardenPillar();
  prefab.pillar_stone = props.add('pillar_stone', pil.stone, stoneHint);
  prefab.pillar_trim = props.add('pillar_trim', pil.trim, ironHint);
  prefab.pillar_glow = props.add('pillar_glow', pil.glow, glowHint);
  for (let v = 0; v < 3; v++) prefab[`crystal_${v}`] = props.add(`crystal_${v}`, crystalCluster(101 + v * 17, 6 + v * 2, 1), crystalHint);
  prefab.crystal_spire = props.add('crystal_spire', crystalCluster(509, 11, 3.2), crystalHint);
  prefab.garden_trunk = props.add('garden_trunk', treeTrunk(), { shader: 'surface', detail: 'rock_face', triplanar: true, tileM: 1.2, albedo: 0.3, normal: 0.8, rough: 0.3, roughness: 0.9 });
  for (let v = 0; v < 2; v++) prefab[`blossom_${v}`] = props.add(`blossom_${v}`, blossomCrown(300 + v * 9), foliageHint(1.1));
  for (let v = 0; v < 2; v++) prefab[`crown_${v}`] = props.add(`crown_${v}`, roundCrown(400 + v * 9), foliageHint(1.3));
  prefab.lavender = props.add('lavender', lavenderBush(5), { ...foliageHint(0.8), albedo: 0.25, normal: 0.5 });
  prefab.flowers = props.add('flowers', flowerPatch(6), { ...foliageHint(0.8), albedo: 0.3, normal: 0.5 });
  const topi = topiary();
  prefab.topiary_leaf = props.add('topiary_leaf', shade(topi.leaf, (_p, _n, c) => c.multiplyScalar(1)), foliageHint(0.9));
  prefab.topiary_pot = props.add('topiary_pot', topi.pot, stoneHint);
  const gz = gazebo();
  prefab.gazebo_stone = props.add('gazebo_stone', paint(gz.stone, GARDEN.stone), stoneHint);
  prefab.gazebo_trim = props.add('gazebo_trim', paint(gz.trim, '#f7f2ea'), trimHint);
  prefab.gazebo_roof = props.add('gazebo_roof', paint(gz.roof, GARDEN.violet), { ...trimHint, roughness: 0.4 });
  prefab.gazebo_crystal = props.add('gazebo_crystal', paint(gz.crystal, GARDEN.crystal[1]), crystalHint);
  const cons = conservatory();
  prefab.conservatory_frame = props.add('conservatory_frame', paint(cons.frame, '#f4f1f7'), { shader: 'vertexColor', roughness: 0.3, metalness: 0.35 });
  prefab.conservatory_glass = props.add('conservatory_glass', paint(cons.glass, '#d9e8ff'), { shader: 'glass', roughness: 0.04, opacity: 0.22 });
  prefab.conservatory_stone = props.add('conservatory_stone', paint(cons.stone, GARDEN.stone), stoneHint);
  prefab.conservatory_crystal = props.add('conservatory_crystal', paint(cons.crystal, GARDEN.crystal[2]), crystalHint);
  const balloons: [string, string][] = [[GARDEN.violet, '#fff1f8'], ['#ff7aa8', '#ffd36b'], ['#58c6e8', '#f7f2ff'], ['#ffb04a', GARDEN.violet]];
  balloons.forEach((cols, v) => {
    const b = balloon(cols);
    prefab[`balloon_${v}`] = { ...props.add(`balloon_${v}`, b.envelope, { shader: 'vertexColor', roughness: 0.55 }), castShadow: false } as MeshEntry;
  });
  const b0 = balloon(balloons[0]!);
  const noShadow = (e: MeshEntry) => ({ ...e, castShadow: false });
  prefab.balloon_basket = noShadow(props.add('balloon_basket', b0.basket, { shader: 'vertexColor', roughness: 0.85 }));
  prefab.balloon_burner = noShadow(props.add('balloon_burner', paint(b0.burner, '#ffb46a'), { ...glowHint, emissive: 14 }));

  // ---- Placement.
  // Hedge pillars and their lanterns; the lanterns nearest the road get real lights.
  const lanternCandidates: { x: number; z: number; d: number }[] = [];
  for (const p of hedge.pillars) {
    put('pillar_stone', p.x, 0, p.z, p.yaw);
    put('pillar_trim', p.x, 0, p.z, p.yaw);
    put('pillar_glow', p.x, 0, p.z, p.yaw);
    lanternCandidates.push({ x: p.x, z: p.z, d: centreline.nearest(p.x, p.z, 120) });
  }
  lanternCandidates.sort((a, b) => a.d - b.d);
  const lit: { x: number; z: number }[] = [];
  for (const c of lanternCandidates) {
    if (lit.length >= MAX_LANTERN_LIGHTS) break;
    if (lit.some((l) => Math.hypot(l.x - c.x, l.z - c.z) < 30)) continue;
    lit.push(c);
    lights.push({ x: c.x, y: PILLAR.height + 0.4 + groundY, z: c.z, color: GARDEN.lantern, candela: 14, radius: 11 });
  }

  // Islands: flower beds, topiaries, crystals and blossom trees; a gazebo in the biggest island.
  const islandSpots: XZ[] = [];
  for (let x = -40; x < 350; x += 3.2) for (let z = -175; z < 170; z += 3.2) {
    const jx = x + (rand() - 0.5) * 2.6, jz = z + (rand() - 0.5) * 2.6;
    if (isIsland(jx, jz) && field.at(jx, jz) > 6.5) islandSpots.push([jx, jz]);
  }
  const groundAt = terrainY;
  // The gazebo: the island spot furthest from any road.
  const gazeboAt = islandSpots.reduce((a, s) => (field.at(s[0], s[1]) > field.at(a[0], a[1]) ? s : a), islandSpots[0] ?? [100, 0]);
  for (const name of ['gazebo_stone', 'gazebo_trim', 'gazebo_roof', 'gazebo_crystal']) put(name, gazeboAt[0], groundAt(...gazeboAt) - 0.2, gazeboAt[1], 0.4);
  lights.push({ x: gazeboAt[0], y: 4 + groundY, z: gazeboAt[1], color: '#ffd9a8', candela: 60, radius: 18 });
  islandSpots.forEach(([x, z], i) => {
    if (Math.hypot(x - gazeboAt[0], z - gazeboAt[1]) < 9) return;
    const d = field.at(x, z), y = groundAt(x, z) - 0.05;
    const roll = rand();
    if (d > 9 && roll < 0.1) {
      put('garden_trunk', x, y, z, rand() * 6, 0.9 + rand() * 0.4);
      put(`blossom_${i % 2}`, x, y + 3.9, z, rand() * 6, 0.85 + rand() * 0.35, { tint: GARDEN.blossom[i % 3] });
    } else if (roll < 0.2) {
      put(`crystal_${i % 3}`, x, y, z, rand() * 6, 0.7 + rand() * 0.9, { tint: GARDEN.crystal[i % 3] });
    } else if (roll < 0.32) {
      put('topiary_pot', x, y, z, rand() * 6, 1);
      put('topiary_leaf', x, y, z, rand() * 6, 1, { tint: GARDEN.foliage[i % 3] });
    } else if (roll < 0.75) {
      put('flowers', x, y, z, rand() * 6, 0.8 + rand() * 0.5);
    } else {
      put('lavender', x, y, z, rand() * 6, 0.9 + rand() * 0.4);
    }
  });

  // Beyond the outer hedge: lavender rows on the near meadows, then orchards of round and blossom trees,
  // crystal outcrops on the rises, all standing on the terrain's own height function.
  const outside = (x: number, z: number) => !pointInLoop(x, z, outerLoop) && field.at(x, z) > 9;
  // Lavender rows: parallel to the outer hedge's long sides, 2.4 m apart, a bush every 1.5 m.
  for (let row = 0; row < 6; row++) {
    const off = 12 + row * 2.4;
    for (let t = -40; t < 360; t += 1.5) {
      for (const [x, z] of [[t, -166.7 - off], [t, 157.4 + off]] as XZ[]) {
        if (outside(x, z) && rand() > 0.04) put('lavender', x + (rand() - 0.5) * 0.3, terrainY(x, z) - 0.08, z + (rand() - 0.5) * 0.3, rand() * 6, 0.85 + rand() * 0.35);
      }
    }
    for (let t = -160; t < 150; t += 1.5) {
      for (const [x, z] of [[-31.6 - off, t], [338.7 + off, t]] as XZ[]) {
        if (outside(x, z) && rand() > 0.04) put('lavender', x + (rand() - 0.5) * 0.3, terrainY(x, z) - 0.08, z + (rand() - 0.5) * 0.3, rand() * 6, 0.85 + rand() * 0.35);
      }
    }
  }
  // Trees and crystals on the hills: jittered grid, density falling off with distance.
  for (let x = -420; x < 740; x += 11) for (let z = -540; z < 540; z += 11) {
    const jx = x + (rand() - 0.5) * 9, jz = z + (rand() - 0.5) * 9;
    const d = field.at(jx, jz);
    if (!outside(jx, jz) || d < 36) continue;
    const keep = d < 180 ? 0.55 : d < 380 ? 0.25 : 0.08;
    if (rand() > keep) continue;
    const y = terrainY(jx, jz) - 0.1;
    const roll = rand();
    if (roll < 0.1) {
      put(`crystal_${Math.floor(rand() * 3)}`, jx, y, jz, rand() * 6, 1.4 + rand() * 1.8, { tint: GARDEN.crystal[Math.floor(rand() * 3)] });
    } else {
      const blossom = roll < 0.42;
      const s = 0.9 + rand() * 0.6;
      put('garden_trunk', jx, y, jz, rand() * 6, s);
      if (blossom) put(`blossom_${Math.floor(rand() * 2)}`, jx, y + 3.9 * s, jz, rand() * 6, s, { tint: GARDEN.blossom[Math.floor(rand() * 3)] });
      else put(`crown_${Math.floor(rand() * 2)}`, jx, y + 3.6 * s, jz, rand() * 6, s, { tint: GARDEN.foliage[Math.floor(rand() * 3)] });
    }
  }
  // Crystal spires on the far ridges (the skyline), a few big ones on near hilltops.
  for (let k = 0; k < 26; k++) {
    const a = (k / 26) * Math.PI * 2 + rand() * 0.2;
    const dist = k % 3 === 0 ? 260 + rand() * 120 : 900 + rand() * 700;
    const x = centre[0] + Math.cos(a) * dist, z = centre[1] + Math.sin(a) * dist * 0.9;
    const s = dist < 500 ? 1.1 + rand() * 0.6 : 6 + rand() * 7;
    put('crystal_spire', x, terrainY(x, z) - 2 * s, z, rand() * 6, s, { tint: GARDEN.crystal[k % 3] });
  }
  // The conservatory beyond the north notch, facing the track; balloons over the hills.
  const notch: XZ = [99, -215];
  for (const name of ['conservatory_stone', 'conservatory_frame', 'conservatory_glass', 'conservatory_crystal']) put(name, notch[0], terrainY(...notch) - 0.6, notch[1], 0);
  lights.push({ x: notch[0], y: 8 + terrainY(...notch) + groundY, z: notch[1], color: '#ffd7a0', candela: 160, radius: 30 });
  [[40, 120, -260], [260, 95, -230], [380, 140, 60], [-110, 110, 90]].forEach(([x, y, z], v) => {
    put(`balloon_${v}`, x!, y!, z!, v, 1.2);
    put('balloon_basket', x!, y!, z!, v, 1.2);
    put('balloon_burner', x!, y!, z!, v, 1.2);
  });
  // The motif footprints outside the hedge (pipes, brick stairs): crystal outcrops on stone plinths.
  footprints.motifs.forEach((m, i) => {
    if (m.min[1] - groundY > 6) return; // the floating rows keep their billboards (export.ts)
    if (Math.abs(m.min[2] + 31.3) < 4 && Math.abs((m.min[0] + m.max[0]) / 2) < 25) return; // the gate's feet
    const cx = (m.min[0] + m.max[0]) / 2, cz = (m.min[2] + m.max[2]) / 2;
    const w = Math.max(m.max[0] - m.min[0], m.max[2] - m.min[2]);
    put('crystal_spire', cx, -0.6, cz, i, Math.max(0.35, w / 12), { tint: GARDEN.crystal[i % 3] });
  });

  // Every sweep wall must stand on visible art: a hedge outline, the gate's towers, a mast or a motif's
  // crystals (within 0.6 m). Recorded in the JSON and asserted by check.mjs.
  const wallCoverage = (() => {
    const near = 0.6;
    const inBox = (x: number, z: number, min: V3, max: V3) => x > min[0] - near && x < max[0] + near && z > min[2] - near && z < max[2] + near;
    const nearLoop = (x: number, z: number, loop: readonly XZ[]) => pointInLoop(x, z, loop) || loop.some(([ax, az], k) => {
      const [bx, bz] = loop[(k + 1) % loop.length]!;
      const dx = bx - ax, dz = bz - az, t = Math.max(0, Math.min(1, ((x - ax) * dx + (z - az) * dz) / (dx * dx + dz * dz || 1)));
      return Math.hypot(ax + dx * t - x, az + dz * t - z) < near;
    });
    const w = footprints.sweepWalls;
    let uncovered = 0;
    for (let k = 0; k < w.length; k += 6) {
      const x = (w[k]! + w[k + 2]!) / 2, z = (w[k + 1]! + w[k + 3]!) / 2;
      const ok = footprints.gantry.some((g) => inBox(x, z, g.min, g.max)) || footprints.motifs.some((m) => inBox(x, z, m.min, m.max))
        || loops.some((l) => nearLoop(x, z, l));
      if (!ok) uncovered++;
    }
    return { segments: w.length / 6, uncovered };
  })();

  // Keep every dressing instance off gameplay spots (a belt-and-braces check: none should be near).
  const kept = instances.filter((s) => clear.nearest(s.x, s.z, 6) >= 5 || s.y - groundY > 20);

  await world.write(outDir);
  await props.write(outDir);
  return {
    meshes: [
      { file: 'violet_garden.glb', decoded: 'Tools/track-export/out/violet/violet_garden.glb', worldSpace: true, transformBaked: true, collision: false, nanite: true, wallCoverage, ueAssets: parts.map((e) => e.ueAsset), parts },
    ],
    prefabs: prefab,
    instances: kept,
    lights: lights.map((l) => ({ ...l, x: r4(l.x), y: r4(l.y), z: r4(l.z) })),
    /** UE-only art direction for the twilight look (TrackBuilder ApplyLook reads look.ue). */
    look: VIOLET_LOOK,
    stats: { hedgeLoops: loops.length, pillars: hedge.pillars.length, instances: kept.length, lights: lights.length, wallCoverage },
  };
}

/**
 * The Violet Gardens at twilight: a low pink-gold sun, a violet-to-peach sky, lavender haze; the lanterns
 * and crystals carry the scene. Keys are read by ATrackBuilder::ApplyLook (look.ue).
 */
export const VIOLET_LOOK = {
  sunDirection: [-0.6, 0.24, -0.76],
  sunColor: '#ffc59a',
  sunScale: 1.2,
  skyLightScale: 1.15,
  skyLightTint: '#d6d0ff',
  skyTop: '#2b3f9e',
  skyMid: '#7b78d8',
  skyHorizon: '#ffb9a4',
  skyBelow: '#9c86c6',
  sunDisc: '#fff0d6',
  glowPower: 10,
  sunGlow: 0.85,
  horizonGlow: 0.6,
  skyBrightness: 1.0,
  clouds: 0.55,
  cloudScale: 0.32,
  cloudLit: '#ffd9e6',
  cloudShade: '#8f86cf',
  fogColor: '#d8c4ec',
  fogDensity: 0.0014,
  fogFalloff: 0.01,
  fogStart: 120,
  fogMaxOpacity: 0.6,
  fogSunScatter: 0.3,
};
