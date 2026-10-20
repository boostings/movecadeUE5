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
import { kartTrackLook } from '@app/games/kart/scene/kartLook';
import { SPAWN_POSITIONS } from '@legacy/Experience/kart/engine/spawnGrid';
import { exportHarborScenery, exportVioletScenery, type MaterialHint } from './scenery';
import { exportMovecadeProps } from './props';
import { exportVioletGarden, type VioletFootprints } from './violetGarden';
import { HARBOR_LOOK, HARBOR_SEA_MATERIAL, HARBOR_SEA_Y } from './harborCoast';

type TrackId = cat.KartTrackId;
type XZ = [number, number];

// ---- Copied constants (modules Node can't import) -------------------------------------------------

/** legacy/Experience/kart/engine/store.ts:328-345 (TRACK_CHECKPOINTS, CHECKPOINT_RADIUS, getStartingCheckpointIndex). */
export const CHECKPOINT_ORDER = [
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
export const CHECKPOINT_RADIUS = 24;
export const START_CHECKPOINT_INDEX = 1;

/** legacy store.ts:444-458 (RACE_CONFIG). */
export const RACE_RULES = {
  laps: 3,
  countdownSeconds: 5,
  countdownDurationMs: 6000,
  maxRaceTimeMs: 300000,
  finishGraceMs: 30000,
  introDurationMs: 6000,
  introCameraHoldMs: 1200,
};

/** legacy MultiplayerPowerups.tsx:27-40, :95, :102, :152, :159-164. */
export const ITEM_BOX_CONFIG = { size: 2, collectRadius: 2.5, respawnS: 15, spinRadPerS: 1.5, bobAmp: 0.1, bobRadPerS: 2.5, perPlayer: true };
/** legacy BoostPads.jsx:19-22. */
export const BOOST_CONFIG = { durationS: 1.5, multiplier: 1.8, cooldownS: 2, triggerPaddingM: 1.5 };
/** The boost pad's look (BoostPads.jsx:40-180 as an emissive material): props.ts mc_boost_pad, scrolled
 *  forward and pulsed (emissive 0.35+ on the web; brighter here so it blooms), with a yellow rim. */
export const BOOST_PAD_LOOK = { texture: 'mc_boost_pad', emissive: 2.2, pulse: 0.6, pulseRadPerS: 6, scrollPerS: 1.6, rim: '#ffcc00' };
/** legacy trackRecovery.ts:3-74. */
export const RECOVERY = { maxDistanceM: 64, skipWaypoints: 3 };
/** legacy MultiplayerKart.tsx:131-132, :1109-1110. */
export const SURFACES = {
  asphalt: { speedMultiplier: 1 },
  dirt: { speedMultiplier: 0.75, meshKeywords: ['dirt', 'sand', 'grass', 'offroad', 'mud', 'gravel'] },
};
/** meshBoundary.ts:27-36 and harborBoundary.ts (shared sweep). */
export const SWEEP = { kartRadiusM: HARBOR_KART_RADIUS, substepM: 0.65, wallNormalY: 0.7 };

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
  'app/src/games/kart/scene/tracksideGeometry.ts',
  'app/src/games/kart/tracks/VioletTrackside.tsx (banner faces), violetBanner.tsx',
  'app/src/games/kart/tracks/HarborTrack.tsx (harborRibbon, buildHarborScenery), harborGeometry.ts, harborTextures.ts',
  'mirrored: app/src/games/kart/tracks/HarborMotion.tsx (animated parts), HarborTrack.tsx start sign',
  'app/public/models/violet-loop.glb (decoded)',
  'copied: app/src/legacy/Experience/kart/engine/store.ts (checkpoints, RACE_CONFIG)',
  'copied: app/src/games/kart/tracks/HarborTrack.tsx (ribbons)',
  'copied: legacy MultiplayerPowerups.tsx, BoostPads.jsx, trackRecovery.ts, MultiplayerKart.tsx (constants)',
];

// ---- Helpers ----------------------------------------------------------------------------------------

export const r = (n: number, d = 4) => {
  const f = 10 ** d;
  const v = Math.round(n * f) / f;
  return Object.is(v, -0) ? 0 : v;
};
export const rr = (a: readonly number[], d = 4) => a.map((n) => r(n, d));
const threeYaw = (tx: number, tz: number) => Math.atan2(-tx, -tz);

export function closedLength(pts: readonly { x: number; z: number }[]) {
  let s = 0;
  for (let i = 0; i < pts.length; i++) {
    const a = pts[i]!;
    const b = pts[(i + 1) % pts.length]!;
    s += Math.hypot(b.x - a.x, b.z - a.z);
  }
  return s;
}

export function sideOut(p: SideProfile & { curbIn?: number; curbOut?: number; shoulder?: number }) {
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

async function exportHarbor(commit: string, assetsDir?: string, movecadeRoot?: string) {
  cat.configureKartTrack('sunset-harbor');
  const art = assetsDir ? await exportHarborScenery(assetsDir, HARBOR_RIBBONS) : undefined;
  const props = assetsDir && movecadeRoot ? await exportMovecadeProps(assetsDir, movecadeRoot) : undefined;
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
    // The web's look record, plus the UE sunset art direction (harborCoast.ts).
    look: { ...kartTrackLook('sunset-harbor'), ue: HARBOR_LOOK },
    collision: {
      ...SWEEP,
      walls: [wall(1), wall(-1)],
    },
    ribbons: HARBOR_RIBBONS,
    meshes: art?.meshes ?? [],
    scenery: {
      instances: [
        // HarborTrack.tsx:957-960's sea, a hair lower so the beach meets it at offset 40.
        { prefab: 'sea_plane', x: 100, y: HARBOR_SEA_Y, z: -40, yaw: 0, scale: [5000, 1, 5000], tint: '#5fb3b3' },
        ...(art?.coastInstances ?? []),
      ],
      ...(art ? { motionParts: art.motionParts, sea: { ...art.sea, material: HARBOR_SEA_MATERIAL } } : {}),
      animated: art?.animated ?? [],
      // The shared Movecade props (the boost pad uses props.boost_pad) and the coast's palms and grass.
      ...(props ? { prefabs: { ...props.prefabs, ...(art?.coastPrefabs ?? {}) } } : {}),
      signs: art?.signs ?? [],
    },
    textures: [...(art?.textures ?? []), ...(props?.textures ?? [])],
    detailSurfaces: props?.surfaces ?? {},
    boostPadLook: BOOST_PAD_LOOK,
    exportNotes: [
      art
        ? 'Scenery binaries (ribbons with their UVs and tones, ~35 prop colour batches incl. the coral tunnel, the HarborMotion parts, canvas textures) are in Tools/track-export/out/harbor; meshes[].parts and scenery.motionParts give each UE asset and its material.'
        : 'Scenery binaries were not exported (run npm run export).',
      'Positions authored in spline t (sectors, items, pads) are not arc length; use the exported xyz or the spline input key t*29.',
    ],
  };
}

// ---- Violet Loop ------------------------------------------------------------------------------------

/** A group of Mario-style motif geometry cut from the visual GLB (violetMesh.mjs), three.js world metres. */
export interface VioletMotif { kind: 'pipe' | 'bricks'; node: string; tris: number; min: [number, number, number]; max: [number, number, number] }

/** What src/violetMesh.mjs measures off the decoded GLB (see that file for the rules). */
export interface VioletMeshData {
  glb: string;
  stats: Record<string, unknown>;
  motifs: VioletMotif[];
  walls: { sweep: number[]; probe: number[]; sweepPieces: number; probePieces: number };
  dirtGrid: { cellM: number; minX: number; minZ: number; cols: number; rows: number; dirtCells: number; runs: number[] };
  /** Object_13's ground blocks as closed outlines, Object_10's pieces: the rebuild's footprints. */
  barriers: VioletFootprints['barriers'];
  gantry: VioletFootprints['gantry'];
}
export type VioletMeshHook = (samples: readonly { x: number; z: number }[], rebaseY: number) => Promise<VioletMeshData>;

/** Where the editor import script (Tools/ue/import_track_assets.py) puts the Violet GLB's meshes. */
const VIOLET_UE_DIR = '/Game/Tracks/VioletLoop';

/** The rules violetMesh.mjs applies, recorded next to the data (Docs/Port/01 §5, §7.2, §7.5). */
const VIOLET_WALL_RULES = {
  wall: 'a GLB triangle (any rendered mesh) with |normal.y| < 0.7, centroid within 80 m of the centreline',
  sweep:
    'constrainMeshMovement tests a 1.15 m sphere at the logical plane y=0, so a wall triangle counts when it reaches into y -1.15..1.15; ' +
    'its segment is the triangle sliced at the height in that range nearest y=0 (the closest point, flattened). Lower faces (the sand verge lip, kerb edges) never collide on the web either.',
  probe: 'checkHorizontalCollisionFast rays run at y=0.5, so probe segments are the wall triangles sliced at y=0.5; materials are double-sided, so both faces hit',
  normal: '[nx, nz] = the triangle geometric normal (as three.js reports face.normal), x and z of the unit 3D vector, not renormalised',
  merge: 'collinear touching pieces facing the same way are joined; the union of lines is unchanged',
  format: 'segments: flat [ax, az, bx, bz, nx, nz, ...] in three.js world metres, no rebase',
};

/** GLB nodes left out of the visual mesh (violetMesh.mjs MOTIF_NODES, REBUILT_NODES). Their faces stay in
 *  collision.walls; violetGarden.ts builds original art over the same footprints (hedges on the barrier
 *  blocks, the start gate on the gantry and its feet, terrain for the hills, crystals for the motifs). */
const VIOLET_CUT_NODES = ['Object_10', 'Object_12', 'Object_13', 'Object_18', 'Object_27', 'Object_47'];

/** World-space CC0 detail per Violet node over its GLB palette (trackSurfaces.ts:88-153 dressVioletMaterials). */
const VIOLET_PALETTE = (material: string) => `${VIOLET_UE_DIR}/violet_loop/Textures/${material}_basecolor`;
const VIOLET_SURFACES: Record<string, MaterialHint> = {
  Object_24: { shader: 'surface', textureAsset: VIOLET_PALETTE('material_18'), detail: 'asphalt_track', tileM: 4.5, albedo: 0.8, normal: 0.6, rough: 0.7, roughness: 0.8, desaturate: 0.8, baseDesaturate: 0.55 },
  Object_25: { shader: 'surface', textureAsset: `${VIOLET_UE_DIR}/violet_loop/Textures/material_19_violet`, detail: 'asphalt_track', tileM: 4.5, albedo: 0.28, normal: 0.4, rough: 0.5, roughness: 0.6 },
  // The verge (the sim's dirt) as a clipped lawn: offroad reads as grass, like the islands' beds but flat.
  Object_11: { shader: 'surface', color: '#5d8a3c', detail: 'leafy_grass', tileM: 2.6, albedo: 0.8, normal: 0.85, rough: 0.4, roughness: 0.95, desaturate: 0.35 },
};

/** Billboards for the brick rows that floated over the perimeter (12-15 m up, no collision): the
 *  violetGarden pass dresses every other motif footprint. */
function violetMotifBillboards(motifs: readonly VioletMotif[], samples: readonly { x: number; z: number }[], roadY: number) {
  const out: Record<string, unknown>[] = [];
  let billboard = 0;
  motifs.forEach((m, i) => {
    if (m.kind !== 'bricks' || m.min[1] - roadY <= 6) return;
    const cx = (m.min[0] + m.max[0]) / 2, cz = (m.min[2] + m.max[2]) / 2;
    const sx = m.max[0] - m.min[0], sz = m.max[2] - m.min[2];
    const near = samples[nearestSample(samples, cx, cz)]!;
    const away = { x: cx - near.x, z: cz - near.z };
    // The rows run along the perimeter, so the faces square up to it, toward the road side.
    const alongX = sx >= sz;
    const face = alongX ? (away.z < 0 ? 0 : Math.PI) : (away.x < 0 ? Math.PI / 2 : -Math.PI / 2);
    const count = Math.max(1, Math.round(Math.max(sx, sz) / 13));
    for (let k = 0; k < count; k++) {
      const t = (k - (count - 1) / 2) * 13;
      const x = alongX ? cx + t : cx, z = alongX ? cz : cz + t;
      const v = billboard++ % 4;
      const at = { x: r(x), y: r(roadY), z: r(z), yaw: r(face, 6), replaces: `${m.node} ${m.kind} ${i}` };
      out.push({ prefab: 'billboard_frame', ...at }, { prefab: 'billboard_face', ...at, variant: v });
    }
  });
  return out;
}

/** The masts by the second straight (Object_10's flagpoles, rebuilt as garden masts), dressed with pennants. */
const VIOLET_FLAGS: Array<[x: number, yTop: number, z: number, prefab: string]> = [
  [157.49, 18.6, 13.8, 'flag_violet'], [168.98, 18.6, 13.8, 'flag_pink'], [180.47, 18.6, 13.8, 'flag_cyan'],
];

async function exportViolet(commit: string, violetMesh?: VioletMeshHook, assetsDir?: string, movecadeRoot?: string) {
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

  const mesh = violetMesh ? await violetMesh(samples, -c.groundY) : undefined;

  const plan = planTrackside(samples);
  const art = assetsDir ? await exportVioletScenery(assetsDir) : undefined;
  const props = assetsDir && movecadeRoot ? await exportMovecadeProps(assetsDir, movecadeRoot) : undefined;
  const garden = assetsDir && mesh ? await exportVioletGarden({
    outRoot: assetsDir,
    samples,
    kerbs: plan.kerbs,
    footprints: { barriers: mesh.barriers, gantry: mesh.gantry, motifs: mesh.motifs, dirtGrid: dirtGrid(mesh) as unknown as VioletFootprints['dirtGrid'], sweepWalls: mesh.walls.sweep },
    groundY: c.groundY,
    keepClear: [...c.itemBoxes, ...c.boostPads, ...c.spawnGrid].map((p) => [p.x as number, p.z as number] as XZ),
  }) : undefined;
  if (garden) console.log(`violet garden: ${JSON.stringify(garden.stats)}`);
  const billboards = mesh ? violetMotifBillboards(mesh.motifs, samples, c.groundY) : [];
  const flags = VIOLET_FLAGS.map(([x, y, z, prefab]) => ({ prefab, x: r(x + 0.25), y, z, yaw: 0 }));
  const visualNodes = VIOLET_GLB_NODES.filter((n) => !VIOLET_CUT_NODES.includes(n.node));
  const place = (prefab: string, p: { x: number; y: number; z: number; yaw: number; side: 1 | -1 }, extra: Record<string, unknown> = {}) => ({
    prefab, x: r(p.x), y: r(p.y), z: r(p.z), yaw: r(p.yaw, 6), side: p.side, ...extra,
  });
  const web = common('violet-loop', c, startWidth);

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
    ...web,
    // The web's look record, plus the UE art direction for the rebuilt garden (twilight).
    look: { ...web.look, ...(garden ? { ue: garden.look } : {}) },
    collision: {
      ...SWEEP,
      // The GLB's own walls when the mesh pass ran (the sim's sweep and probes), else the profile fallback.
      walls: mesh ? meshWalls(mesh) : walls,
      wallsAreFallback: !mesh,
      profileWalls: walls,
      ...(mesh ? { wallRules: VIOLET_WALL_RULES, dirtGrid: dirtGrid(mesh) } : {}),
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
        // The decoded copy UE imports: mount and rebase baked into the vertices, so it sits at the origin.
        decoded: 'Tools/track-export/out/violet/violet_loop.glb',
        transformBaked: true,
        bakedRebaseY: -c.groundY,
        ueAssetDir: VIOLET_UE_DIR,
        // Interchange puts a GLB's meshes in <dir>/<file>/StaticMeshes; they keep the GLB's own materials.
        // The IP cleanup cuts VIOLET_CUT_NODES (and the voxel clouds) from the visual copy; collision keeps them.
        ueAssets: visualNodes.map((n) => `${VIOLET_UE_DIR}/violet_loop/StaticMeshes/${n.node}`),
        parts: visualNodes.filter((n) => VIOLET_SURFACES[n.node]).map((n) => ({
          node: n.node, ueAsset: `${VIOLET_UE_DIR}/violet_loop/StaticMeshes/${n.node}`, material: VIOLET_SURFACES[n.node],
        })),
        ipCleanup: {
          cutNodes: VIOLET_CUT_NODES,
          rebuild: 'violetGarden.ts: hedges on the barrier blocks, a stone gate on the start structure, terrain for the hills, crystals for the motifs',
          motifs: mesh?.motifs ?? [],
        },
      },
      ...(garden?.meshes ?? []),
    ],
    scenery: {
      instances: [
        ...plan.stands.map((p) => place('grandstand', p)),
        // The web hangs its banners on top of the 3.4 m barrier blocks; the garden hangs them on the hedge
        // faces (0.5-1.7 m), a hand in front of the wall line, so nothing floats.
        ...plan.banners.map((p, i) => place('banner_board', { ...p, y: c.groundY + 0.45, x: p.x + Math.sin(p.yaw) * 0.08, z: p.z + Math.cos(p.yaw) * 0.08 }, { variant: i % 4 })),
        ...billboards,
        ...flags,
        ...(garden?.instances ?? []),
      ],
      kerbRuns: plan.kerbs.map((k) => ({ side: k.side, fromSample: k.from, toSample: k.to })),
      ...(art ? { prefabs: { ...art.prefabs, ...(props?.prefabs ?? {}), ...(garden?.prefabs ?? {}) } } : {}),
      lights: garden?.lights ?? [],
      animated: [],
      signs: art?.signs ?? [],
    },
    textures: [...(art?.textures ?? []), ...(props?.textures ?? [])],
    detailSurfaces: props?.surfaces ?? {},
    boostPadLook: BOOST_PAD_LOOK,
    exportNotes: [
      mesh
        ? 'collision.walls holds the GLB wall faces as the web sweep (role "sweep") and probe rays (role "probe") meet them (collision.wallRules); collision.dirtGrid marks where the first floor hit under the kart is Object_11 "ground dirt". collision.profileWalls is the older profile trace, for reference.'
        : 'collision.walls is a fallback traced from the baked profile (the GLB pass did not run).',
      'Licence of violet-loop.glb is unverified (Docs/Port/00-PLAN.md, open risks).',
    ],
  };
}

function meshWalls(mesh: VioletMeshData) {
  const tuples = (flat: number[]) => Array.from({ length: flat.length / 6 }, (_, i) => flat.slice(i * 6, i * 6 + 6));
  return [
    { side: 0, role: 'sweep', source: 'violet-loop.glb', pieces: mesh.walls.sweepPieces, segments: tuples(mesh.walls.sweep) },
    { side: 0, role: 'probe', source: 'violet-loop.glb', pieces: mesh.walls.probePieces, segments: tuples(mesh.walls.probe) },
  ];
}

function dirtGrid(mesh: VioletMeshData) {
  const g = mesh.dirtGrid;
  return {
    rule: 'cell centre down-ray from y=1, far 3: the first hit with |normal.y| >= 0.7 is dirt if its mesh is Object_11 "ground dirt" (MultiplayerKart.tsx:1588-1610)',
    cellM: g.cellM, minX: g.minX, minZ: g.minZ, cols: g.cols, rows: g.rows, dirtCells: g.dirtCells,
    runsFormat: '[row, firstCol, lastCol]; cell (col,row) covers x minX+col*cellM.., z minZ+row*cellM..',
    runs: Array.from({ length: g.runs.length / 3 }, (_, i) => g.runs.slice(i * 3, i * 3 + 3)),
  };
}

/** Pretty JSON with every leaf array of numbers on one line (points, segments, runs). */
export function stringify(data: unknown) {
  return JSON.stringify(data, null, 1).replace(/\[\s*(?:-?[\d.eE+-]+|null)(?:\s*,\s*(?:-?[\d.eE+-]+|null))*\s*\]/g, (m) => m.replace(/\s+/g, ''));
}

export async function exportTracks(opts: { outDir: string; commit: string; movecadeRoot: string; violetMesh?: VioletMeshHook; assetsDir?: string }) {
  mkdirSync(opts.outDir, { recursive: true });
  const out: string[] = [];
  for (const data of [
    await exportHarbor(opts.commit, opts.assetsDir, opts.movecadeRoot),
    await exportViolet(opts.commit, opts.violetMesh, opts.assetsDir, opts.movecadeRoot),
  ]) {
    const file = path.join(opts.outDir, `${data.track.id}.track.json`);
    writeFileSync(file, stringify(data) + '\n');
    out.push(file);
  }
  cat.configureKartTrack('violet-loop');
  return out;
}
