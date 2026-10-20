// Violet Loop GLB: decode (Draco + WebP), bake the mount transform, and measure what the web's
// collision code sees, so the UE sim can stay analytic (Docs/Port/03 §3.2-3.3, Docs/Port/01 §5, §7).
//
// Outputs:
//  - out/violet/violet_loop.glb: plain glTF (no Draco, PNG textures), the [155,-28,15] x0.08 mount
//    and the +1.605 m rebase baked into the vertices, node and mesh names kept (Object_10 ...).
//    Object_22 (never rendered by violet-loop.jsx) is left out, and the 53 voxel trees are cut
//    out of Object_18 (the web discards them in the shader and draws soft instanced trees).
//  - The data for the track JSON: wall segments for the sweep and for the probe rays, and a
//    dirt grid for the terrain ray. All in source units (three.js world, no rebase).
import { Document, NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import draco3d from 'draco3dgltf';
import sharp from 'sharp';
import { mkdirSync } from 'node:fs';
import path from 'node:path';

/** violet-loop.jsx:113-122: <group position={[155,-28,15]} scale={0.08}>, and each rendered node's name. */
export const VIOLET_MOUNT = { position: [155, -28, 15], scale: 0.08 };
const RENDERED = {
  Object_10: 'ground',
  Object_11: 'ground dirt',
  Object_12: 'ground',
  Object_13: 'ground',
  Object_18: 'ground',
  Object_24: 'ground',
  Object_25: 'ground',
  Object_27: 'ground speed',
  Object_47: 'ground',
};
/** MultiplayerKart.tsx:131-132. */
const DIRT_KEYWORDS = ['dirt', 'sand', 'grass', 'offroad', 'mud', 'gravel'];
/** trackSurfaces.ts:71-73 VIOLET_TREE_CELLS (column, row of the 8x8 palette, raw UVs), cut from material_12. */
const TREE_CELLS = [[1, 6], [1, 3], [1, 4], [0, 4], [0, 5], [0, 6], [1, 1]];

// The web's collision constants (meshBoundary.ts, MultiplayerKart.tsx).
const WALL_NORMAL_Y = 0.7;
const KART_RADIUS = 1.15;
/** The physics plane is y = 0 (logical) on both tracks; the drawn Violet road is at -1.605. */
const LOGICAL_Y = 0;
/** checkHorizontalCollisionFast casts from position.y + 0.5. */
const PROBE_Y = LOGICAL_Y + 0.5;
/** The terrain ray: from position.y + 1, straight down, far 3. */
const TERRAIN_TOP = LOGICAL_Y + 1;
const TERRAIN_BOTTOM = LOGICAL_Y + 1 - 3;
/** Geometry further than this from every centreline sample can't matter: recovery fires at 64 m. */
const NEAR_TRACK_M = 80;
const DIRT_CELL_M = 0.25;

const r3 = (n) => {
  const v = Math.round(n * 1000) / 1000;
  return Object.is(v, -0) ? 0 : v;
};
const r4 = (n) => {
  const v = Math.round(n * 10000) / 10000;
  return Object.is(v, -0) ? 0 : v;
};

/** Point-to-centreline distance through a coarse grid of the samples. */
function nearTrackTest(samples, limit) {
  const cell = 20;
  const grid = new Map();
  const key = (i, j) => `${i},${j}`;
  for (const s of samples) {
    const k = key(Math.floor(s.x / cell), Math.floor(s.z / cell));
    if (!grid.has(k)) grid.set(k, []);
    grid.get(k).push(s);
  }
  const reach = Math.ceil(limit / cell);
  return (x, z) => {
    const ci = Math.floor(x / cell);
    const cj = Math.floor(z / cell);
    const limitSq = limit * limit;
    for (let i = ci - reach; i <= ci + reach; i++) {
      for (let j = cj - reach; j <= cj + reach; j++) {
        const list = grid.get(key(i, j));
        if (!list) continue;
        for (const s of list) if ((s.x - x) ** 2 + (s.z - z) ** 2 < limitSq) return true;
      }
    }
    return false;
  };
}

/** The segment where a triangle meets the plane y = h, or null when that is a point or empty. */
function slice(a, b, c, h) {
  const pts = [];
  const verts = [a, b, c];
  for (let i = 0; i < 3; i++) {
    const p = verts[i];
    const q = verts[(i + 1) % 3];
    if (p[1] === h) pts.push([p[0], p[2]]);
    if ((p[1] - h) * (q[1] - h) < 0) {
      const t = (h - p[1]) / (q[1] - p[1]);
      pts.push([p[0] + (q[0] - p[0]) * t, p[2] + (q[2] - p[2]) * t]);
    }
  }
  if (pts.length < 2) return null;
  // The two points furthest apart (a vertex on the plane can repeat as an edge crossing).
  let best = null;
  let bestD = 1e-4; // 1 cm
  for (let i = 0; i < pts.length; i++) {
    for (let j = i + 1; j < pts.length; j++) {
      const d = (pts[i][0] - pts[j][0]) ** 2 + (pts[i][1] - pts[j][1]) ** 2;
      if (d > bestD) { bestD = d; best = [pts[i], pts[j]]; }
    }
  }
  return best;
}

class SegmentSet {
  constructor() { this.flat = []; this.seen = new Set(); }
  add([p, q], nx, nz) {
    const a = [r3(p[0]), r3(p[1])];
    const b = [r3(q[0]), r3(q[1])];
    if (a[0] === b[0] && a[1] === b[1]) return;
    const k1 = `${a[0]},${a[1]},${b[0]},${b[1]}`;
    const k2 = `${b[0]},${b[1]},${a[0]},${a[1]}`;
    if (this.seen.has(k1) || this.seen.has(k2)) return;
    this.seen.add(k1);
    this.flat.push(a[0], a[1], b[0], b[1], r4(nx), r4(nz));
  }
  get count() { return this.flat.length / 6; }

  /**
   * Joins collinear pieces that touch or overlap and face the same way (the barrier blocks are
   * finely tessellated). The union of the lines is unchanged, so the disc sweep and the rays see
   * the same walls with far fewer segments. Returns a new flat array in a stable order.
   */
  merged() {
    const groups = new Map();
    const f = this.flat;
    for (let i = 0; i < f.length; i += 6) {
      let ax = f[i], az = f[i + 1], bx = f[i + 2], bz = f[i + 3];
      let dx = bx - ax, dz = bz - az;
      const len = Math.hypot(dx, dz);
      dx /= len; dz /= len;
      if (dx < 0 || (dx === 0 && dz < 0)) { dx = -dx; dz = -dz; [ax, az, bx, bz] = [bx, bz, ax, az]; }
      const angle = Math.round(Math.atan2(dz, dx) * 2000); // 0.5 mrad bins
      const offset = Math.round((-dz * ax + dx * az) * 200); // 5 mm bins
      const key = `${angle}|${offset}|${Math.round(f[i + 4] * 20)}|${Math.round(f[i + 5] * 20)}`;
      if (!groups.has(key)) groups.set(key, { ox: ax, oz: az, dx, dz, nx: f[i + 4], nz: f[i + 5], spans: [] });
      const g = groups.get(key);
      const t0 = (ax - g.ox) * g.dx + (az - g.oz) * g.dz;
      const t1 = (bx - g.ox) * g.dx + (bz - g.oz) * g.dz;
      g.spans.push([Math.min(t0, t1), Math.max(t0, t1)]);
    }
    const out = [];
    for (const g of groups.values()) {
      g.spans.sort((p, q) => p[0] - q[0]);
      let [s0, s1] = g.spans[0];
      const emit = () => out.push(r3(g.ox + g.dx * s0), r3(g.oz + g.dz * s0), r3(g.ox + g.dx * s1), r3(g.oz + g.dz * s1), g.nx, g.nz);
      for (const [t0, t1] of g.spans.slice(1)) {
        if (t0 <= s1 + 0.005) s1 = Math.max(s1, t1);
        else { emit(); [s0, s1] = [t0, t1]; }
      }
      emit();
    }
    return out;
  }
}

async function webpToPng(texture) {
  const png = await sharp(Buffer.from(texture.getImage())).png().toBuffer();
  return new Uint8Array(png);
}

/**
 * @param {{ glbPath: string, outDir: string, samples: {x:number,z:number}[], rebaseY: number }} opts
 */
export async function processVioletMesh({ glbPath, outDir, samples, rebaseY }) {
  const io = new NodeIO()
    .registerExtensions(ALL_EXTENSIONS)
    .registerDependencies({ 'draco3d.decoder': await draco3d.createDecoderModule() });
  const src = await io.read(glbPath);
  const nearTrack = nearTrackTest(samples, NEAR_TRACK_M);
  const { position: [ox, oy, oz], scale: s } = VIOLET_MOUNT;

  // ---- Dirt grid bounds: every centreline sample plus the near-track margin.
  const xs = samples.map((p) => p.x);
  const zs = samples.map((p) => p.z);
  const minX = Math.floor(Math.min(...xs) - NEAR_TRACK_M);
  const minZ = Math.floor(Math.min(...zs) - NEAR_TRACK_M);
  const cols = Math.ceil((Math.max(...xs) + NEAR_TRACK_M - minX) / DIRT_CELL_M);
  const rows = Math.ceil((Math.max(...zs) + NEAR_TRACK_M - minZ) / DIRT_CELL_M);
  const topY = new Float32Array(cols * rows).fill(-Infinity);
  const isDirt = new Uint8Array(cols * rows);

  const sweep = new SegmentSet();
  const probe = new SegmentSet();
  const stats = {};

  // ---- Output document.
  const out = new Document();
  const buffer = out.createBuffer();
  const scene = out.createScene('violet_loop');
  const materials = new Map();
  const material = async (m) => {
    if (materials.has(m)) return materials.get(m);
    const mat = out.createMaterial(m.getName())
      .setBaseColorFactor(m.getBaseColorFactor())
      .setRoughnessFactor(m.getRoughnessFactor())
      .setMetallicFactor(m.getMetallicFactor())
      .setEmissiveFactor(m.getEmissiveFactor())
      .setDoubleSided(m.getDoubleSided());
    const copyTex = async (t, name) => t && out.createTexture(`${m.getName()}_${name}`).setMimeType('image/png').setImage(await webpToPng(t));
    const base = await copyTex(m.getBaseColorTexture(), 'basecolor');
    if (base) mat.setBaseColorTexture(base);
    const emissive = await copyTex(m.getEmissiveTexture(), 'emissive');
    if (emissive) mat.setEmissiveTexture(emissive);
    // material_6's metallic-roughness map is left out: its factors (0.6, 0) already give the look,
    // and UE's glTF material reads the channels differently enough to read as wet sand.
    materials.set(m, mat);
    return mat;
  };

  for (const node of src.getRoot().listNodes()) {
    const name = node.getName();
    const meshName = RENDERED[name];
    if (!meshName) continue; // Object_22 is never rendered (violet-loop.jsx).
    const prim = node.getMesh().listPrimitives()[0];
    const pos = prim.getAttribute('POSITION').getArray();
    const nrm = prim.getAttribute('NORMAL')?.getArray();
    const uv = prim.getAttribute('TEXCOORD_0')?.getArray();
    const uv1 = prim.getAttribute('TEXCOORD_1')?.getArray();
    const idx = prim.getIndices()?.getArray() ?? Uint32Array.from({ length: pos.length / 3 }, (_, i) => i);
    const vcount = pos.length / 3;

    // World (three.js) positions under the mount transform.
    const world = new Float32Array(pos.length);
    for (let i = 0; i < vcount; i++) {
      world[i * 3] = ox + s * pos[i * 3];
      world[i * 3 + 1] = oy + s * pos[i * 3 + 1];
      world[i * 3 + 2] = oz + s * pos[i * 3 + 2];
    }
    const dirt = DIRT_KEYWORDS.some((k) => meshName.includes(k));
    const st = (stats[name] = { tris: idx.length / 3, wallTris: 0, sweepSegs: 0, probeSegs: 0, treeTrisCut: 0 });

    // ---- Collision analysis (all rendered meshes are kart colliders: kartCollisionMeshes.ts).
    const V = (i) => [world[i * 3], world[i * 3 + 1], world[i * 3 + 2]];
    for (let t = 0; t < idx.length; t += 3) {
      const a = V(idx[t]), b = V(idx[t + 1]), c = V(idx[t + 2]);
      const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
      const vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
      let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      const len = Math.hypot(nx, ny, nz);
      if (len === 0) continue;
      nx /= len; ny /= len; nz /= len;
      const minY = Math.min(a[1], b[1], c[1]);
      const maxY = Math.max(a[1], b[1], c[1]);

      if (Math.abs(ny) >= WALL_NORMAL_Y) {
        // Floor or ceiling: the terrain ray takes the first such hit from y+1 down to y-2.
        if (maxY < TERRAIN_BOTTOM || minY > TERRAIN_TOP) continue;
        rasterFloor(a, b, c, dirt);
        continue;
      }

      const cx = (a[0] + b[0] + c[0]) / 3, cz = (a[2] + b[2] + c[2]) / 3;
      if (!nearTrack(cx, cz)) continue;
      st.wallTris++;
      // Sweep: a disc on the plane y=0; the BVH bounds test is a 1.15 m sphere at y=0, so only
      // faces reaching into y in [-1.15, 1.15] are tested. The push uses the closest point,
      // flattened: take the face's slice at the height nearest the plane.
      if (maxY >= LOGICAL_Y - KART_RADIUS && minY <= LOGICAL_Y + KART_RADIUS) {
        const h = Math.min(Math.max(LOGICAL_Y, minY), maxY);
        const seg = slice(a, b, c, h);
        if (seg) { sweep.add(seg, nx, nz); st.sweepSegs++; }
      }
      // Probes: horizontal rays at y=0.5 hit exactly this slice; the hit normal is the face normal.
      if (minY <= PROBE_Y && maxY >= PROBE_Y) {
        const seg = slice(a, b, c, PROBE_Y);
        if (seg) { probe.add(seg, nx, nz); st.probeSegs++; }
      }
    }

    // ---- Output mesh: rebased world positions, unchanged normals (uniform scale).
    let keep = idx;
    if (name === 'Object_18' && uv) {
      const cells = TREE_CELLS.map(([col, row]) => [col / 8, row / 8, (col + 1) / 8, (row + 1) / 8]);
      const kept = [];
      for (let t = 0; t < idx.length; t += 3) {
        const u = (uv[idx[t] * 2] + uv[idx[t + 1] * 2] + uv[idx[t + 2] * 2]) / 3;
        const v = (uv[idx[t] * 2 + 1] + uv[idx[t + 1] * 2 + 1] + uv[idx[t + 2] * 2 + 1]) / 3;
        if (cells.some(([u0, v0, u1, v1]) => u > u0 && u < u1 && v > v0 && v < v1)) { st.treeTrisCut++; continue; }
        kept.push(idx[t], idx[t + 1], idx[t + 2]);
      }
      keep = Uint32Array.from(kept);
    }
    const rebased = Float32Array.from(world);
    for (let i = 0; i < vcount; i++) rebased[i * 3 + 1] += rebaseY;
    const outPrim = out.createPrimitive()
      .setAttribute('POSITION', out.createAccessor().setType('VEC3').setArray(rebased).setBuffer(buffer))
      .setIndices(out.createAccessor().setType('SCALAR').setArray(Uint32Array.from(keep)).setBuffer(buffer))
      .setMaterial(await material(prim.getMaterial()));
    if (nrm) outPrim.setAttribute('NORMAL', out.createAccessor().setType('VEC3').setArray(Float32Array.from(nrm)).setBuffer(buffer));
    if (uv) outPrim.setAttribute('TEXCOORD_0', out.createAccessor().setType('VEC2').setArray(Float32Array.from(uv)).setBuffer(buffer));
    if (uv1) outPrim.setAttribute('TEXCOORD_1', out.createAccessor().setType('VEC2').setArray(Float32Array.from(uv1)).setBuffer(buffer));
    const mesh = out.createMesh(name).addPrimitive(outPrim);
    scene.addChild(out.createNode(name).setMesh(mesh).setExtras({ webName: meshName }));
  }

  mkdirSync(outDir, { recursive: true });
  const glbOut = path.join(outDir, 'violet_loop.glb');
  await new NodeIO().write(glbOut, out);

  // ---- Dirt runs, row by row: [row, firstCol, lastCol, ...].
  const runs = [];
  let dirtCells = 0;
  for (let j = 0; j < rows; j++) {
    let start = -1;
    for (let i = 0; i <= cols; i++) {
      const d = i < cols && isDirt[j * cols + i] === 1;
      if (d) dirtCells++;
      if (d && start < 0) start = i;
      if (!d && start >= 0) { runs.push(j, start, i - 1); start = -1; }
    }
  }

  return {
    glb: glbOut,
    stats,
    walls: { sweep: sweep.merged(), probe: probe.merged(), sweepPieces: sweep.count, probePieces: probe.count },
    dirtGrid: { cellM: DIRT_CELL_M, minX, minZ, cols, rows, dirtCells, runs },
  };

  function rasterFloor(a, b, c, dirt) {
    const x0 = Math.max(0, Math.floor((Math.min(a[0], b[0], c[0]) - minX) / DIRT_CELL_M - 0.5));
    const x1 = Math.min(cols - 1, Math.ceil((Math.max(a[0], b[0], c[0]) - minX) / DIRT_CELL_M - 0.5));
    const z0 = Math.max(0, Math.floor((Math.min(a[2], b[2], c[2]) - minZ) / DIRT_CELL_M - 0.5));
    const z1 = Math.min(rows - 1, Math.ceil((Math.max(a[2], b[2], c[2]) - minZ) / DIRT_CELL_M - 0.5));
    if (x0 > x1 || z0 > z1) return;
    // Barycentric in XZ.
    const d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
    if (Math.abs(d) < 1e-12) return;
    for (let j = z0; j <= z1; j++) {
      const pz = minZ + (j + 0.5) * DIRT_CELL_M;
      for (let i = x0; i <= x1; i++) {
        const px = minX + (i + 0.5) * DIRT_CELL_M;
        const l1 = ((b[2] - c[2]) * (px - c[0]) + (c[0] - b[0]) * (pz - c[2])) / d;
        const l2 = ((c[2] - a[2]) * (px - c[0]) + (a[0] - c[0]) * (pz - c[2])) / d;
        const l3 = 1 - l1 - l2;
        if (l1 < -1e-9 || l2 < -1e-9 || l3 < -1e-9) continue;
        const y = l1 * a[1] + l2 * b[1] + l3 * c[1];
        if (y > TERRAIN_TOP || y < TERRAIN_BOTTOM) continue;
        const k = j * cols + i;
        if (y > topY[k]) { topY[k] = y; isDirt[k] = dirt ? 1 : 0; }
      }
    }
  }
}
