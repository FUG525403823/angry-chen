import { defineConfig } from 'vitest/config';

// O10：门槛取值规则 = 当次实测基线**向下取整到 5 的倍数**（禁止高于基线，否则门槛本身不可达）。
// 实测基线（2026-09-22，覆盖三个包）：shared lines 88.91 / branches 69.65；server lines 88.56；client lines 69.39。
const SHARED_LINE_COVERAGE_THRESHOLD = 80; // §5 冻结：维持 80（低于基线，属收紧余量）
const SHARED_BRANCH_COVERAGE_THRESHOLD = 65; // 基线 69.65 → 65
const SERVER_LINE_COVERAGE_THRESHOLD = 85; // 基线 88.56 → 85
const CLIENT_LINE_COVERAGE_THRESHOLD = 65; // 基线 69.39 → 65

export default defineConfig({
  test: {
    coverage: {
      provider: 'v8',
      // O10：覆盖率门槛覆盖三个包（原先只统计 @ac/shared）。
      include: ['packages/*/src/**/*.ts'],
      exclude: ['**/*.test.ts', 'packages/*/src/test/**'],
      reporter: ['text'],
      thresholds: {
        'packages/shared/src/**/*.ts': {
          lines: SHARED_LINE_COVERAGE_THRESHOLD,
          branches: SHARED_BRANCH_COVERAGE_THRESHOLD,
        },
        'packages/server/src/**/*.ts': { lines: SERVER_LINE_COVERAGE_THRESHOLD },
        'packages/client/src/**/*.ts': { lines: CLIENT_LINE_COVERAGE_THRESHOLD },
      },
    },
    projects: [
      {
        test: {
          name: 'shared',
          environment: 'node',
          include: ['packages/shared/src/**/*.test.ts'],
        },
      },
      {
        test: {
          name: 'server',
          environment: 'node',
          include: ['packages/server/src/**/*.test.ts'],
        },
      },
      {
        test: {
          name: 'client',
          environment: 'node',
          include: ['packages/client/src/**/*.test.ts'],
          // Node 环境没有 OffscreenCanvas/document：程序化贴图测试用最小 canvas 替身（P09 §4）。
          setupFiles: ['packages/client/src/test/canvas.ts'],
          // 客户端单测只覆盖纯逻辑（设置/采样/插值/视图状态），不引入 jsdom：
          // DOM 相关行为通过无头 Edge 冒烟与注入式 Storage 覆盖（P04 §5.5）。
          passWithNoTests: false,
        },
      },
    ],
  },
});
