import { registerHooks } from 'node:module';
import path from 'node:path';
import assert from 'node:assert/strict';
import { describe, it, mock } from 'node:test';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const aliases: Record<string, string> = {
  '@agentic-synth/data': path.join(repoRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src/index.ts'),
  '@agentic-synth/engine-bridge': path.join(repoRoot, 'libs/engine-bridge/src/index.ts'),
  '@agentic-synth/prompt': path.join(repoRoot, 'libs/prompt/src/index.ts'),
  '@agentic-synth/modval': path.join(repoRoot, 'libs/modval/src/index.ts'),
  '@agentic-synth/codec': path.join(repoRoot, 'libs/codec/src/index.ts'),
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

const { runMobileGenerateFlow } = await import('../src/services/mobileGenerateFlow.ts');
const { makeDefaultPatch } = await import('@agentic-synth/shared-types');

describe('runMobileGenerateFlow', () => {
  it('rejects empty prompt without network', async () => {
    const result = await runMobileGenerateFlow('   ');
    assert.equal(result.ok, false);
    if (!result.ok) assert.match(result.message, /Didn't catch that/i);
  });

  it('surfaces 429 without crashing', async () => {
    const original = globalThis.fetch;
    globalThis.fetch = mock.fn(async (url: string | URL | Request) => {
      const u = String(url);
      if (u.includes('/api/brief')) {
        return new Response(JSON.stringify({ error: 'Slow down' }), { status: 429 });
      }
      return new Response('{}', { status: 500 });
    }) as typeof fetch;

    try {
      const result = await runMobileGenerateFlow('warm pad');
      assert.equal(result.ok, false);
      if (!result.ok) {
        assert.match(result.message, /Slow down|Rate limited/i);
        assert.equal(result.retryable, true);
      }
    } finally {
      globalThis.fetch = original;
    }
  });

  it('returns patch on happy path', async () => {
    const patch = makeDefaultPatch();
    const original = globalThis.fetch;
    globalThis.fetch = mock.fn(async (url: string | URL | Request) => {
      const u = String(url);
      if (u.includes('/api/brief')) {
        return new Response(JSON.stringify({ brief: 'SONIC: warm pad' }), { status: 200 });
      }
      if (u.includes('/api/generate')) {
        return new Response(
          JSON.stringify({ patch, rationale: 'test' }),
          { status: 200 },
        );
      }
      return new Response('{}', { status: 404 });
    }) as typeof fetch;

    try {
      const result = await runMobileGenerateFlow('warm pad');
      assert.equal(result.ok, true);
      if (result.ok) assert.equal(result.patch.master_gain, patch.master_gain);
    } finally {
      globalThis.fetch = original;
    }
  });
});
