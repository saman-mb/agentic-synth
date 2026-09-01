import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { packPatchParams, PATCH_STRUCT_SIZE } from './patchAbi.ts';

function fallbackLikePatch() {
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
    master_gain: 0.625,
    portamento_s: 0,
    voice_count: 8,
  };
}

describe('packPatchParams', () => {
  it('packs a fallback-like patch to the PatchStruct layout', () => {
    const patch = fallbackLikePatch();
    const buf = packPatchParams(patch);
    const bytes = new Uint8Array(buf);
    const view = new DataView(buf);

    assert.equal(buf.byteLength, PATCH_STRUCT_SIZE);
    assert.equal(PATCH_STRUCT_SIZE, 828);

    assert.equal(view.getUint32(0, true), 1);
    assert.equal(bytes[0], 1);
    assert.equal(bytes[1], 0);
    assert.equal(bytes[2], 0);
    assert.equal(bytes[3], 0);
    assert.equal(view.getUint32(0, false), 0x01000000);

    assert.equal(view.getUint32(4, true), 0);

    assert.equal(view.getUint8(8), 2);
    assert.equal(view.getFloat32(8 + 24, true), 1);
    assert.equal(view.getUint8(8 + 36), 1);
    assert.equal(view.getFloat32(8 + 40 + 24, true), 0);

    assert.equal(view.getFloat32(132, true), 18000);

    const offChorus = 260;
    assert.equal(view.getFloat32(offChorus + 0, true), Math.fround(0.4));
    assert.equal(view.getFloat32(offChorus + 4, true), Math.fround(0.35));
    assert.equal(view.getFloat32(offChorus + 8, true), 0);

    const offTubesat = 280;
    assert.equal(view.getFloat32(offTubesat + 0, true), 0);
    assert.equal(view.getFloat32(offTubesat + 4, true), 1);

    assert.equal(view.getFloat32(296, true), 0);
    assert.equal(view.getFloat32(304, true), 0.625);
    assert.equal(view.getFloat32(308, true), 0);
    assert.equal(view.getUint8(312), 8);

    for (let i = 316; i < 828; i++) {
      assert.equal(bytes[i], 0);
    }
  });

  it('writes little-endian floats that a big-endian read would not match', () => {
    const patch = fallbackLikePatch();
    patch.master_gain = 1;
    const view = new DataView(packPatchParams(patch));
    assert.equal(view.getFloat32(304, true), 1);
    assert.notEqual(view.getFloat32(304, false), 1);
  });
});
