#!/usr/bin/env node
// 素材与依赖门禁（v2）：仓库零外部素材（NFR-07）+ 双端依赖白名单。
// 素材：按扩展名黑名单与二进制嗅探检查所有受版本控制的文件。
// 依赖：v2 只允许自研代码与官方 SDK —— 不允许 v1 的 Node 工具链回归，不允许 C++/Unity 引入第三方包。
import { existsSync, readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const errors = [];
const notes = [];
const fail = (msg) => errors.push(msg);

const BANNED_ASSET =
  /\.(png|jpe?g|gif|bmp|tga|psd|webp|ico|svg|wav|mp3|ogg|m4a|aiff|flac|ttf|otf|woff2?|fbx|obj|gltf|glb|blend|dae|mp4|mov|webm|unitypackage|assetbundle|dll|so|dylib|zip|7z|rar)$/i;

// Unity 官方包与模块白名单（C01 §5 冻结）
const UNITY_ALLOWED = /^(com\.unity\.(render-pipelines\.universal|ugui|modules\..*|test-framework|ide\..*))$/;

function trackedFiles() {
  return execFileSync('git', ['ls-files', '-z'], { cwd: ROOT, encoding: 'utf8' })
    .split('\u0000')
    .filter(Boolean);
}

function checkAssets(listed) {
  for (const f of listed.filter((f) => BANNED_ASSET.test(f))) {
    fail('版本控制中包含外部素材候选：' + f);
  }
  let sniffed = 0;
  for (const f of listed) {
    const abs = join(ROOT, f);
    if (!existsSync(abs)) continue;
    if (readFileSync(abs).subarray(0, 8192).includes(0)) {
      fail('受版本控制的文件含 NUL 字节（疑似二进制）：' + f);
    }
    sniffed += 1;
  }
  notes.push('素材扫描：' + listed.length + ' 个受控文件，二进制嗅探 ' + sniffed + ' 个，零素材类扩展名');
}

function checkDependencies() {
  if (existsSync(join(ROOT, 'package.json')) || existsSync(join(ROOT, 'packages'))) {
    fail('检测到 v1 的 Node 工程形态（package.json 或 packages/）：v2 只保留 client/ server/ tools/ docs/');
  }
  const manifest = join(ROOT, 'client', 'Packages', 'manifest.json');
  if (existsSync(manifest)) {
    const json = JSON.parse(readFileSync(manifest, 'utf8'));
    const deps = Object.keys(json.dependencies ?? {});
    for (const dep of deps) {
      if (!UNITY_ALLOWED.test(dep)) fail('client/Packages/manifest.json 含白名单外依赖：' + dep);
    }
    notes.push('Unity 依赖：' + deps.length + ' 个，全部在官方白名单内');
  } else {
    notes.push('Unity 工程清单尚未创建（C01 交付）');
  }
  const cmake = join(ROOT, 'server', 'CMakeLists.txt');
  if (existsSync(cmake)) {
    const text = readFileSync(cmake, 'utf8');
    for (const bad of ['FetchContent', 'ExternalProject', 'vcpkg']) {
      if (text.includes(bad)) fail('server/CMakeLists.txt 引入了第三方依赖：' + bad);
    }
    notes.push('C++ 构建清单：未发现第三方依赖引入');
  } else {
    notes.push('C++ 构建清单尚未创建（S01 交付）');
  }
}

const listed = trackedFiles();
checkAssets(listed);
checkDependencies();

console.log('=== check-assets ===');
for (const n of notes) console.log('  · ' + n);
for (const e of errors) console.log('  ✗ ' + e);
if (errors.length === 0) {
  console.log('');
  console.log('OK：仓库零外部素材，依赖白名单未被破坏。');
  process.exit(0);
}
console.log('');
console.log('FAIL：' + errors.length + ' 处错误。');
process.exit(1);
