import type { PatchParams } from '@agentic-synth/shared-types';
import { makeDefaultPatch } from '@agentic-synth/shared-types';

/** Bundled offline demo patch — plays without network (#316 AC). */
export const DEMO_PATCH: PatchParams = {
  ...makeDefaultPatch(),
  osc: [
    {
      type: 2,
      volume: 0.85,
      detune_cents: 4,
      semitone_offset: 0,
      wavetable_pos: 0.35,
      fm_ratio: 1,
      fm_depth: 0.1,
      pulse_width: 0.5,
      pan: 0,
      enabled: 1,
    },
    {
      type: 0,
      volume: 0,
      detune_cents: 0,
      semitone_offset: 0,
      wavetable_pos: 0,
      fm_ratio: 1,
      fm_depth: 0,
      pulse_width: 0.5,
      pan: -0.2,
      enabled: 0,
    },
    {
      type: 0,
      volume: 0,
      detune_cents: 0,
      semitone_offset: 0,
      wavetable_pos: 0,
      fm_ratio: 1,
      fm_depth: 0,
      pulse_width: 0.5,
      pan: 0.2,
      enabled: 0,
    },
  ],
  filter: {
    type: 0,
    cutoff_hz: 4200,
    resonance: 0.35,
    env_mod: 0.2,
    key_track: 0.4,
    drive: 0,
  },
  reverb: { size: 0.55, damping: 0.45, width: 1, mix: 0.22 },
  delay: { time_s: 0.375, feedback: 0.25, mix: 0.08, stereo: 0.5, bpm_sync: 0 },
  master_gain: 0.9,
};
