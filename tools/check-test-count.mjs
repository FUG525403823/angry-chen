import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';

const MIN_TESTS = 120;
const RESULTS = 'coverage/test-results.json';

function countFromFile(path) {
  try {
    const parsed = JSON.parse(readFileSync(path, 'utf8'));
    const total = Number(parsed.numTotalTests);
    return Number.isFinite(total) ? total : null;
  } catch {
    return null;
  }
}

function countByRunning() {
  try {
    execFileSync('pnpm', ['exec', 'vitest', 'run', '--reporter=json', '--outputFile=' + RESULTS], {
      stdio: 'ignore',
      shell: true,
    });
  } catch {
    return null;
  }
  return countFromFile(RESULTS);
}

const total = existsSync(RESULTS) ? (countFromFile(RESULTS) ?? countByRunning()) : countByRunning();
if (total === null) {
  console.error('=== check-test-count ===');
  console.error('FAIL: 无法读取 vitest 结果，测试用例数门槛未能校验');
  process.exit(1);
}
if (total < MIN_TESTS) {
  console.error('=== check-test-count ===');
  console.error('FAIL: 用例数 ' + total + ' 低于门槛 ' + MIN_TESTS);
  process.exit(1);
}
console.log('=== check-test-count ===');
console.log('OK：用例数 ' + total + ' ≥ ' + MIN_TESTS);
