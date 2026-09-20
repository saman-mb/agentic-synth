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

const { projectMacroPatch, MACRO_DEFAULTS, clampMacroPositions } = await import(
  '../src/macros/macroProjection.ts'
);
const { makeDefaultPatch } = await import('@agentic-synth/shared-types');
const { getPatchParam } = await import('@agentic-synth/engine-bridge');

describe('macroProjection', () => {
  it('clamps macro positions to 0..1', () => {
    const out = clampMacroPositions([-0.2, 2, Number.NaN, 0.5]);
    assert.equal(out[0], 0);
    assert.equal(out[1], 1);
    assert.ok(out[2]! >= 0 && out[2]! <= 1);
    assert.equal(out[3], 0.5);
  });

  it('projects brightness macro toward higher cutoff', () => {
    const base = makeDefaultPatch();
    const low = projectMacroPatch(base, [0, ...MACRO_DEFAULTS.slice(1)]);
    const high = projectMacroPatch(base, [1, ...MACRO_DEFAULTS.slice(1)]);
    const lowCutoff = getPatchParam(low, 'filter.cutoff_hz')!;
    const highCutoff = getPatchParam(high, 'filter.cutoff_hz')!;
    assert.ok(highCutoff > lowCutoff);
  });
});
