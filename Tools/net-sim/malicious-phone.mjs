// A MALICIOUS PHONE for the Party Mode pentest (pentest-net branch). LOCAL ONLY.
//
// It joins a real local room (through Tools/net-sim/run-party.ps1 on :1999 or your patched clone) as an ordinary
// seated controller, then fires hostile payloads. It is used to confirm what HOLDS UP: the room server gates
// host-only events and control messages, and the UE host re-validates / clamps everything a seated phone can send.
//
//   node Tools/net-sim/malicious-phone.mjs --url "<join url>" [--attacks all]
//   node Tools/net-sim/malicious-phone.mjs --from-log Saved/Logs/NetE2E.log
//
// Attacks (comma list, or "all"):
//   host-only     send host-only game events (pad:prompt, darts:turn, darts:result) -> expect server HOST_ONLY error
//   control       send host-only control messages (complete-game, start-game, kick-player) -> expect HOST_ONLY error
//   feedback      send play:feedback as a phone -> relayed with our fromPlayerId; phone/host consumers must ignore it
//   nan           darts:release / bowling:release with NaN/Inf/huge power & aim -> host must clamp, never crash
//   spoof         game events naming another playerId in data -> host keys off server fromPlayerId, not our data
//   oversize      a ~1 MB game-event payload and a deeply nested one -> expect MALFORMED / size refusal
//   flood         hammer control + sensor frames -> expect RATE_LIMITED
//   badframe      malformed binary sensor frames (truncated, oversized, bad flags, player-index sizes)
import { readFileSync, existsSync } from 'node:fs';
import { randomUUID } from 'node:crypto';

const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 && i + 1 < args.length ? args[i + 1] : d; };
const attacks = opt('attacks', 'all').split(',');
const on = (a) => attacks.includes('all') || attacks.includes(a);
const log = (...p) => console.log(`[malicious-phone +${((Date.now() - t0) / 1000).toFixed(2)}s]`, ...p);
const t0 = Date.now();

async function joinUrl() {
  const direct = opt('url', '');
  if (direct) return direct;
  const file = opt('from-log', '');
  if (!file) throw new Error('pass --url <join url> or --from-log <UE log>');
  for (let i = 0; i < 240; i++) {
    if (existsSync(file)) {
      const lines = readFileSync(file, 'utf8').split(/\r?\n/).filter((l) => l.includes('JoinUrl='));
      if (lines.length) return lines[lines.length - 1].split('JoinUrl=')[1].trim();
    }
    await new Promise((r) => setTimeout(r, 500));
  }
  throw new Error(`no JoinUrl= in ${file}`);
}

const url = new URL(await joinUrl());
const code = url.pathname.split('/').pop();
const frag = new URLSearchParams(url.hash.slice(1));
const token = frag.get('t') || '';
const party = opt('party', 'localhost:1999');
const socketUrl = `ws://${party}/parties/main/r-${code.toLowerCase()}`;
log(`joining ${socketUrl}`);
const ws = new WebSocket(socketUrl);
const send = (m) => ws.send(JSON.stringify({ v: 2, ...m }));
let playerId = null;

function badFrames() {
  const mk = (len, fill) => { const b = new Uint8Array(len); if (fill) fill(b); return b; };
  // truncated, oversized, bad type, bad flags, quaternion-flag-without-size, player-index sizes (must be rejected).
  ws.send(mk(10));                                                   // truncated
  ws.send(mk(4096, (b) => b.fill(0xff)));                           // oversized junk
  ws.send(mk(30, (b) => { b[0] = 0x99; }));                         // bad frame type
  ws.send(mk(30, (b) => { b[0] = 0x02; b[1] = 0xff; }));            // bad flags
  ws.send(mk(30, (b) => { b[0] = 0x02; b[1] = 1 << 5; }));          // quaternion flag, 30 bytes
  ws.send(mk(31, (b) => { b[0] = 0x02; }));                         // 31-byte (player-index) size from a phone
  ws.send(mk(39, (b) => { b[0] = 0x02; b[1] = 1 << 5; }));          // 39-byte (player-index) size from a phone
  log('badframe: sent 7 malformed binary frames (host must drop all)');
}

function fire() {
  if (on('host-only')) {
    for (const event of ['pad:prompt', 'darts:turn', 'darts:result', 'darts:aim']) {
      send({ t: 'game-event', event, data: { playerId: 'p1', title: 'OWNED' } });
    }
    log('host-only: sent pad:prompt / darts:turn / darts:result / darts:aim (expect HOST_ONLY errors)');
  }
  if (on('control')) {
    send({ t: 'complete-game', scores: [{ playerId: playerId || 'p1', score: 1 }] });
    send({ t: 'start-game' });
    send({ t: 'kick-player', playerId: 'p2' });
    send({ t: 'configure-room', gameId: 'kart' });
    log('control: sent complete-game / start-game / kick-player / configure-room (expect HOST_ONLY errors)');
  }
  if (on('feedback')) {
    send({ t: 'game-event', event: 'play:feedback', data: { gameId: 'kart', playerId: 'p1', title: 'FAKE', detail: 'x', tone: 'success' } });
    log('feedback: sent play:feedback (consumers must ignore events carrying our fromPlayerId)');
  }
  if (on('nan')) {
    for (const p of [NaN, Infinity, -Infinity, 1e308, -1e308]) {
      send({ t: 'game-event', event: 'darts:release', data: { power: p, aimX: p, aimY: p, spinDegPerSec: p } });
      send({ t: 'game-event', event: 'bowling:release', data: { power: p, aim: p, curve: p, loft: p } });
    }
    log('nan: sent darts/bowling release with NaN/Inf/huge (host must clamp, never crash)');
  }
  if (on('spoof')) {
    send({ t: 'game-event', event: 'kart-input', data: { playerId: 'p2', fromPlayerId: 'p2', isAccelerating: true } });
    send({ t: 'game-event', event: 'darts:release', data: { playerId: 'p2', power: 1 } });
    log('spoof: named p2 in data (host keys off server fromPlayerId, not payload)');
  }
  if (on('oversize')) {
    send({ t: 'game-event', event: 'kart-input', data: { blob: 'A'.repeat(1_000_000) } });
    let deep = {}; let c = deep; for (let i = 0; i < 2000; i++) { c.a = {}; c = c.a; }
    send({ t: 'game-event', event: 'kart-input', data: deep });
    log('oversize: sent ~1 MB and deeply-nested payloads (expect MALFORMED / size refusal)');
  }
  if (on('flood')) {
    for (let i = 0; i < 500; i++) send({ t: 'game-event', event: 'kart-input', data: { isDrifting: i % 2 === 0 } });
    log('flood: sent 500 control messages in a burst (expect RATE_LIMITED)');
  }
  if (on('badframe')) badFrames();
}

ws.addEventListener('open', () => { log('open; hello'); send({ t: 'hello', role: 'controller', client: 'malicious-phone/0.1', ...(token ? { joinToken: token } : {}) }); });
ws.addEventListener('close', (e) => log(`closed code=${e.code} reason=${e.reason}`));
ws.addEventListener('message', (ev) => {
  if (typeof ev.data !== 'string') return;
  const m = JSON.parse(ev.data);
  if (m.t === 'ping') return send({ t: 'pong' });
  if (m.t === 'error') return log(`server error <- ${m.code}: ${m.message}`);
  if (m.t === 'welcome') {
    if (!m.playerId) { log('welcome; join-player'); send({ t: 'join-player', name: 'Mallory', identity: randomUUID(), avatar: { kartPaint: 'pink', vehicle: 'kart', face: 'kai' } }); }
    else { playerId = m.playerId; log(`seated ${playerId}; calibrate + ready, then attack`); send({ t: 'calibrate', alpha: 0, beta: 0, gamma: 0, screenOrientation: 1 }); send({ t: 'set-ready', ready: true }); setTimeout(fire, 500); }
  }
});
setTimeout(() => { log('done'); ws.close(); process.exit(0); }, Number(opt('seconds', '12')) * 1000);
