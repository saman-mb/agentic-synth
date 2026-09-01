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

function hasJsExt(specifier: string): boolean {
  return /\.[cm]?[jt]sx?$/.test(specifier);
}

registerHooks({
  resolve(specifier, context, nextResolve) {
    const mapped = aliases[specifier];
    if (mapped) return nextResolve(pathToFileURL(mapped).href, context);
    if (
      !specifier.startsWith('node:') &&
      !hasJsExt(specifier) &&
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

const { createMobileEngine, bootDemoPatch, shouldUseMockEngine } = await import(
  '../src/engine/createMobileEngine.ts'
);
const { createMockAgsynthHost } = await import('../src/engine/mockAgsynthHost.ts');

const demoPatch = JSON.parse(
  readFileSync(path.join(repoRoot, 'apps/mobile/assets/demo-patch.json'), 'utf8'),
);

describe('createMobileEngine', () => {
  it('falls back to mock when native is unavailable', () => {
    assert.equal(shouldUseMockEngine(true), true);
    const { backend, engine } = createMobileEngine({ forceMock: true });
    assert.equal(backend, 'mock');
    engine.dispose();
  });

  it('loads demo patch and starts via mock binding', async () => {
    const { engine } = createMobileEngine({ forceMock: true });
    await bootDemoPatch(engine, demoPatch, 60, 100);
    const host = globalThis.__AgsynthHost as ReturnType<typeof createMockAgsynthHost> | undefined;
    assert.ok(host);
    assert.ok(host.state.started);
    assert.ok(host.state.patchBytes);
    assert.ok(host.state.notesOn.includes(60));
    engine.dispose();
  });
});
