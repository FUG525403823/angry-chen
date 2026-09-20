#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, extname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SKIP_DIRS = new Set(['node_modules', '.git', 'dist', 'coverage', '.vite', 'data']);

const BANNED_EXTENSIONS = new Set([
  '.png',
  '.jpg',
  '.jpeg',
  '.gif',
  '.webp',
  '.avif',
  '.bmp',
  '.ico',
  '.tga',
  '.tiff',
  '.svg',
  '.mp3',
  '.wav',
  '.ogg',
  '.oga',
  '.m4a',
  '.aac',
  '.flac',
  '.opus',
  '.glb',
  '.gltf',
  '.fbx',
  '.obj',
  '.dae',
  '.stl',
  '.ply',
  '.ktx2',
  '.hdr',
  '.exr',
  '.dds',
  '.basis',
  '.ttf',
  '.otf',
  '.woff',
  '.woff2',
  '.eot',
  '.mp4',
  '.webm',
  '.mov',
  '.avi',
]);

const ALLOWED_RUNTIME_DEPENDENCIES = new Set(['three', 'ws', 'tsx', '@ac/shared']);

const errors = [];
const notes = [];

function walk(dir) {
  const out = [];
  for (const entry of readdirSync(dir)) {
    if (SKIP_DIRS.has(entry)) continue;
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) out.push(...walk(full));
    else out.push(full);
  }
  return out;
}

function trackedFiles() {
  try {
    const output = execFileSync('git', ['ls-files', '-z'], {
      cwd: ROOT,
      encoding: 'utf8',
      maxBuffer: 32 * 1024 * 1024,
    });
    return { source: 'git ls-files', files: output.split('\0').filter((entry) => entry !== '') };
  } catch (error) {
    notes.push(`git ls-files 不可用（${String(error)}），退化为文件树扫描`);
    return {
      source: 'filesystem walk',
      files: walk(ROOT).map((full) => relative(ROOT, full).split('\\').join('/')),
    };
  }
}

function scanAssets(source, files) {
  const offenders = files.filter((file) => BANNED_EXTENSIONS.has(extname(file).toLowerCase()));
  notes.push(`素材扫描来源：${source}，共 ${files.length} 个文件`);
  if (offenders.length > 0) {
    for (const file of offenders)
      errors.push(`发现第三方素材文件（违反 NFR-07 / P10 §2.4）：${file}`);
  } else {
    notes.push('未发现图片/音频/模型/字体等素材文件');
  }
}

// 二道检查：扩展名可能被改名绕过，这里按内容嗅探（含 NUL 字节即视为二进制资产）。
function scanBinaryAssets(files) {
  const binaries = [];
  for (const file of files) {
    const full = join(ROOT, file);
    let stat;
    try {
      stat = statSync(full);
    } catch {
      continue;
    }
    if (!stat.isFile() || stat.size === 0) continue;
    const head = readFileSync(full).subarray(0, Math.min(8192, stat.size));
    if (head.includes(0)) binaries.push(file);
  }
  notes.push(
    `二进制嗅探：检查 ${files.length} 个受版本控制的文件（前 8KB 含 NUL 字节即判为二进制）`,
  );
  if (binaries.length > 0) {
    for (const file of binaries)
      errors.push(`仓库不应携带二进制资产（P10 §5.1 素材扫描）：${file}`);
  } else {
    notes.push('全部受控文件均为纯文本，零二进制资产');
  }
}

function packageFiles() {
  const files = [join(ROOT, 'package.json')];
  const packagesDir = join(ROOT, 'packages');
  for (const entry of readdirSync(packagesDir)) {
    const candidate = join(packagesDir, entry, 'package.json');
    if (statSync(join(packagesDir, entry)).isDirectory() && statSync(candidate).isFile()) {
      files.push(candidate);
    }
  }
  return files;
}

function scanRuntimeDependencies() {
  const found = [];
  for (const file of packageFiles()) {
    const pkg = JSON.parse(readFileSync(file, 'utf8'));
    const rel = relative(ROOT, file).split('\\').join('/');
    const dependencies = pkg.dependencies ?? {};
    for (const name of Object.keys(dependencies)) {
      found.push({ rel, name });
      if (!ALLOWED_RUNTIME_DEPENDENCIES.has(name)) {
        errors.push(`运行时依赖越界：${rel} 声明了 ${name}（ADR-004 仅允许 three + ws + tsx）`);
      }
    }
  }
  notes.push(
    `运行时依赖：${
      found.length === 0 ? '（无）' : found.map((item) => `${item.name} <- ${item.rel}`).join(', ')
    }`,
  );
  notes.push(`允许集合：${[...ALLOWED_RUNTIME_DEPENDENCIES].join(' + ')}`);
}

const tracked = trackedFiles();
scanAssets(tracked.source, tracked.files);
scanBinaryAssets(tracked.files);
scanRuntimeDependencies();

console.log('=== check-assets ===');
for (const note of notes) console.log(`  · ${note}`);
if (errors.length === 0) {
  console.log('\nOK：仓库零外部素材，运行时依赖未越界。');
  process.exit(0);
}
console.log('');
for (const error of errors) console.log(`  ✗ ${error}`);
console.log(`\nFAIL：${errors.length} 处问题。`);
process.exit(1);
