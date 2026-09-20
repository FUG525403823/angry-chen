import tseslint from 'typescript-eslint';

const purityRules = {
  'no-restricted-imports': [
    'error',
    {
      paths: [{ name: 'three', message: '@ac/shared 必须保持纯净：不得依赖 three。' }],
      patterns: [
        {
          group: ['three/*', 'node:*', 'fs', 'path', 'os', 'http', 'https', 'crypto'],
          message: '@ac/shared 必须保持纯净：不得依赖 three 或 Node 内置模块。',
        },
      ],
    },
  ],
  'no-restricted-globals': [
    'error',
    { name: 'window', message: '@ac/shared 必须保持纯净：不得访问 DOM。' },
    { name: 'document', message: '@ac/shared 必须保持纯净：不得访问 DOM。' },
    { name: 'process', message: '@ac/shared 必须保持纯净：不得依赖 Node 运行时。' },
    { name: 'require', message: '@ac/shared 只允许 ESM import。' },
    { name: 'fetch', message: '@ac/shared 必须保持纯净：不得做 I/O。' },
  ],
  'no-restricted-syntax': [
    'error',
    {
      selector: "MemberExpression[object.name='Math'][property.name='random']",
      message: '@ac/shared 必须保持确定性：禁止 Math.random，请使用 rng.ts 的种子随机数。',
    },
    {
      selector: "MemberExpression[object.name='Date'][property.name='now']",
      message: '@ac/shared 必须保持确定性：禁止 Date.now，时间必须由调用方传入。',
    },
    {
      selector: "MemberExpression[object.name='performance'][property.name='now']",
      message: '@ac/shared 必须保持确定性：禁止 performance.now，时间必须由调用方传入。',
    },
  ],
};

export default tseslint.config(
  { ignores: ['**/node_modules/**', '**/dist/**', '**/coverage/**', 'docs/**'] },
  ...tseslint.configs.recommended,
  {
    files: ['**/*.ts'],
    rules: {
      '@typescript-eslint/no-explicit-any': 'error',
      '@typescript-eslint/consistent-type-imports': 'error',
    },
  },
  {
    files: ['packages/shared/src/**/*.ts'],
    rules: purityRules,
  },
  {
    files: ['packages/client/src/render/**/*.ts'],
    rules: {
      'no-restricted-imports': [
        'error',
        {
          paths: [
            {
              name: '@ac/shared',
              message: '渲染层只允许接收 SnapshotView、Settings 与本地输入状态（P04 §5.1）。',
            },
          ],
          patterns: [
            {
              group: [
                '**/sim.ts',
                '**/world.ts',
                '**/rng.ts',
                '**/snapshot.ts',
                '**/net/connection.ts',
              ],
              message: '渲染层不得引用模拟内核与网络协议内部（P04 §5.1）。',
            },
          ],
        },
      ],
    },
  },
  {
    files: ['packages/client/src/**/*.ts'],
    ignores: [
      'packages/client/src/**/*.test.ts',
      'packages/client/src/prediction/**',
      'packages/client/src/render/**',
    ],
    rules: {
      'no-restricted-imports': [
        'error',
        {
          paths: [
            {
              name: '@ac/shared',
              importNames: [
                'countActive',
                'createWorld',
                'despawnEntity',
                'snapshotWorld',
                'spawnEntity',
                'stepWorld',
              ],
              message: '客户端只能通过 prediction/** 的 stepLocalPlayer 复用移动代码（P05 §5.2）。',
            },
          ],
          patterns: [
            {
              group: ['**/sim.ts', '**/world.ts', '**/rng.ts', '**/snapshot.ts'],
              message: '客户端不得引用模拟内核内部（P05 §5.2）。',
            },
          ],
        },
      ],
    },
  },
);
