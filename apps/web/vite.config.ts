import react from '@vitejs/plugin-react';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

export default defineConfig({
  base: './',
  plugins: [react()],
  resolve: {
    alias: {
      '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src'),
      '@agentic-synth/data': path.join(repoRoot, 'libs/data/src'),
      '@agentic-synth/engine-bridge': path.join(repoRoot, 'libs/engine-bridge/src'),
    },
  },
  build: {
    outDir: 'dist',
  },
});
