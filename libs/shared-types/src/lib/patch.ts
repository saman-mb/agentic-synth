export interface OscParams {
  type: number;
  volume: number;
  detune_cents: number;
  semitone_offset: number;
  wavetable_pos: number;
  fm_ratio: number;
  fm_depth: number;
  pulse_width: number;
  pan: number;
  enabled: number;
}

export interface FilterParams {
  type: number;
  cutoff_hz: number;
  resonance: number;
  env_mod: number;
  key_track: number;
  drive: number;
}

export interface EnvParams {
  attack_s: number;
  decay_s: number;
  sustain: number;
  release_s: number;
}

export interface LfoParams {
  waveform: number;
  target: number;
  rate_hz: number;
  depth: number;
  phase_offset: number;
  bpm_sync: number;
}

export interface ReverbParams {
  size: number;
  damping: number;
  width: number;
  mix: number;
}

export interface DelayParams {
  time_s: number;
  feedback: number;
  mix: number;
  stereo: number;
  bpm_sync: number;
}

export interface PatchParams {
  osc: OscParams[];
  filter: FilterParams;
  filter_env: EnvParams;
  amp_env: EnvParams;
  lfo: LfoParams[];
  reverb: ReverbParams;
  delay: DelayParams;
  master_gain: number;
  portamento_s: number;
  voice_count: number;
}

export function makeDefaultPatch(): PatchParams {
  const osc: OscParams = {
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
