// Exact development-tool admission. No wildcard exception and no vendor code is executed.
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, lstatSync, realpathSync } from 'node:fs';
import { resolve, join, relative, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

export const recordPath = 'tools/codely-admission.json';
export function treeFingerprint(root, optionalMetadata = {}) {
  if (lstatSync(root).isSymbolicLink() || !lstatSync(root).isDirectory()) throw Error('Dependency root must be a real directory');
  const files = [];
  function walk(dir) {
    for (const name of readdirSync(dir)) {
      const path = join(dir, name), stat = lstatSync(path);
      if (stat.isSymbolicLink()) throw Error('Dependency symlink forbidden: ' + path);
      if (stat.isDirectory()) walk(path);
      else if (stat.isFile()) {
        const key = relative(root, path).split(sep).join('/');
        if (Object.hasOwn(optionalMetadata, key)) {
          if (!key.endsWith('.meta') || createHash('sha256').update(readFileSync(path)).digest('hex') !== optionalMetadata[key]) throw Error('Generated metadata changed: ' + key);
        } else files.push(key);
      }
      else throw Error('Unsupported dependency file: ' + path);
    }
  }
  walk(root); files.sort();
  const hash = createHash('sha256');
  for (const file of files) hash.update(file + '\0').update(readFileSync(join(root, file))).update('\0');
  return { files: files.length, sha256: hash.digest('hex') };
}
export function loadRecord(root) { return JSON.parse(readFileSync(join(root, recordPath), 'utf8')); }
export function verifyPackage(root, entry) {
  const path = resolve(root, entry.path);
  if (!path.startsWith(resolve(root) + sep)) throw Error('Dependency path escapes workspace');
  if (!realpathSync(path).startsWith(realpathSync(root) + sep)) throw Error('Dependency real path escapes workspace');
  const metadata = JSON.parse(readFileSync(join(path, 'package.json'), 'utf8'));
  if (metadata.name !== entry.name || metadata.version !== entry.version) throw Error('Package identity/version mismatch: ' + entry.name);
  const actual = treeFingerprint(path, entry.optionalGeneratedMetadata);
  if (actual.files !== entry.files || actual.sha256 !== entry.treeSha256) throw Error('Package content fingerprint mismatch: ' + entry.name);
  return actual;
}
export function verifyAdmission(root, manifest, lock) {
  const record = loadRecord(root);
  if (record.schema !== 1 || record.scope !== 'development-only-excluded-from-player') throw Error('Invalid admission scope/schema');
  const admitted = new Set();
  for (const entry of record.packages) {
    if (manifest.dependencies?.[entry.name] !== entry.manifestValue) throw Error('Manifest source/version mismatch: ' + entry.name);
    const locked = lock.dependencies?.[entry.name];
    if (!locked || locked.source !== entry.lockSource || locked.version !== entry.manifestValue || (entry.registry && locked.url !== entry.registry)) throw Error('Lock source/version mismatch: ' + entry.name);
    if (JSON.stringify(Object.entries(locked.dependencies ?? {}).sort()) !== JSON.stringify(Object.entries(entry.dependencies).sort())) throw Error('Dependency closure changed: ' + entry.name);
    verifyPackage(root, entry); admitted.add(entry.name);
  }
  for (const [name, spec] of Object.entries(record.additionalLock)) {
    const actual = lock.dependencies?.[name];
    if (!actual || actual.version !== spec.version || actual.source !== 'registry' || actual.url !== spec.registry || Object.keys(actual.dependencies ?? {}).length) throw Error('Transitive dependency mismatch: ' + name);
  }
  return admitted;
}
// Actual player stage audit: names plus hashes of every vendor file, including renamed native files.
export function auditRelease(root, stage) {
  if (!stage || lstatSync(stage).isSymbolicLink() || !lstatSync(stage).isDirectory()) throw Error('Release stage must be an existing real directory');
  const record = loadRecord(root), vendorHashes = new Set(), names = new Set(record.forbiddenPlayerNames.map(n => n.toLowerCase()));
  for (const entry of record.packages) {
    verifyPackage(root, entry);
    const pkg = join(root, entry.path);
    function scan(dir) { for (const e of readdirSync(dir, { withFileTypes: true })) { const p = join(dir, e.name); if (e.isDirectory()) scan(p); else if (e.isFile() && !e.name.endsWith('.meta')) vendorHashes.add(createHash('sha256').update(readFileSync(p)).digest('hex')); } }
    scan(pkg);
  }
  const leaks = []; let files = 0;
  function scan(dir) { for (const e of readdirSync(dir, { withFileTypes: true })) { const p = join(dir, e.name); if (e.isSymbolicLink()) throw Error('Release symlink forbidden'); if (e.isDirectory()) scan(p); else if (e.isFile()) { files++; const b = readFileSync(p); if (names.has(e.name.toLowerCase()) || vendorHashes.has(createHash('sha256').update(b).digest('hex'))) leaks.push(relative(stage, p)); } } }
  scan(stage); if (leaks.length) throw Error('Development dependency leaked into player: ' + leaks.join(', '));
  if (!files) throw Error('Release stage is empty; nothing audited');
  return { pass: true, stage: resolve(stage), files };
}
if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const root = resolve(process.argv[3] ?? '.'), action = process.argv[2] ?? 'verify';
    if (action === 'verify') {
      const admitted = verifyAdmission(root, JSON.parse(readFileSync(join(root, 'client/Packages/manifest.json'))), JSON.parse(readFileSync(join(root, 'client/Packages/packages-lock.json'))));
      console.log('CODELY ADMISSION OK ' + [...admitted].join(', '));
    } else if (action === 'audit-release') {
      if (!process.argv[4]) throw Error('audit-release requires an explicit stage directory');
      console.log(JSON.stringify(auditRelease(root, resolve(process.argv[4]))));
    }
    else throw Error('Usage: codely-dependencies.mjs verify <root> | audit-release <root> <stage>');
  } catch (error) { console.error('CODELY ADMISSION FAIL ' + error.message); process.exitCode = 1; }
}
