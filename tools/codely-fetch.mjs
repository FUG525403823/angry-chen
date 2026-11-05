// Public official archives only. Immutable bytes are checked before extraction; no credentials.
// Extraction: tgz uses tar; zip uses bsdtar when present, otherwise unzip (GNU tar cannot read zip).
// Usage: node tools/codely-fetch.mjs <staging-root> [--install-missing <workspace>]
import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync, existsSync, cpSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { loadRecord, verifyPackage } from './codely-dependencies.mjs';
const workspace = resolve(dirname(fileURLToPath(import.meta.url)), '..');
export function verifyArchive(data, archive, name = '') {
  if (!['tgz', 'zip'].includes(archive.kind) || !/^https:\/\//.test(archive.url)) throw Error('Invalid official archive specification');
  const digest = createHash('sha256').update(data).digest('hex');
  if (digest !== archive.sha256) throw Error('Official archive digest changed: ' + name);
  return digest;
}
export function verifyArchivePaths(listing) {
  if (!listing.trim() || listing.split(/\r?\n/).some(p => /(^|[\\/])\.\.([\\/]|$)|^[\\/]|^[A-Za-z]:|[\x00-\x08\x0b-\x1f]/.test(p))) throw Error('Unsafe archive path');
}
const LISTING_MAX_BUFFER = 16 * 1024 * 1024;
function runArchiveTool(command, args) {
  try {
    return execFileSync(command, args, { encoding: 'utf8', maxBuffer: LISTING_MAX_BUFFER, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
  } catch (error) {
    const detail = String(error.stderr ?? '').trim() || error.message;
    throw Error(`Archive tool ${command} failed: ${detail}`);
  }
}
let zipCommand;
function zipTool() {
  if (zipCommand) return zipCommand;
  try {
    const version = execFileSync('tar', ['--version'], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'], windowsHide: true });
    zipCommand = /bsdtar|libarchive/i.test(version) ? 'tar' : 'unzip';
  } catch { zipCommand = 'unzip'; }
  return zipCommand;
}
export function listArchive(kind, archive) {
  if (kind === 'tgz') return runArchiveTool('tar', ['-tzf', archive]);
  if (kind !== 'zip') throw Error('Unsupported archive kind: ' + kind);
  return zipTool() === 'tar' ? runArchiveTool('tar', ['-tf', archive]) : runArchiveTool('unzip', ['-Z1', archive]);
}
export function extractArchive(kind, archive, destination) {
  if (kind === 'tgz') {
    runArchiveTool('tar', ['-xzf', archive, '-C', destination]);
    return;
  }
  if (kind !== 'zip') throw Error('Unsupported archive kind: ' + kind);
  if (zipTool() === 'tar') runArchiveTool('tar', ['-xf', archive, '-C', destination]);
  else runArchiveTool('unzip', ['-q', '-o', archive, '-d', destination]);
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) try {
  if (!process.argv[2] || (process.argv[3] && process.argv[3] !== '--install-missing') || process.argv.length > 5) throw Error('Usage: codely-fetch.mjs <staging-root> [--install-missing <workspace>]');
  const destination = resolve(process.argv[2] ?? 'build/codely-fetch');
  if (existsSync(destination)) throw Error('Staging directory already exists; refusing overwrite');
  mkdirSync(destination, { recursive: true });
  const record = loadRecord(workspace);
  for (const [i, entry] of record.packages.entries()) {
    const response = await fetch(entry.archive.url, { signal: AbortSignal.timeout(180000) });
    if (!response.ok) throw Error('Official archive download HTTP ' + response.status);
    const data = Buffer.from(await response.arrayBuffer());
    const digest = verifyArchive(data, entry.archive, entry.name);
    const archive = join(destination, `${i}.${entry.archive.kind}`), extracted = join(destination, `${i}-unpacked`);
    writeFileSync(archive, data); mkdirSync(extracted);
    const listing = listArchive(entry.archive.kind, archive);
    verifyArchivePaths(listing);
    extractArchive(entry.archive.kind, archive, extracted);
    const packageRoot = join(extracted, entry.archive.subdirectory);
    verifyPackage(extracted, { ...entry, path: entry.archive.subdirectory });
    if (process.argv[3] === '--install-missing') {
      const targetRoot = resolve(process.argv[4] ?? workspace), target = join(targetRoot, entry.path);
      if (existsSync(target)) {
        verifyPackage(targetRoot, entry); // Never overwrite another person's package.
      } else { mkdirSync(join(target, '..'), { recursive: true }); cpSync(packageRoot, target, { recursive: true, errorOnExist: true, force: false }); verifyPackage(targetRoot, entry); }
    }
    console.log('FETCH VERIFIED ' + entry.name + '@' + entry.version + ' archive=' + digest);
  }
} catch (error) { console.error('FETCH FAIL ' + error.message); process.exitCode = 1; }
