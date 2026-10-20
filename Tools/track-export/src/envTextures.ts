/**
 * Generated environment textures shared by every track (out/shared/textures, imported to
 * /Game/Tracks/Shared/Textures): all procedural and seeded, so they are original and identical per export.
 *
 * - env_clouds: a tileable cloud density map (R = G = B = density, linear) for M_TrackSky's cloud layer.
 *   Billowy fractal noise on a 4D torus, so it wraps seamlessly in both directions.
 * - env_waves_normal: a tileable wave normal map (DirectX green) for M_TrackWater: a sum of directional
 *   swells whose wave vectors are whole numbers of cycles per tile, plus fine ripple noise.
 */
import { createCanvas } from '@napi-rs/canvas';
import { Noise } from './envKit';
import { savePng, type TextureEntry } from './scenery';

const TAU = Math.PI * 2;

/** Fractal noise sampled on a 4D torus: seamless over u, v in [0, 1). */
function torusFbm(noise: Noise, u: number, v: number, freq: number, octaves: number, billow: boolean) {
  let sum = 0, amp = 1, norm = 0, f = freq;
  for (let o = 0; o < octaves; o++) {
    const r = f / TAU;
    // SimplexNoise.noise4d through Noise: the 4D torus embedding of (u, v).
    const n = noise.n4(Math.cos(TAU * u) * r, Math.sin(TAU * u) * r, Math.cos(TAU * v) * r + 31.7 * o, Math.sin(TAU * v) * r);
    sum += amp * (billow ? 1 - Math.abs(n) * 2 : n);
    norm += amp; amp *= 0.5; f *= 2;
  }
  return sum / norm;
}

function cloudMap(size: number) {
  const noise = new Noise(2026);
  const c = createCanvas(size, size);
  const ctx = c.getContext('2d');
  const img = ctx.createImageData(size, size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const u = x / size, v = y / size;
      // Big soft masses (warped), with billowy detail riding on them.
      const warp = 0.08 * torusFbm(noise, u + 0.3, v, 2, 3, false);
      const mass = torusFbm(noise, u + warp, v - warp, 3, 4, false) * 0.5 + 0.5;
      const detail = torusFbm(noise, u, v, 9, 4, true) * 0.5 + 0.5;
      const d = Math.min(1, Math.max(0, mass * 0.78 + detail * 0.32 - 0.05));
      const k = (y * size + x) * 4;
      const byte = Math.round(d * 255);
      img.data[k] = byte; img.data[k + 1] = byte; img.data[k + 2] = byte; img.data[k + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  return c;
}

function waveNormalMap(size: number) {
  const noise = new Noise(77);
  // Swells: (cycles across u, cycles across v, amplitude, phase), whole cycles so the tile wraps.
  const swells: [number, number, number, number][] = [[3, 1, 1, 0.3], [-2, 3, 0.8, 1.7], [5, -2, 0.5, 2.9], [1, 6, 0.35, 4.1], [-7, -3, 0.25, 0.9], [9, 4, 0.18, 5.3]];
  const height = (u: number, v: number) => {
    let h = 0;
    for (const [a, b, amp, ph] of swells) h += amp * Math.sin(TAU * (a * u + b * v) + ph);
    return h + 0.6 * torusFbm(noise, u, v, 12, 3, false);
  };
  const c = createCanvas(size, size);
  const ctx = c.getContext('2d');
  const img = ctx.createImageData(size, size);
  const e = 1 / size, strength = 0.035;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const u = x / size, v = y / size;
      const dx = (height(u + e, v) - height(u - e, v)) / (2 * e) * strength / size * 64;
      const dy = (height(u, v + e) - height(u, v - e)) / (2 * e) * strength / size * 64;
      const l = Math.hypot(dx, dy, 1);
      const k = (y * size + x) * 4;
      img.data[k] = Math.round((-dx / l * 0.5 + 0.5) * 255);
      img.data[k + 1] = Math.round((dy / l * 0.5 + 0.5) * 255); // DirectX: green points down the image
      img.data[k + 2] = Math.round((1 / l * 0.5 + 0.5) * 255);
      img.data[k + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  return c;
}

export function exportEnvTextures(texDir: string, ueDir: string): TextureEntry[] {
  return [
    savePng(texDir, ueDir, 'env_clouds', cloudMap(1024)),
    savePng(texDir, ueDir, 'env_waves_normal', waveNormalMap(512)),
  ];
}
