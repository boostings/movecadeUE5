/**
 * The environment kit: seeded noise and procedural geometry shared by the UE environment passes
 * (violetGarden.ts, harborCoast.ts). Everything is three.js axes and metres, base at y = 0 unless a
 * function says otherwise, with vertex colours (the masters multiply by them) and smooth normals.
 *
 * The shapes are soft and rounded on purpose (the art direction: cartoony shapes, realistic light), and
 * dense enough for Nanite to keep their silhouettes: bevels are real geometry, not normal-map tricks.
 */
import * as THREE from 'three';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { SimplexNoise } from 'three/addons/math/SimplexNoise.js';

export type V3 = [number, number, number];

// ---- Randomness and noise ---------------------------------------------------------------------------

/** A seeded LCG in [0, 1), so every export is byte-identical. */
export function rng(seed: number) {
  let s = seed >>> 0;
  return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296);
}

/** Seeded simplex noise with fractal helpers. */
export class Noise {
  private readonly simplex: SimplexNoise;
  constructor(seed: number) {
    this.simplex = new SimplexNoise({ random: rng(seed) });
  }
  n2(x: number, y: number) { return this.simplex.noise(x, y); }
  n3(x: number, y: number, z: number) { return this.simplex.noise3d(x, y, z); }
  n4(x: number, y: number, z: number, w: number) { return this.simplex.noise4d(x, y, z, w); }
  /** Fractal sum, normalised to about -1..1. */
  fbm2(x: number, y: number, octaves = 4, lacunarity = 2, gain = 0.5) {
    let sum = 0, amp = 1, norm = 0, f = 1;
    for (let o = 0; o < octaves; o++) { sum += amp * this.simplex.noise(x * f, y * f); norm += amp; amp *= gain; f *= lacunarity; }
    return sum / norm;
  }
  fbm3(x: number, y: number, z: number, octaves = 3) {
    let sum = 0, amp = 1, norm = 0, f = 1;
    for (let o = 0; o < octaves; o++) { sum += amp * this.simplex.noise3d(x * f, y * f, z * f); norm += amp; amp *= 0.5; f *= 2; }
    return sum / norm;
  }
}

export const smoothstep = (a: number, b: number, x: number) => {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
};
export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;

// ---- Attribute helpers ------------------------------------------------------------------------------

/** Keeps only position, normal and color (merge needs matching attributes); paints `color` if given. */
export function paint(g: THREE.BufferGeometry, color?: string | THREE.Color): THREE.BufferGeometry {
  const out = g.index ? g.toNonIndexed() : g.clone();
  for (const key of Object.keys(out.attributes)) if (!['position', 'normal', 'color'].includes(key)) out.deleteAttribute(key);
  if (!out.getAttribute('normal')) out.computeVertexNormals();
  if (color !== undefined || !out.getAttribute('color')) {
    const c = new THREE.Color(color ?? '#ffffff');
    const n = out.getAttribute('position').count;
    const a = new Float32Array(n * 3);
    for (let i = 0; i < n; i++) { a[i * 3] = c.r; a[i * 3 + 1] = c.g; a[i * 3 + 2] = c.b; }
    out.setAttribute('color', new THREE.BufferAttribute(a, 3));
  }
  return out;
}

/** Vertex colour from a function of the vertex position and normal (gradients, AO-ish darkening). */
export function shade(g: THREE.BufferGeometry, fn: (p: THREE.Vector3, n: THREE.Vector3, c: THREE.Color) => void) {
  const pos = g.getAttribute('position'), nor = g.getAttribute('normal'), col = g.getAttribute('color');
  const p = new THREE.Vector3(), n = new THREE.Vector3(), c = new THREE.Color();
  for (let i = 0; i < pos.count; i++) {
    p.fromBufferAttribute(pos, i); n.fromBufferAttribute(nor, i); c.fromBufferAttribute(col as THREE.BufferAttribute, i);
    fn(p, n, c);
    col.setXYZ(i, c.r, c.g, c.b);
  }
  return g;
}

/** Merges painted geometries (position, normal, color). */
export function merge(parts: THREE.BufferGeometry[]): THREE.BufferGeometry {
  return mergeGeometries(parts.map((g) => paint(g)), false)!;
}

/** Applies translate / rotate (XYZ Euler) / scale to a geometry, returning it. */
export function place(g: THREE.BufferGeometry, position: V3 = [0, 0, 0], rotation: V3 = [0, 0, 0], scale: V3 | number = 1) {
  const s = typeof scale === 'number' ? [scale, scale, scale] as V3 : scale;
  g.applyMatrix4(new THREE.Matrix4().compose(new THREE.Vector3(...position), new THREE.Quaternion().setFromEuler(new THREE.Euler(...rotation)), new THREE.Vector3(...s)));
  return g;
}

/** Recomputes smooth normals across coincident vertices (welds by position only for the normal pass). */
export function smoothNormals(g: THREE.BufferGeometry, cell = 1e-4) {
  const pos = g.getAttribute('position');
  const acc = new Map<string, THREE.Vector3>();
  const key = (i: number) => `${Math.round(pos.getX(i) / cell)},${Math.round(pos.getY(i) / cell)},${Math.round(pos.getZ(i) / cell)}`;
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), f = new THREE.Vector3();
  for (let i = 0; i < pos.count; i += 3) {
    a.fromBufferAttribute(pos, i); b.fromBufferAttribute(pos, i + 1); c.fromBufferAttribute(pos, i + 2);
    f.subVectors(c, b).cross(new THREE.Vector3().subVectors(a, b)); // area-weighted
    for (let k = 0; k < 3; k++) {
      const id = key(i + k);
      const v = acc.get(id) ?? new THREE.Vector3();
      v.add(f);
      acc.set(id, v);
    }
  }
  const n = new Float32Array(pos.count * 3);
  for (let i = 0; i < pos.count; i++) {
    const v = acc.get(key(i))!.clone().normalize();
    n[i * 3] = v.x; n[i * 3 + 1] = v.y; n[i * 3 + 2] = v.z;
  }
  g.setAttribute('normal', new THREE.BufferAttribute(n, 3));
  return g;
}

// ---- Primitives -------------------------------------------------------------------------------------

/**
 * A rounded box with every face subdivided to about `cell` metres (so it can be displaced), base at y = 0,
 * centred in x and z. The classic construction: a subdivided box whose points are pulled onto the
 * Minkowski sum of an inner box and a sphere of `radius`.
 */
export function roundedBox(w: number, h: number, d: number, radius: number, cell = 0.4) {
  const r = Math.min(radius, w / 2, h / 2, d / 2);
  const seg = (s: number) => Math.max(2, Math.ceil(s / cell));
  const g = new THREE.BoxGeometry(w, h, d, seg(w), seg(h), seg(d));
  const pos = g.getAttribute('position');
  const inner = new THREE.Vector3(w / 2 - r, h / 2 - r, d / 2 - r);
  const p = new THREE.Vector3(), q = new THREE.Vector3(), n = new Float32Array(pos.count * 3);
  for (let i = 0; i < pos.count; i++) {
    p.fromBufferAttribute(pos, i);
    q.set(THREE.MathUtils.clamp(p.x, -inner.x, inner.x), THREE.MathUtils.clamp(p.y, -inner.y, inner.y), THREE.MathUtils.clamp(p.z, -inner.z, inner.z));
    const dir = p.clone().sub(q);
    if (dir.lengthSq() < 1e-12) dir.set(0, 1, 0);
    dir.normalize();
    p.copy(q).addScaledVector(dir, r);
    pos.setXYZ(i, p.x, p.y + h / 2, p.z);
    n[i * 3] = dir.x; n[i * 3 + 1] = dir.y; n[i * 3 + 2] = dir.z;
  }
  g.setAttribute('normal', new THREE.BufferAttribute(n, 3));
  return paint(g);
}

/** Pushes every vertex along its normal by fn(position); then smooths normals. */
export function displace(g: THREE.BufferGeometry, fn: (p: THREE.Vector3, n: THREE.Vector3) => number) {
  const pos = g.getAttribute('position'), nor = g.getAttribute('normal');
  const p = new THREE.Vector3(), n = new THREE.Vector3();
  // Displace per unique position so seams stay closed.
  const done = new Map<string, THREE.Vector3>();
  for (let i = 0; i < pos.count; i++) {
    p.fromBufferAttribute(pos, i); n.fromBufferAttribute(nor, i);
    const k = `${p.x.toFixed(4)},${p.y.toFixed(4)},${p.z.toFixed(4)}`;
    let out = done.get(k);
    if (!out) { out = p.clone().addScaledVector(n, fn(p, n)); done.set(k, out); }
    pos.setXYZ(i, out.x, out.y, out.z);
  }
  return smoothNormals(g);
}

/** A box with chamfered edges (all twelve), base at y = 0. */
export function chamferBox(w: number, h: number, d: number, c: number) {
  return roundedBox(w, h, d, c, Math.max(w, h, d)); // one segment per face span: flat faces, faceted bevel
}

/**
 * One crystal: a hexagonal prism with a pyramid tip, base at y = 0 (sunk below), axis +Y.
 * `tip` is the share of the height taken by the point.
 */
export function crystal(height: number, radius: number, sides = 6, tip = 0.32, seed = 1) {
  const rand = rng(seed);
  const shaft = height * (1 - tip);
  const ring = (y: number, r: number) => Array.from({ length: sides }, (_, k) => {
    const a = (k / sides) * Math.PI * 2 + rand() * 0.15;
    const rr = r * (0.88 + rand() * 0.24);
    return new THREE.Vector3(Math.cos(a) * rr, y, Math.sin(a) * rr);
  });
  const base = ring(-height * 0.15, radius * 0.92);
  const top = ring(shaft, radius);
  const apex = new THREE.Vector3((rand() - 0.5) * radius * 0.3, height, (rand() - 0.5) * radius * 0.3);
  const tris: number[] = [];
  const push = (...v: THREE.Vector3[]) => v.forEach((p) => tris.push(p.x, p.y, p.z));
  for (let k = 0; k < sides; k++) {
    const k1 = (k + 1) % sides;
    push(base[k]!, top[k1]!, base[k1]!); push(base[k]!, top[k]!, top[k1]!);
    push(top[k]!, apex, top[k1]!);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(tris, 3));
  g.computeVertexNormals(); // faceted: crystals read by their facets
  return paint(g);
}

/** A cluster of crystals leaning out from a common root, in a unit-ish footprint (about 2 m across at scale 1). */
export function crystalCluster(seed: number, count = 7, scale = 1) {
  const rand = rng(seed);
  const parts: THREE.BufferGeometry[] = [];
  for (let i = 0; i < count; i++) {
    const main = i === 0;
    const h = (main ? 3.2 : 1.1 + rand() * 1.9) * scale;
    const r = (main ? 0.55 : 0.22 + rand() * 0.3) * scale;
    const yaw = rand() * Math.PI * 2;
    const lean = main ? rand() * 0.12 : 0.25 + rand() * 0.55;
    const off = main ? 0 : (0.25 + rand() * 0.6) * scale;
    const g = crystal(h, r, 6, 0.3 + rand() * 0.1, seed * 31 + i);
    place(g, [Math.cos(yaw) * off, 0, Math.sin(yaw) * off], [Math.sin(yaw) * lean, 0, -Math.cos(yaw) * lean]);
    parts.push(g);
  }
  return merge(parts);
}

/**
 * A soft canopy blob: a subdivided icosphere with seeded lumps, squashed, base around y = 0
 * (the trunk top). Vertex colours darken toward the underside, so it reads as foliage even before light.
 */
export function canopy(seed: number, radius = 3, squash = 0.82, lumps = 0.22) {
  const noise = new Noise(seed);
  const g = paint(new THREE.IcosahedronGeometry(radius, 5));
  const pos = g.getAttribute('position');
  const p = new THREE.Vector3();
  for (let i = 0; i < pos.count; i++) {
    p.fromBufferAttribute(pos, i);
    const n = p.clone().normalize();
    const big = noise.n3(n.x * 1.3, n.y * 1.3, n.z * 1.3);
    const small = noise.n3(n.x * 4.1 + 7, n.y * 4.1, n.z * 4.1);
    const k = 1 + lumps * (0.75 * big + 0.35 * Math.max(0, small));
    pos.setXYZ(i, n.x * radius * k, n.y * radius * k * squash + radius * squash, n.z * radius * k);
  }
  smoothNormals(g);
  return shade(g, (q, _n, c) => {
    const t = THREE.MathUtils.clamp(q.y / (radius * squash * 2), 0, 1);
    c.setScalar(0.55 + 0.45 * t);
  });
}

/** A tapered, slightly bent trunk from y = 0 to `height`. */
export function trunk(height: number, r0: number, r1: number, bend = 0.4, seed = 1) {
  const rand = rng(seed);
  const parts: THREE.BufferGeometry[] = [];
  const steps = 6;
  const dir = rand() * Math.PI * 2;
  const at = (t: number) => new THREE.Vector3(Math.cos(dir) * bend * t * t, t * height, Math.sin(dir) * bend * t * t);
  for (let s = 0; s < steps; s++) {
    const a = at(s / steps), b = at((s + 1) / steps);
    const d = b.clone().sub(a);
    const g = new THREE.CylinderGeometry(lerp(r0, r1, (s + 1) / steps), lerp(r0, r1, s / steps), d.length() * 1.02, 10, 1, true);
    g.applyMatrix4(new THREE.Matrix4().compose(a.clone().add(b).multiplyScalar(0.5), new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize()), new THREE.Vector3(1, 1, 1)));
    parts.push(paint(g));
  }
  return smoothNormals(merge(parts));
}

// ---- Paths -----------------------------------------------------------------------------------------

/** A point along a track edge: position, outward unit normal (away from the road), and height. */
export interface EdgePoint { x: number; z: number; nx: number; nz: number; y: number }

/**
 * Sweeps a 2D profile along a path: profile points are (out, up) offsets from each path point, `out`
 * along the path's outward normal. `v` per path point gives the distance along (for stripes).
 * Returns a triangle soup with smooth normals across the profile where `smooth` is true.
 */
export function sweep(path: readonly EdgePoint[], profile: readonly [number, number][], opts: { closed?: boolean; color?: (along: number, k: number) => THREE.Color } = {}) {
  const tris: number[] = [];
  const cols: number[] = [];
  const n = path.length;
  const segs = opts.closed ? n : n - 1;
  let along = 0;
  const at = (i: number, k: number) => {
    const p = path[i % n]!;
    const [o, u] = profile[k]!;
    return [p.x + p.nx * o, p.y + u, p.z + p.nz * o];
  };
  for (let i = 0; i < segs; i++) {
    const a = path[i % n]!, b = path[(i + 1) % n]!;
    const len = Math.hypot(b.x - a.x, b.z - a.z);
    for (let k = 0; k < profile.length - 1; k++) {
      const v = [at(i, k), at(i + 1, k), at(i, k + 1), at(i + 1, k + 1)];
      const c = opts.color ? opts.color(along + len / 2, k) : new THREE.Color(1, 1, 1);
      for (const q of [v[0]!, v[2]!, v[1]!, v[1]!, v[2]!, v[3]!]) { tris.push(...q); cols.push(c.r, c.g, c.b); }
    }
    along += len;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(tris, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(cols, 3));
  g.computeVertexNormals();
  return g;
}

/** Reverses every triangle's winding (a mirrored sweep), recomputing normals. */
export function flipWinding(g: THREE.BufferGeometry) {
  for (const name of ['position', 'color']) {
    const a = g.getAttribute(name);
    if (!a) continue;
    for (let i = 0; i < a.count; i += 3) {
      const t = [a.getX(i + 1), a.getY(i + 1), a.getZ(i + 1)];
      a.setXYZ(i + 1, a.getX(i + 2), a.getY(i + 2), a.getZ(i + 2));
      a.setXYZ(i + 2, t[0]!, t[1]!, t[2]!);
    }
  }
  g.computeVertexNormals();
  return g;
}

/** Orients every triangle so its normal has a positive dot with `hint(p)` (fixes sweep winding per side). */
export function orient(g: THREE.BufferGeometry, hint: (p: THREE.Vector3) => THREE.Vector3) {
  const pos = g.getAttribute('position');
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3();
  const col = g.getAttribute('color');
  for (let i = 0; i < pos.count; i += 3) {
    a.fromBufferAttribute(pos, i); b.fromBufferAttribute(pos, i + 1); c.fromBufferAttribute(pos, i + 2);
    const f = new THREE.Vector3().subVectors(b, a).cross(new THREE.Vector3().subVectors(c, a));
    const mid = a.clone().add(b).add(c).multiplyScalar(1 / 3);
    if (f.dot(hint(mid)) < 0) {
      pos.setXYZ(i + 1, c.x, c.y, c.z); pos.setXYZ(i + 2, b.x, b.y, b.z);
      if (col) { const t = [col.getX(i + 1), col.getY(i + 1), col.getZ(i + 1)]; col.setXYZ(i + 1, col.getX(i + 2), col.getY(i + 2), col.getZ(i + 2)); col.setXYZ(i + 2, t[0]!, t[1]!, t[2]!); }
    }
  }
  g.computeVertexNormals();
  return g;
}

/**
 * A rumble kerb along a road edge: a rounded crown 5 cm high with shallow ridges every stripe, starting
 * `inset` inside the edge and ending `outset` beyond it. Colours alternate every `stripe` metres.
 * Cosmetic only: the sim's road stays flat.
 */
export function rumbleKerb(path: readonly EdgePoint[], colors: [string, string], opts: { inset?: number; outset?: number; stripe?: number; height?: number } = {}) {
  const inset = opts.inset ?? 0.35, outset = opts.outset ?? 1.0, stripe = opts.stripe ?? 1.5, height = opts.height ?? 0.05;
  // Densify to 0.25 m so the ridges and the stripe edges are crisp.
  const dense: (EdgePoint & { s: number })[] = [];
  let s = 0;
  for (let i = 0; i < path.length - 1; i++) {
    const a = path[i]!, b = path[i + 1]!;
    const len = Math.hypot(b.x - a.x, b.z - a.z);
    const steps = Math.max(1, Math.ceil(len / 0.25));
    for (let k = 0; k < steps; k++) {
      const t = k / steps;
      const nx = lerp(a.nx, b.nx, t), nz = lerp(a.nz, b.nz, t), l = Math.hypot(nx, nz) || 1;
      dense.push({ x: lerp(a.x, b.x, t), z: lerp(a.z, b.z, t), y: lerp(a.y, b.y, t), nx: nx / l, nz: nz / l, s: s + len * t });
    }
    s += len;
  }
  const last = path[path.length - 1]!;
  dense.push({ ...last, s });
  const width = inset + outset;
  const prof: [number, number][] = [];
  const N = 8;
  for (let k = 0; k <= N; k++) {
    const u = k / N;
    // A flattened sine crown with a lip: 0 at both edges.
    prof.push([-inset + u * width, height * Math.pow(Math.sin(Math.PI * Math.min(1, u * 1.15)), 0.6)]);
  }
  const ca = new THREE.Color(colors[0]), cb = new THREE.Color(colors[1]);
  const ridged = dense.map((p) => {
    const phase = (p.s / stripe) % 1;
    const ridge = 1 + 0.35 * Math.max(0, Math.cos(phase * Math.PI * 2)); // a soft bump mid-stripe
    return { ...p, ridge };
  });
  const tris: number[] = [], cols: number[] = [];
  for (let i = 0; i < ridged.length - 1; i++) {
    const a = ridged[i]!, b = ridged[i + 1]!;
    const c = Math.floor(((a.s + b.s) / 2) / stripe) % 2 === 0 ? ca : cb;
    for (let k = 0; k < prof.length - 1; k++) {
      const P = (p: typeof a, j: number) => [p.x + p.nx * prof[j]![0], p.y + prof[j]![1] * p.ridge, p.z + p.nz * prof[j]![0]];
      const q = [P(a, k), P(b, k), P(a, k + 1), P(b, k + 1)];
      for (const v of [q[0]!, q[2]!, q[1]!, q[1]!, q[2]!, q[3]!]) { tris.push(...v); cols.push(c.r, c.g, c.b); }
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(tris, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(cols, 3));
  return orient(g, () => new THREE.Vector3(0, 1, 0));
}

// ---- Terrain ---------------------------------------------------------------------------------------

/**
 * A square heightfield mesh (y = height(x, z)) centred on (cx, cz), `size` metres across in `res` cells,
 * finer near the centre: cells are spaced by a power curve so the play area gets ~1 m detail and the far
 * hills ~20 m. Vertex colours come from `color(x, z, y, slope)`.
 */
export function terrain(cx: number, cz: number, size: number, res: number, height: (x: number, z: number) => number,
  color: (x: number, z: number, y: number, slope: number) => THREE.Color, warp = 2.2) {
  const coord = (i: number) => {
    const u = (i / res) * 2 - 1; // -1..1
    return Math.sign(u) * Math.pow(Math.abs(u), warp) * size / 2;
  };
  const xs = Array.from({ length: res + 1 }, (_, i) => cx + coord(i));
  const zs = Array.from({ length: res + 1 }, (_, i) => cz + coord(i));
  const H = xs.map((x) => zs.map((z) => height(x, z)));
  const normalAt = (i: number, j: number) => {
    const i0 = Math.max(0, i - 1), i1 = Math.min(res, i + 1), j0 = Math.max(0, j - 1), j1 = Math.min(res, j + 1);
    const dx = (H[i1]![j]! - H[i0]![j]!) / (xs[i1]! - xs[i0]!);
    const dz = (H[i]![j1]! - H[i]![j0]!) / (zs[j1]! - zs[j0]!);
    return new THREE.Vector3(-dx, 1, -dz).normalize();
  };
  // Indexed: one vertex per grid point (the GLB stays small; UE welds nothing it does not need to).
  const count = (res + 1) * (res + 1);
  const pos = new Float32Array(count * 3), nor = new Float32Array(count * 3), col = new Float32Array(count * 3);
  for (let i = 0; i <= res; i++) {
    for (let j = 0; j <= res; j++) {
      const k = (i * (res + 1) + j) * 3;
      const n = normalAt(i, j), c = color(xs[i]!, zs[j]!, H[i]![j]!, 1 - n.y);
      pos[k] = xs[i]!; pos[k + 1] = H[i]![j]!; pos[k + 2] = zs[j]!;
      nor[k] = n.x; nor[k + 1] = n.y; nor[k + 2] = n.z;
      col[k] = c.r; col[k + 1] = c.g; col[k + 2] = c.b;
    }
  }
  const index = new Uint32Array(res * res * 6);
  let t = 0;
  const id = (i: number, j: number) => i * (res + 1) + j;
  for (let i = 0; i < res; i++) {
    for (let j = 0; j < res; j++) {
      // Wound so the normal faces +Y.
      index.set([id(i, j), id(i, j + 1), id(i + 1, j), id(i + 1, j), id(i, j + 1), id(i + 1, j + 1)], t);
      t += 6;
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  g.setAttribute('color', new THREE.BufferAttribute(col, 3));
  g.setIndex(new THREE.BufferAttribute(index, 1));
  return g;
}

/** Distance from (x, z) to an axis-aligned rectangle (0 inside). */
export function rectDistance(x: number, z: number, r: { minX: number; maxX: number; minZ: number; maxZ: number }) {
  const dx = Math.max(r.minX - x, 0, x - r.maxX), dz = Math.max(r.minZ - z, 0, z - r.maxZ);
  return Math.hypot(dx, dz);
}

/** A uniform grid over points for fast "distance to the nearest point" queries (centrelines, walls). */
export class PointGrid {
  private readonly cells = new Map<string, [number, number][]>();
  constructor(points: readonly [number, number][], private readonly cell = 16) {
    for (const p of points) {
      const k = this.key(Math.floor(p[0] / cell), Math.floor(p[1] / cell));
      (this.cells.get(k) ?? this.cells.set(k, []).get(k)!).push(p);
    }
  }
  private key(i: number, j: number) { return `${i},${j}`; }
  /** The distance to the nearest point, searching rings out to `limit` metres (returns limit if none). */
  nearest(x: number, z: number, limit = 200) {
    const ci = Math.floor(x / this.cell), cj = Math.floor(z / this.cell);
    let best = limit * limit;
    const reach = Math.ceil(limit / this.cell);
    for (let ring = 0; ring <= reach; ring++) {
      if (ring > 0 && (ring - 1) * this.cell > Math.sqrt(best)) break;
      for (let i = ci - ring; i <= ci + ring; i++) {
        for (let j = cj - ring; j <= cj + ring; j++) {
          if (Math.max(Math.abs(i - ci), Math.abs(j - cj)) !== ring) continue;
          for (const p of this.cells.get(this.key(i, j)) ?? []) best = Math.min(best, (p[0] - x) ** 2 + (p[1] - z) ** 2);
        }
      }
    }
    return Math.sqrt(best);
  }
}
