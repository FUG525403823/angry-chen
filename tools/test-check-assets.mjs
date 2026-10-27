import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { copyFileSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const fixture = mkdtempSync(join(tmpdir(), 'ac-asset-summary-'));
try {
  mkdirSync(join(fixture, 'tools'));
  mkdirSync(join(fixture, 'client/Packages'), { recursive: true });
  copyFileSync(join(root, 'tools/check-assets.mjs'), join(fixture, 'tools/check-assets.mjs'));
  const git = spawnSync('git', ['init', '--quiet'], { cwd: fixture, encoding: 'utf8' });
  assert.ifError(git.error);
  assert.equal(git.status, 0, git.stderr);
  for (const rejected of [0, 1, 2]) {
    const dependencies = { 'com.unity.ugui': '1.0.0' };
    for (let i = 0; i < rejected; i++) dependencies[`unapproved.fixture${i}`] = '1.0.0';
    writeFileSync(join(fixture, 'client/Packages/manifest.json'), JSON.stringify({ dependencies }));
    const result = spawnSync(process.execPath, ['tools/check-assets.mjs'], { cwd: fixture, encoding: 'utf8' });
    assert.ifError(result.error);
    assert.equal(result.status, rejected === 0 ? 0 : 1, result.stderr + result.stdout);
    assert.ok(result.stdout.includes(`Unity 依赖：${1 + rejected} 个，白名单内 1 个，白名单外 ${rejected} 个`));
    assert.equal([...result.stdout.matchAll(/含白名单外依赖：/g)].length, rejected);
    assert.ok(!result.stdout.includes('全部在官方白名单内'));
    console.log(`PASS check-assets dependency summary rejected=${rejected}`);
  }
} finally {
  rmSync(fixture, { recursive: true, force: true });
}
