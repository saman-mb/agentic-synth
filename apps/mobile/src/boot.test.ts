import { readFileSync } from 'node:fs';
import path from 'node:path';
import { registerHooks } from 'node:module';
import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
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

const demoPatch = JSON.parse(
  readFileSync(path.join(repoRoot, 'apps/mobile/assets/demo-patch.json'), 'utf8'),
);
const { bootDemoPatch, createMobileEngine } = await import('./engine/createMobileEngine.ts');
const { canTransition } = await import('./state/mobileStateMachine.ts');

describe('mobileStateMachine', () => {
  it('allows idle → hear for boot demo path', () => {
    assert.equal(canTransition('idle', 'hear'), true);
  });

  it('forbids say → shape on first generate', () => {
    assert.equal(canTransition('say', 'shape'), false);
  });
});

describe('boot engine', () => {
  it('loads bundled demo patch via mock JSI and starts playback', async () => {
    const { engine, backend } = createMobileEngine({ forceMock: true });
    assert.equal(backend, 'mock');
    await bootDemoPatch(engine, demoPatch);
    engine.noteOff(60);
    engine.dispose();
  });
});
