import { makeDefaultPatch, PatchParams } from '../components/KnobGrid';

// ── Starter Presets (Phase 6 + Phase 14 + sound-design spec rev) ─────
//
// Factory preset catalogue spanning all six TIMBRE tags
// (Bass, Lead, Pad, Pluck, Keys, FX). Each preset is a focused diff
// over makeDefaultPatch(): we always rewrite the full state for the
// three oscillators, filter, both envelopes, both LFOs, reverb, delay,
// master gain, portamento and voice count. This way every preset is
// fully self-describing and behaves identically regardless of the
// default's evolution over time.
//
// Enum reference (PatchStruct.h):
//   OscType:     Sine=0, Triangle=1, Sawtooth=2, Square=3, Pulse=4,
//                Wavetable=5, FM=6, Noise=7
//   FilterType:  LowPass=0, HighPass=1, BandPass=2, Notch=3, Peak=4
//   LfoWaveform: Sine=0, Triangle=1, Sawtooth=2, Square=3, S&H=4
//   LfoTarget:   None=0, Pitch=1, FilterCutoff=2, Amplitude=3, Pan=4,
//                WavetablePos=5, FmRatio=6

export type PresetTag = 'Bass' | 'Lead' | 'Pad' | 'Pluck' | 'Keys' | 'FX';

export const PRESET_TAGS: readonly PresetTag[] = [
  'Bass',
  'Lead',
  'Pad',
  'Pluck',
  'Keys',
  'FX',
] as const;

export interface PresetEntry {
  id: string;
  name: string;
  tags: PresetTag[];
  params: PatchParams;
  builtIn: boolean;
}

// Small helper: clone default + apply a mutator so each preset reads
// like a focused diff. Mutators are local — no shared state.
function fromDefault(mutate: (p: PatchParams) => void): PatchParams {
  const p = makeDefaultPatch();
  mutate(p);
  return p;
}

// Local oscillator-state setter to keep each preset terse but explicit
// for all three slots. Every slot is enabled (engine treats vol>0 as
// audible, but enabled is set for clarity and future-proofing).
type OscShape = Partial<PatchParams['osc'][number]>;
function setOsc(p: PatchParams, i: number, s: OscShape): void {
  const o = p.osc[i];
  o.enabled = 1;
  if (s.type !== undefined) o.type = s.type;
  if (s.volume !== undefined) o.volume = s.volume;
  if (s.detune_cents !== undefined) o.detune_cents = s.detune_cents;
  if (s.semitone_offset !== undefined) o.semitone_offset = s.semitone_offset;
  if (s.wavetable_pos !== undefined) o.wavetable_pos = s.wavetable_pos;
  if (s.fm_ratio !== undefined) o.fm_ratio = s.fm_ratio;
  if (s.fm_depth !== undefined) o.fm_depth = s.fm_depth;
  if (s.pulse_width !== undefined) o.pulse_width = s.pulse_width;
  if (s.pan !== undefined) o.pan = s.pan;
}

function setLfo(
  p: PatchParams,
  i: number,
  waveform: number,
  target: number,
  rate_hz: number,
  depth: number,
  phase_offset = 0,
  bpm_sync = 0,
): void {
  p.lfo[i] = { waveform, target, rate_hz, depth, phase_offset, bpm_sync };
}

function setFilter(
  p: PatchParams,
  type: number,
  cutoff_hz: number,
  resonance: number,
  env_mod: number,
  key_track: number,
  drive: number,
): void {
  p.filter = { type, cutoff_hz, resonance, env_mod, key_track, drive };
}

function setAmp(p: PatchParams, a: number, d: number, s: number, r: number): void {
  p.amp_env = { attack_s: a, decay_s: d, sustain: s, release_s: r };
}

function setFEnv(p: PatchParams, a: number, d: number, s: number, r: number): void {
  p.filter_env = { attack_s: a, decay_s: d, sustain: s, release_s: r };
}

function setReverb(p: PatchParams, size: number, damping: number, width: number, mix: number): void {
  p.reverb = { size, damping, width, mix };
}

function setDelay(
  p: PatchParams,
  time_s: number,
  feedback: number,
  mix: number,
  stereo: number,
  bpm_sync: number,
): void {
  p.delay = { time_s, feedback, mix, stereo, bpm_sync };
}

// Oscillator type shorthands.
const SINE = 0;
const TRIANGLE = 1;
const SAW = 2;
const SQUARE = 3;
const PULSE = 4;
const WAVETABLE = 5;
const FM = 6;
const NOISE = 7;

// Filter type shorthands.
const LP = 0;
const HP = 1;
const BP = 2;

// LFO waveform shorthands.
const LFO_SINE = 0;
const LFO_TRI = 1;
const LFO_SH = 4;

// LFO target shorthands.
const T_PITCH = 1;
const T_CUTOFF = 2;
const T_AMP = 3;
const T_PAN = 4;
const T_WT = 5;
const T_FMR = 6;

export const STARTER_PRESETS: PresetEntry[] = [
  // ─────────────────────────────────────────────────────────────
  // BASS
  // ─────────────────────────────────────────────────────────────
  {
    id: 'starter-lunar-sub',
    name: 'Lunar Sub',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SINE, semitone_offset: -12, volume: 0.95 });
      setOsc(p, 1, { type: SINE, semitone_offset: -24, volume: 0.55 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: -12, detune_cents: 4, volume: 0.25 });
      setFilter(p, LP, 320, 0.15, 0.2, 0.4, 0.25);
      setAmp(p, 0.02, 0.2, 0.9, 0.35);
      setFEnv(p, 0.005, 0.18, 0.0, 0.1);
      setLfo(p, 0, LFO_SINE, T_AMP, 0.18, 0.08);
      setReverb(p, 0.2, 0.5, 0.6, 0.0);
      setDelay(p, 0.25, 0, 0, 0.5, 0);
      p.master_gain = 0.9;
      p.portamento_s = 0.05;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-rubber-bass',
    name: 'Rubber Bass',
    tags: ['Bass', 'Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -4, volume: 0.85, pan: -0.15 });
      setOsc(p, 1, { type: SAW, detune_cents: 12, volume: 0.55, pan: 0.15 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.6 });
      setFilter(p, LP, 900, 0.4, 0.7, 0.5, 0.3);
      setAmp(p, 0.002, 0.18, 0.4, 0.12);
      setFEnv(p, 0.005, 0.12, 0.1, 0.08);
      setReverb(p, 0.2, 0.5, 0.6, 0.08);
      setDelay(p, 0.25, 0, 0, 0.5, 0);
      p.master_gain = 0.85;
      p.portamento_s = 0.04;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-808-tonal',
    name: '808 Tonal',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SINE, semitone_offset: -24, volume: 1 });
      setOsc(p, 1, { type: SINE, semitone_offset: -12, detune_cents: -3, volume: 0.35 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: -24, detune_cents: 5, volume: 0.2 });
      setFilter(p, LP, 240, 0.08, 0.3, 0.5, 0.15);
      setAmp(p, 0.001, 0.9, 0.0, 0.6);
      setFEnv(p, 0.001, 0.6, 0.0, 0.4);
      setReverb(p, 0, 0, 0, 0);
      setDelay(p, 0.25, 0, 0, 0.5, 0);
      p.master_gain = 0.95;
      p.portamento_s = 0.05;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-reese-bite',
    name: 'Reese Bite',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -11, volume: 0.9, pan: -0.25 });
      setOsc(p, 1, { type: SAW, detune_cents: 11, volume: 0.9, pan: 0.25 });
      setOsc(p, 2, { type: SQUARE, semitone_offset: -12, volume: 0.4, pulse_width: 0.5 });
      setFilter(p, LP, 780, 0.32, 0.25, 0.4, 0.5);
      setAmp(p, 0.005, 0.25, 0.85, 0.25);
      setFEnv(p, 0.5, 0.6, 0.5, 0.3);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.22, 0.4);
      p.master_gain = 0.75;
      p.portamento_s = 0;
      p.voice_count = 2;
    }),
  },
  {
    id: 'starter-fm-bass-lab',
    name: 'FM Bass Lab',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, semitone_offset: -12, volume: 0.9, fm_ratio: 2, fm_depth: 0.7 });
      setOsc(p, 1, { type: SINE, semitone_offset: -24, volume: 0.55 });
      setOsc(p, 2, {
        type: FM, semitone_offset: -12, detune_cents: 8, volume: 0.3, fm_ratio: 4.01, fm_depth: 0.25,
      });
      setFilter(p, LP, 1600, 0.2, 0.45, 0.4, 0.4);
      setAmp(p, 0.002, 0.35, 0.3, 0.18);
      setFEnv(p, 0.001, 0.25, 0.05, 0.15);
      setLfo(p, 0, LFO_SINE, T_FMR, 0.3, 0.1);
      p.master_gain = 0.78;
      p.portamento_s = 0.05;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-wobble-mono',
    name: 'Wobble Mono',
    tags: ['Bass', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -5, volume: 0.95, pan: -0.2 });
      setOsc(p, 1, { type: SAW, detune_cents: 5, volume: 0.85, pan: 0.2 });
      setOsc(p, 2, { type: SQUARE, semitone_offset: -12, volume: 0.4, pulse_width: 0.5 });
      setFilter(p, LP, 900, 0.65, 0.4, 0.4, 0.5);
      setAmp(p, 0.005, 0.4, 0.9, 0.2);
      setFEnv(p, 0.001, 0.2, 0.2, 0.1);
      setLfo(p, 0, LFO_TRI, T_CUTOFF, 0.5, 0.85, 0, 1);
      setDelay(p, 0.25, 0.2, 0.1, 0.5, 1);
      p.master_gain = 0.7;
      p.portamento_s = 0.04;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-sub-drop',
    name: 'Sub Drop',
    tags: ['Bass', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SINE, semitone_offset: -12, volume: 1 });
      setOsc(p, 1, { type: SINE, semitone_offset: -24, volume: 0.6 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: -12, detune_cents: -4, volume: 0.25 });
      setFilter(p, LP, 300, 0.1, 0.2, 0.6, 0.1);
      setAmp(p, 0.01, 0.6, 0.6, 1.2);
      setLfo(p, 0, LFO_SINE, T_PITCH, 0.2, 0.06);
      setReverb(p, 0.5, 0.6, 0.7, 0.18);
      p.master_gain = 0.9;
      p.portamento_s = 1.2;
      p.voice_count = 1;
    }),
  },

  // ─────────────────────────────────────────────────────────────
  // LEAD
  // ─────────────────────────────────────────────────────────────
  {
    id: 'starter-acid-daydream',
    name: 'Acid Daydream',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, volume: 0.95 });
      setOsc(p, 1, { type: PULSE, detune_cents: -5, volume: 0.5, pan: -0.2, pulse_width: 0.35 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.45 });
      setFilter(p, LP, 700, 0.78, 0.85, 0.5, 0.45);
      setAmp(p, 0.002, 0.18, 0.6, 0.18);
      setFEnv(p, 0.003, 0.22, 0.05, 0.1);
      setLfo(p, 0, LFO_SINE, T_PITCH, 5, 0.04);
      setReverb(p, 0.35, 0.4, 1, 0.18);
      setDelay(p, 0.375, 0.5, 0.25, 0.5, 1);
      p.master_gain = 0.7;
      p.portamento_s = 0.08;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-soaring-lead',
    name: 'Soaring Lead',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -9, volume: 0.85, pan: -0.3 });
      setOsc(p, 1, { type: SAW, detune_cents: 9, volume: 0.85, pan: 0.3 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: 12, volume: 0.4 });
      setFilter(p, LP, 5400, 0.18, 0.3, 0.3, 0.15);
      setAmp(p, 0.05, 0.3, 0.85, 0.45);
      setFEnv(p, 0.01, 0.4, 0.4, 0.3);
      setLfo(p, 0, LFO_SINE, T_PITCH, 5.5, 0.05);
      setLfo(p, 1, LFO_SINE, T_CUTOFF, 0.2, 0.15);
      setReverb(p, 0.55, 0.3, 1, 0.3);
      setDelay(p, 0.375, 0.45, 0.28, 0.5, 1);
      p.master_gain = 0.78;
      p.portamento_s = 0.08;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-saw-stack',
    name: 'Saw Stack',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -14, volume: 0.85, pan: -0.35 });
      setOsc(p, 1, { type: SAW, detune_cents: 14, volume: 0.85, pan: 0.35 });
      setOsc(p, 2, { type: SAW, semitone_offset: 12, volume: 0.55 });
      setFilter(p, LP, 6800, 0.12, 0.2, 0.3, 0.1);
      setAmp(p, 0.01, 0.3, 0.85, 0.3);
      setFEnv(p, 0.01, 0.3, 0.6, 0.2);
      setLfo(p, 0, LFO_SINE, T_PITCH, 0.4, 0.03);
      setLfo(p, 1, LFO_TRI, T_CUTOFF, 0.18, 0.15);
      setReverb(p, 0.5, 0.3, 1, 0.25);
      setDelay(p, 0.5, 0.35, 0.18, 0.5, 1);
      p.master_gain = 0.7;
      p.voice_count = 6;
    }),
  },
  {
    id: 'starter-square-pulse',
    name: 'Square Pulse',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SQUARE, volume: 0.95, pan: -0.2, pulse_width: 0.32 });
      setOsc(p, 1, { type: PULSE, detune_cents: 6, volume: 0.55, pan: 0.2, pulse_width: 0.6 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.4 });
      setFilter(p, LP, 3800, 0.18, 0.25, 0.4, 0.2);
      setAmp(p, 0.005, 0.2, 0.8, 0.2);
      setFEnv(p, 0.005, 0.2, 0.2, 0.1);
      setLfo(p, 0, LFO_SINE, T_PITCH, 1.2, 0.05);
      setLfo(p, 1, LFO_TRI, T_CUTOFF, 0.25, 0.15);
      setReverb(p, 0.4, 0.4, 1, 0.2);
      setDelay(p, 0.25, 0.3, 0.15, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 6;
    }),
  },
  {
    id: 'starter-vintage-mono',
    name: 'Vintage Mono',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, volume: 0.95 });
      setOsc(p, 1, { type: SAW, detune_cents: -6, volume: 0.7 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.45 });
      setFilter(p, LP, 2400, 0.45, 0.4, 0.5, 0.55);
      setAmp(p, 0.008, 0.3, 0.7, 0.25);
      setFEnv(p, 0.005, 0.25, 0.2, 0.15);
      setLfo(p, 0, LFO_SINE, T_PITCH, 4.5, 0.05);
      setReverb(p, 0.35, 0.4, 0.8, 0.15);
      p.master_gain = 0.75;
      p.portamento_s = 0.12;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-pwm-drift',
    name: 'PWM Drift',
    tags: ['Lead', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: PULSE, volume: 0.85, pan: -0.25, pulse_width: 0.4 });
      setOsc(p, 1, { type: PULSE, detune_cents: 4, volume: 0.6, pan: 0.25, pulse_width: 0.55 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: -12, volume: 0.5 });
      setFilter(p, LP, 3200, 0.15, 0.2, 0.4, 0.15);
      setAmp(p, 0.04, 0.35, 0.75, 0.4);
      setFEnv(p, 0.02, 0.3, 0.4, 0.2);
      setLfo(p, 0, LFO_SINE, T_PITCH, 0.35, 0.05);
      setLfo(p, 1, LFO_TRI, T_CUTOFF, 0.18, 0.2);
      setReverb(p, 0.55, 0.3, 1, 0.3);
      setDelay(p, 0.25, 0.3, 0.15, 0.5, 1);
      p.master_gain = 0.78;
      p.portamento_s = 0.05;
      p.voice_count = 6;
    }),
  },
  {
    id: 'starter-cyber-lead',
    name: 'Cyber Lead',
    tags: ['Lead', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.9, fm_ratio: 2.5, fm_depth: 0.55 });
      setOsc(p, 1, { type: SAW, detune_cents: 7, volume: 0.6, pan: 0.3 });
      setOsc(p, 2, { type: SQUARE, semitone_offset: -12, volume: 0.4, pan: -0.3, pulse_width: 0.5 });
      setFilter(p, LP, 2200, 0.6, 0.65, 0.5, 0.6);
      setAmp(p, 0.003, 0.3, 0.6, 0.22);
      setFEnv(p, 0.002, 0.18, 0.15, 0.15);
      setLfo(p, 0, LFO_SINE, T_FMR, 0.4, 0.1);
      setReverb(p, 0.45, 0.3, 1, 0.25);
      setDelay(p, 0.375, 0.4, 0.22, 0.5, 1);
      p.master_gain = 0.7;
      p.portamento_s = 0.06;
      p.voice_count = 1;
    }),
  },

  // ─────────────────────────────────────────────────────────────
  // PAD
  // ─────────────────────────────────────────────────────────────
  {
    id: 'starter-glass-choir',
    name: 'Glass Choir',
    tags: ['Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: TRIANGLE, detune_cents: -9, volume: 0.8, pan: -0.3 });
      setOsc(p, 1, { type: TRIANGLE, detune_cents: 9, volume: 0.7, pan: 0.3 });
      setOsc(p, 2, { type: SINE, semitone_offset: 12, volume: 0.5 });
      setFilter(p, LP, 3200, 0.1, 0.2, 0.3, 0.1);
      setAmp(p, 1.4, 0.6, 0.9, 2.6);
      setFEnv(p, 1.0, 1.0, 0.6, 1.5);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.4, 0.18);
      setLfo(p, 1, LFO_SINE, T_PITCH, 0.4, 0.04, 0.5);
      setReverb(p, 0.85, 0.3, 1, 0.55);
      setDelay(p, 0.5, 0.3, 0.15, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-night-pad',
    name: 'Night Drift',
    tags: ['Pad', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, volume: 0.85, pan: -0.25 });
      setOsc(p, 1, { type: SAW, detune_cents: -5, volume: 0.6, pan: 0.25 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.45 });
      setFilter(p, LP, 2200, 0.2, 0.25, 0.3, 0.15);
      setAmp(p, 0.9, 0.5, 0.85, 1.8);
      setFEnv(p, 0.8, 0.8, 0.6, 1.2);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.25, 0.3);
      setLfo(p, 1, LFO_SINE, T_PITCH, 0.35, 0.03);
      setReverb(p, 0.7, 0.4, 1, 0.45);
      setDelay(p, 0.5, 0.3, 0.18, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-dust-atmos',
    name: 'Dust Atmos',
    tags: ['Pad', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: TRIANGLE, semitone_offset: -12, volume: 0.75, pan: -0.3 });
      setOsc(p, 1, {
        type: WAVETABLE, detune_cents: -8, volume: 0.55, pan: 0.3, wavetable_pos: 0.35,
      });
      setOsc(p, 2, { type: NOISE, volume: 0.18 });
      setFilter(p, LP, 1600, 0.12, -0.2, 0.2, 0.2);
      setAmp(p, 2.2, 0.8, 0.85, 3.6);
      setFEnv(p, 1.5, 1.0, 0.5, 2.0);
      setLfo(p, 0, LFO_TRI, T_WT, 0.15, 0.5);
      setLfo(p, 1, LFO_SINE, T_PAN, 0.1, 0.3);
      setReverb(p, 0.92, 0.7, 1, 0.65);
      setDelay(p, 0.75, 0.4, 0.2, 0.5, 0);
      p.master_gain = 0.72;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-string-sweep',
    name: 'String Sweep',
    tags: ['Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -3, volume: 0.9, pan: -0.3 });
      setOsc(p, 1, { type: SAW, detune_cents: 3, volume: 0.8, pan: 0.3 });
      setOsc(p, 2, { type: SAW, semitone_offset: 7, volume: 0.45 });
      setFilter(p, LP, 2800, 0.15, 0.55, 0.3, 0.1);
      setAmp(p, 1.1, 0.5, 0.85, 2.0);
      setFEnv(p, 1.2, 1.4, 0.6, 1.0);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.18, 0.2);
      setReverb(p, 0.75, 0.35, 1, 0.5);
      setDelay(p, 0.5, 0.25, 0.15, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-lush-polyfade',
    name: 'Lush Polyfade',
    tags: ['Pad', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, volume: 0.85, pan: -0.4 });
      setOsc(p, 1, { type: SAW, detune_cents: 11, volume: 0.85, pan: 0.4 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: -12, volume: 0.45 });
      setFilter(p, LP, 4200, 0.08, 0.2, 0.2, 0.1);
      setAmp(p, 1.3, 0.5, 0.9, 2.4);
      setFEnv(p, 0.8, 0.8, 0.6, 1.0);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.3, 0.12);
      setLfo(p, 1, LFO_SINE, T_PITCH, 0.6, 0.03);
      setReverb(p, 0.8, 0.3, 1, 0.55);
      setDelay(p, 0.375, 0.3, 0.2, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-tape-pad',
    name: 'Tape Pad',
    tags: ['Pad', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, volume: 0.9, pan: -0.2 });
      setOsc(p, 1, { type: SAW, detune_cents: -9, volume: 0.6, pan: 0.2 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: 12, volume: 0.35 });
      setFilter(p, LP, 2800, 0.1, 0.2, 0.3, 0.25);
      setAmp(p, 0.6, 0.4, 0.85, 1.4);
      setFEnv(p, 0.5, 0.6, 0.5, 0.8);
      setLfo(p, 0, LFO_SINE, T_PITCH, 4.2, 0.08);
      setLfo(p, 1, LFO_SINE, T_AMP, 6, 0.07);
      setReverb(p, 0.6, 0.5, 1, 0.35);
      p.master_gain = 0.8;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-ambient-drone',
    name: 'Ambient Drone',
    tags: ['Pad', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, semitone_offset: -12, detune_cents: -2, volume: 0.7, pan: -0.3 });
      setOsc(p, 1, { type: SAW, detune_cents: 2, volume: 0.7, pan: 0.3 });
      setOsc(p, 2, { type: SINE, semitone_offset: 12, volume: 0.45 });
      setFilter(p, LP, 2400, 0.05, 0.1, 0.2, 0.15);
      setAmp(p, 3.5, 0.5, 1.0, 4.5);
      setFEnv(p, 2.0, 2.0, 1.0, 3.0);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.08, 0.4);
      setLfo(p, 1, LFO_TRI, T_PAN, 0.07, 0.4);
      setReverb(p, 0.98, 0.3, 1, 0.7);
      setDelay(p, 0.5, 0.7, 0.4, 0.5, 1);
      p.master_gain = 0.7;
      p.voice_count = 4;
    }),
  },

  // ─────────────────────────────────────────────────────────────
  // PLUCK
  // ─────────────────────────────────────────────────────────────
  {
    id: 'starter-velvet-pluck',
    name: 'Velvet Pluck',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, volume: 0.95, pan: -0.2 });
      setOsc(p, 1, { type: TRIANGLE, semitone_offset: 12, volume: 0.4, pan: 0.2 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.45 });
      setFilter(p, LP, 4200, 0.25, 0.6, 0.4, 0.2);
      setAmp(p, 0.002, 0.28, 0.0, 0.18);
      setFEnv(p, 0.001, 0.18, 0.0, 0.1);
      setReverb(p, 0.5, 0.4, 1, 0.25);
      setDelay(p, 0.25, 0.25, 0.12, 0.5, 1);
      p.master_gain = 0.8;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-koto-pluck',
    name: 'Koto Ghost',
    tags: ['Pluck', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.95, fm_ratio: 2, fm_depth: 0.45 });
      setOsc(p, 1, { type: TRIANGLE, detune_cents: -4, volume: 0.4, pan: -0.25 });
      setOsc(p, 2, { type: SINE, semitone_offset: 19, volume: 0.3, pan: 0.25 });
      setFilter(p, LP, 2800, 0.35, 0.3, 0.3, 0.2);
      setAmp(p, 0.001, 0.5, 0.0, 0.3);
      setFEnv(p, 0.001, 0.3, 0.0, 0.2);
      setLfo(p, 0, LFO_SINE, T_PITCH, 4, 0.04);
      setReverb(p, 0.65, 0.4, 1, 0.45);
      setDelay(p, 0.375, 0.4, 0.2, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-bell-pluck',
    name: 'Bell Pluck',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.95, fm_ratio: 3, fm_depth: 0.55 });
      setOsc(p, 1, { type: SINE, semitone_offset: 12, volume: 0.45, pan: -0.25 });
      setOsc(p, 2, { type: SINE, semitone_offset: 19, volume: 0.3, pan: 0.25 });
      setFilter(p, LP, 6800, 0.08, 0.15, 0.2, 0.05);
      setAmp(p, 0.001, 0.45, 0.0, 0.3);
      setLfo(p, 0, LFO_SINE, T_FMR, 0.15, 0.1);
      setReverb(p, 0.75, 0.3, 1, 0.4);
      setDelay(p, 0.5, 0.3, 0.15, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-wood-pizz',
    name: 'Wood Pizz',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: TRIANGLE, volume: 0.95 });
      setOsc(p, 1, { type: SQUARE, semitone_offset: 7, volume: 0.4, pan: -0.2, pulse_width: 0.5 });
      setOsc(p, 2, { type: NOISE, volume: 0.25, pan: 0.2 });
      setFilter(p, LP, 2400, 0.18, 0.8, 0.6, 0.15);
      setAmp(p, 0.001, 0.12, 0.0, 0.08);
      setFEnv(p, 0.001, 0.08, 0.0, 0.05);
      setReverb(p, 0.35, 0.5, 1, 0.18);
      p.master_gain = 0.85;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-steel-string',
    name: 'Steel String',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -4, volume: 0.85, pan: -0.25 });
      setOsc(p, 1, { type: SAW, semitone_offset: 12, detune_cents: 4, volume: 0.5, pan: 0.25 });
      setOsc(p, 2, { type: NOISE, volume: 0.15 });
      setFilter(p, LP, 5200, 0.2, 0.45, 0.4, 0.15);
      setAmp(p, 0.001, 0.6, 0.0, 0.3);
      setFEnv(p, 0.001, 0.2, 0.05, 0.15);
      setLfo(p, 0, LFO_SINE, T_PITCH, 4.5, 0.03);
      setReverb(p, 0.4, 0.4, 1, 0.25);
      setDelay(p, 0.25, 0.35, 0.18, 0.5, 1);
      p.master_gain = 0.78;
      p.voice_count = 6;
    }),
  },
  {
    id: 'starter-marimba-click',
    name: 'Marimba Click',
    tags: ['Pluck', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.9, fm_ratio: 4, fm_depth: 0.25 });
      setOsc(p, 1, { type: SINE, volume: 0.55, pan: -0.15 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: 12, volume: 0.3, pan: 0.15 });
      setFilter(p, LP, 3600, 0.05, 0.2, 0.3, 0.1);
      setAmp(p, 0.001, 0.35, 0.0, 0.18);
      setFEnv(p, 0.001, 0.15, 0.0, 0.1);
      setReverb(p, 0.55, 0.45, 1, 0.22);
      p.master_gain = 0.8;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-bright-stab',
    name: 'Bright Stab',
    tags: ['Pluck', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -7, volume: 0.9, pan: -0.3 });
      setOsc(p, 1, { type: SAW, detune_cents: 7, volume: 0.9, pan: 0.3 });
      setOsc(p, 2, { type: SQUARE, semitone_offset: -12, volume: 0.4, pulse_width: 0.5 });
      setFilter(p, LP, 1800, 0.5, 0.9, 0.4, 0.3);
      setAmp(p, 0.002, 0.22, 0.2, 0.18);
      setFEnv(p, 0.001, 0.18, 0.0, 0.1);
      setReverb(p, 0.45, 0.4, 1, 0.28);
      setDelay(p, 0.375, 0.35, 0.22, 0.5, 1);
      p.master_gain = 0.75;
      p.voice_count = 6;
    }),
  },

  // ─────────────────────────────────────────────────────────────
  // KEYS
  // ─────────────────────────────────────────────────────────────
  {
    id: 'starter-tape-keys',
    name: 'Tape Keys',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, volume: 0.9, pan: -0.2 });
      setOsc(p, 1, { type: TRIANGLE, detune_cents: -7, volume: 0.55, pan: 0.2 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.4 });
      setFilter(p, LP, 3800, 0.08, 0.15, 0.4, 0.15);
      setAmp(p, 0.01, 0.5, 0.7, 0.35);
      setFEnv(p, 0.005, 0.3, 0.3, 0.2);
      setLfo(p, 0, LFO_SINE, T_PITCH, 5.8, 0.06);
      setReverb(p, 0.4, 0.4, 1, 0.22);
      p.master_gain = 0.82;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-fm-bells',
    name: 'FM Bells',
    tags: ['Keys', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 1, fm_ratio: 3.5, fm_depth: 0.6 });
      setOsc(p, 1, { type: SINE, semitone_offset: 12, volume: 0.45, pan: -0.25 });
      setOsc(p, 2, {
        type: FM, semitone_offset: 19, volume: 0.3, pan: 0.25, fm_ratio: 2.01, fm_depth: 0.25,
      });
      setFilter(p, LP, 7000, 0.05, 0.1, 0.2, 0);
      setAmp(p, 0.001, 1.4, 0.0, 0.8);
      setLfo(p, 0, LFO_SINE, T_FMR, 0.1, 0.08);
      setReverb(p, 0.75, 0.3, 1, 0.5);
      setDelay(p, 0.5, 0.35, 0.2, 0.5, 1);
      p.master_gain = 0.75;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-rhodes-lab',
    name: 'Rhodes Lab',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.9, pan: -0.15, fm_ratio: 2, fm_depth: 0.32 });
      setOsc(p, 1, { type: SINE, volume: 0.6, pan: 0.15 });
      setOsc(p, 2, { type: SINE, semitone_offset: 12, volume: 0.35 });
      setFilter(p, LP, 3400, 0.08, 0.2, 0.4, 0.15);
      setAmp(p, 0.003, 0.8, 0.4, 0.4);
      setFEnv(p, 0.001, 0.5, 0.1, 0.2);
      setLfo(p, 0, LFO_SINE, T_PAN, 5, 0.15);
      setReverb(p, 0.55, 0.4, 1, 0.3);
      p.master_gain = 0.8;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-vintage-epiano',
    name: 'Vintage E-Piano',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.9, fm_ratio: 1, fm_depth: 0.25 });
      setOsc(p, 1, { type: TRIANGLE, detune_cents: -5, volume: 0.5, pan: -0.2 });
      setOsc(p, 2, { type: SINE, semitone_offset: 12, volume: 0.35, pan: 0.2 });
      setFilter(p, LP, 2800, 0.1, 0.2, 0.4, 0.3);
      setAmp(p, 0.005, 0.6, 0.55, 0.35);
      setFEnv(p, 0.005, 0.3, 0.2, 0.2);
      setLfo(p, 0, LFO_SINE, T_AMP, 5.2, 0.1);
      setReverb(p, 0.45, 0.4, 1, 0.28);
      p.master_gain = 0.8;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-wurly-drive',
    name: 'Wurly Drive',
    tags: ['Keys', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 1, fm_ratio: 2, fm_depth: 0.45 });
      setOsc(p, 1, { type: SAW, detune_cents: 5, volume: 0.45, pan: -0.2 });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.4, pan: 0.2 });
      setFilter(p, LP, 2200, 0.18, 0.25, 0.4, 0.6);
      setAmp(p, 0.003, 0.55, 0.5, 0.3);
      setFEnv(p, 0.005, 0.3, 0.2, 0.2);
      setLfo(p, 0, LFO_SINE, T_AMP, 6, 0.12);
      setReverb(p, 0.35, 0.4, 0.8, 0.2);
      setDelay(p, 0.25, 0.25, 0.15, 0.5, 1);
      p.master_gain = 0.7;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-suitcase-soft',
    name: 'Suitcase Soft',
    tags: ['Keys', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.85, pan: -0.15, fm_ratio: 2, fm_depth: 0.18 });
      setOsc(p, 1, { type: SINE, volume: 0.55, pan: 0.15 });
      setOsc(p, 2, { type: TRIANGLE, semitone_offset: 12, volume: 0.4 });
      setFilter(p, LP, 2200, 0.05, 0.1, 0.3, 0.1);
      setAmp(p, 0.02, 1.1, 0.5, 0.6);
      setFEnv(p, 0.01, 0.6, 0.3, 0.3);
      setLfo(p, 0, LFO_SINE, T_AMP, 4.8, 0.06);
      setReverb(p, 0.7, 0.4, 1, 0.45);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-clav-wah',
    name: 'Clav Wah',
    tags: ['Keys', 'Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: PULSE, volume: 0.95, pan: -0.25, pulse_width: 0.3 });
      setOsc(p, 1, { type: PULSE, semitone_offset: 7, volume: 0.55, pan: 0.25, pulse_width: 0.7 });
      setOsc(p, 2, { type: SQUARE, semitone_offset: -12, volume: 0.4, pulse_width: 0.5 });
      setFilter(p, BP, 1400, 0.55, 0.45, 0.5, 0.25);
      setAmp(p, 0.001, 0.25, 0.45, 0.18);
      setFEnv(p, 0.001, 0.12, 0.1, 0.08);
      setLfo(p, 0, LFO_TRI, T_CUTOFF, 3.2, 0.4);
      setReverb(p, 0.3, 0.4, 0.8, 0.15);
      setDelay(p, 0.25, 0.2, 0.1, 0.5, 1);
      p.master_gain = 0.75;
      p.voice_count = 6;
    }),
  },
  {
    id: 'starter-synth-piano',
    name: 'Synth Piano',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.9, pan: -0.2, fm_ratio: 1, fm_depth: 0.4 });
      setOsc(p, 1, {
        type: FM, semitone_offset: 12, volume: 0.55, pan: 0.2, fm_ratio: 2, fm_depth: 0.25,
      });
      setOsc(p, 2, { type: SINE, semitone_offset: -12, volume: 0.4 });
      setFilter(p, LP, 5200, 0.05, 0.2, 0.4, 0.1);
      setAmp(p, 0.002, 1.0, 0.2, 0.45);
      setFEnv(p, 0.001, 0.5, 0.1, 0.3);
      setReverb(p, 0.6, 0.3, 1, 0.32);
      p.master_gain = 0.78;
      p.voice_count = 8;
    }),
  },

  // ─────────────────────────────────────────────────────────────
  // FX
  // ─────────────────────────────────────────────────────────────
  {
    id: 'starter-shimmer-fx',
    name: 'Shimmer Wash',
    tags: ['FX', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: WAVETABLE, volume: 0.8, pan: -0.4, wavetable_pos: 0.3 });
      setOsc(p, 1, {
        type: WAVETABLE, semitone_offset: 12, detune_cents: 12, volume: 0.65, pan: 0.4,
        wavetable_pos: 0.7,
      });
      setOsc(p, 2, { type: SINE, semitone_offset: 19, volume: 0.4 });
      setFilter(p, HP, 600, 0.1, 0.1, 0.2, 0.05);
      setAmp(p, 1.8, 0.5, 0.85, 3.2);
      setFEnv(p, 1.0, 1.0, 0.6, 2.0);
      setLfo(p, 0, LFO_TRI, T_WT, 0.18, 0.6);
      setLfo(p, 1, LFO_SINE, T_AMP, 0.25, 0.15);
      setReverb(p, 0.95, 0.25, 1, 0.7);
      setDelay(p, 0.5, 0.6, 0.35, 0.5, 1);
      p.master_gain = 0.7;
      p.voice_count = 8;
    }),
  },
  {
    id: 'starter-broken-radio',
    name: 'Broken Radio',
    tags: ['FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.85, pan: -0.2, fm_ratio: 1.5, fm_depth: 0.85 });
      setOsc(p, 1, { type: NOISE, volume: 0.35, pan: 0.2 });
      setOsc(p, 2, { type: SQUARE, semitone_offset: -12, volume: 0.4, pulse_width: 0.3 });
      setFilter(p, BP, 1400, 0.6, -0.2, 0.2, 0.7);
      setAmp(p, 0.04, 0.3, 0.55, 0.25);
      setFEnv(p, 0.01, 0.5, 0.3, 0.2);
      setLfo(p, 0, LFO_SH, T_CUTOFF, 7.5, 0.6);
      setLfo(p, 1, LFO_SINE, T_AMP, 4, 0.3);
      setReverb(p, 0.4, 0.6, 0.6, 0.25);
      setDelay(p, 0.18, 0.55, 0.3, 0.5, 0);
      p.master_gain = 0.65;
      p.voice_count = 2;
    }),
  },
  {
    id: 'starter-noise-wash',
    name: 'Noise Wash',
    tags: ['FX', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, {
        type: WAVETABLE, semitone_offset: 24, detune_cents: 17, volume: 0.6, pan: -0.4,
        wavetable_pos: 0.5,
      });
      setOsc(p, 1, {
        type: WAVETABLE, semitone_offset: 19, detune_cents: -23, volume: 0.55, pan: 0.4,
        wavetable_pos: 0.7,
      });
      setOsc(p, 2, { type: NOISE, volume: 0.3 });
      setFilter(p, BP, 4800, 0.25, 0.2, 0.1, 0.15);
      setAmp(p, 2.8, 0.6, 0.85, 3.5);
      setFEnv(p, 1.5, 1.5, 0.5, 2.0);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.12, 0.5);
      setLfo(p, 1, LFO_TRI, T_WT, 0.08, 0.4, 0.5);
      setReverb(p, 0.98, 0.4, 1, 0.7);
      setDelay(p, 0.75, 0.65, 0.3, 0.5, 0);
      p.master_gain = 0.65;
      p.voice_count = 4;
    }),
  },
  {
    id: 'starter-glitch-pop',
    name: 'Glitch Pop',
    tags: ['FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: FM, volume: 0.95, fm_ratio: 5, fm_depth: 0.4 });
      setOsc(p, 1, {
        type: SQUARE, semitone_offset: 7, detune_cents: -8, volume: 0.5, pan: -0.3, pulse_width: 0.3,
      });
      setOsc(p, 2, { type: NOISE, volume: 0.3, pan: 0.3 });
      setFilter(p, BP, 1800, 0.7, 0.4, 0.2, 0.3);
      setAmp(p, 0.001, 0.18, 0.0, 0.1);
      setFEnv(p, 0.001, 0.12, 0.0, 0.08);
      setLfo(p, 0, LFO_SH, T_CUTOFF, 12, 0.8);
      setLfo(p, 1, LFO_SH, T_PAN, 8, 0.5, 0.5);
      setReverb(p, 0.3, 0.5, 1, 0.15);
      setDelay(p, 0.06, 0.55, 0.35, 0.5, 0);
      p.master_gain = 0.7;
      p.voice_count = 4;
    }),
  },
  {
    id: 'starter-whoosh-up',
    name: 'Whoosh Up',
    tags: ['FX'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -20, volume: 0.8, pan: -0.4 });
      setOsc(p, 1, { type: SAW, detune_cents: 20, volume: 0.8, pan: 0.4 });
      setOsc(p, 2, { type: NOISE, volume: 0.35 });
      setFilter(p, LP, 300, 0.4, 0.95, 0.3, 0.3);
      setAmp(p, 1.5, 0.5, 1.0, 0.6);
      setFEnv(p, 3.5, 0.8, 1.0, 0.4);
      setLfo(p, 0, LFO_SINE, T_PITCH, 0.5, 0.1);
      setReverb(p, 0.7, 0.3, 1, 0.5);
      setDelay(p, 0.5, 0.45, 0.3, 0.5, 1);
      p.master_gain = 0.7;
      p.voice_count = 1;
    }),
  },
  {
    id: 'starter-risers',
    name: 'Risers',
    tags: ['FX', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, detune_cents: -12, volume: 0.85, pan: -0.4 });
      setOsc(p, 1, { type: SAW, detune_cents: 12, volume: 0.85, pan: 0.4 });
      setOsc(p, 2, { type: SAW, semitone_offset: 12, volume: 0.55 });
      setFilter(p, LP, 800, 0.35, 0.85, 0.3, 0.3);
      setAmp(p, 0.5, 0.5, 1.0, 0.4);
      setFEnv(p, 2.8, 1.0, 1.0, 0.5);
      setLfo(p, 0, LFO_SINE, T_PITCH, 0.3, 0.2);
      setLfo(p, 1, LFO_SINE, T_CUTOFF, 4, 0.15);
      setReverb(p, 0.88, 0.3, 1, 0.55);
      setDelay(p, 0.5, 0.55, 0.3, 0.5, 1);
      p.master_gain = 0.7;
      p.voice_count = 2;
    }),
  },
  {
    id: 'starter-tonal-drone',
    name: 'Tonal Drone',
    tags: ['FX', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: SAW, semitone_offset: -12, detune_cents: -2, volume: 0.75, pan: -0.35 });
      setOsc(p, 1, { type: SAW, semitone_offset: -12, detune_cents: 2, volume: 0.75, pan: 0.35 });
      setOsc(p, 2, { type: SINE, volume: 0.55 });
      setFilter(p, LP, 2000, 0.05, -0.15, 0.2, 0.2);
      setAmp(p, 2.5, 0.5, 1.0, 3.5);
      setFEnv(p, 1.5, 2.0, 0.8, 2.0);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 0.05, 0.4);
      setLfo(p, 1, LFO_TRI, T_PAN, 0.08, 0.4, 0.5);
      setReverb(p, 0.95, 0.35, 1, 0.6);
      setDelay(p, 1.0, 0.5, 0.25, 0.5, 0);
      p.master_gain = 0.7;
      p.voice_count = 4;
    }),
  },
  {
    id: 'starter-robotic-voice',
    name: 'Robotic Voice',
    tags: ['FX', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      setOsc(p, 0, { type: PULSE, volume: 0.9, pan: -0.25, pulse_width: 0.25 });
      setOsc(p, 1, { type: PULSE, semitone_offset: 7, volume: 0.7, pan: 0.25, pulse_width: 0.65 });
      setOsc(p, 2, { type: NOISE, volume: 0.18 });
      setFilter(p, BP, 1600, 0.62, 0.3, 0.3, 0.35);
      setAmp(p, 0.04, 0.25, 0.5, 0.3);
      setFEnv(p, 0.02, 0.2, 0.3, 0.2);
      setLfo(p, 0, LFO_SINE, T_CUTOFF, 4.5, 0.35);
      setLfo(p, 1, LFO_SINE, T_PITCH, 5, 0.05);
      setReverb(p, 0.4, 0.4, 0.8, 0.25);
      setDelay(p, 0.12, 0.4, 0.25, 0.5, 0);
      p.master_gain = 0.7;
      p.portamento_s = 0.05;
      p.voice_count = 1;
    }),
  },
];

// ── User Presets (localStorage) ──────────────────────────────────────

const USER_PRESETS_KEY = 'timbre:user-presets';
const FAVORITES_KEY = 'timbre:favorites';

export function loadUserPresets(): PresetEntry[] {
  try {
    const raw = localStorage.getItem(USER_PRESETS_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as PresetEntry[];
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function saveUserPresets(presets: PresetEntry[]): void {
  try {
    localStorage.setItem(USER_PRESETS_KEY, JSON.stringify(presets));
  } catch {
    // ignore quota / private-mode errors
  }
}

export function loadFavorites(): string[] {
  try {
    const raw = localStorage.getItem(FAVORITES_KEY);
    if (!raw) return [];
    const parsed = JSON.parse(raw) as string[];
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

export function saveFavorites(ids: string[]): void {
  try {
    localStorage.setItem(FAVORITES_KEY, JSON.stringify(ids));
  } catch {
    // ignore
  }
}

export function makeUserPresetId(): string {
  return `user-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`;
}
