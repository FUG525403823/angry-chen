#!/usr/bin/env node
import { createRequire } from 'node:module';

const require = createRequire(new URL('../packages/server/package.json', import.meta.url));
const wsModule = await import('ws').catch(() => require('ws'));
const WebSocket = wsModule.WebSocket ?? wsModule.default;

const shared = await import('../packages/shared/src/index.ts');
const {
  LIMITS,
  OPCODE,
  NEW_ROOM_CODE,
  createCommand,
  createSnapshotMirror,
  decodeError,
  decodePong,
  decodeSnapshot,
  decodeWelcome,
  encodeChat,
  encodeCommand,
  encodeJoin,
  encodePing,
} = shared;

const argv = process.argv.slice(2);
const args = new Map();
for (let i = 0; i < argv.length; i += 1) {
  const token = argv[i] ?? '';
  if (!token.startsWith('--')) continue;
  const key = token.slice(2);
  const next = argv[i + 1];
  if (next !== undefined && !next.startsWith('--')) {
    args.set(key, next);
    i += 1;
  } else {
    args.set(key, 'true');
  }
}

const url = args.get('url') ?? 'ws://127.0.0.1:8787';
const clientCount = Number(args.get('clients') ?? 2);
const durationSec = Number(args.get('duration') ?? 5);
const flood = args.has('flood');
const badFrame = args.has('bad-frame');
const chatty = args.has('chat');
const forcedRoom = args.get('room');
const stepMs = 33;
const scratch = new Uint8Array(LIMITS.maxFrameBytes);

function createState(index) {
  return {
    index,
    socket: undefined,
    mirror: createSnapshotMirror(),
    welcome: undefined,
    name: 'probe' + String(index + 1),
    roomCode: undefined,
    snapshots: 0,
    snapshotBytes: 0,
    snapshotMaxBytes: 0,
    records: 0,
    events: 0,
    matchStates: 0,
    errors: new Map(),
    rtt: [],
    cmdSeq: 0,
    firstTick: 0,
    oversizedSent: false,
    lastTick: 0,
    closed: false,
    closeCode: 0,
    pongs: 0,
    malformedSent: 0,
    floodSent: 0,
  };
}

function sendJoin(state, code) {
  const size = encodeJoin({ protocolVersion: 1, name: state.name, roomCode: code }, scratch);
  state.socket.send(scratch.subarray(0, size));
}

const MOD32 = 4294967296;

function clientClockMs() {
  return Date.now() % MOD32;
}

function elapsedMs(startMs) {
  return (clientClockMs() - startMs + MOD32) % MOD32;
}

function sendPing(state) {
  const size = encodePing({ clientTimeMs: clientClockMs(), lastRecvTick: state.lastTick }, scratch);
  state.socket.send(scratch.subarray(0, size));
}

function handleFrame(state, frame) {
  const opcode = frame[0];
  if (opcode === OPCODE.welcome) {
    const decoded = decodeWelcome(frame);
    if (decoded.ok) {
      state.welcome = decoded.value;
      state.roomCode = decoded.value.roomCode;
    }
    return;
  }
  if (opcode === OPCODE.snapshot) {
    const decoded = decodeSnapshot(frame, state.mirror);
    if (!decoded.ok) throw new Error('snapshot decode failed: ' + decoded.reason);
    state.snapshots += 1;
    state.snapshotBytes += frame.length;
    if (frame.length > state.snapshotMaxBytes) state.snapshotMaxBytes = frame.length;
    state.records += decoded.value.recordCount;
    state.lastTick = decoded.value.tick;
    if (state.firstTick === 0) state.firstTick = decoded.value.tick;
    return;
  }
  if (opcode === OPCODE.pong) {
    const decoded = decodePong(frame);
    if (!decoded.ok) return;
    state.pongs += 1;
    state.rtt.push(elapsedMs(decoded.value.clientTimeMs));
    return;
  }
  if (opcode === OPCODE.event) {
    state.events += 1;
    return;
  }
  if (opcode === OPCODE.matchState) {
    state.matchStates += 1;
    return;
  }
  if (opcode === OPCODE.error) {
    const decoded = decodeError(frame);
    if (!decoded.ok) return;
    const key = String(decoded.value.code);
    state.errors.set(key, (state.errors.get(key) ?? 0) + 1);
  }
}

function connect(state, code) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(url);
    socket.binaryType = 'arraybuffer';
    state.socket = socket;
    socket.on('open', () => {
      sendJoin(state, code);
      resolve(state);
    });
    socket.on('message', (data) => {
      try {
        handleFrame(state, new Uint8Array(data));
      } catch (error) {
        console.error('client ' + state.name + ' frame handling failed: ' + String(error));
        process.exitCode = 1;
      }
    });
    socket.on('close', (code_) => {
      state.closed = true;
      state.closeCode = code_;
    });
    socket.on('error', (error) => reject(error));
  });
}

const states = [];
for (let i = 0; i < clientCount; i += 1) states.push(createState(i));
await connect(states[0], forcedRoom ?? NEW_ROOM_CODE);
await new Promise((resolve) => setTimeout(resolve, 150));
const roomCode = states[0].roomCode;
for (let i = 1; i < states.length; i += 1) {
  await connect(states[i], forcedRoom ?? roomCode ?? NEW_ROOM_CODE);
  await new Promise((resolve) => setTimeout(resolve, 60));
}
console.log(
  'probe: room=' +
    String(roomCode) +
    ' clients=' +
    String(states.length) +
    ' mode=' +
    (flood ? 'flood' : badFrame ? 'bad-frame' : 'normal'),
);

const startedAt = Date.now();
for (const state of states) {
  state.snapshots = 0;
  state.snapshotBytes = 0;
  state.snapshotMaxBytes = 0;
  state.records = 0;
  state.events = 0;
  state.matchStates = 0;
  state.firstTick = 0;
  state.rtt.length = 0;
}
let lastPingAt = 0;
const timer = setInterval(() => {
  const now = Date.now();
  for (const state of states) {
    if (state.closed || state.socket === undefined) continue;
    if (flood) {
      for (let i = 0; i < 17; i += 1) {
        sendPing(state);
        state.floodSent += 1;
      }
      continue;
    }
    if (badFrame) {
      if (state.index === states.length - 1) {
        if (!state.oversizedSent) {
          state.socket.send(new Uint8Array(LIMITS.maxFrameBytes + 1024));
          state.oversizedSent = true;
        }
        continue;
      }
      if (state.malformedSent < 6) {
        state.socket.send(new Uint8Array([0xff, 1, 2, 3]));
        state.socket.send(new Uint8Array([OPCODE.inputCmd, 9]));
        state.malformedSent += 2;
      }
      continue;
    }
    const t = (now - startedAt) / 1000 + state.index * 0.7;
    const command = createCommand();
    command.seq = state.cmdSeq;
    state.cmdSeq = (state.cmdSeq + 1) & 0xffff;
    command.tick = state.lastTick;
    command.moveX = 0.6 + Math.sin(t) * 0.4;
    command.moveY = Math.cos(t * 1.3) * 0.7;
    command.yaw = t * 0.9;
    command.pitch = Math.sin(t * 0.5) * 0.3;
    const size = encodeCommand(command, scratch);
    state.socket.send(scratch.subarray(0, size));
    if (chatty && Math.random() < 0.02) {
      const chatSize = encodeChat('hello from ' + state.name, scratch);
      state.socket.send(scratch.subarray(0, chatSize));
    }
  }
  if (now - lastPingAt >= 500) {
    lastPingAt = now;
    for (const state of states) if (!state.closed && state.socket !== undefined) sendPing(state);
  }
}, stepMs);

await new Promise((resolve) => setTimeout(resolve, durationSec * 1000));
const elapsedSec = (Date.now() - startedAt) / 1000;
clearInterval(timer);
await new Promise((resolve) => setTimeout(resolve, 250));

function percentile(values, p) {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * p))] ?? 0;
}

const totals = { snapshots: 0, bytes: 0, records: 0, errors: 0 };
const clientReports = states.map((state) => {
  const avgBytes = state.snapshots > 0 ? state.snapshotBytes / state.snapshots : 0;
  const avgRecords = state.snapshots > 0 ? state.records / state.snapshots : 0;
  const errorTotal = [...state.errors.values()].reduce((sum, value) => sum + value, 0);
  totals.snapshots += state.snapshots;
  totals.bytes += state.snapshotBytes;
  totals.records += state.records;
  totals.errors += errorTotal;
  return {
    name: state.name,
    pid: state.welcome?.pid ?? 0,
    room: state.roomCode ?? '',
    snapshots: state.snapshots,
    firstTick: state.firstTick,
    lastTick: state.lastTick,
    oversizedSent: state.oversizedSent,
    snapshotRate: Number((state.snapshots / elapsedSec).toFixed(2)),
    avgBytes: Number(avgBytes.toFixed(1)),
    maxBytes: state.snapshotMaxBytes,
    avgRecords: Number(avgRecords.toFixed(2)),
    entities: state.mirror.ids.length,
    rttAvgMs: Number(
      (state.rtt.reduce((sum, value) => sum + value, 0) / Math.max(1, state.rtt.length)).toFixed(1),
    ),
    rttP95Ms: percentile(state.rtt, 0.95),
    errors: Object.fromEntries(state.errors),
    closed: state.closed,
    closeCode: state.closeCode,
    mirrorBytes: state.mirror.bytes.length,
  };
});

const summary = {
  url,
  room: roomCode,
  mode: flood ? 'flood' : badFrame ? 'bad-frame' : 'normal',
  durationSec,
  elapsedSec: Number(elapsedSec.toFixed(2)),
  clients: clientReports,
  aggregate: {
    snapshots: totals.snapshots,
    avgBytes: Number((totals.bytes / Math.max(1, totals.snapshots)).toFixed(1)),
    avgRecords: Number((totals.records / Math.max(1, totals.snapshots)).toFixed(2)),
    errorFrames: totals.errors,
  },
};
console.log(JSON.stringify(summary, null, 2));
for (const report of clientReports) {
  console.log(
    'probe: ' +
      report.name +
      ' ticks=' +
      String(report.firstTick) +
      '..' +
      String(report.lastTick) +
      ' entities=' +
      String(report.entities) +
      ' oversizedSent=' +
      String(report.oversizedSent),
  );
}

const failures = [];
for (const report of clientReports) {
  if (flood) {
    if (report.errors['4'] === undefined)
      failures.push(report.name + ': expected rate limit error code 4');
    if (!report.closed) failures.push(report.name + ': expected the flood connection to be closed');
    continue;
  }
  if (badFrame) {
    if (report.oversizedSent) {
      if (!report.closed)
        failures.push(report.name + ': oversized frame should close the connection');
      continue;
    }
    if (report.errors['5'] === undefined)
      failures.push(report.name + ': expected malformed frame error code 5');
    if (report.closed) failures.push(report.name + ': connection should survive malformed frames');
    if (report.firstTick < 1)
      failures.push(report.name + ': no snapshot received after malformed frames');
    continue;
  }
  if (report.snapshots === 0) failures.push(report.name + ': no snapshots received');
  if (report.snapshots > 0 && (report.snapshotRate < 19 || report.snapshotRate > 21))
    failures.push(report.name + ': snapshot rate out of 19-21/s: ' + String(report.snapshotRate));
  if (report.avgBytes > 1200)
    failures.push(report.name + ': snapshot average too large ' + String(report.avgBytes));
  if (report.entities < 1) failures.push(report.name + ': snapshot mirror is empty');
  if (report.rttP95Ms > 250)
    failures.push(report.name + ': rtt p95 too high ' + String(report.rttP95Ms));
}
if (failures.length > 0) {
  console.error('probe FAILED:');
  for (const failure of failures) console.error(' - ' + failure);
  process.exitCode = 1;
} else {
  console.log('probe OK');
}
for (const state of states) {
  const socket = state.socket;
  if (socket !== undefined && socket.readyState === 1) socket.close();
}
setTimeout(() => process.exit(process.exitCode ?? 0), 150);
