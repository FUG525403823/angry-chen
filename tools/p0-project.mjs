// Disposable engine project: optional editor automation never enters player CI.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

export function fingerprint(source) {
  const hash = crypto.createHash('sha256');
  function visit(dir, rel) {
    for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name, 'en'))) {
      const r = `${rel}/${e.name}`, p = path.join(dir, e.name);
      if (e.isSymbolicLink()) throw new Error(`Source link refused ${r}`);
      if (e.isDirectory()) visit(p, r);
      else if (r !== 'Assets/Scripts/Core/BuildInfo.g.cs') { hash.update(r + '\0'); hash.update(fs.readFileSync(p)); hash.update('\0'); }
    }
  }
  for (const name of ['Assets', 'Packages', 'ProjectSettings']) visit(path.join(source, name), name);
  return hash.digest('hex');
}

export function project(source, dest) {
  source = path.resolve(source); dest = path.resolve(dest);
  if (dest === source || source.startsWith(dest + path.sep) || dest.startsWith(source + path.sep)) throw new Error('Unsafe projection path');
  if (fs.existsSync(dest)) throw new Error('Refusing to overwrite existing projection: ' + dest);
  const manifest = JSON.parse(fs.readFileSync(path.join(source, 'Packages/manifest.json'), 'utf8'));
  const excluded = ['cn.tuanjie.codely.bridge', 'cn.tuanjie.ai.generators'];
  for (const name of excluded) delete manifest.dependencies[name];
  for (const [name, value] of Object.entries(manifest.dependencies))
    if (!name.startsWith('com.unity.') || typeof value !== 'string' || value.startsWith('file:')) throw new Error('Unreviewed runtime dependency ' + name);
  const sourceFingerprint = fingerprint(source);
  for (const name of ['Assets', 'Packages', 'ProjectSettings', 'tools', 'build.ps1', 'server.txt.example']) {
    const p = path.join(source, name);
    if (fs.existsSync(p)) fs.cpSync(p, path.join(dest, name), { recursive: true, dereference: false });
  }
  // Editor suites read repository-relative documentation and protocol fixtures.
  // Keep that context beside nested client, never symlink back into mutable source.
  const repo = path.dirname(source), context = path.dirname(dest);
  for (const name of ['docs', 'server/tests/fixtures']) {
    const p = path.join(repo, name), target = path.join(context, name);
    if (fs.existsSync(p)) {
      if (fs.existsSync(target)) throw new Error('Refusing to overwrite projection context: ' + target);
      fs.cpSync(p, target, { recursive: true, dereference: false });
    }
  }
  fs.writeFileSync(path.join(dest, 'Packages/manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  fs.rmSync(path.join(dest, 'Packages/packages-lock.json'), { force: true });
  const record = { schema: 1, sourceFingerprint, source, projection: dest, excluded,
    reason: 'Optional editor automation is not a player dependency; original manifest/lock untouched. Fingerprint excludes generated BuildInfo only.' };
  fs.writeFileSync(path.join(dest, 'p0-projection.json'), JSON.stringify(record, null, 2) + '\n');
  return record;
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const args = Object.fromEntries(process.argv.slice(2).map(x => { const i = x.indexOf('='); if (i < 1) throw new Error('Expected key=value'); return [x.slice(0, i), x.slice(i + 1)]; }));
  console.log(JSON.stringify(args.fingerprint ? { sourceFingerprint: fingerprint(path.resolve(args.source ?? 'client')) }
    : project(args.source ?? 'client', args.out ?? 'build/scratch/p0-player-project/client')));
}
