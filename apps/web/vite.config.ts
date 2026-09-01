import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import { defineConfig, type Plugin } from 'vite';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const publicDir = path.join(repoRoot, 'apps/web/public');
const wasmSrcDir = path.join(repoRoot, 'dist/wasm');
const workletSrc = path.join(repoRoot, 'libs/engine-bridge/src/lib/agsynth-worklet.js');
const WASM_FILES = ['agsynth.js', 'agsynth.wasm'] as const;

function copyAgsynthAssets(warnIfMissing: boolean): void {
  fs.mkdirSync(publicDir, { recursive: true });
  for (const name of WASM_FILES) {
    const src = path.join(wasmSrcDir, name);
    if (!fs.existsSync(src)) {
      if (warnIfMissing) {
        console.warn(`agsynth: ${src} not found; run npx nx run wasm:build-wasm`);
      }
      continue;
    }
    fs.copyFileSync(src, path.join(publicDir, name));
  }
  if (fs.existsSync(workletSrc)) {
    fs.copyFileSync(workletSrc, path.join(publicDir, 'agsynth-worklet.js'));
  } else if (warnIfMissing) {
    console.warn(`agsynth: ${workletSrc} not found`);
  }
}

function copyAgsynthPlugin(): Plugin {
  return {
    name: 'copy-agsynth-wasm',
    configureServer() {
      copyAgsynthAssets(true);
    },
    buildStart() {
      copyAgsynthAssets(true);
    },
    closeBundle() {
      copyAgsynthAssets(false);
      const outDir = path.join(repoRoot, 'apps/web/dist');
      if (!fs.existsSync(outDir)) return;
      for (const name of [...WASM_FILES, 'agsynth-worklet.js'] as const) {
        const src = path.join(publicDir, name);
        if (fs.existsSync(src)) fs.copyFileSync(src, path.join(outDir, name));
      }
    },
  };
}

export default defineConfig({
  base: './',
  plugins: [react(), copyAgsynthPlugin()],
  resolve: {
    alias: {
      '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src'),
      '@agentic-synth/data': path.join(repoRoot, 'libs/data/src'),
      '@agentic-synth/engine-bridge': path.join(repoRoot, 'libs/engine-bridge/src'),
      '@agentic-synth/codec': path.join(repoRoot, 'libs/codec/src'),
      '@agentic-synth/prompt': path.join(repoRoot, 'libs/prompt/src'),
      '@agentic-synth/modval': path.join(repoRoot, 'libs/modval/src'),
    },
  },
  build: {
    outDir: 'dist',
  },
});
