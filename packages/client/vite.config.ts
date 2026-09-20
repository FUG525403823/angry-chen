import { defineConfig } from 'vite';

export default defineConfig({
  server: {
    port: 5173,
    strictPort: true,
  },
  optimizeDeps: {
    // @ac/shared 通过 exports 指向源码，必须交由 Vite 直接处理而不是预打包（见 P01 §8 风险表）。
    exclude: ['@ac/shared'],
  },
});
