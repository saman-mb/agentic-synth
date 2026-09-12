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

// The wasm engine only backs the browser demo: in the JUCE desktop/plugin
// WebView, `window.__JUCE__` is present so `src/demo/bootstrap.ts` never
// installs the shim, and CMake's UI_BINARY_ASSETS glob never embeds
// agsynth.{js,wasm}. Requiring Emscripten to produce the bundle the native
// build embeds would block `cmake --build` on any machine without emsdk, so
// missing wasm is a warning unless the consumer explicitly asks for it.
// Deploys set AGSYNTH_REQUIRE_WASM=1 (see .github/workflows/deploy.yml).
const onMissingWasm: 'warn' | 'throw' =
  process.env.AGSYNTH_REQUIRE_WASM === '1' ? 'throw' : 'warn';

function copyAgsynthAssets(onMissing: 'warn' | 'throw'): void {
  fs.mkdirSync(publicDir, { recursive: true });
  for (const name of WASM_FILES) {
    const src = path.join(wasmSrcDir, name);
    if (!fs.existsSync(src)) {
      const message = `agsynth: ${src} not found; run npx nx run wasm:build-wasm`;
      if (onMissing === 'throw') {
        throw new Error(message);
      }
      console.warn(message);
      continue;
    }
    fs.copyFileSync(src, path.join(publicDir, name));
  }
  if (fs.existsSync(workletSrc)) {
    fs.copyFileSync(workletSrc, path.join(publicDir, 'agsynth-worklet.js'));
  } else if (onMissing === 'warn') {
    console.warn(`agsynth: ${workletSrc} not found`);
  }
}

function copyAgsynthPlugin(): Plugin {
  return {
    name: 'copy-agsynth-wasm',
    configureServer() {
      copyAgsynthAssets('warn');
    },
    buildStart() {
      copyAgsynthAssets(onMissingWasm);
    },
    closeBundle() {
      copyAgsynthAssets(onMissingWasm);
      const outDir = path.join(repoRoot, 'apps/web/dist');
      if (!fs.existsSync(outDir)) {
        throw new Error('agsynth: apps/web/dist missing after build');
      }
      for (const name of [...WASM_FILES, 'agsynth-worklet.js'] as const) {
        const src = path.join(publicDir, name);
        if (fs.existsSync(src)) fs.copyFileSync(src, path.join(outDir, name));
      }
      for (const name of WASM_FILES) {
        const dest = path.join(outDir, name);
        if (fs.existsSync(dest)) continue;
        const message = `agsynth: ${dest} missing after build; run npx nx run wasm:build-wasm`;
        if (onMissingWasm === 'throw') throw new Error(message);
        console.warn(`${message} (browser demo will have no audio engine)`);
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
