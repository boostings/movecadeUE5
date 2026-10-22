// Bundles src/export.ts against the movecade-v2 sources and runs it.
// The movecade repo is only read. Its own node_modules are not needed: bare
// imports from its files resolve to this folder's node_modules.
import { build } from 'esbuild';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { processVioletMesh } from './src/violetMesh.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(here, '../..');
// Default: <Documents>/Projects/movecade/movecade-v2 next to <Documents>/Unreal Projects/MyProject. Only that one
// place: this script runs code from it, so it must not pick up a folder of that name further up the disk (C:\ is
// writable by every local user). Git worktrees (MyProject/.claude/worktrees/...) resolve it from the main checkout.
function mainCheckout() {
  try {
    const common = execFileSync('git', ['-C', projectDir, 'rev-parse', '--path-format=absolute', '--git-common-dir'], { encoding: 'utf8' }).trim();
    return path.dirname(common);
  } catch {
    return projectDir;
  }
}
function findMovecade() {
  return path.resolve(mainCheckout(), '../../Projects/movecade/movecade-v2');
}
const movecade = path.resolve(process.env.MOVECADE_ROOT ?? findMovecade());
const appSrc = path.join(movecade, 'app/src');
if (!existsSync(appSrc)) {
  console.error(`movecade-v2 not found at ${movecade}. Set MOVECADE_ROOT.`);
  process.exit(1);
}

// The same aliases as movecade-v2/vite.config.ts, limited to what the track modules use.
const aliases = {
  name: 'movecade-aliases',
  setup(b) {
    b.onResolve({ filter: /^@app\// }, (a) => ({ path: resolveTs(path.join(appSrc, a.path.slice(5))) }));
    b.onResolve({ filter: /^@legacy\// }, (a) => ({ path: resolveTs(path.join(appSrc, 'legacy', a.path.slice(8))) }));
    b.onResolve({ filter: /^@\// }, (a) => ({ path: resolveTs(path.join(appSrc, 'legacy', a.path.slice(2))) }));
    // catalog.ts builds a zustand store for React renderers; the exporter never reads it, and zustand pulls in React.
    b.onResolve({ filter: /^zustand$/ }, () => ({ path: 'zustand', namespace: 'stub' }));
    b.onLoad({ filter: /^zustand$/, namespace: 'stub' }, () => ({
      contents: 'export const create = (init) => { let s; const set = (p) => { s = { ...s, ...p }; }; s = init(set, () => s); return () => s; };',
      loader: 'js',
    }));
  },
};

// The scenery modules are React components around plain three.js builders. These stubs let Node load
// them without a DOM or WebGL; nothing they stand in for is used by the export (Docs/Port/03 §4.2).
const STUBS = {
  react: 'export const useMemo = (f) => f(); export const useRef = (v = null) => ({ current: v }); export const useEffect = () => {}; export const useLayoutEffect = () => {}; export const useCallback = (f) => f; export const useState = (v) => [v, () => {}]; export default {};',
  'react/jsx-runtime': 'export const jsx = () => null; export const jsxs = () => null; export const Fragment = null;',
  '@react-three/fiber': 'export const useFrame = () => {}; export const useThree = () => ({});',
  '@react-three/drei': 'export const useTexture = () => ({}); export const useGLTF = () => ({ nodes: {}, materials: {} });',
  quality: 'export const useQualityController = () => ({ tier: "high", profile: {} }); export const useQualityVersion = () => {};',
  trackSurfaces: 'export const KART_SURFACE_URLS = {}; export const KART_SURFACE_SETS = {}; export const groupKartSurfaceTextures = () => ({});',
  worldSurface: 'export const applyWorldSurface = (m) => m;',
  // A canvas texture is drawn into @napi-rs/canvas; the export saves `image` as a PNG.
  canvasTexture: `import { createCanvas } from '@napi-rs/canvas';
    export function canvasTexture(width, height, draw) {
      const image = createCanvas(width, height);
      draw(image.getContext('2d'), width, height);
      const v2 = { set() {} };
      return { image, wrapS: 0, wrapT: 0, repeat: v2, offset: v2, dispose() {}, colorSpace: '' };
    }`,
};
const stubs = {
  name: 'movecade-stubs',
  setup(b) {
    for (const bare of ['react', 'react/jsx-runtime', '@react-three/fiber', '@react-three/drei']) {
      b.onResolve({ filter: new RegExp(`^${bare}$`) }, () => ({ path: bare, namespace: 'movecade-stub' }));
    }
    b.onResolve({ filter: /render\/quality$|scene\/trackSurfaces$|scene\/worldSurface$|render\/canvasTexture$/ }, (a) => ({
      path: a.path.split('/').pop(),
      namespace: 'movecade-stub',
    }));
    b.onLoad({ filter: /.*/, namespace: 'movecade-stub' }, (a) => ({ contents: STUBS[a.path], loader: 'js', resolveDir: here }));
    // Expose the builders these components keep private, in memory only (the file on disk is untouched).
    const expose = { 'HarborTrack.tsx': 'buildHarborScenery', 'VioletTrackside.tsx': 'BANNER_FACES, bannerTexture' };
    b.onLoad({ filter: /(HarborTrack|VioletTrackside)\.tsx$/ }, async (a) => {
      const source = await readFile(a.path, 'utf8');
      return { contents: `${source}\nexport { ${expose[path.basename(a.path)]} };\n`, loader: 'tsx' };
    });
  },
};
function resolveTs(p) {
  for (const ext of ['', '.ts', '.tsx', '.js', '.jsx', '/index.ts']) if (existsSync(p + ext)) return p + ext;
  return p;
}

const outDir = path.join(here, '.cache');
mkdirSync(outDir, { recursive: true });
const outfile = path.join(outDir, 'export.bundle.mjs');
await build({
  entryPoints: [path.join(here, 'src/export.ts')],
  bundle: true,
  platform: 'node',
  format: 'esm',
  target: 'node20',
  outfile,
  nodePaths: [path.join(here, 'node_modules')],
  plugins: [aliases, stubs],
  jsx: 'automatic',
  external: ['@napi-rs/canvas', 'sharp', 'draco3dgltf'],
  logLevel: 'warning',
});

let commit = 'unknown';
let dirty = false;
try {
  commit = execFileSync('git', ['-C', movecade, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim();
  dirty = execFileSync('git', ['-C', movecade, 'status', '--porcelain', '--', 'app/src'], { encoding: 'utf8' }).trim() !== '';
} catch {}

const { exportTracks } = await import(pathToFileURL(outfile).href);
const outTracks = path.join(projectDir, 'Data/Tracks');
// Generated binaries (decoded GLBs, prefab meshes, canvas PNGs) go to out/, which git ignores;
// Tools/ue/import_track_assets.py imports them into Content/Tracks.
const outAssets = path.join(here, 'out');
const violetMesh = async (samples, rebaseY) => {
  const res = await processVioletMesh({
    glbPath: path.join(movecade, 'app/public/models/violet-loop.glb'),
    outDir: path.join(outAssets, 'violet'),
    samples,
    rebaseY,
  });
  console.log(`decoded ${path.relative(projectDir, res.glb)}; walls: ${res.walls.sweep.length / 6} sweep, ${res.walls.probe.length / 6} probe segments; dirt cells ${res.dirtGrid.dirtCells}`);
  return res;
};
const written = await exportTracks({ outDir: outTracks, commit: dirty ? `${commit}-dirty` : commit, movecadeRoot: movecade, violetMesh, assetsDir: outAssets });
for (const f of written) console.log(`wrote ${path.relative(projectDir, f)}`);
