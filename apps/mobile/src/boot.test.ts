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
const { INITIAL_SESSION } = await import('./state/mobileState.ts');
const { addUserMessage, addAgentMessage } = await import('./state/mobileStateMachine.ts');

describe('mobileStateMachine', () => {
  it('appends messages and tracks session flow', () => {
    let session = addUserMessage(INITIAL_SESSION, 'Hello');
    session = addAgentMessage(session, 'Hi there');
    assert.equal(session.messages.length, 3);
    assert.equal(session.messages[1].text, 'Hello');
    assert.equal(session.messages[2].text, 'Hi there');
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
