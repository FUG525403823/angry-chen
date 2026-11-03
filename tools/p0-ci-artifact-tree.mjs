// Bind the entire player tree (including *_Data), not only its executable, to frozen input.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const [directory, projection, role, output] = process.argv.slice(2);
if (!directory || !projection || !['release', 'development'].includes(role) || !output) throw new Error('Usage: directory projection.json release|development out.json');
const record = JSON.parse(fs.readFileSync(projection, 'utf8'));
if (!/^[a-f0-9]{64}$/.test(record.sourceFingerprint)) throw new Error('Frozen input fingerprint required');
const files = [];
function visit(dir, rel = '') {
  for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name, 'en'))) {
    const r = rel ? rel + '/' + e.name : e.name, p = path.join(dir, e.name);
    if (e.isSymbolicLink()) throw new Error('Artifact link refused');
    if (e.isDirectory()) visit(p, r);
    else { const bytes = fs.readFileSync(p); files.push({ path: r, bytes: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex') }); }
  }
}
visit(directory);
if (!files.some(f => f.path === 'angry-chen.exe') || !files.some(f => f.path.startsWith('angry-chen_Data/'))) throw new Error('Incomplete player tree');
const treeSha256 = crypto.createHash('sha256').update(JSON.stringify(files)).digest('hex');
fs.writeFileSync(output, JSON.stringify({ schema: 1, role, sourceFingerprint: record.sourceFingerprint,
  sourceAttestation: 'Frozen Assets/Packages/ProjectSettings inputs excluding generated BuildInfo.g.cs. Release packaging temporarily generates commit/time metadata; development retains projection metadata. This is not byte-identical generated metadata.',
  treeSha256, files }, null, 2) + '\n');
console.log(`P0 ARTIFACT TREE ${role} files=${files.length} sha256=${treeSha256}`);
