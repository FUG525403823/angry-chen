#!/usr/bin/env node
// ADR-019: the C++ exporter computes all outputs; this driver only serializes and audits.
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync, openSync, closeSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { serializeFixture } from './export-fixtures.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const dir = path.join(root, 'docs/evidence/fixtures');
const legacy = path.join(dir, 'legacy/targeting-v1');
const names = ['rifle-burst-hit-120t','shotgun-spread-60t','downed-revive-140t','sheep-grunt-ai-600t',
  'sheep-ram-charge-300t','sheep-elite-bolt-300t','sheep-king-phases-900t','wave-director-1to5-1200t',
  'snapshot-roundtrip-240t','rng-streams-600t'];
let check = false;
let buildDir = path.join(root, 'server/build');
let exporter;
let config;
const args = process.argv.slice(2);
for (let i = 0; i < args.length; ++i) {
  const arg = args[i];
  if (arg === '--check') { check = true; continue; }
  if (!['--exporter', '--build-dir', '--config'].includes(arg)) throw new Error('Unknown option: ' + arg);
  const value = args[++i];
  if (!value || value.startsWith('--')) throw new Error('Missing value for ' + arg);
  if (arg === '--exporter') {
    if (!path.isAbsolute(value)) throw new Error('--exporter requires an absolute path');
    exporter = value;
  } else if (arg === '--build-dir') buildDir = path.resolve(value);
  else {
    if (!['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'].includes(value)) throw new Error('Unsupported --config: ' + value);
    config = value;
  }
}
const binary = 'ac_export_targeting' + (process.platform === 'win32' ? '.exe' : '');
const candidates = exporter ? [exporter] : config ? [path.join(buildDir, config, binary), path.join(buildDir, binary)] :
  [path.join(buildDir, binary), ...['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'].map(name => path.join(buildDir, name, binary))];
const available = candidates.filter(candidate => existsSync(candidate));
if (!exporter && !config && available.length > 1) {
  throw new Error('Multiple exporters found; select --config or --exporter: ' + available.join(', '));
}
exporter = available[0];
if (!exporter) {
  throw new Error('Exporter binary not found. Looked for: ' + candidates.join(', ') +
    '\nBuild it with: cmake -S server -B "' + buildDir + '" && cmake --build "' + buildDir +
    '" --target ac_export_targeting --config ' + (config ?? 'Release') +
    '\nOr pass --exporter <absolute-path> to an existing binary.');
}
const reports = path.join(buildDir, 'reports/targeting-fixtures');
mkdirSync(reports, { recursive: true });
const sha = data => createHash('sha256').update(data).digest('hex');
const rendered = [];
for (const name of names) {
  const active = path.join(dir, name + '.json');
  const archived = path.join(legacy, name + '.json');
  if (!existsSync(archived)) {
    if (check) throw new Error('Missing legacy archive: ' + name);
    mkdirSync(legacy, { recursive: true });
    writeFileSync(archived, readFileSync(active), { flag: 'wx' });
  }
  const original = readFileSync(archived);
  const fixture = JSON.parse(original);
  const output = path.join(reports, 'targeting-' + name + '.jsonl');
  const fd = openSync(output, 'w');
  let result;
  try {
    result = spawnSync(exporter, [archived.replaceAll('\\', '/')],
      { cwd: root, stdio: ['ignore', fd, 'inherit'] });
  } finally { closeSync(fd); }
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error('Authoritative export failed: ' + name);
  const rows = readFileSync(output, 'utf8').trim().split('\n').map(line => JSON.parse(line));
  if (rows.length !== fixture.ticks || rows.some((row, i) => row.tick !== i + 1)) throw new Error('Incomplete export: ' + name);
  const firstDifference = rows.find(row => row.hash !== fixture.hashChain[row.tick - 1]);
  if (!firstDifference) throw new Error('Unexpected unchanged affected fixture: ' + name);
  fixture.hashChain = rows.map(row => row.hash);
  fixture.keyframes = fixture.keyframes.map(frame => {
    const { tick, entities, events, rngState } = rows[frame.tick - 1];
    return { tick, entities, events, rng: rngState };
  });
  fixture.snapshot = fixture.snapshot.map(frame => {
    const snapshot = rows[frame.tick - 1].snapshot;
    if (!snapshot) throw new Error('Missing snapshot: ' + name);
    return snapshot;
  });
  const text = serializeFixture(fixture);
  if (Buffer.byteLength(text) > 65536) throw new Error('Fixture size gate: ' + name);
  if (check && readFileSync(active, 'utf8') !== text) throw new Error('Non-idempotent output: ' + name);
  rendered.push({ active, text });
  console.log(name + ' firstHashDiff=' + firstDifference.tick + ' legacySHA256=' + sha(original) +
    ' activeSHA256=' + sha(text) + ' finalChain=' + fixture.hashChain.at(-1));
}
if (rendered.reduce((sum, item) => sum + Buffer.byteLength(item.text), 0) > 524288) throw new Error('Batch size gate');
if (!check) for (const item of rendered) writeFileSync(item.active, item.text);
console.log((check ? 'check' : 'write') + ' ok: ' + names.length + '/' + names.length);
