/**
 * Node smoke test — validates bundled demo patch + mock engine boot (#316 CI).
 */
import { registerHooks } from 'node:module';
import path from 'node:path';
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const aliases: Record<string, string> = {
  '@agentic-synth/data': path.join(repoRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src/index.ts'),
  '@agentic-synth/engine-bridge': path.join(repoRoot, 'libs/engine-bridge/src/index.ts'),
};

registerHooks({
  resolve(specifier, context, nextResolve) {
    const mapped = aliases[specifier];
    if (mapped) return nextResolve(pathToFileURL(mapped).href, context);
    if (
      !specifier.startsWith('node:') &&
      !/\.[cm]?[jt]sx?$/.test(specifier) &&
      (specifier.startsWith('.') || specifier.startsWith('/'))
    ) {
      try {
        return nextResolve(`${specifier}.ts`, context);
      } catch {
        // fall through
      }
    }
    return nextResolve(specifier, context);
  },
});

const { createMobileEngine, bootDemoPatch } = await import('../src/engine/createMobileEngine.ts');
const { packPatchParams } = await import('@agentic-synth/engine-bridge');

const demoPatch = JSON.parse(
  readFileSync(path.join(repoRoot, 'apps/mobile/assets/demo-patch.json'), 'utf8'),
);

describe('mobile smoke', () => {
  it('demo patch is valid PatchParams POD for JSI pack', () => {
    const bytes = packPatchParams(demoPatch);
    assert.ok(bytes.byteLength > 0);
  });

  it('cold boot: mock engine loads demo patch', async () => {
    process.env.AGSYNTH_FORCE_MOCK = '1';
    const { engine, backend } = createMobileEngine({ forceMock: true });
    assert.equal(backend, 'mock');
    await bootDemoPatch(engine, demoPatch);
    assert.ok(engine);
    engine.dispose();
    delete process.env.AGSYNTH_FORCE_MOCK;
  });
});
