import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { validateGeneratePayload } from './validateGeneratePayload.ts';

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

describe('validateGeneratePayload (generateFlow wiring)', () => {
  it('accepts a valid patch with undefined modulation', () => {
    const verdict = validateGeneratePayload(numericDefaultPatch(), undefined);
    assert.equal(verdict.ok, true);
    if (verdict.ok) assert.equal(verdict.modulation, undefined);
  });

  it('rejects invalid modulation before apply would run', () => {
    const verdict = validateGeneratePayload(numericDefaultPatch(), {
      macros: [{ routes: [{ target: 'nope', amount: 2 }] }],
    });
    assert.equal(verdict.ok, false);
    if (!verdict.ok) assert.match(verdict.error, /modulation/);
  });

  it('round-trips a valid modulation plan', () => {
    const mod = {
      macros: [{ name: 'Move', routes: [{ target: 'filter.cutoff_hz', amount: 0.5 }] }],
    };
    const verdict = validateGeneratePayload(numericDefaultPatch(), mod);
    assert.equal(verdict.ok, true);
    if (verdict.ok) assert.deepEqual(verdict.modulation, mod);
  });
});
