import { defineConfig, type Plugin } from 'vite';

const GZIP_BUDGET_BYTES = 1_500_000;
const THREE_CHUNK_NAME = 'three';

function formatBytes(bytes: number): string {
  return `${String(bytes)} B (${(bytes / 1024 / 1024).toFixed(3)} MiB)`;
}

async function gzipSize(code: string): Promise<number> {
  const compressed = new Blob([code]).stream().pipeThrough(new CompressionStream('gzip'));
  return (await new Response(compressed).arrayBuffer()).byteLength;
}

function sizeBudgetPlugin(): Plugin {
  return {
    name: 'ac-size-budget',
    apply: 'build',
    async generateBundle(_options, bundle) {
      const rows: string[] = [];
      let totalBytes = 0;
      for (const [fileName, output] of Object.entries(bundle)) {
        if (output.type !== 'chunk') continue;
        const gzipBytes = await gzipSize(output.code);
        totalBytes += gzipBytes;
        rows.push(`  · ${fileName}: ${formatBytes(gzipBytes)}`);
      }
      rows.sort();
      console.log('=== 客户端 JS 体积（gzip）===');
      for (const row of rows) console.log(row);
      const budget = formatBytes(GZIP_BUDGET_BYTES);
      console.log(`  = JS 合计 ${formatBytes(totalBytes)} / 预算 ${budget}`);
      if (totalBytes > GZIP_BUDGET_BYTES) {
        this.error(`客户端 JS gzip 体积 ${formatBytes(totalBytes)} 超过预算 ${budget}（P10 §5.1）`);
      }
      console.log(
        `OK：客户端 JS gzip 体积占预算 ${((totalBytes / GZIP_BUDGET_BYTES) * 100).toFixed(1)}%`,
      );
    },
  };
}

function manualChunks(id: string): string | undefined {
  const normalized = id.split('\\').join('/');
  if (normalized.includes('/node_modules/three/')) return THREE_CHUNK_NAME;
  return undefined;
}

export default defineConfig({
  plugins: [sizeBudgetPlugin()],
  server: {
    port: 5173,
    strictPort: true,
    proxy: {
      '/api': 'http://127.0.0.1:8787',
    },
  },
  optimizeDeps: {
    // @ac/shared 通过 exports 指向源码，必须交由 Vite 直接处理而不是预打包（见 P01 §8 风险表）。
    exclude: ['@ac/shared'],
  },
  build: {
    target: 'es2022',
    sourcemap: false,
    // three 被刻意拆成独立 chunk，必然会超过 Vite 默认的 500KB 提示阈值。
    chunkSizeWarningLimit: 800,
    rollupOptions: {
      output: {
        manualChunks,
      },
    },
  },
});
