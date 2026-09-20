#!/usr/bin/env node
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const RESULTS_FILE = join(ROOT, 'coverage', 'test-results.json');
const MIN_TEST_COUNT = 120;

console.log('=== check-test-count ===');

if (!existsSync(RESULTS_FILE)) {
  console.log(`  ✗ 未找到 ${relative(ROOT, RESULTS_FILE)}`);
  console.log('    请先运行 pnpm test（scripts.test 已带 vitest json reporter 生成该文件）。');
  process.exit(1);
}

let report;
try {
  report = JSON.parse(readFileSync(RESULTS_FILE, 'utf8'));
} catch (error) {
  console.log(`  ✗ 解析 ${relative(ROOT, RESULTS_FILE)} 失败：${String(error)}`);
  process.exit(1);
}

const total = typeof report.numTotalTests === 'number' ? report.numTotalTests : 0;
const failed = typeof report.numFailedTests === 'number' ? report.numFailedTests : 0;
console.log(`  · vitest 报告：用例 ${total}，失败 ${failed}，阈值 ≥ ${MIN_TEST_COUNT}`);

if (total < MIN_TEST_COUNT) {
  console.log(`  ✗ 用例数 ${total} 低于阈值 ${MIN_TEST_COUNT}（P10 §5.1）`);
  process.exit(1);
}

console.log(`OK：${total} 个用例 ≥ ${MIN_TEST_COUNT}`);
