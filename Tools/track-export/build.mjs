// Bundles src/export.ts against the movecade-v2 sources and runs it.
// The movecade repo is only read. Its own node_modules are not needed: bare
// imports from its files resolve to this folder's node_modules.
import { build } from 'esbuild';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const projectDir = path.resolve(here, '../..');
const movecade = path.resolve(process.env.MOVECADE_ROOT ?? path.join(projectDir, '../../Projects/movecade/movecade-v2'));
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
    b.onLoad({ filter: /.*/, namespace: 'stub' }, () => ({
      contents: 'export const create = (init) => { let s; const set = (p) => { s = { ...s, ...p }; }; s = init(set, () => s); return () => s; };',
      loader: 'js',
    }));
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
  plugins: [aliases],
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
const written = await exportTracks({ outDir: outTracks, commit: dirty ? `${commit}-dirty` : commit, movecadeRoot: movecade });
for (const f of written) console.log(`wrote ${path.relative(projectDir, f)}`);
