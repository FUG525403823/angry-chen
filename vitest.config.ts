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
          // P04 起客户端才有可单测的渲染/输入逻辑，届时补上 jsdom 环境与真实用例。
          passWithNoTests: true,
        },
      },
    ],
  },
});
