import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { convertLlmPatch, validatePatch, type LlmOsc, type LlmPatch } from './patchCodec.ts';

function validOsc(overrides: Partial<LlmOsc> = {}): LlmOsc {
  return {
    type: 'Sawtooth',
    semitone_offset: 0,
    detune_cents: 0,
    wavetable_pos: 0,
    fm_ratio: 1,
    fm_depth: 0,
    volume: 0.8,
    pan: 0,
    pulse_width: 0.5,
    enabled: true,
    ...overrides,
  };
}

function validLlmPatch(overrides: Partial<LlmPatch> = {}): LlmPatch {
  return {
    version: 1,
    patch_id: 1,
    osc: [validOsc(), validOsc({ volume: 0, enabled: false }), validOsc({ volume: 0, enabled: false })],
    filter: {
      type: 'LowPass',
      cutoff_hz: 1000,
      resonance: 0.2,
      env_mod: 0,
      key_track: 0,
      drive: 0,
    },
    filter_env: { attack_s: 0.01, decay_s: 0.2, sustain: 0, release_s: 0.1 },
    amp_env: { attack_s: 0.005, decay_s: 0.1, sustain: 1, release_s: 0.1 },
    lfo: [
      { waveform: 'Sine', target: 'None', rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: false },
      { waveform: 'Sine', target: 'None', rate_hz: 1, depth: 0, phase_offset: 0, bpm_sync: false },
    ],
    reverb: { size: 0.5, damping: 0.5, width: 1, mix: 0 },
    delay: { time_s: 0.25, feedback: 0.3, mix: 0, stereo: 0.5, bpm_sync: false },
    master_gain: 0.8,
    portamento_s: 0,
    voice_count: 8,
    ...overrides,
  };
}

// Mirrors makeDefaultPatch() in shared-types (tests cannot use path aliases
// under node --experimental-strip-types, and relative cross-lib imports
// fail @nx/enforce-module-boundaries).
function numericDefaultPatch() {
  const osc = {
    type: 0, volume: 1, detune_cents: 0, semitone_offset: 0,
    wavetable_pos: 0, fm_ratio: 1, fm_depth: 0, pulse_width: 0.5, pan: 0,
    enabled: 0,
  };
  return {
    osc: [{ ...osc, type: 2, enabled: 1 }, { ...osc, volume: 0 }, { ...osc, volume: 0 }],
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

describe('patchCodec', () => {
  it('round-trips a valid 3-osc / 2-lfo LLM patch', () => {
    const converted = convertLlmPatch(validLlmPatch());
    assert.equal(validatePatch(converted).ok, true);
    assert.equal(converted.osc.length, 3);
    assert.equal(converted.lfo.length, 2);
  });

  it('rejects osc.length !== 3', () => {
    const patch = numericDefaultPatch();
    patch.osc = patch.osc.slice(0, 2);
    const verdict = validatePatch(patch);
    assert.equal(verdict.ok, false);
    if (!verdict.ok) assert.match(verdict.error, /osc/);
  });

  it('rejects lfo.length !== 2', () => {
    const patch = numericDefaultPatch();
    patch.lfo = patch.lfo.slice(0, 1);
    const verdict = validatePatch(patch);
    assert.equal(verdict.ok, false);
    if (!verdict.ok) assert.match(verdict.error, /lfo/);
  });

  it('rejects an unknown enum name via NaN', () => {
    const llm = validLlmPatch();
    llm.osc[0].type = 'NotAWave' as LlmOsc['type'];
    const verdict = validatePatch(convertLlmPatch(llm));
    assert.equal(verdict.ok, false);
  });

  it('rejects cutoff_hz out of range', () => {
    const llm = validLlmPatch();
    llm.filter.cutoff_hz = 25000;
    const verdict = validatePatch(convertLlmPatch(llm));
    assert.equal(verdict.ok, false);
    if (!verdict.ok) assert.match(verdict.error, /cutoff_hz/);
  });

  it('rejects a non-bool enabled flag', () => {
    const llm = validLlmPatch();
    llm.osc[0].enabled = 1 as unknown as boolean;
    const verdict = validatePatch(convertLlmPatch(llm));
    assert.equal(verdict.ok, false);
    if (!verdict.ok) assert.match(verdict.error, /enabled/);
  });

  it('accepts a makeDefaultPatch()-shaped numeric patch', () => {
    assert.equal(validatePatch(numericDefaultPatch()).ok, true);
  });

  it('rejects a non-object patch', () => {
    const verdict = validatePatch(null);
    assert.equal(verdict.ok, false);
    if (!verdict.ok) assert.match(verdict.error, /object/);
  });
});
