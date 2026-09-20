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

const { lerpPatch, crossfadePatches } = await import('../src/audio/crossfade.ts');
const { makeDefaultPatch } = await import('@agentic-synth/shared-types');
const { getPatchParam } = await import('@agentic-synth/engine-bridge');

describe('crossfade', () => {
  it('lerpPatch blends numeric params', () => {
    const a = makeDefaultPatch();
    const b = makeDefaultPatch();
    b.filter.cutoff_hz = a.filter.cutoff_hz + 500;
    const mid = lerpPatch(a, b, 0.5);
    const midCutoff = getPatchParam(mid, 'filter.cutoff_hz')!;
    assert.ok(Math.abs(midCutoff - (a.filter.cutoff_hz + 250)) < 1);
  });

  it('crossfadePatches ends on target patch', async () => {
    const from = makeDefaultPatch();
    const to = makeDefaultPatch();
    to.reverb.mix = 0.8;
    let last = from;
    await crossfadePatches((p) => {
      last = p;
    }, from, to, 16);
    assert.equal(getPatchParam(last, 'reverb.mix'), to.reverb.mix);
  });
});
