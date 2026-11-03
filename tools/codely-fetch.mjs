// Public official archives only. Immutable bytes are checked before extraction; no credentials.
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
    const args = entry.archive.kind === 'tgz' ? ['-tzf', archive] : ['-tf', archive];
    const listing = execFileSync('tar', args, { encoding: 'utf8' });
    verifyArchivePaths(listing);
    execFileSync('tar', [entry.archive.kind === 'tgz' ? '-xzf' : '-xf', archive, '-C', extracted]);
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
