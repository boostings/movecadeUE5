/**
 * Builds <trackId>.track.json for each shipped kart track (schema: Docs/Port/03 §4.3, schemaVersion 1).
 *
 * Every value is in source units: metres, radians, three.js axes (right-handed, Y up). Every `yaw` is a
 * three.js rotation.y; for a kart, yaw 0 faces -Z. Lateral offsets are positive on the driver's right
 * (side +1). The UE importer converts once (Docs/Port/03 §1):
 *   UE.X = -z*100, UE.Y = x*100, UE.Z = (y + rebaseY)*100, UE.Yaw = -yaw in degrees.
 *
 * Numbers come from the live movecade modules wherever they can be imported. Values that only exist
 * in modules Node can't load (legacy store.ts, React components) are copied below, with refs.
 */
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import * as cat from '@app/games/kart/tracks/catalog';
import { HARBOR_CONTROL_POINTS, HARBOR_ROAD_WIDTH, HARBOR_SECTORS, harborPoint } from '@app/games/kart/tracks/harbor';
import { HARBOR_KART_RADIUS, HARBOR_WALL_OFFSET } from '@app/games/kart/tracks/harborBoundary';
import { VIOLET_PROFILE, VIOLET_PROFILE_STEP } from '@app/games/kart/tracks/violetProfile';
import {
  curvatures,
  decodeTrackProfile,
  densifyLoop,
  planTrackside,
  sideNormal,
  type PathSample,
  type SideProfile,
  type TrackSample,
} from '@app/games/kart/scene/trackside';
import { TREE_BARK, VIOLET_TREES, foliageColorFor, placeTrees } from '@app/games/kart/scene/violetTrees';
import { kartTrackLook } from '@app/games/kart/scene/kartLook';
import { SPAWN_POSITIONS } from '@legacy/Experience/kart/engine/spawnGrid';

type TrackId = cat.KartTrackId;
type XZ = [number, number];

// ---- Copied constants (modules Node can't import) -------------------------------------------------

/** legacy/Experience/kart/engine/store.ts:328-345 (TRACK_CHECKPOINTS, CHECKPOINT_RADIUS, getStartingCheckpointIndex). */
const CHECKPOINT_ORDER = [
  ['finish', 'START_LINE'],
  ['turn_1', 'TURN_1_APEX'],
  ['outer_curve', 'OUTER_CURVE_APEX'],
  ['north_hairpin_1', 'NORTH_HAIRPIN_1'],
  ['crossover', 'CROSSOVER'],
  ['upper_loop', 'UPPER_LOOP_EAST'],
  ['north_hairpin_2', 'NORTH_HAIRPIN_2'],
  ['s_curve', 'S_CURVE'],
  ['final_curve', 'FINAL_CURVE'],
] as const;
const CHECKPOINT_RADIUS = 24;
const START_CHECKPOINT_INDEX = 1;

/** legacy store.ts:444-458 (RACE_CONFIG). */
const RACE_RULES = {
  laps: 3,
  countdownSeconds: 5,
  countdownDurationMs: 6000,
  maxRaceTimeMs: 300000,
  finishGraceMs: 30000,
  introDurationMs: 6000,
  introCameraHoldMs: 1200,
};

/** legacy MultiplayerPowerups.tsx:27-40, :95, :102, :152, :159-164. */
const ITEM_BOX_CONFIG = { size: 2, collectRadius: 2.5, respawnS: 15, spinRadPerS: 1.5, bobAmp: 0.1, bobRadPerS: 2.5, perPlayer: true };
/** legacy BoostPads.jsx:19-22. */
const BOOST_CONFIG = { durationS: 1.5, multiplier: 1.8, cooldownS: 2, triggerPaddingM: 1.5 };
/** legacy trackRecovery.ts:3-74. */
const RECOVERY = { maxDistanceM: 64, skipWaypoints: 3 };
/** legacy MultiplayerKart.tsx:131-132, :1109-1110. */
const SURFACES = {
  asphalt: { speedMultiplier: 1 },
  dirt: { speedMultiplier: 0.75, meshKeywords: ['dirt', 'sand', 'grass', 'offroad', 'mud', 'gravel'] },
};
/** meshBoundary.ts:27-36 and harborBoundary.ts (shared sweep). */
const SWEEP = { kartRadiusM: HARBOR_KART_RADIUS, substepM: 0.65, wallNormalY: 0.7 };

/** kart/tracks/HarborTrack.tsx:843-861 (ribbons), :917-926 (mesh names), :957-960 (sea). */
const HARBOR_RIBBONS = [
  { name: 'road', meshName: 'ground-harbor-road', inner: -HARBOR_ROAD_WIDTH / 2, outer: HARBOR_ROAD_WIDTH / 2, innerY: 0, outerY: 0, material: 'asphalt', collision: 'ground', surface: 'asphalt' },
  { name: 'leftEdge', inner: -13.7, outer: -13.3, innerY: 0.02, outerY: 0.02, material: 'edgeLine', collision: 'none' },
  { name: 'rightEdge', inner: 13.3, outer: 13.7, innerY: 0.02, outerY: 0.02, material: 'edgeLine', collision: 'none' },
  { name: 'shoulder', meshName: 'ground-dirt-shoulder', inner: -17, outer: 17, innerY: -0.04, outerY: -0.04, material: 'dirtShoulder', collision: 'ground', surface: 'dirt' },
  { name: 'leftBeach', inner: -40, outer: -17, innerY: -6.9, outerY: -0.06, material: 'sand', collision: 'none' },
  { name: 'rightBeach', inner: 17, outer: 40, innerY: -0.06, outerY: -6.9, material: 'sand', collision: 'none' },
  { name: 'leftSurf', inner: -43, outer: -39, innerY: -6.92, outerY: -6.92, material: 'surf', collision: 'none' },
  { name: 'rightSurf', inner: 39, outer: 43, innerY: -6.92, outerY: -6.92, material: 'surf', collision: 'none' },
  { name: 'leftShallows', inner: -52, outer: -43, innerY: -6.93, outerY: -6.93, material: 'shallows', collision: 'none' },
  { name: 'rightShallows', inner: 43, outer: 52, innerY: -6.93, outerY: -6.93, material: 'shallows', collision: 'none' },
  { name: 'leftCurb', meshName: 'ground-harbor-curb', inner: -15.5, outer: -14, innerY: 0.035, outerY: 0.035, material: 'curb', collision: 'ground', surface: 'asphalt', stripePeriodSamples: 4 },
  { name: 'rightCurb', meshName: 'ground-harbor-curb', inner: 14, outer: 15.5, innerY: 0.035, outerY: 0.035, material: 'curb', collision: 'ground', surface: 'asphalt', stripePeriodSamples: 4 },
  { name: 'leftWall', meshName: 'harbor-barrier', inner: -HARBOR_WALL_OFFSET, outer: -HARBOR_WALL_OFFSET, innerY: 0, outerY: 1.3, material: 'barrier', collision: 'wall' },
  { name: 'rightWall', meshName: 'harbor-barrier', inner: HARBOR_WALL_OFFSET, outer: HARBOR_WALL_OFFSET, innerY: 1.3, outerY: 0, material: 'barrier', collision: 'wall' },
  { name: 'leftCap', inner: -16.7, outer: -15.9, innerY: 1.3, outerY: 1.3, material: 'wallCap', collision: 'none' },
  { name: 'rightCap', inner: 15.9, outer: 16.7, innerY: 1.3, outerY: 1.3, material: 'wallCap', collision: 'none' },
];
/** HarborTrack.tsx `SAMPLES` (:16): rows per ribbon, uniform in spline t. */
const HARBOR_RIBBON_ROWS = 870;

/** Docs/Port/03 §3.2: Violet GLB nodes, measured from the GLB accessors. Object_22 is never rendered. */
const VIOLET_GLB_NODES = [
  { node: 'Object_10', role: 'mixed', surface: 'asphalt', note: 'start-line structure, gantry' },
  { node: 'Object_11', role: 'ground', surface: 'dirt', note: 'sand verge (rendered as "ground dirt")' },
  { node: 'Object_12', role: 'mixed', surface: 'asphalt', note: 'misc ground' },
  { node: 'Object_13', role: 'mixed', surface: 'asphalt', note: 'barrier blocks + gantry (main walls)' },
  { node: 'Object_18', role: 'mixed', surface: 'asphalt', note: 'hills, mountains, voxel trees' },
  { node: 'Object_24', role: 'ground', surface: 'asphalt', note: 'road asphalt' },
  { node: 'Object_25', role: 'ground', surface: 'asphalt', note: 'edge lines + chequers' },
  { node: 'Object_27', role: 'ground', surface: 'asphalt', note: 'painted speed quad, no gameplay effect' },
  { node: 'Object_47', role: 'mixed', surface: 'asphalt', note: 'rocks + cliffs' },
] as const;

const SOURCE_FILES = [
  'app/src/games/kart/tracks/catalog.ts',
  'app/src/games/kart/tracks/harbor.ts',
  'app/src/games/kart/tracks/harborBoundary.ts',
  'app/src/games/kart/tracks/violetLoop.ts',
  'app/src/games/kart/tracks/violetProfile.ts',
  'app/src/games/kart/scene/trackside.ts',
  'app/src/games/kart/scene/violetTrees.ts',
  'app/src/games/kart/scene/kartLook.ts',
  'app/src/legacy/Experience/kart/engine/spawnGrid.ts',
  'copied: app/src/legacy/Experience/kart/engine/store.ts (checkpoints, RACE_CONFIG)',
  'copied: app/src/games/kart/tracks/HarborTrack.tsx (ribbons)',
  'copied: legacy MultiplayerPowerups.tsx, BoostPads.jsx, trackRecovery.ts, MultiplayerKart.tsx (constants)',
];

// ---- Helpers ----------------------------------------------------------------------------------------

const r = (n: number, d = 4) => {
  const f = 10 ** d;
  const v = Math.round(n * f) / f;
  return Object.is(v, -0) ? 0 : v;
};
const rr = (a: readonly number[], d = 4) => a.map((n) => r(n, d));
const threeYaw = (tx: number, tz: number) => Math.atan2(-tx, -tz);

function closedLength(pts: readonly { x: number; z: number }[]) {
  let s = 0;
  for (let i = 0; i < pts.length; i++) {
    const a = pts[i]!;
    const b = pts[(i + 1) % pts.length]!;
    s += Math.hypot(b.x - a.x, b.z - a.z);
  }
  return s;
}

function sideOut(p: SideProfile & { curbIn?: number; curbOut?: number; shoulder?: number }) {
  const o: Record<string, number | null> = {
    edge: r(p.edge),
    wall: p.wall === null ? null : r(p.wall),
    wallTop: r(p.wallTop),
    back: p.back === null ? null : r(p.back),
    backY: r(p.backY),
  };
  if (p.curbIn !== undefined) o.curbIn = p.curbIn;
  if (p.curbOut !== undefined) o.curbOut = p.curbOut;
  if (p.shoulder !== undefined) o.shoulder = p.shoulder;
  return o;
}

function nearestSample<T extends { x: number; z: number }>(samples: readonly T[], x: number, z: number) {
  let best = 0;
  let bestD = Infinity;
  samples.forEach((s, i) => {
    const d = Math.hypot(s.x - x, s.z - z);
    if (d < bestD) { bestD = d; best = i; }
  });
  return best;
}

/** Reads the live catalog bindings; call after configureKartTrack(id). */
function catalogBlock(id: TrackId) {
  const entry = cat.KART_TRACKS.find((t) => t.id === id)!;
  const landmarks = cat.TRACK_LANDMARKS as Record<string, { x: number; z: number }>;
  return {
    entry,
    checkpoints: CHECKPOINT_ORDER.map(([cid, key], index) => {
      const p = landmarks[key]!;
      const cp: Record<string, unknown> = { index, id: cid, landmark: key, x: r(p.x), z: r(p.z), radius: CHECKPOINT_RADIUS };
      if (index === 0) cp.isFinish = true;
      if (id === 'sunset-harbor' && index > 0) cp.sectorName = HARBOR_SECTORS[index]!.name;
      return cp;
    }),
    navigation: {
      waypoints: cat.TRACK_PATH_POINTS.map((p) => (p.label ? { x: r(p.x), z: r(p.z), label: p.label } : { x: r(p.x), z: r(p.z) })),
      lookAhead: 2,
    },
    spawnGrid: SPAWN_POSITIONS.map((s, slot) => ({ slot, x: s.position[0], y: s.position[1], z: s.position[2], yaw: s.rotation })),
    itemBoxes: cat.DIRECT_POWERUP_POSITIONS.map(([x, y, z], i) => ({
      id: `${id}-box-${i}`,
      x: r(x), y: r(y), z: r(z),
      row: id === 'sunset-harbor' ? Math.floor(i / 3) : 0,
    })),
    boostPads: cat.TRACK_BOOST_PADS.map((p) => ({ x: r(p.position[0]!), z: r(p.position[1]!), yaw: r(p.rotation), width: p.width, length: p.length })),
    staticCameras: cat.STATIC_CAMERAS.map((c) => ({
      id: c.id,
      name: c.name,
      position: rr([c.position.x, c.position.y, c.position.z]),
      lookAt: rr([c.lookAt.x, c.lookAt.y, c.lookAt.z]),
    })),
    minimap: { bounds: { ...cat.MINIMAP_BOUNDS }, svgPath: cat.TRACK_SVG_PATH },
    groundY: cat.TRACK_GROUND_Y,
  };
}

function header(id: TrackId, commit: string, c: ReturnType<typeof catalogBlock>, lapLengthM: number, trackType: string) {
  return {
    schemaVersion: 1,
    coordinateSystem: 'three-rh-yup-m',
    source: { repo: 'movecade-v2', commit, exportedAt: new Date().toISOString(), files: SOURCE_FILES },
    track: {
      id,
      name: c.entry.name,
      description: c.entry.description,
      uiColor: c.entry.color,
      laps: RACE_RULES.laps,
      groundY: c.groundY,
      suggestedRebaseY: -c.groundY,
      closedLoop: true,
      lapLengthM: r(lapLengthM, 2),
      trackType,
      default: id === 'violet-loop',
    },
    raceRules: RACE_RULES,
  };
}

function common(id: TrackId, c: ReturnType<typeof catalogBlock>, startWidth: number) {
  return {
    navigation: c.navigation,
    checkpoints: c.checkpoints,
    startCheckpointIndex: START_CHECKPOINT_INDEX,
    startLine: { x: c.checkpoints[0]!.x, z: c.checkpoints[0]!.z, yaw: 0, width: r(startWidth, 2) },
    spawnGrid: c.spawnGrid,
    itemBoxes: c.itemBoxes,
    itemBoxConfig: ITEM_BOX_CONFIG,
    boostPads: c.boostPads,
    boostConfig: BOOST_CONFIG,
    staticCameras: c.staticCameras,
    minimap: c.minimap,
    recovery: RECOVERY,
    surfaces: SURFACES,
    look: kartTrackLook(id),
    lookNight: kartTrackLook(id, true),
  };
}

// ---- Sunset Harbor ----------------------------------------------------------------------------------

function exportHarbor(commit: string) {
  cat.configureKartTrack('sunset-harbor');
  const c = catalogBlock('sunset-harbor');

  const raw: (PathSample & { t: number; yaw: number })[] = [];
  for (let i = 0; i < HARBOR_RIBBON_ROWS; i++) {
    const t = i / HARBOR_RIBBON_ROWS;
    const p = harborPoint(t);
    // harborPoint's normal is n = (-tz, tx), so the unit tangent is (nz, -nx).
    raw.push({ x: p.x, z: p.z, tx: p.nz, tz: -p.nx, s: 0, t, yaw: p.yaw });
  }
  for (let i = 1; i < raw.length; i++) raw[i]!.s = raw[i - 1]!.s + Math.hypot(raw[i]!.x - raw[i - 1]!.x, raw[i]!.z - raw[i - 1]!.z);
  const fine = Array.from({ length: 8700 }, (_, i) => harborPoint(i / 8700));
  const lapLengthM = closedLength(fine);
  const curv = curvatures(raw, 3);
  const side = { edge: HARBOR_ROAD_WIDTH / 2, curbIn: 14, curbOut: 15.5, wall: HARBOR_WALL_OFFSET, wallTop: 1.3, shoulder: 17, back: 17, backY: 0 };

  const wall = (s: 1 | -1) => ({
    side: s,
    height: 1.3,
    closed: true,
    polyline: Array.from({ length: HARBOR_RIBBON_ROWS }, (_, i) => {
      const p = harborPoint(i / HARBOR_RIBBON_ROWS, s * HARBOR_WALL_OFFSET);
      return [r(p.x), r(p.z)] as XZ;
    }),
  });

  return {
    ...header('sunset-harbor', commit, c, lapLengthM, 'analytic-spline'),
    spline: {
      kind: 'catmull-rom-uniform-closed',
      controlPoints: HARBOR_CONTROL_POINTS.map(([x, z]) => [x, z] as XZ),
    },
    samples: {
      spacingM: r(lapLengthM / HARBOR_RIBBON_ROWS),
      uniformIn: 't',
      points: raw.map((p, i) => ({
        i,
        s: r(p.s, 3),
        t: r(p.t, 10),
        x: r(p.x), y: 0, z: r(p.z),
        tx: r(p.tx, 6), tz: r(p.tz, 6), yaw: r(p.yaw, 6),
        curvature: r(curv[i]!, 6),
        bankDeg: 0, grade: 0,
        surface: 'asphalt',
        right: sideOut({ ...side }),
        left: sideOut({ ...side }),
      })),
    },
    ...common('sunset-harbor', c, HARBOR_ROAD_WIDTH),
    collision: {
      ...SWEEP,
      walls: [wall(1), wall(-1)],
    },
    ribbons: HARBOR_RIBBONS,
    meshes: [],
    scenery: {
      instances: [
        { prefab: 'sea_plane', x: 100, y: -7, z: -40, yaw: 0, scale: [5000, 1, 5000], tint: '#5fb3b3' },
      ],
      animated: [],
      signs: [],
    },
    textures: [],
    exportNotes: [
      'Phase 1b: static props from HarborTrack.tsx#buildHarborScenery (~35 colour batches), the coral tunnel vault and ribs, start banner and signs, the 8 animated actors (HarborMotion.tsx) and the canvas textures are not exported yet.',
      'Positions authored in spline t (sectors, items, pads) are not arc length; use the exported xyz or the spline input key t*29.',
    ],
  };
}

// ---- Violet Loop ------------------------------------------------------------------------------------

function exportViolet(commit: string) {
  cat.configureKartTrack('violet-loop');
  const c = catalogBlock('violet-loop');

  const dense = densifyLoop(cat.TRACK_PATH_POINTS, VIOLET_PROFILE_STEP);
  const samples: TrackSample[] = decodeTrackProfile(dense, VIOLET_PROFILE);
  if (samples.length !== dense.length) {
    throw new Error(`Violet profile has ${samples.length} samples but the centreline has ${dense.length}; the bake is stale.`);
  }
  const last = samples[samples.length - 1]!;
  const lapLengthM = last.s + Math.hypot(samples[0]!.x - last.x, samples[0]!.z - last.z);
  const curv = curvatures(samples, 3);
  const start = nearestSample(samples, c.checkpoints[0]!.x as number, c.checkpoints[0]!.z as number);
  const startWidth = samples[start]!.sides[1].edge + samples[start]!.sides[-1].edge;

  // Fallback wall polylines from the baked profile, split wherever a side has no wall.
  const walls: { side: 1 | -1; height: number; closed: boolean; polyline: XZ[]; tops: number[] }[] = [];
  for (const s of [1, -1] as const) {
    type Run = { polyline: XZ[]; tops: number[]; startsAt0: boolean };
    const runs: Run[] = [];
    let cur: Run | null = null;
    samples.forEach((p, i) => {
      const prof = p.sides[s];
      if (prof.wall === null) { cur = null; return; }
      const n = sideNormal(p, s);
      if (!cur) { cur = { polyline: [], tops: [], startsAt0: i === 0 }; runs.push(cur); }
      cur.polyline.push([r(p.x + n.x * prof.wall), r(p.z + n.z * prof.wall)]);
      cur.tops.push(r(prof.wallTop));
    });
    // Join a run that wraps across sample 0 so a continuous wall stays one polyline.
    const full = runs.length === 1 && runs[0]!.polyline.length === samples.length;
    if (!full && runs.length > 1 && runs[0]!.startsAt0 && samples[samples.length - 1]!.sides[s].wall !== null) {
      const head = runs.shift()!;
      const tail = runs[runs.length - 1]!;
      tail.polyline.push(...head.polyline);
      tail.tops.push(...head.tops);
    }
    for (const run2 of runs) walls.push({ side: s, height: Math.max(...run2.tops), closed: full, polyline: run2.polyline, tops: run2.tops });
  }

  const plan = planTrackside(samples);
  const place = (prefab: string, p: { x: number; y: number; z: number; yaw: number; side: 1 | -1 }, extra: Record<string, unknown> = {}) => ({
    prefab, x: r(p.x), y: r(p.y), z: r(p.z), yaw: r(p.yaw, 6), side: p.side, ...extra,
  });
  const trees = placeTrees(VIOLET_TREES).flatMap((t) => [
    { prefab: 'tree_trunk', x: r(t.trunk.x), y: r(t.trunk.y), z: r(t.trunk.z), yaw: 0, scale: rr([t.trunk.radius, t.trunk.height, t.trunk.radius]), tint: TREE_BARK },
    { prefab: 'tree_canopy', x: r(t.canopy.x), y: r(t.canopy.y), z: r(t.canopy.z), yaw: r(t.canopy.yaw, 6), scale: rr([t.canopy.width, t.canopy.height, t.canopy.width]), tint: foliageColorFor(t.tint) },
  ]);

  return {
    ...header('violet-loop', commit, c, lapLengthM, 'imported-mesh'),
    spline: {
      kind: 'catmull-rom-uniform-closed',
      controlPoints: cat.TRACK_PATH_POINTS.map((p) => [p.x, p.z] as XZ),
      labels: cat.TRACK_PATH_POINTS.map((p) => p.label ?? ''),
    },
    samples: {
      spacingM: VIOLET_PROFILE_STEP,
      uniformIn: 's',
      points: samples.map((p, i) => ({
        i,
        s: r(p.s, 3),
        t: r(p.s / lapLengthM, 6),
        x: r(p.x), y: r(p.y), z: r(p.z),
        tx: r(p.tx, 6), tz: r(p.tz, 6), yaw: r(threeYaw(p.tx, p.tz), 6),
        curvature: r(curv[i]!, 6),
        bankDeg: 0, grade: 0,
        surface: 'asphalt',
        right: sideOut(p.sides[1]),
        left: sideOut(p.sides[-1]),
      })),
    },
    ...common('violet-loop', c, startWidth),
    collision: {
      ...SWEEP,
      walls,
      wallsAreFallback: true,
      meshColliders: VIOLET_GLB_NODES.map((n) => ({ file: 'violet-loop.glb', ...n })),
    },
    meshes: [
      {
        file: 'violet-loop.glb',
        sourcePath: 'app/public/models/violet-loop.glb',
        nodes: VIOLET_GLB_NODES.map((n) => n.node),
        transform: { position: [155, -28, 15], scale: 0.08 },
        collision: true,
        nanite: true,
        encoding: ['KHR_draco_mesh_compression', 'EXT_texture_webp'],
      },
    ],
    scenery: {
      instances: [
        ...plan.stands.map((p) => place('grandstand', p)),
        ...plan.lamps.flatMap((p) => [place('lamp_post', p), place('lamp_head', p)]),
        ...plan.banners.map((p, i) => place('banner_board', p, { variant: i % 4 })),
        ...trees,
      ],
      kerbRuns: plan.kerbs.map((k) => ({ side: k.side, fromSample: k.from, toSample: k.to })),
      animated: [],
      signs: [],
    },
    textures: [],
    exportNotes: [
      'Collision comes from the GLB meshes (any triangle with |normal.y| < 0.7 is a wall). collision.walls is a fallback traced from the baked profile.',
      'Phase 1b: GLB conversion (decode Draco + WebP, bake the mount transform), prefab meshes (kerbs, grandstand, lamp, banner board, trees), banner and sign textures are not exported yet.',
      'Licence of violet-loop.glb is unverified (Docs/Port/00-PLAN.md, open risks).',
    ],
  };
}

export async function exportTracks(opts: { outDir: string; commit: string; movecadeRoot: string }) {
  mkdirSync(opts.outDir, { recursive: true });
  const out: string[] = [];
  for (const data of [exportHarbor(opts.commit), exportViolet(opts.commit)]) {
    const file = path.join(opts.outDir, `${data.track.id}.track.json`);
    writeFileSync(file, JSON.stringify(data, null, 1) + '\n');
    out.push(file);
  }
  cat.configureKartTrack('violet-loop');
  return out;
}
