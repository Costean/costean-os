import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
// Firmware file server only knows .html/.js/.css/.svg/.png/.json and serves "<path>.gz".
export default defineConfig({
  base: './',
  plugins: [svelte()],
  build: {
    outDir: 'dist/axe-os', emptyOutDir: true, assetsDir: 'assets', cssCodeSplit: false, modulePreload: false,
    rollupOptions: { output: { entryFileNames: 'assets/app.js', assetFileNames: 'assets/app[extname]', codeSplitting: false } },
  },
  server: { proxy: { '/api': { target: process.env.MINER || 'http://127.0.0.1:8099', ws: true } } },
  test: { environment: 'node', include: ['test/**/*.test.js'] },
});
