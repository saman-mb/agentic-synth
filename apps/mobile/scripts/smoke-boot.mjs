#!/usr/bin/env node
/** CI smoke: bundled demo patch boots through mock JSI without network. */
import { registerHooks } from 'node:module';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const aliases = {
  '@agentic-synth/data': path.join(repoRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src/index.ts'),
  '@agentic-synth/engine-bridge': path.join(repoRoot, 'libs/engine-bridge/src/index.ts'),
};

registerHooks({
  resolve(specifier, context, nextResolve) {
    const mapped = aliases[specifier];
    if (mapped) return nextResolve(pathToFileURL(mapped).href, context);
    if (!specifier.startsWith('node:') && !/\.[cm]?[jt]sx?$/.test(specifier) && specifier.startsWith('.')) {
      try {
        return nextResolve(`${specifier}.ts`, context);
      } catch {
        // fall through
      }
    }
    return nextResolve(specifier, context);
  },
});

const { bootDemoPatch, createMobileEngine } = await import('../src/engine/createMobileEngine.ts');
const { DEMO_PATCH } = await import('../src/assets/demoPatch.ts');

const { engine, backend } = createMobileEngine({ forceMock: true });
await bootDemoPatch(engine, DEMO_PATCH);
engine.dispose();

console.log(JSON.stringify({ ok: true, backend, patchOscCount: DEMO_PATCH.osc.length }));
