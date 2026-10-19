// Writes Data/Net/protocol-fixtures.json for the MovecadeKart.Net.* automation tests.
//
// Every value comes from movecade-v2's own code, run from the local clone in Tools/.cache/movecade
// (never the read-only checkout): the protocol package (sensor frames, join URLs, room codes,
// client message validation) and the qrcode-generator package that the web lobby's QR uses.
//
//   node Tools/net-sim/make-fixtures.mjs [--clone <path>]
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..', '..');
const argClone = process.argv.indexOf('--clone');
const clone = resolve(argClone > 0 ? process.argv[argClone + 1] : join(root, 'Tools', '.cache', 'movecade'));

const esbuild = await import(pathToFileURL(join(clone, 'node_modules', 'esbuild', 'lib', 'main.js')).href);
const bundle = join(root, 'Tools', '.cache', 'net-sim', 'protocol.mjs');
mkdirSync(dirname(bundle), { recursive: true });
await esbuild.build({
  entryPoints: [join(clone, 'protocol', 'src', 'index.ts')],
  bundle: true,
  format: 'esm',
  platform: 'node',
  outfile: bundle,
  logLevel: 'warning',
});
const P = await import(pathToFileURL(bundle).href);
const qrcode = (await import(pathToFileURL(join(clone, 'node_modules', 'qrcode-generator', 'dist', 'qrcode.mjs')).href)).default;

const hex = (buffer) => Buffer.from(buffer).toString('hex');

// ── Sensor frames: the exact frame from protocol/src/protocol.test.ts 'sensor frames'. ──
const frame = {
  screenOrientation: P.ScreenOrientation.LandscapeLeft,
  sequence: 42,
  flags: P.SENSOR_FLAG_CALIBRATED | P.SENSOR_FLAG_HAS_GYRO | P.SENSOR_FLAG_HAS_GRAVITY,
  alpha: 123.45,
  beta: -12.34,
  gamma: 0.5,
  gyro: { x: 10.5, y: -200.25, z: 0 },
  accel: { x: 1.234, y: -2.5, z: 0.001 },
  gravity: { x: 0, y: -9.81, z: 0.5 },
  dtMs: 16,
};
const quaternion = P.normalizeQuaternion({ x: 0.1, y: 0.2, z: 0.3, w: 0.9 });
const sensorCase = (name, buffer) => ({ name, hex: hex(buffer), decoded: P.decodeSensorFrame(buffer) });
const badCase = (name, buffer) => ({ name, hex: hex(buffer), error: P.validateSensorFrame(buffer) });
const mutate = (buffer, offset, value) => {
  const copy = buffer.slice(0);
  new DataView(copy).setUint8(offset, value);
  return copy;
};
const base = P.encodeSensorFrame(frame);
const steering = P.encodeSensorFrame({ ...frame, gamma: -37.25, sequence: 255 });
const sensor = {
  valid: [
    sensorCase('phone 30 bytes', base),
    sensorCase('relayed 31 bytes, seat index 3', P.appendPlayerIndex(base, 3)),
    sensorCase('relayed 31 bytes, seat index 0, gamma -37.25', P.appendPlayerIndex(steering, 0)),
    sensorCase('phone 38 bytes with quaternion', P.encodeSensorFrame({ ...frame, quaternion })),
    sensorCase('relayed 39 bytes with quaternion, seat index 2', P.appendPlayerIndex(P.encodeSensorFrame({ ...frame, quaternion }), 2)),
    sensorCase('clamped alpha', P.encodeSensorFrame({ ...frame, alpha: 99999 })),
  ],
  invalid: [
    badCase('15 bytes', new ArrayBuffer(15)),
    badCase('32 bytes', (() => { const b = new ArrayBuffer(32); new DataView(b).setUint8(0, 2); return b; })()),
    badCase('frame type 1', mutate(base, 0, 0x01)),
    badCase('flags 0xff', mutate(base, 1, 0xff)),
    badCase('orientation 7', mutate(base, 2, 7)),
    badCase('quaternion flag on a 31-byte frame', mutate(P.appendPlayerIndex(base, 1), 1, frame.flags | P.SENSOR_FLAG_HAS_QUATERNION)),
  ],
};

// ── Join URL (roomCode.ts buildJoinUrl, as LobbyScreen builds the QR payload). ──
const joinUrls = [
  { base: 'http://localhost:5173', code: 'ABCDEF', token: 'tok_123-XY', url: P.buildJoinUrl('http://localhost:5173', { v: 2, code: 'ABCDEF', token: 'tok_123-XY' }) },
  { base: 'https://movecade.com', code: 'Q7MZ2K', token: '', url: P.buildJoinUrl('https://movecade.com', { v: 2, code: 'Q7MZ2K' }) },
  { base: 'http://192.168.1.20:5173/', code: 'HJKMNP', token: 'a.b~c', url: P.buildJoinUrl('http://192.168.1.20:5173/', { v: 2, code: 'HJKMNP', token: 'a.b~c' }) },
];

// ── Room codes: generateRoomCode with fixed random bytes (rejection sampling at 248). ──
const roomCodes = [
  [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
  [255, 250, 248, 247, 30, 31, 62, 100, 200, 1, 2, 3],
  [249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 249, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18],
].map((bytes) => {
  let offset = 0;
  const code = P.generateRoomCode((length) => {
    const out = new Uint8Array(length);
    for (let i = 0; i < length; i += 1) out[i] = bytes[(offset + i) % bytes.length];
    offset += length;
    return out;
  });
  return { bytes, code, roomId: P.roomIdForCode(code) };
});

// ── Host → server messages. Each fixture is what encodeClientMessage produces, and parseClientMessage
//    must accept it (the server's own validator), so a C++ builder that matches is server-valid. ──
const clientMessages = {
  hello: { t: 'hello', role: 'host', client: 'ue5-host/0.1.0', gameId: 'kart', identity: '0f8fad5b-d9cb-469f-a165-70867728950e' },
  helloReclaim: { t: 'hello', role: 'host', client: 'ue5-host/0.1.0', hostToken: 'host-token_1', gameId: 'kart', identity: '0f8fad5b-d9cb-469f-a165-70867728950e' },
  pong: { t: 'pong' },
  stateAck: { t: 'state-ack', stateVersion: 42 },
  configureMap: { t: 'configure-room', mapId: 'sunset-harbor' },
  lobbyCountdown: { t: 'set-countdown', kind: 'lobby', durationMs: 5000 },
  lobbyCountdownCancel: { t: 'set-countdown', kind: 'lobby', durationMs: 0 },
  matchCountdown: { t: 'set-countdown', kind: 'match', durationMs: 5000 },
  matchGo: { t: 'set-countdown', kind: 'match', durationMs: 0 },
  startGame: { t: 'start-game' },
  advanceTurn: { t: 'advance-turn', activePlayerId: null, phase: 'active' },
  scoreUpdate: { t: 'score-update', scores: [{ playerId: 'p2', score: 61234 }, { playerId: 'p1', score: 65000 }] },
  completeGame: { t: 'complete-game', scores: [{ playerId: 'p2', score: 61234 }, { playerId: 'p1', score: 65000 }] },
  restartGame: { t: 'restart-game', actionId: 'ue-restart-1' },
  returnToLobby: { t: 'return-to-lobby', actionId: 'ue-lobby-2' },
  kickPlayer: { t: 'kick-player', playerId: 'p3' },
  dropPlayer: { t: 'drop-player', playerId: 'p3' },
  closeRoom: { t: 'close-room' },
  haptic: {
    t: 'game-event',
    event: 'collision',
    data: { type: 'collision', targetPlayerId: 'p2', intensity: 0.45, collisionType: 'barrier', sustained: false },
  },
};
const clientFixtures = {};
for (const [name, message] of Object.entries(clientMessages)) {
  const json = P.encodeClientMessage(message);
  const parsed = P.parseClientMessage(json);
  if (!parsed.ok) throw new Error(`${name} rejected by parseClientMessage: ${parsed.error}`);
  clientFixtures[name] = json;
}
// Not in the protocol union; engine.ts handleScoresAck parses it ahead of the parser.
clientFixtures.scoresAck = JSON.stringify({ v: P.PROTOCOL_VERSION, t: 'scores-ack', matchNumber: 3 });

// Phone → host relays the host must read (legacyBridge.ts kart-input; README §4).
const serverMessages = {
  kartInputGas: { v: 2, t: 'game-event', event: 'kart-input', data: { isAccelerating: true }, fromPlayerId: 'p1' },
  kartInputPedalsOff: { v: 2, t: 'game-event', event: 'kart-input', data: { isAccelerating: false, isReversing: false, isDrifting: false }, fromPlayerId: 'p1' },
  kartInputBot: { v: 2, t: 'game-event', event: 'kart-input', data: { isAccelerating: true, isDrifting: true, steeringInput: -1.7 }, fromPlayerId: 'p2' },
  kartInputItem: { v: 2, t: 'game-event', event: 'kart-input', data: { useItem: true }, fromPlayerId: 'p2' },
  ping: { v: 2, t: 'ping', serverTime: 1700000000000 },
  welcome: {
    v: 2, t: 'welcome', sessionId: 's1', role: 'host', playerId: null, resumeToken: null,
    hostToken: 'Hst0k3n_abc', joinToken: 'J0in-T0ken', resumed: false,
    state: {
      roomId: 'r-abcdef', joinCode: 'ABCDEF', gameId: 'kart', selectedMapId: 'violet-loop', stateVersion: 3,
      hostSessionId: 's1', hostMissing: false, hostGraceRemainingMs: 0, closeOnHostLeave: false,
      players: [], spectators: [],
      game: { status: 'lobby', activePlayerId: null, turnNumber: 1, roundNumber: 1, phase: 'waiting', matchNumber: 1, startedAt: null, pauseReason: null, pausedForPlayerId: null, pausedAt: null, snapshot: null, snapshotAt: null },
      serverTime: 1700000000000,
    },
  },
  state: {
    v: 2, t: 'state', reason: 'player-joined',
    state: {
      roomId: 'r-abcdef', joinCode: 'ABCDEF', gameId: 'kart', selectedMapId: 'sunset-harbor', stateVersion: 9,
      hostSessionId: 's1', hostMissing: false, hostGraceRemainingMs: 0, closeOnHostLeave: false,
      players: [
        { id: 'p1', name: 'Jimmy', emoji: '🏎️', avatar: { ...P.DEFAULT_AVATAR_APPEARANCE, kartPaint: 'cyan', vehicle: 'kart', face: 'leo', body: 'male' }, seat: 1, score: 0, ready: true, calibrated: true, connected: true, eliminated: false, graceRemainingMs: 0, screen: 'ready' },
        { id: 'p3', name: 'Ana', avatar: P.DEFAULT_AVATAR_APPEARANCE, seat: 3, score: 0, ready: false, calibrated: false, connected: false, eliminated: false, disconnectedAt: 1699999990000, graceRemainingMs: 125000, screen: 'reconnecting', bot: false },
      ],
      spectators: [],
      game: { status: 'paused', activePlayerId: null, turnNumber: 1, roundNumber: 1, phase: 'active', matchNumber: 2, startedAt: 1699999900000, pauseReason: 'player-disconnected', pausedForPlayerId: 'p3', pausedAt: 1699999990000, snapshot: null, snapshotAt: null },
      countdown: { kind: 'lobby', endsAt: 1700000004000 },
      serverTime: 1700000000000,
    },
  },
};

// ── QR reference matrices from qrcode-generator (the library behind the web lobby's react-qr-code). ──
const qrSamples = [
  'Movecade',
  P.buildJoinUrl('http://localhost:5173', { v: 2, code: 'ABCDEF', token: 'Tk_0123456789abcdefghijKLMNOP' }),
  'https://movecade.com/join/Q7MZ2K#v=2&t=' + 'x'.repeat(60),
  'Movecade kart racer: phones are the steering wheels, the TV is the room host. ' +
    'Scan the code, hold the phone sideways like a wheel, and drift through Sunset Harbor. ' + '0123456789'.repeat(3),
];
const qr = qrSamples.map((text) => {
  const code = qrcode(0, 'M');
  code.addData(text, 'Byte');
  code.make();
  const size = code.getModuleCount();
  const rows = [];
  for (let r = 0; r < size; r += 1) {
    let row = '';
    for (let c = 0; c < size; c += 1) row += code.isDark(r, c) ? '1' : '0';
    rows.push(row);
  }
  return { text, bytes: Buffer.byteLength(text, 'utf8'), version: (size - 17) / 4, size, rows };
});

const out = {
  generatedBy: 'Tools/net-sim/make-fixtures.mjs from movecade-v2 protocol/src and qrcode-generator',
  sensor,
  joinUrls,
  roomCodes,
  clientMessages: clientFixtures,
  serverMessages,
  qr,
};
const target = join(root, 'Data', 'Net', 'protocol-fixtures.json');
mkdirSync(dirname(target), { recursive: true });
writeFileSync(target, JSON.stringify(out, null, 2) + '\n');
console.log(`wrote ${target}`);
