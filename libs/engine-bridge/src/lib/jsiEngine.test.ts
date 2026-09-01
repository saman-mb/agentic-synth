import { registerHooks } from 'node:module';
import path from 'node:path';
import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const aliases: Record<string, string> = {
  '@agentic-synth/data': path.join(repoRoot, 'libs/data/src/index.ts'),
  '@agentic-synth/shared-types': path.join(repoRoot, 'libs/shared-types/src/index.ts'),
};

function hasJsExt(specifier: string): boolean {
  return /\.[cm]?[jt]sx?$/.test(specifier);
}

registerHooks({
  resolve(specifier, context, nextResolve) {
    const mapped = aliases[specifier];
    if (mapped) return nextResolve(pathToFileURL(mapped).href, context);
    if (!specifier.startsWith('node:') && !hasJsExt(specifier) && (specifier.startsWith('.') || specifier.startsWith('/'))) {
      try {
        return nextResolve(`${specifier}.ts`, context);
      } catch {
        // fall through to the original specifier
      }
    }
    return nextResolve(specifier, context);
  },
});

const { JsiSynthEngine, AgsynthError, AGS_ERR_SIZE, AGS_ERR_QUEUE } = await import('./jsiEngine.ts');
const { packPatchParams, PATCH_STRUCT_SIZE } = await import('./patchAbi.ts');
const { createSynthEngine } = await import('./engine.ts');
const { WasmSynthEngine } = await import('./wasmEngine.ts');

function fallbackPatch() {
  const osc = {
    type: 2,
    volume: 1,
    detune_cents: 0,
    semitone_offset: 0,
    wavetable_pos: 0,
    fm_ratio: 1,
    fm_depth: 0,
    pulse_width: 0.5,
    pan: 0,
    enabled: 1,
  };
  return {
    osc: [{ ...osc }, { ...osc, volume: 0 }, { ...osc, volume: 0 }],
    filter: { type: 0, cutoff_hz: 18000, resonance: 0, env_mod: 0, key_track: 0, drive: 0 },
    filter_env: { attack_s: 0.01, decay_s: 0.2, sustain: 0, release_s: 0.1 },
    amp_env: { attack_s: 0.005, decay_s: 0.1, sustain: 1, release_s: 0.1 },
    lfo: [
      { waveform: 0, target: 0, rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: 0 },
      { waveform: 0, target: 0, rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: 0 },
    ],
    reverb: { size: 0.5, damping: 0.5, width: 1, mix: 0 },
    delay: { time_s: 0.25, feedback: 0.3, mix: 0, stereo: 0.5, bpm_sync: 0 },
    master_gain: 1,
    portamento_s: 0,
    voice_count: 8,
  };
}

function mockBinding(overrides: Record<string, unknown> = {}) {
  const calls: { method: string; args: unknown[] }[] = [];
  const binding = {
    setPatch(bytes: ArrayBuffer): number {
      calls.push({ method: 'setPatch', args: [bytes] });
      return 0;
    },
    setParam(path: string, value: number): number {
      calls.push({ method: 'setParam', args: [path, value] });
      return 0;
    },
    noteOn(note: number, velocity: number): number {
      calls.push({ method: 'noteOn', args: [note, velocity] });
      return 0;
    },
    noteOff(note: number): number {
      calls.push({ method: 'noteOff', args: [note] });
      return 0;
    },
    dispose(): number {
      calls.push({ method: 'dispose', args: [] });
      return 0;
    },
    recreate(sampleRate: number): number {
      calls.push({ method: 'recreate', args: [sampleRate] });
      return 0;
    },
    ...overrides,
  };
  return { binding, calls };
}

function isAgsynthError(err: unknown, code: string): boolean {
  assert.ok(err instanceof AgsynthError);
  assert.equal(err.code, code);
  return true;
}

describe('JsiSynthEngine', () => {
  it('setPatch marshals through packPatchParams on the JS thread', () => {
    const { binding, calls } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    const patch = fallbackPatch();
    engine.setPatch(patch);

    const setPatchCalls = calls.filter((c) => c.method === 'setPatch');
    assert.equal(setPatchCalls.length, 1);
    const bytes = setPatchCalls[0]?.args[0] as ArrayBuffer;
    assert.ok(bytes instanceof ArrayBuffer);
    assert.equal(bytes.byteLength, PATCH_STRUCT_SIZE);
    assert.deepEqual(new Uint8Array(bytes), new Uint8Array(packPatchParams(patch)));
  });

  it('keeps a cloned PatchParams independent of the caller', () => {
    const { binding } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    const patch = fallbackPatch();
    engine.setPatch(patch);
    patch.filter.cutoff_hz = 99;
    engine.setParam('filter.cutoff_hz', 1200);
    assert.equal(patch.filter.cutoff_hz, 99);
  });

  it('throws AgsynthError PARAM on NaN setParam', () => {
    const { binding, calls } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    assert.throws(() => engine.setParam('filter.cutoff_hz', Number.NaN), (err: unknown) =>
      isAgsynthError(err, 'PARAM'),
    );
    assert.equal(calls.filter((c) => c.method === 'setParam').length, 0);
  });

  it('throws AgsynthError PARAM on unknown param path', () => {
    const { binding, calls } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    assert.throws(() => engine.setParam('not.a.path', 1), (err: unknown) => isAgsynthError(err, 'PARAM'));
    assert.equal(calls.filter((c) => c.method === 'setParam').length, 0);
  });

  it('throws AgsynthError PARAM on a non-object patch', () => {
    const { binding } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    assert.throws(() => engine.setPatch(undefined as never), (err: unknown) => isAgsynthError(err, 'NULL'));
    assert.throws(() => engine.setPatch('nope' as never), (err: unknown) => isAgsynthError(err, 'PARAM'));
  });

  it('maps native return codes to AgsynthError', () => {
    const { binding: sizeBinding } = mockBinding({
      setPatch: (): number => AGS_ERR_SIZE,
    });
    const sizeEngine = new JsiSynthEngine(sizeBinding);
    assert.throws(() => sizeEngine.setPatch(fallbackPatch()), (err: unknown) => isAgsynthError(err, 'SIZE'));

    const { binding: queueBinding } = mockBinding({
      noteOn: (): number => AGS_ERR_QUEUE,
    });
    const queueEngine = new JsiSynthEngine(queueBinding);
    assert.throws(() => queueEngine.noteOn(60, 100), (err: unknown) => isAgsynthError(err, 'QUEUE'));
  });

  it('ensureStarted is a no-op without binding.start, and forwards when present', async () => {
    const { binding, calls } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    await engine.ensureStarted();
    assert.equal(calls.length, 0);

    let started = 0;
    const { binding: withStart } = mockBinding({
      start: (): number => {
        started += 1;
        return 0;
      },
    });
    const startedEngine = new JsiSynthEngine(withStart);
    await startedEngine.ensureStarted();
    assert.equal(started, 1);
  });

  it('setOutputDevice rejects with a typed AgsynthError (no WebAudio sinkId)', async () => {
    const { binding } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    await assert.rejects(() => engine.setOutputDevice('default'), (err: unknown) => isAgsynthError(err, 'STATE'));
  });

  it('forwards noteOn/noteOff/recreate/dispose to the binding', () => {
    const { binding, calls } = mockBinding();
    const engine = new JsiSynthEngine(binding);
    engine.noteOn(64, 90);
    engine.noteOff(64);
    engine.recreate(48000);
    engine.dispose();
    assert.deepEqual(
      calls.map((c) => c.method),
      ['noteOn', 'noteOff', 'recreate', 'dispose'],
    );
    assert.deepEqual(calls[0]?.args, [64, 90]);
    assert.deepEqual(calls[2]?.args, [48000]);
  });

  it('createSynthEngine still returns WasmSynthEngine', () => {
    const engine = createSynthEngine();
    assert.equal(engine.constructor.name, 'WasmSynthEngine');
    assert.ok(engine instanceof WasmSynthEngine);
  });
});
