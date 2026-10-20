// Checks the exported track JSON against values measured independently in Docs/Port/03,
// and proves a UE spline (Hermite, tangent (P[i+1]-P[i-1])/2) reproduces the Harbor centreline.
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const dir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../Data/Tracks');
const load = (id) => JSON.parse(readFileSync(path.join(dir, `${id}.track.json`), 'utf8'));
let failures = 0;
const check = (label, ok, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${detail ? `  (${detail})` : ''}`);
  if (!ok) failures++;
};
const near = (a, b, tol) => Math.abs(a - b) <= tol;
const nearXZ = (p, [x, z], tol = 0.01) => near(p.x, x, tol) && near(p.z, z, tol);

// Docs/Port/03 §2.3
const GRID = [[-3.06, -22.23], [2.96, -17.26], [-0.13, -15.05], [5.97, -9.67]];
const CHECKPOINT_IDS = ['finish', 'turn_1', 'outer_curve', 'north_hairpin_1', 'crossover', 'upper_loop', 'north_hairpin_2', 's_curve', 'final_curve'];

function shared(t, id) {
  check(`${id}: schema 1, three-rh-yup-m`, t.schemaVersion === 1 && t.coordinateSystem === 'three-rh-yup-m');
  check(`${id}: 3 laps`, t.track.laps === 3);
  check(`${id}: 9 checkpoints in order, radius 24`, t.checkpoints.map((c) => c.id).join() === CHECKPOINT_IDS.join() && t.checkpoints.every((c) => c.radius === 24));
  check(`${id}: start checkpoint index 1`, t.startCheckpointIndex === 1);
  check(`${id}: finish at (3,-30.32)`, nearXZ(t.checkpoints[0], [3, -30.32]));
  check(`${id}: spawn grid`, t.spawnGrid.length === 4 && t.spawnGrid.every((s, i) => nearXZ(s, GRID[i]) && s.yaw === 0));
  check(`${id}: 9 static cameras`, t.staticCameras.length === 9);
  check(`${id}: sample indices contiguous`, t.samples.points.every((p, i) => p.i === i));
  check(`${id}: unit tangents`, t.samples.points.every((p) => near(Math.hypot(p.tx, p.tz), 1, 1e-4)));
}

// ---- Sunset Harbor (Docs/Port/03 §2.5, §3.1)
const h = load('sunset-harbor');
shared(h, 'harbor');
check('harbor: lap 1612.1 m', near(h.track.lapLengthM, 1612.1, 0.1), `${h.track.lapLengthM}`);
check('harbor: 29 control points', h.spline.controlPoints.length === 29);
check('harbor: 870 samples', h.samples.points.length === 870);
check('harbor: 87 waypoints', h.navigation.waypoints.length === 87);
check('harbor: 12 item boxes', h.itemBoxes.length === 12);
const HARBOR_ITEMS = [[0.43, -113.42], [8, -116], [15.57, -118.58], [193.51, -136.62], [198, -130], [202.49, -123.38], [252.99, 79.45], [245, 79], [237.01, 78.55], [-77.27, 48.66], [-70, 52], [-62.73, 55.34]];
check('harbor: item box positions', h.itemBoxes.every((b, i) => nearXZ(b, HARBOR_ITEMS[i], 0.01) && b.y === 1));
const HARBOR_PADS = [[78.93, -224.47, -2.14], [281.98, -44.38, 2.09], [137.94, 148.69, 1.15]];
check('harbor: boost pads', h.boostPads.length === 3 && h.boostPads.every((p, i) => nearXZ(p, HARBOR_PADS[i]) && near(p.yaw, HARBOR_PADS[i][2], 0.01) && p.width === 12 && p.length === 6));
const HARBOR_CP = [[-7, -235], [142, -110], [326, -111], [223, 30], [164, 155], [63, 72], [-65, 101], [3, 30]];
check('harbor: checkpoints land on control points', h.checkpoints.slice(1).every((c, i) => nearXZ(c, HARBOR_CP[i], 0.001)));
check('harbor: minimap bounds', JSON.stringify(h.minimap.bounds) === JSON.stringify({ minX: -100, maxX: 350, minZ: -260, maxZ: 180 }));
check('harbor: two closed walls, 870 points each, at ±16.3', h.collision.walls.length === 2 && h.collision.walls.every((w) => w.closed && w.polyline.length === 870));
check('harbor: 16 ribbons', h.ribbons.length === 16);
const w = h.collision.walls[0];
const wallGap = Math.hypot(w.polyline[0][0] - h.samples.points[0].x, w.polyline[0][1] - h.samples.points[0].z);
check('harbor: right wall 16.3 m from the centreline', near(wallGap, 16.3, 0.001), wallGap.toFixed(4));
check('harbor: sample 0 at (3,-7), yaw 0', nearXZ(h.samples.points[0], [3, -7]) && near(h.samples.points[0].yaw, 0, 1e-6));

// UE spline equivalence: a Hermite spline with tangent (P[i+1]-P[i-1])/2 per unit key, evaluated at key t*29.
const P = h.spline.controlPoints;
const n = P.length;
function hermite(key) {
  const i = Math.floor(key) % n;
  const u = key - Math.floor(key);
  const p0 = P[i], p1 = P[(i + 1) % n];
  const m0 = [(P[(i + 1) % n][0] - P[(i - 1 + n) % n][0]) / 2, (P[(i + 1) % n][1] - P[(i - 1 + n) % n][1]) / 2];
  const m1 = [(P[(i + 2) % n][0] - P[i][0]) / 2, (P[(i + 2) % n][1] - P[i][1]) / 2];
  const h00 = 2 * u ** 3 - 3 * u ** 2 + 1, h10 = u ** 3 - 2 * u ** 2 + u, h01 = -2 * u ** 3 + 3 * u ** 2, h11 = u ** 3 - u ** 2;
  return [h00 * p0[0] + h10 * m0[0] + h01 * p1[0] + h11 * m1[0], h00 * p0[1] + h10 * m0[1] + h01 * p1[1] + h11 * m1[1]];
}
const maxErr = Math.max(...h.samples.points.map((p) => {
  const [x, z] = hermite(p.t * n);
  return Math.hypot(x - p.x, z - p.z);
}));
check('harbor: UE Hermite spline matches every sample', maxErr < 0.001, `max error ${maxErr.toExponential(2)} m`);

// ---- Violet Loop (Docs/Port/03 §2.4, §3.2, §4.1)
const v = load('violet-loop');
shared(v, 'violet');
check('violet: lap 1469.7 m', near(v.track.lapLengthM, 1469.7, 0.1), `${v.track.lapLengthM}`);
check('violet: ground y -1.605, rebase +1.605', v.track.groundY === -1.605 && v.track.suggestedRebaseY === 1.605);
check('violet: 55 control points / waypoints', v.spline.controlPoints.length === 55 && v.navigation.waypoints.length === 55);
check('violet: 490 profile samples', v.samples.points.length === 490);
const ys = v.samples.points.map((p) => p.y);
check('violet: road y in -1.61..-1.60', Math.min(...ys) >= -1.6105 && Math.max(...ys) <= -1.5995, `${Math.min(...ys)}..${Math.max(...ys)}`);
const widths = v.samples.points.map((p) => p.left.edge + p.right.edge);
check('violet: road width 18–58.2 m', near(Math.min(...widths), 18, 0.5) && near(Math.max(...widths), 58.2, 0.1), `${Math.min(...widths).toFixed(2)}..${Math.max(...widths).toFixed(2)}`);
const open = v.samples.points.filter((p) => p.left.wall === null || p.right.wall === null).length;
check('violet: 101 samples open on a side', open === 101, `${open}`);
const VIOLET_CP = [[40, -140], [256, -142], [305, -88], [110, 6], [261, 33], [306, 74], [156, 81], [64, 133]];
check('violet: checkpoints', v.checkpoints.slice(1).every((c, i) => nearXZ(c, VIOLET_CP[i], 0.001)));
const VIOLET_ITEMS = [[164.88, -59.22], [164.96, -62.16], [165.22, -71.48], [165.07, -66.37]];
check('violet: 4 item boxes', v.itemBoxes.length === 4 && v.itemBoxes.every((b, i) => nearXZ(b, VIOLET_ITEMS[i]) && b.y === 1));
const VIOLET_PADS = [[205.34, -91.97, -0.37], [262.08, -36.32, -4.42], [242.92, 27.15, -1.52], [110.18, 121.33, -3.99]];
check('violet: 4 boost pads', v.boostPads.length === 4 && v.boostPads.every((p, i) => nearXZ(p, VIOLET_PADS[i]) && near(p.yaw, VIOLET_PADS[i][2], 1e-6) && p.width === 8 && p.length === 4));
check('violet: minimap bounds', JSON.stringify(v.minimap.bounds) === JSON.stringify({ minX: -15, maxX: 320, minZ: -155, maxZ: 145 }));
const count = (prefab) => v.scenery.instances.filter((s) => s.prefab === prefab).length;
check('violet: trackside plan 34 kerb runs, 2 stands, 100 banners, 37 lamps',
  v.scenery.kerbRuns.length === 34 && count('grandstand') === 2 && count('banner_board') === 100 && count('lamp_post') === 37,
  `${v.scenery.kerbRuns.length}/${count('grandstand')}/${count('banner_board')}/${count('lamp_post')}`);
check('violet: 53 trees', count('tree_trunk') === 53 && count('tree_canopy') === 53);
check('violet: GLB mount (155,-28,15) x0.08', JSON.stringify(v.meshes[0].transform) === JSON.stringify({ position: [155, -28, 15], scale: 0.08 }));
const wallPts = v.collision.profileWalls.reduce((a, w2) => a + w2.polyline.length, 0);
const wallSamples = v.samples.points.filter((p) => p.left.wall !== null).length + v.samples.points.filter((p) => p.right.wall !== null).length;
check('violet: profile walls cover every walled sample', wallPts === wallSamples, `${v.collision.profileWalls.length} runs, ${wallPts} points`);

// ---- Violet mesh walls and dirt (src/violetMesh.mjs; Docs/Port/01 §5, §7.2, §7.5)
const roles = Object.fromEntries(v.collision.walls.map((w2) => [w2.role, w2.segments]));
check('violet: GLB walls replace the profile fallback', v.collision.wallsAreFallback === false && v.collision.walls.length === 2 && roles.sweep && roles.probe);
const segOk = (s) => s.length === 6 && s.every(Number.isFinite) && Math.hypot(s[2] - s[0], s[3] - s[1]) > 0 && Math.hypot(s[4], s[5]) > 0.71 && Math.hypot(s[4], s[5]) <= 1.0001;
check('violet: wall segments well formed (|n.xz| > 0.71 since |n.y| < 0.7)', [...roles.sweep, ...roles.probe].every(segOk), `${roles.sweep.length} sweep, ${roles.probe.length} probe`);
check('violet: 15k-30k segments per role', [roles.sweep, roles.probe].every((s) => s.length > 15000 && s.length < 30000));
function segDist(px, pz, s) {
  const dx = s[2] - s[0], dz = s[3] - s[1];
  const u = Math.max(0, Math.min(1, ((px - s[0]) * dx + (pz - s[1]) * dz) / (dx * dx + dz * dz)));
  return Math.hypot(px - s[0] - dx * u, pz - s[1] - dz * u);
}
const nearestWall = (segs, px, pz) => segs.reduce((m, s) => Math.min(m, segDist(px, pz, s)), Infinity);
// The road must be clear: at every sample, the nearest sweep wall is beyond the narrower road edge.
let worstClear = Infinity;
for (const p of v.samples.points) worstClear = Math.min(worstClear, nearestWall(roles.sweep, p.x, p.z) - Math.min(p.left.edge, p.right.edge));
check('violet: no sweep wall on the road (centre to wall >= road edge)', worstClear >= 0, `worst margin ${worstClear.toFixed(2)} m`);
// The profile (baked by down-rays against the live GLB) puts a wall where the mesh walls are.
let agree = 0, walled = 0;
for (const p of v.samples.points) {
  const n = { x: -p.tz, z: p.tx }; // side +1 (driver's right)
  for (const [side, prof] of [[1, p.right], [-1, p.left]]) {
    if (prof.wall === null) continue;
    walled++;
    if (nearestWall(roles.sweep, p.x + n.x * side * prof.wall, p.z + n.z * side * prof.wall) < 1.0) agree++;
  }
}
check('violet: >= 95% of profile wall points lie on a GLB sweep wall (within 1 m)', agree / walled >= 0.95, `${agree}/${walled}`);
const g = v.collision.dirtGrid;
const dirtAt = (x, z) => {
  const col = Math.floor((x - g.minX) / g.cellM), row = Math.floor((z - g.minZ) / g.cellM);
  return g.runs.some(([r0, c0, c1]) => r0 === row && col >= c0 && col <= c1);
};
check('violet: dirt grid present, 0.25 m cells', g && g.cellM === 0.25 && g.runs.length > 1000);
check('violet: centreline is never dirt', v.samples.points.every((p) => !dirtAt(p.x, p.z)));
let verge = 0, vergeDirt = 0;
for (const p of v.samples.points) {
  const n = { x: -p.tz, z: p.tx };
  for (const [side, prof] of [[1, p.right], [-1, p.left]]) {
    if (prof.wall === null || prof.wall - prof.edge < 4) continue;
    verge++;
    const o = prof.edge + 2; // 2 m off the asphalt, still short of the wall
    if (dirtAt(p.x + n.x * side * o, p.z + n.z * side * o)) vergeDirt++;
  }
}
check('violet: the sand verge 2 m past the road edge is dirt (>= 90%)', vergeDirt / verge >= 0.9, `${vergeDirt}/${verge}`);
check('violet: decoded GLB recorded for the UE import (7 visual nodes)', v.meshes[0].transformBaked === true && v.meshes[0].ueAssets.length === 7);

// ---- Violet IP cleanup (src/violetMesh.mjs): visual only, every cut motif replaced by an original prop
const ip = v.meshes[0].ipCleanup;
check('violet: warp pipes and brick blocks cut from the visual mesh', ip && ip.cutNodes.join() === 'Object_12,Object_47' && !v.meshes[0].ueAssets.some((a) => /Object_(12|47)$/.test(a)));
check('violet: cut nodes still collide (meshColliders keep all 9 nodes)', v.collision.meshColliders.length === 9);
const pipes = ip.motifs.filter((m) => m.kind === 'pipe'), bricks = ip.motifs.filter((m) => m.kind === 'bricks');
check('violet: 6 pipes, 19 brick groups found', pipes.length === 6 && bricks.length === 19, `${pipes.length} / ${bricks.length}`);
const replaced = new Set(v.scenery.instances.filter((s) => s.replaces).map((s) => s.replaces.split(' ').slice(1).join(' ')));
check('violet: every motif has a replacement prop', ip.motifs.every((m, i) => replaced.has(`${m.kind} ${i}`)), `${replaced.size}/${ip.motifs.length}`);
// A motif that reaches the kart's sweep band keeps its wall; its prop must cover the same footprint.
const sweepBand = (m) => m.min[1] < 1.15 + v.track.groundY + 1.605 && m.max[1] > -1.15;
const covered = ip.motifs.filter(sweepBand).every((m, i) => {
  const props = v.scenery.instances.filter((s) => s.replaces && s.replaces.endsWith(`${m.kind} ${ip.motifs.indexOf(m)}`));
  return props.length > 0 && props.every((s) => Math.abs(s.x - (m.min[0] + m.max[0]) / 2) < 6 && Math.abs(s.z - (m.min[2] + m.max[2]) / 2) < 6);
});
check('violet: props stand where the colliding motifs were', covered);
check('violet: shared props and CC0 surfaces referenced', ['ad_column_body', 'billboard_face', 'rock_formation', 'plinth', 'boost_pad'].every((p) => v.scenery.prefabs[p])
  && ['asphalt_track', 'painted_concrete', 'leafy_grass', 'gravelly_sand'].every((s) => v.detailSurfaces[s]?.mean?.length === 3));

// ---- Neon City, the original third track (src/neonCity.ts): Harbor's schema and rules
const nc = load('neon-city');
shared(nc, 'neon');
check('neon: analytic spline, 1300-1800 m lap', nc.track.trackType === 'analytic-spline' && nc.track.lapLengthM >= 1300 && nc.track.lapLengthM <= 1800, `${nc.track.lapLengthM}`);
const ncCurv = Math.max(...nc.samples.points.map((p) => Math.abs(p.curvature)));
check('neon: no bend tighter than the wall offset + 5 m (B14)', 1 / ncCurv > 21.3, `tightest ${(1 / ncCurv).toFixed(1)} m`);
check('neon: two closed walls at ±16.3, one point per sample', nc.collision.walls.length === 2 && nc.collision.walls.every((w2) => w2.closed && w2.polyline.length === nc.samples.points.length));
const ncWall = Math.min(...nc.collision.walls.flatMap((w2) => w2.polyline.map(([x, z]) => Math.min(...nc.samples.points.map((p) => Math.hypot(p.x - x, p.z - z))))));
check('neon: every wall point a full offset from the centreline (no fold)', ncWall > 16.2, `${ncWall.toFixed(3)} m`);
const gameplay = (t) => JSON.stringify(t.ribbons.filter((rb) => rb.collision !== 'none').map(({ name, inner, outer, innerY, outerY, collision, surface }) => ({ name, inner, outer, innerY, outerY, collision, surface })));
check("neon: gameplay ribbons match Harbor's (same handling)", gameplay(nc) === gameplay(h));
check('neon: 12 item boxes in 4 rows of 3 at y 1', nc.itemBoxes.length === 12 && nc.itemBoxes.every((b) => b.y === 1) && new Set(nc.itemBoxes.map((b) => b.row)).size === 4);
check('neon: 4 boost pads 12 x 6', nc.boostPads.length === 4 && nc.boostPads.every((p) => p.width === 12 && p.length === 6));
const NP = nc.spline.controlPoints, nn = NP.length;
const ncErr = Math.max(...nc.samples.points.map((p) => {
  const key = p.t * nn, i = Math.floor(key) % nn, u = key - Math.floor(key);
  const at = (k) => NP[(i + k + nn) % nn];
  const h00 = 2 * u ** 3 - 3 * u ** 2 + 1, h10 = u ** 3 - 2 * u ** 2 + u, h01 = -2 * u ** 3 + 3 * u ** 2, h11 = u ** 3 - u ** 2;
  const m = (k, a) => (at(k + 1)[a] - at(k - 1)[a]) / 2;
  const x = h00 * at(0)[0] + h10 * m(0, 0) + h01 * at(1)[0] + h11 * m(1, 0), z = h00 * at(0)[1] + h10 * m(0, 1) + h01 * at(1)[1] + h11 * m(1, 1);
  return Math.hypot(x - p.x, z - p.z);
}));
check('neon: UE Hermite spline matches every sample', ncErr < 0.001, `max error ${ncErr.toExponential(2)} m`);
check('neon: checkpoints on the line', nc.checkpoints.slice(1).every((c) => nc.samples.points.some((p) => Math.hypot(p.x - c.x, p.z - c.z) < 0.6)));
check('neon: scenery exported (buildings, props, lights)', nc.meshes.length === 2 && nc.scenery.instances.length > 50 && nc.scenery.lights.length > 20);

console.log(failures ? `\n${failures} check(s) failed` : '\nall checks passed');
process.exit(failures ? 1 : 0);
