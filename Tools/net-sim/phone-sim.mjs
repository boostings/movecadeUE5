// A simulated Movecade phone controller for testing the UE room host without a phone.
//
// It speaks the same controller protocol as the web phone (protocol/README.md §3-§6, KartController.tsx):
// hello -> join-player -> calibrate -> set-ready, answers server pings, and once its screen goes
// `active` it holds GAS (game-event kart-input) and streams 30-byte sensor frames at 30 Hz whose
// wheel-mode gamma sweeps left and right, plus one ITEM press. No dependencies (Node 22 WebSocket).
//
//   node Tools/net-sim/phone-sim.mjs --url "http://localhost:5173/join/ABCDEF#v=2&t=..." [options]
//   node Tools/net-sim/phone-sim.mjs --from-log Saved/Logs/KartGame.log [options]
//
// Options: --party localhost:1999  --name Sim1  --paint cyan  --seconds 30  --sweep 60 (deg)
//          --period 4 (s per sweep)  --hz 30  --game kart|bowling|darts|samurai
//
// --game bowling: on its turn (state.game.activePlayerId) the phone streams a swing (accel frames) and sends
// game-event bowling:release, as BowlingController.tsx does, every few seconds until the turn moves on.
// --game darts: it streams gravity frames (the host's reticle) and, while darts:turn names it with darts left,
// sends darts:release (thumb-lift) every 1.6 s, aimed near the board's top half.
// --game samurai: it streams 38-byte frames at 100 Hz with the calibrated attitude as a quaternion: the katana held
// up, then every 0.9 s a 0.3 s downward cut at a random yaw, gyro high through the sweep (the host's intent gate).
import { readFileSync, existsSync } from 'node:fs';
import { randomUUID } from 'node:crypto';

const args = process.argv.slice(2);
const opt = (name, fallback) => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : fallback;
};
const party = opt('party', 'localhost:1999');
const name = opt('name', 'Sim1');
const paint = opt('paint', 'cyan');
const seconds = Number(opt('seconds', '30'));
const sweepDeg = Number(opt('sweep', '60'));
const periodS = Number(opt('period', '4'));
const hz = Number(opt('hz', '30'));
const game = opt('game', 'kart');
const log = (...parts) => console.log(`[phone-sim ${name} +${((Date.now() - t0) / 1000).toFixed(2)}s]`, ...parts);
const t0 = Date.now();

async function joinUrl() {
  const direct = opt('url', '');
  if (direct) return direct;
  const file = opt('from-log', '');
  if (!file) throw new Error('pass --url <join url> or --from-log <UE log>');
  // URoomHostSubsystem logs "RoomHost: JoinUrl=<url>" once the room is open.
  for (let tries = 0; tries < 240; tries += 1) {
    if (existsSync(file)) {
      const lines = readFileSync(file, 'utf8').split(/\r?\n/).filter((line) => line.includes('JoinUrl='));
      if (lines.length > 0) return lines[lines.length - 1].split('JoinUrl=')[1].trim();
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error(`no JoinUrl= line in ${file} after 120 s`);
}

// protocol/src/sensor.ts encodeSensorFrame, 30-byte form.
function sensorFrame(sequence, gamma, dtMs) {
  const buffer = new ArrayBuffer(30);
  const view = new DataView(buffer);
  const i16 = (v) => Math.max(-32768, Math.min(32767, Math.round(v)));
  const CALIBRATED = 1, HAS_GRAVITY = 1 << 4;
  view.setUint8(0, 0x02);
  view.setUint8(1, CALIBRATED | HAS_GRAVITY);
  view.setUint8(2, 1); // landscape-left, as a phone held like a wheel
  view.setUint8(3, sequence & 255);
  view.setInt16(4, 0, true);
  view.setInt16(6, 0, true);
  view.setInt16(8, i16(gamma * 100), true);
  // Gravity in the screen plane, matching the wheel angle (lib/wheel.ts atan2(gRight, -gUp)).
  const rad = (gamma * Math.PI) / 180;
  view.setInt16(22, i16(9.81 * Math.sin(rad) * 1000), true);
  view.setInt16(24, i16(-9.81 * Math.cos(rad) * 1000), true);
  view.setInt16(26, 0, true);
  view.setUint16(28, dtMs, true);
  return buffer;
}

const url = await joinUrl();
const parsed = new URL(url);
const code = parsed.pathname.split('/').filter(Boolean).pop().toUpperCase();
const token = new URLSearchParams(parsed.hash.replace(/^#/, '')).get('t') ?? undefined;
const socketUrl = `ws://${party}/parties/main/r-${code.toLowerCase()}`;
log(`join url ${url} -> ${socketUrl}`);

const ws = new WebSocket(socketUrl);
ws.binaryType = 'arraybuffer';
const send = (message) => ws.send(JSON.stringify({ ...message, v: 2 }));
let playerId = null;
let readySent = false;
let streaming = null;
let sequence = 0;
let lastFrameAt = Date.now();
let stats = { frames: 0, events: 0 };

// A bowling swing: gravity down the phone, a forward push that peaks and fades over ~0.7 s.
function swingFrame(sequence, push, dtMs) {
  const buffer = new ArrayBuffer(30);
  const view = new DataView(buffer);
  const i16 = (v) => Math.max(-32768, Math.min(32767, Math.round(v)));
  const CALIBRATED = 1, HAS_ACCEL = 8, HAS_GRAVITY = 16;
  view.setUint8(0, 0x02);
  view.setUint8(1, CALIBRATED | HAS_ACCEL | HAS_GRAVITY);
  view.setUint8(2, 0);
  view.setUint8(3, sequence & 255);
  view.setInt16(16, 0, true);
  view.setInt16(18, i16(push * 1000), true);
  view.setInt16(20, i16(push * 300), true);
  view.setInt16(22, 0, true);
  view.setInt16(24, i16(-9.81 * 1000), true);
  view.setInt16(26, 0, true);
  view.setUint16(28, dtMs, true);
  return buffer;
}

let myTurn = false;
let bowling = null;
let throws = 0;
function bowl() {
  if (!myTurn) return;
  const started = Date.now();
  let sent = 0;
  const swing = setInterval(() => {
    const t = (Date.now() - started) / 1000;
    const due = Math.floor(t * 60) + 1 - sent;
    for (let i = 0; i < Math.min(due, 2); i += 1) {
      const push = 22 * Math.sin(Math.PI * Math.min(1, t / 0.7));
      ws.send(swingFrame(sequence, push, 16));
      sequence = (sequence + 1) & 255;
      stats.frames += 1;
      sent += 1;
    }
    if (t >= 0.7) {
      clearInterval(swing);
      const release = { aim: (Math.random() - 0.5) * 0.12, power: 0.72 + Math.random() * 0.2, curve: 0.12 + Math.random() * 0.1,
        loft: 0.08, confidence: 1, source: 'accel', hookUnavailable: false };
      send({ t: 'game-event', event: 'bowling:release', data: release });
      stats.events += 1;
      throws += 1;
      log(`bowling:release #${throws} ${JSON.stringify(release)}`);
    }
  }, 5);
}
// The calibrated attitude for a blade pitched by `pitch` (about x) then turned by `yaw` (about y), radians.
function bladeQuaternion(pitch, yaw) {
  const qx = [Math.sin(pitch / 2), 0, 0, Math.cos(pitch / 2)];
  const qy = [0, Math.sin(yaw / 2), 0, Math.cos(yaw / 2)];
  // qy * qx
  return [
    qy[3] * qx[0] + qy[0] * qx[3] + qy[1] * qx[2] - qy[2] * qx[1],
    qy[3] * qx[1] - qy[0] * qx[2] + qy[1] * qx[3] + qy[2] * qx[0],
    qy[3] * qx[2] + qy[0] * qx[1] - qy[1] * qx[0] + qy[2] * qx[3],
    qy[3] * qx[3] - qy[0] * qx[0] - qy[1] * qx[1] - qy[2] * qx[2],
  ];
}
function attitudeFrame(sequence, q, gyroDegPerSec, dtMs) {
  const buffer = new ArrayBuffer(38);
  const view = new DataView(buffer);
  const i16 = (v) => Math.max(-32768, Math.min(32767, Math.round(v)));
  const CALIBRATED = 1, HAS_GYRO = 4, HAS_ACCEL = 8, HAS_GRAVITY = 16, HAS_QUATERNION = 32;
  view.setUint8(0, 0x02);
  view.setUint8(1, CALIBRATED | HAS_GYRO | HAS_ACCEL | HAS_GRAVITY | HAS_QUATERNION);
  view.setUint8(2, 0);
  view.setUint8(3, sequence & 255);
  view.setInt16(10, i16(gyroDegPerSec * 20), true);
  view.setInt16(24, i16(-9.81 * 1000), true);
  view.setUint16(28, dtMs, true);
  for (let i = 0; i < 4; i += 1) view.setInt16(30 + i * 2, i16(q[i] * 16384), true);
  return buffer;
}
let samuraiTimer = null;
function startSamurai() {
  if (samuraiTimer) return;
  log('samurai: streaming the katana at 100 Hz');
  const started = Date.now();
  let sent = 0;
  let cutAt = 1.0;
  let yaw = 0;
  samuraiTimer = setInterval(() => {
    const t = (Date.now() - started) / 1000;
    const due = Math.floor(t * 100) + 1 - sent;
    for (let i = 0; i < Math.min(due, 3); i += 1) {
      const ft = (sent + 1) / 100;
      if (ft > cutAt + 0.3) {
        cutAt = ft + 0.6;
        yaw = (Math.random() - 0.5) * 0.7;
        throws += 1;
      }
      const k = Math.max(0, Math.min(1, (ft - cutAt) / 0.3));
      const cutting = ft >= cutAt && ft <= cutAt + 0.3;
      const pitch = cutting ? 0.5 - 2.2 * k : 0.25;
      ws.send(attitudeFrame(sequence, bladeQuaternion(pitch, yaw), cutting ? 420 : 0, 10));
      sequence = (sequence + 1) & 255;
      stats.frames += 1;
      sent += 1;
    }
  }, 4);
}

let dartsTimer = null;
let dartsLeft = 0;
let dartsMine = false;
function dartsTick() {
  if (!dartsMine || dartsLeft <= 0) return;
  for (let i = 0; i < 4; i += 1) {
    ws.send(swingFrame(sequence, 0, 16));
    sequence = (sequence + 1) & 255;
    stats.frames += 1;
  }
  const release = { aimX: (Math.random() - 0.5) * 0.3, aimY: 0.25 + (Math.random() - 0.5) * 0.3, power: 0.55 + (Math.random() - 0.5) * 0.12,
    spinDegPerSec: (Math.random() - 0.5) * 60, rollDegPerSec: 0, confidence: 0.9, steadiness: 0.85, peakMs2: 18, durationMs: 160, source: 'accel' };
  send({ t: 'game-event', event: 'darts:release', data: release });
  stats.events += 1;
  throws += 1;
  log(`darts:release #${throws} aim (${release.aimX.toFixed(2)}, ${release.aimY.toFixed(2)}) power ${release.power.toFixed(2)}`);
}

function setTurn(active) {
  if (active === myTurn) return;
  myTurn = active;
  log(active ? 'my turn: swinging every 5 s' : 'turn moved on');
  clearInterval(bowling);
  bowling = null;
  if (active) {
    setTimeout(bowl, 1500);
    bowling = setInterval(bowl, 5000);
  }
}

function stopDriving() {
  if (!streaming) return;
  clearInterval(streaming);
  streaming = null;
  log('screen left active: stopped streaming');
}

function startDriving() {
  if (streaming || game !== 'kart') return;
  log('screen active: GAS down, streaming sensor frames');
  send({ t: 'game-event', event: 'kart-input', data: { isAccelerating: true } });
  stats.events += 1;
  const started = Date.now();
  let sent = 0;
  let itemSent = false;
  let lastReport = 0;
  // Windows timers tick at ~15.6 ms, so pace by the clock: send whatever frames are due (at most 2 per tick).
  streaming = setInterval(() => {
    const now = Date.now();
    const t = (now - started) / 1000;
    const due = Math.floor(t * hz) + 1 - sent;
    if (due <= 0) return;
    for (let i = 0; i < Math.min(due, 2); i += 1) {
      const gamma = sweepDeg * Math.sin((2 * Math.PI * t) / periodS);
      ws.send(sensorFrame(sequence, gamma, now - lastFrameAt));
      sequence = (sequence + 1) & 255;
      lastFrameAt = now;
      stats.frames += 1;
      sent += 1;
    }
    const gamma = sweepDeg * Math.sin((2 * Math.PI * t) / periodS);
    if (now - lastReport >= 1000) {
      lastReport = now;
      log(`frame seq=${sequence} gamma=${gamma.toFixed(2)} (frames sent ${stats.frames})`);
    }
    if (!itemSent && t > 3) {
      itemSent = true;
      send({ t: 'game-event', event: 'kart-input', data: { useItem: true } });
      stats.events += 1;
      log('ITEM pressed');
    }
  }, 5);
}

ws.addEventListener('open', () => {
  log('socket open, hello');
  send({ t: 'hello', role: 'controller', client: 'ue-net-sim/0.1.0', ...(token ? { joinToken: token } : {}) });
});
ws.addEventListener('message', (event) => {
  if (typeof event.data !== 'string') return;
  const m = JSON.parse(event.data);
  switch (m.t) {
    case 'ping':
      send({ t: 'pong' });
      break;
    case 'welcome':
      if (!m.playerId) {
        log(`welcome (session ${m.sessionId}); join-player`);
        send({ t: 'join-player', name, identity: randomUUID(), avatar: { kartPaint: paint, vehicle: 'kart', face: 'kai' } });
      } else {
        playerId = m.playerId;
        log(`seated as ${playerId}; calibrate`);
        send({ t: 'calibrate', alpha: 0, beta: 0, gamma: 0, screenOrientation: 1 });
      }
      break;
    case 'screen':
      log(`screen -> ${m.screen}`);
      if (m.screen === 'ready' && !readySent) {
        readySent = true;
        send({ t: 'set-ready', ready: true });
        log('set-ready');
      }
      if (m.screen !== 'ready') readySent = false;
      if (m.screen === 'active') startDriving();
      else stopDriving();
      break;
    case 'state':
      if (m.state?.countdown) log(`room countdown ${m.state.countdown.kind} ends in ${m.state.countdown.endsAt - m.state.serverTime} ms`);
      if (m.state?.game?.status === 'in-progress') startDriving();
      if (game === 'samurai' && m.state?.game?.status === 'in-progress') startSamurai();
      if (game === 'bowling') setTurn(m.state?.game?.status === 'in-progress' && !!playerId && m.state?.game?.activePlayerId === playerId);
      break;
    case 'game-event':
      if (m.event === 'darts:turn') {
        const mine = m.data?.activePlayerId === playerId && m.data?.phase === 'playing';
        dartsLeft = m.data?.dartsLeft ?? 0;
        if (mine !== dartsMine) {
          dartsMine = mine;
          log(mine ? `darts: my visit (${dartsLeft} darts)` : 'darts: visit over');
          clearInterval(dartsTimer);
          dartsTimer = mine ? setInterval(dartsTick, 1600) : null;
        }
        break;
      }
      if (m.event === 'darts:aim') break;
      if (m.event !== 'kart-input') log(`game-event ${m.event} ${JSON.stringify(m.data ?? null)} from ${m.fromPlayerId}`);
      break;
    case 'error':
      log(`error ${m.code}: ${m.message}${m.fatal ? ' (fatal)' : ''}`);
      break;
    default:
      break;
  }
});
ws.addEventListener('close', (event) => {
  log(`socket closed ${event.code} ${event.reason}`);
  clearInterval(streaming);
  process.exit(0);
});

setTimeout(() => {
  log(`done: ${stats.frames} sensor frames, ${stats.events} events, ${throws} throws/cuts; leaving`);
  clearInterval(streaming);
  if (ws.readyState === WebSocket.OPEN) {
    send({ t: 'game-event', event: 'kart-input', data: { isAccelerating: false, isReversing: false, isDrifting: false } });
    setTimeout(() => ws.close(1000, 'sim done'), 200);
  }
  setTimeout(() => process.exit(0), 1000);
}, seconds * 1000);
