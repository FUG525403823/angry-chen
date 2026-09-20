import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
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
