import { registerHooks } from 'node:module';
import path from 'node:path';
import assert from 'node:assert/strict';
import { describe, it } from 'node:test';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const aliases: Record<string, string> = {
  '@agentic-synth/data': path.join(repoRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src/index.ts'),
  '@agentic-synth/engine-bridge': path.join(repoRoot, 'libs/engine-bridge/src/index.ts'),
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

const { perturbPatch } = await import('../src/services/perturbVariation.ts');
const { makeDefaultPatch } = await import('@agentic-synth/shared-types');
const { getPatchParam } = await import('@agentic-synth/engine-bridge');
const {
  createMemoryStorage,
  loadPresets,
  savePreset,
  presetPatchBytes,
} = await import('../src/services/presetStore.ts');

describe('perturbVariation', () => {
  it('changes params deterministically by seed', () => {
    const base = makeDefaultPatch();
    const a = perturbPatch(base, 42);
    const b = perturbPatch(base, 42);
    assert.equal(getPatchParam(a, 'filter.cutoff_hz'), getPatchParam(b, 'filter.cutoff_hz'));
    assert.notEqual(getPatchParam(a, 'filter.cutoff_hz'), getPatchParam(base, 'filter.cutoff_hz'));
  });
});

describe('presetStore', () => {
  it('round-trips patch bytes through validate + pack', async () => {
    const storage = createMemoryStorage();
    const patch = makeDefaultPatch();
    const saved = await savePreset(storage, {
      name: 'Test',
      prompt: 'warm pad',
      patch,
      macros: [0.5, 0.5, 0.5, 0.5],
      variation: { index: 0 },
    });
    const bytesA = presetPatchBytes(saved);
    const loaded = await loadPresets(storage);
    assert.equal(loaded.length, 1);
    const bytesB = presetPatchBytes(loaded[0]!);
    assert.deepEqual(bytesA, bytesB);
  });

  it('skips corrupt storage rows', async () => {
    const storage = createMemoryStorage();
    await storage.setItem(
      '@tambra/kept-presets/v1',
      JSON.stringify([{ id: 'x', name: 'bad', patch: { nope: true } }]),
    );
    const rows = await loadPresets(storage);
    assert.equal(rows.length, 0);
  });
});
