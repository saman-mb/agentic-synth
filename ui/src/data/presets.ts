import { makeDefaultPatch, PatchParams } from '../components/KnobGrid';

// ── Starter Presets (Phase 6) ────────────────────────────────────────
//
// Hardcoded set of 12 factory presets that span all six TIMBRE tags
// (Bass, Lead, Pad, Pluck, Keys, FX). Each preset is a partial override
// on top of makeDefaultPatch(), so we never have to enumerate every
// param — start from default, mutate the ones that define the sound.
//
// Phase 6 only ships the static catalogue + the UI to load them; full
// preset metadata (BPM, author, description) lands later.

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

export const STARTER_PRESETS: PresetEntry[] = [
  {
    id: 'starter-lunar-sub',
    name: 'Lunar Sub',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 1;
      p.osc[0].semitone_offset = -12;
      p.osc[1].volume = 0.4;
      p.osc[1].semitone_offset = -24;
      p.filter.cutoff_hz = 320;
      p.filter.resonance = 0.15;
      p.filter.drive = 0.25;
      p.amp_env.attack_s = 0.02;
      p.amp_env.release_s = 0.35;
      p.master_gain = 0.9;
    }),
  },
  {
    id: 'starter-rubber-bass',
    name: 'Rubber Bass',
    tags: ['Bass', 'Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.9;
      p.osc[1].volume = 0.5;
      p.osc[1].detune_cents = 12;
      p.filter.cutoff_hz = 900;
      p.filter.resonance = 0.4;
      p.filter.env_mod = 0.7;
      p.filter_env.attack_s = 0.005;
      p.filter_env.decay_s = 0.12;
      p.filter_env.sustain = 0.1;
      p.amp_env.attack_s = 0.002;
      p.amp_env.decay_s = 0.18;
      p.amp_env.sustain = 0.4;
      p.amp_env.release_s = 0.12;
    }),
  },
  {
    id: 'starter-glass-choir',
    name: 'Glass Choir',
    tags: ['Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.8;
      p.osc[1].volume = 0.7;
      p.osc[1].detune_cents = 7;
      p.osc[2].volume = 0.5;
      p.osc[2].detune_cents = -9;
      p.filter.cutoff_hz = 3200;
      p.filter.resonance = 0.1;
      p.amp_env.attack_s = 1.4;
      p.amp_env.release_s = 2.6;
      p.lfo[0].rate_hz = 0.4;
      p.lfo[0].depth = 0.18;
      p.reverb.size = 0.85;
      p.reverb.mix = 0.55;
      p.delay.mix = 0.15;
    }),
  },
  {
    id: 'starter-night-pad',
    name: 'Night Drift',
    tags: ['Pad', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.85;
      p.osc[1].volume = 0.6;
      p.osc[1].detune_cents = -5;
      p.filter.cutoff_hz = 2200;
      p.filter.resonance = 0.2;
      p.amp_env.attack_s = 0.9;
      p.amp_env.release_s = 1.8;
      p.lfo[0].rate_hz = 0.25;
      p.lfo[0].depth = 0.3;
      p.reverb.size = 0.7;
      p.reverb.mix = 0.45;
    }),
  },
  {
    id: 'starter-acid-daydream',
    name: 'Acid Daydream',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 1;
      p.osc[0].pulse_width = 0.35;
      p.filter.cutoff_hz = 700;
      p.filter.resonance = 0.78;
      p.filter.env_mod = 0.85;
      p.filter.drive = 0.45;
      p.filter_env.attack_s = 0.003;
      p.filter_env.decay_s = 0.22;
      p.filter_env.sustain = 0.05;
      p.amp_env.attack_s = 0.002;
      p.amp_env.decay_s = 0.18;
      p.amp_env.sustain = 0.6;
      p.amp_env.release_s = 0.18;
      p.delay.mix = 0.22;
    }),
  },
  {
    id: 'starter-soaring-lead',
    name: 'Soaring Lead',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.9;
      p.osc[1].volume = 0.55;
      p.osc[1].detune_cents = 9;
      p.filter.cutoff_hz = 5400;
      p.filter.resonance = 0.18;
      p.amp_env.attack_s = 0.05;
      p.amp_env.release_s = 0.45;
      p.portamento_s = 0.08;
      p.lfo[0].rate_hz = 5.5;
      p.lfo[0].depth = 0.05;
      p.reverb.mix = 0.3;
      p.delay.mix = 0.28;
      p.delay.feedback = 0.45;
    }),
  },
  {
    id: 'starter-velvet-pluck',
    name: 'Velvet Pluck',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.95;
      p.osc[1].volume = 0.3;
      p.osc[1].semitone_offset = 12;
      p.filter.cutoff_hz = 4200;
      p.filter.resonance = 0.25;
      p.filter.env_mod = 0.6;
      p.filter_env.attack_s = 0.001;
      p.filter_env.decay_s = 0.18;
      p.filter_env.sustain = 0;
      p.amp_env.attack_s = 0.002;
      p.amp_env.decay_s = 0.28;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.18;
      p.reverb.mix = 0.25;
    }),
  },
  {
    id: 'starter-koto-pluck',
    name: 'Koto Ghost',
    tags: ['Pluck', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 1;
      p.osc[0].fm_ratio = 2;
      p.osc[0].fm_depth = 0.45;
      p.filter.cutoff_hz = 2800;
      p.filter.resonance = 0.35;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.5;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.3;
      p.reverb.size = 0.6;
      p.reverb.mix = 0.4;
    }),
  },
  {
    id: 'starter-tape-keys',
    name: 'Tape Keys',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.9;
      p.osc[1].volume = 0.5;
      p.osc[1].detune_cents = -7;
      p.filter.cutoff_hz = 3800;
      p.filter.resonance = 0.08;
      p.amp_env.attack_s = 0.01;
      p.amp_env.decay_s = 0.5;
      p.amp_env.sustain = 0.7;
      p.amp_env.release_s = 0.35;
      p.lfo[0].rate_hz = 5.8;
      p.lfo[0].depth = 0.06;
      p.reverb.mix = 0.22;
    }),
  },
  {
    id: 'starter-fm-bells',
    name: 'FM Bells',
    tags: ['Keys', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 1;
      p.osc[0].fm_ratio = 3.5;
      p.osc[0].fm_depth = 0.6;
      p.osc[1].volume = 0.35;
      p.osc[1].semitone_offset = 12;
      p.filter.cutoff_hz = 7000;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 1.4;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.8;
      p.reverb.size = 0.75;
      p.reverb.mix = 0.5;
    }),
  },
  {
    id: 'starter-shimmer-fx',
    name: 'Shimmer Wash',
    tags: ['FX', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.8;
      p.osc[1].volume = 0.7;
      p.osc[1].semitone_offset = 12;
      p.osc[1].detune_cents = 12;
      p.osc[2].volume = 0.4;
      p.osc[2].semitone_offset = 19;
      p.filter.cutoff_hz = 6500;
      p.amp_env.attack_s = 1.8;
      p.amp_env.release_s = 3.2;
      p.lfo[0].rate_hz = 0.18;
      p.lfo[0].depth = 0.25;
      p.reverb.size = 0.95;
      p.reverb.mix = 0.7;
      p.delay.mix = 0.35;
      p.delay.feedback = 0.6;
    }),
  },
  {
    id: 'starter-broken-radio',
    name: 'Broken Radio',
    tags: ['FX'],
    builtIn: true,
    params: fromDefault((p) => {
      p.osc[0].volume = 0.9;
      p.osc[0].fm_ratio = 1.5;
      p.osc[0].fm_depth = 0.85;
      p.filter.cutoff_hz = 1400;
      p.filter.resonance = 0.55;
      p.filter.drive = 0.7;
      p.amp_env.attack_s = 0.04;
      p.amp_env.release_s = 0.25;
      p.lfo[0].rate_hz = 7.5;
      p.lfo[0].depth = 0.6;
      p.delay.time_s = 0.18;
      p.delay.feedback = 0.55;
      p.delay.mix = 0.3;
    }),
  },

  // ── Phase 14 expansion (Bass) ───────────────────────────────────
  {
    id: 'starter-808-tonal',
    name: '808 Tonal',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      // Sub-leaning sine, dropped two octaves, gentle pitch sweep
      // via filter env mimicking the classic 808 body.
      p.osc[0].volume = 1;
      p.osc[0].semitone_offset = -24;
      p.osc[1].volume = 0;
      p.filter.cutoff_hz = 220;
      p.filter.resonance = 0.05;
      p.filter.drive = 0.1;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.9;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.6;
      p.master_gain = 0.95;
    }),
  },
  {
    id: 'starter-reese-bite',
    name: 'Reese Bite',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      // Detuned saw pair, low filter, mild drive for the classic Reese.
      p.osc[0].volume = 0.9;
      p.osc[0].detune_cents = -11;
      p.osc[1].volume = 0.9;
      p.osc[1].detune_cents = 11;
      p.osc[1].semitone_offset = 0;
      p.filter.cutoff_hz = 780;
      p.filter.resonance = 0.32;
      p.filter.drive = 0.5;
      p.amp_env.attack_s = 0.005;
      p.amp_env.decay_s = 0.25;
      p.amp_env.sustain = 0.85;
      p.amp_env.release_s = 0.25;
    }),
  },
  {
    id: 'starter-fm-bass-lab',
    name: 'FM Bass Lab',
    tags: ['Bass'],
    builtIn: true,
    params: fromDefault((p) => {
      // Carrier + heavy FM modulator → metallic, plucky DX-style bass.
      p.osc[0].volume = 1;
      p.osc[0].semitone_offset = -12;
      p.osc[0].fm_ratio = 2;
      p.osc[0].fm_depth = 0.7;
      p.osc[1].volume = 0.3;
      p.osc[1].semitone_offset = -12;
      p.filter.cutoff_hz = 1600;
      p.filter.resonance = 0.2;
      p.amp_env.attack_s = 0.002;
      p.amp_env.decay_s = 0.35;
      p.amp_env.sustain = 0.3;
      p.amp_env.release_s = 0.18;
    }),
  },
  {
    id: 'starter-wobble-mono',
    name: 'Wobble Mono',
    tags: ['Bass', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      // Slow LFO on filter cutoff — dubstep wobble. Mod matrix wiring
      // would target FilterCutoff; here we approximate via LFO depth
      // and a mid filter with high resonance.
      p.osc[0].volume = 1;
      p.osc[0].detune_cents = -5;
      p.osc[1].volume = 0.8;
      p.osc[1].detune_cents = 5;
      p.filter.cutoff_hz = 900;
      p.filter.resonance = 0.65;
      p.filter.drive = 0.45;
      p.amp_env.attack_s = 0.005;
      p.amp_env.decay_s = 0.4;
      p.amp_env.sustain = 0.9;
      p.amp_env.release_s = 0.2;
      p.lfo[0].rate_hz = 0.5;
      p.lfo[0].depth = 0.8;
    }),
  },
  {
    id: 'starter-sub-drop',
    name: 'Sub Drop',
    tags: ['Bass', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      // Slow downward pitch sweep via portamento; sine sub.
      p.osc[0].volume = 1;
      p.osc[0].semitone_offset = -12;
      p.osc[1].volume = 0;
      p.filter.cutoff_hz = 280;
      p.filter.resonance = 0.1;
      p.amp_env.attack_s = 0.01;
      p.amp_env.decay_s = 0.6;
      p.amp_env.sustain = 0.6;
      p.amp_env.release_s = 1.2;
      p.portamento_s = 1.2;
      p.reverb.size = 0.5;
      p.reverb.mix = 0.2;
    }),
  },

  // ── Phase 14 expansion (Lead) ───────────────────────────────────
  {
    id: 'starter-saw-stack',
    name: 'Saw Stack',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      // Triple-detuned saw stack — supersaw-style hyperlead.
      p.osc[0].volume = 0.85;
      p.osc[0].detune_cents = -14;
      p.osc[1].volume = 0.85;
      p.osc[1].detune_cents = 14;
      p.osc[2].volume = 0.6;
      p.osc[2].semitone_offset = 12;
      p.filter.cutoff_hz = 6800;
      p.filter.resonance = 0.12;
      p.amp_env.attack_s = 0.01;
      p.amp_env.decay_s = 0.3;
      p.amp_env.sustain = 0.85;
      p.amp_env.release_s = 0.3;
      p.reverb.mix = 0.25;
      p.delay.mix = 0.18;
    }),
  },
  {
    id: 'starter-square-pulse',
    name: 'Square Pulse',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      // Hollow PWM lead, animated pulse width via LFO.
      p.osc[0].volume = 1;
      p.osc[0].pulse_width = 0.32;
      p.osc[1].volume = 0;
      p.filter.cutoff_hz = 3800;
      p.filter.resonance = 0.18;
      p.amp_env.attack_s = 0.005;
      p.amp_env.decay_s = 0.2;
      p.amp_env.sustain = 0.8;
      p.amp_env.release_s = 0.2;
      p.lfo[0].rate_hz = 1.2;
      p.lfo[0].depth = 0.25;
      p.delay.mix = 0.15;
    }),
  },
  {
    id: 'starter-vintage-mono',
    name: 'Vintage Mono',
    tags: ['Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      // Minimoog-style fat mono: saw + slightly detuned saw, drive,
      // light portamento for the legato glide.
      p.osc[0].volume = 0.95;
      p.osc[1].volume = 0.7;
      p.osc[1].detune_cents = -6;
      p.filter.cutoff_hz = 2400;
      p.filter.resonance = 0.45;
      p.filter.drive = 0.55;
      p.amp_env.attack_s = 0.008;
      p.amp_env.decay_s = 0.3;
      p.amp_env.sustain = 0.7;
      p.amp_env.release_s = 0.25;
      p.portamento_s = 0.12;
    }),
  },
  {
    id: 'starter-pwm-drift',
    name: 'PWM Drift',
    tags: ['Lead', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      // Slow PWM + slight detune = drifting JP-style lead.
      p.osc[0].volume = 0.9;
      p.osc[0].pulse_width = 0.4;
      p.osc[1].volume = 0.55;
      p.osc[1].pulse_width = 0.55;
      p.osc[1].detune_cents = 4;
      p.filter.cutoff_hz = 3200;
      p.filter.resonance = 0.15;
      p.amp_env.attack_s = 0.04;
      p.amp_env.release_s = 0.4;
      p.lfo[0].rate_hz = 0.35;
      p.lfo[0].depth = 0.18;
      p.reverb.mix = 0.3;
    }),
  },
  {
    id: 'starter-cyber-lead',
    name: 'Cyber Lead',
    tags: ['Lead', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      // Aggressive FM + drive, fast filter env. Synthwave-ready.
      p.osc[0].volume = 1;
      p.osc[0].fm_ratio = 2.5;
      p.osc[0].fm_depth = 0.55;
      p.osc[1].volume = 0.5;
      p.osc[1].detune_cents = 7;
      p.filter.cutoff_hz = 2200;
      p.filter.resonance = 0.6;
      p.filter.env_mod = 0.65;
      p.filter.drive = 0.6;
      p.filter_env.attack_s = 0.002;
      p.filter_env.decay_s = 0.18;
      p.filter_env.sustain = 0.15;
      p.amp_env.attack_s = 0.003;
      p.amp_env.release_s = 0.22;
      p.delay.mix = 0.22;
      p.delay.feedback = 0.4;
    }),
  },

  // ── Phase 14 expansion (Pad) ────────────────────────────────────
  {
    id: 'starter-dust-atmos',
    name: 'Dust Atmos',
    tags: ['Pad', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      // Dark, dusty, slow attack pad with damped reverb.
      p.osc[0].volume = 0.8;
      p.osc[0].semitone_offset = -12;
      p.osc[1].volume = 0.55;
      p.osc[1].detune_cents = -8;
      p.filter.cutoff_hz = 1600;
      p.filter.resonance = 0.12;
      p.amp_env.attack_s = 2.2;
      p.amp_env.release_s = 3.6;
      p.lfo[0].rate_hz = 0.15;
      p.lfo[0].depth = 0.2;
      p.reverb.size = 0.92;
      p.reverb.damping = 0.7;
      p.reverb.mix = 0.65;
    }),
  },
  {
    id: 'starter-string-sweep',
    name: 'String Sweep',
    tags: ['Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      // Bowed-string-ish pad with a filter sweep on note-on.
      p.osc[0].volume = 0.9;
      p.osc[0].detune_cents = -3;
      p.osc[1].volume = 0.8;
      p.osc[1].detune_cents = 3;
      p.osc[2].volume = 0.4;
      p.osc[2].semitone_offset = 7;
      p.filter.cutoff_hz = 2800;
      p.filter.resonance = 0.15;
      p.filter.env_mod = 0.55;
      p.filter_env.attack_s = 1.2;
      p.filter_env.decay_s = 1.4;
      p.filter_env.sustain = 0.6;
      p.amp_env.attack_s = 1.1;
      p.amp_env.release_s = 2.0;
      p.reverb.size = 0.75;
      p.reverb.mix = 0.5;
    }),
  },
  {
    id: 'starter-lush-polyfade',
    name: 'Lush Polyfade',
    tags: ['Pad', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      // Wide, chorused, slow-attack poly pad.
      p.osc[0].volume = 0.85;
      p.osc[0].pan = -0.4;
      p.osc[1].volume = 0.85;
      p.osc[1].pan = 0.4;
      p.osc[1].detune_cents = 11;
      p.filter.cutoff_hz = 4200;
      p.filter.resonance = 0.08;
      p.amp_env.attack_s = 1.3;
      p.amp_env.release_s = 2.4;
      p.lfo[0].rate_hz = 0.3;
      p.lfo[0].depth = 0.12;
      p.reverb.size = 0.8;
      p.reverb.width = 1;
      p.reverb.mix = 0.55;
      p.delay.mix = 0.2;
    }),
  },
  {
    id: 'starter-tape-pad',
    name: 'Tape Pad',
    tags: ['Pad', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      // Mellotron-meets-tape-wow pad with audible LFO wobble.
      p.osc[0].volume = 0.95;
      p.osc[1].volume = 0.6;
      p.osc[1].detune_cents = -9;
      p.filter.cutoff_hz = 2800;
      p.filter.resonance = 0.1;
      p.filter.drive = 0.2;
      p.amp_env.attack_s = 0.6;
      p.amp_env.release_s = 1.4;
      p.lfo[0].rate_hz = 4.2;
      p.lfo[0].depth = 0.08;
      p.reverb.size = 0.6;
      p.reverb.mix = 0.35;
    }),
  },
  {
    id: 'starter-ambient-drone',
    name: 'Ambient Drone',
    tags: ['Pad', 'FX'],
    builtIn: true,
    params: fromDefault((p) => {
      // Endless drone, three octave-stacked oscs, massive reverb tail.
      p.osc[0].volume = 0.7;
      p.osc[0].semitone_offset = -12;
      p.osc[1].volume = 0.7;
      p.osc[1].semitone_offset = 0;
      p.osc[2].volume = 0.5;
      p.osc[2].semitone_offset = 12;
      p.filter.cutoff_hz = 2400;
      p.filter.resonance = 0.05;
      p.amp_env.attack_s = 3.5;
      p.amp_env.sustain = 1;
      p.amp_env.release_s = 4.5;
      p.lfo[0].rate_hz = 0.1;
      p.lfo[0].depth = 0.3;
      p.reverb.size = 0.98;
      p.reverb.mix = 0.75;
      p.delay.time_s = 0.5;
      p.delay.feedback = 0.7;
      p.delay.mix = 0.4;
    }),
  },

  // ── Phase 14 expansion (Pluck) ──────────────────────────────────
  {
    id: 'starter-bell-pluck',
    name: 'Bell Pluck',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      // Glassy, short FM bell.
      p.osc[0].volume = 1;
      p.osc[0].fm_ratio = 3;
      p.osc[0].fm_depth = 0.55;
      p.filter.cutoff_hz = 6800;
      p.filter.resonance = 0.08;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.45;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.3;
      p.reverb.size = 0.7;
      p.reverb.mix = 0.35;
    }),
  },
  {
    id: 'starter-wood-pizz',
    name: 'Wood Pizz',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      // Pizzicato / wood-block hybrid with low resonance, short decay.
      p.osc[0].volume = 1;
      p.osc[1].volume = 0.35;
      p.osc[1].semitone_offset = 7;
      p.filter.cutoff_hz = 2400;
      p.filter.resonance = 0.18;
      p.filter.env_mod = 0.8;
      p.filter_env.attack_s = 0.001;
      p.filter_env.decay_s = 0.08;
      p.filter_env.sustain = 0;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.12;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.08;
      p.reverb.mix = 0.18;
    }),
  },
  {
    id: 'starter-steel-string',
    name: 'Steel String',
    tags: ['Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      // Bright steel-string evoking guitar harmonic with light delay.
      p.osc[0].volume = 0.9;
      p.osc[0].detune_cents = -4;
      p.osc[1].volume = 0.55;
      p.osc[1].detune_cents = 4;
      p.osc[1].semitone_offset = 12;
      p.filter.cutoff_hz = 5200;
      p.filter.resonance = 0.2;
      p.filter.env_mod = 0.45;
      p.filter_env.attack_s = 0.001;
      p.filter_env.decay_s = 0.2;
      p.filter_env.sustain = 0.05;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.6;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.3;
      p.delay.mix = 0.2;
      p.delay.feedback = 0.35;
      p.reverb.mix = 0.25;
    }),
  },
  {
    id: 'starter-marimba-click',
    name: 'Marimba Click',
    tags: ['Pluck', 'Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      // Marimba bar — sine + slight FM transient.
      p.osc[0].volume = 1;
      p.osc[0].fm_ratio = 4;
      p.osc[0].fm_depth = 0.25;
      p.osc[1].volume = 0;
      p.filter.cutoff_hz = 3600;
      p.filter.resonance = 0.05;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.35;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.18;
      p.reverb.size = 0.55;
      p.reverb.mix = 0.22;
    }),
  },
  {
    id: 'starter-bright-stab',
    name: 'Bright Stab',
    tags: ['Pluck', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      // Trance / house-style bright stab. Snappy filter env, short tail.
      p.osc[0].volume = 0.9;
      p.osc[0].detune_cents = -7;
      p.osc[1].volume = 0.9;
      p.osc[1].detune_cents = 7;
      p.filter.cutoff_hz = 1800;
      p.filter.resonance = 0.5;
      p.filter.env_mod = 0.9;
      p.filter_env.attack_s = 0.001;
      p.filter_env.decay_s = 0.18;
      p.filter_env.sustain = 0;
      p.amp_env.attack_s = 0.002;
      p.amp_env.decay_s = 0.22;
      p.amp_env.sustain = 0.2;
      p.amp_env.release_s = 0.18;
      p.delay.mix = 0.25;
      p.reverb.mix = 0.28;
    }),
  },

  // ── Phase 14 expansion (Keys) ───────────────────────────────────
  {
    id: 'starter-rhodes-lab',
    name: 'Rhodes Lab',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      // Rhodes-style FM tine + bell partial.
      p.osc[0].volume = 0.9;
      p.osc[0].fm_ratio = 2;
      p.osc[0].fm_depth = 0.32;
      p.osc[1].volume = 0.35;
      p.osc[1].semitone_offset = 12;
      p.filter.cutoff_hz = 3400;
      p.filter.resonance = 0.08;
      p.amp_env.attack_s = 0.003;
      p.amp_env.decay_s = 0.8;
      p.amp_env.sustain = 0.4;
      p.amp_env.release_s = 0.4;
      p.reverb.size = 0.55;
      p.reverb.mix = 0.3;
    }),
  },
  {
    id: 'starter-vintage-epiano',
    name: 'Vintage E-Piano',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      // Wurly-meets-Rhodes, with chorus-y LFO wobble.
      p.osc[0].volume = 0.95;
      p.osc[0].fm_ratio = 1;
      p.osc[0].fm_depth = 0.25;
      p.osc[1].volume = 0.4;
      p.osc[1].detune_cents = -5;
      p.filter.cutoff_hz = 2800;
      p.filter.resonance = 0.1;
      p.filter.drive = 0.3;
      p.amp_env.attack_s = 0.005;
      p.amp_env.decay_s = 0.6;
      p.amp_env.sustain = 0.55;
      p.amp_env.release_s = 0.35;
      p.lfo[0].rate_hz = 5.2;
      p.lfo[0].depth = 0.07;
      p.reverb.mix = 0.28;
    }),
  },
  {
    id: 'starter-wurly-drive',
    name: 'Wurly Drive',
    tags: ['Keys', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      // Driven Wurlitzer with bark and bite.
      p.osc[0].volume = 1;
      p.osc[0].fm_ratio = 2;
      p.osc[0].fm_depth = 0.45;
      p.osc[1].volume = 0.45;
      p.filter.cutoff_hz = 2200;
      p.filter.resonance = 0.18;
      p.filter.drive = 0.6;
      p.amp_env.attack_s = 0.003;
      p.amp_env.decay_s = 0.55;
      p.amp_env.sustain = 0.5;
      p.amp_env.release_s = 0.3;
      p.lfo[0].rate_hz = 6;
      p.lfo[0].depth = 0.1;
      p.delay.mix = 0.15;
    }),
  },
  {
    id: 'starter-suitcase-soft',
    name: 'Suitcase Soft',
    tags: ['Keys', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      // Soft mellow suitcase Rhodes; pillowy.
      p.osc[0].volume = 0.85;
      p.osc[0].fm_ratio = 2;
      p.osc[0].fm_depth = 0.18;
      p.osc[1].volume = 0.45;
      p.osc[1].semitone_offset = 12;
      p.filter.cutoff_hz = 2200;
      p.filter.resonance = 0.05;
      p.amp_env.attack_s = 0.02;
      p.amp_env.decay_s = 1.1;
      p.amp_env.sustain = 0.5;
      p.amp_env.release_s = 0.6;
      p.reverb.size = 0.7;
      p.reverb.mix = 0.45;
    }),
  },
  {
    id: 'starter-clav-wah',
    name: 'Clav Wah',
    tags: ['Keys', 'Pluck'],
    builtIn: true,
    params: fromDefault((p) => {
      // Clavinet-style snap with auto-wah LFO on filter.
      p.osc[0].volume = 0.95;
      p.osc[0].pulse_width = 0.3;
      p.osc[1].volume = 0.4;
      p.osc[1].pulse_width = 0.7;
      p.filter.cutoff_hz = 1400;
      p.filter.resonance = 0.55;
      p.filter.env_mod = 0.45;
      p.filter_env.attack_s = 0.001;
      p.filter_env.decay_s = 0.12;
      p.filter_env.sustain = 0.1;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.25;
      p.amp_env.sustain = 0.45;
      p.amp_env.release_s = 0.18;
      p.lfo[0].rate_hz = 3.2;
      p.lfo[0].depth = 0.35;
    }),
  },
  {
    id: 'starter-synth-piano',
    name: 'Synth Piano',
    tags: ['Keys'],
    builtIn: true,
    params: fromDefault((p) => {
      // Digital DX-style synth piano with sparkle.
      p.osc[0].volume = 0.9;
      p.osc[0].fm_ratio = 1;
      p.osc[0].fm_depth = 0.4;
      p.osc[1].volume = 0.55;
      p.osc[1].semitone_offset = 12;
      p.osc[1].fm_ratio = 2;
      p.osc[1].fm_depth = 0.25;
      p.filter.cutoff_hz = 5200;
      p.filter.resonance = 0.05;
      p.amp_env.attack_s = 0.002;
      p.amp_env.decay_s = 1.0;
      p.amp_env.sustain = 0.2;
      p.amp_env.release_s = 0.45;
      p.reverb.size = 0.6;
      p.reverb.mix = 0.32;
    }),
  },

  // ── Phase 14 expansion (FX) ─────────────────────────────────────
  {
    id: 'starter-noise-wash',
    name: 'Noise Wash',
    tags: ['FX', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      // High-pitched detuned wash; slow LFO sweep + giant reverb.
      p.osc[0].volume = 0.6;
      p.osc[0].semitone_offset = 24;
      p.osc[0].detune_cents = 17;
      p.osc[1].volume = 0.55;
      p.osc[1].semitone_offset = 19;
      p.osc[1].detune_cents = -23;
      p.filter.cutoff_hz = 4800;
      p.filter.resonance = 0.25;
      p.amp_env.attack_s = 2.8;
      p.amp_env.release_s = 3.5;
      p.lfo[0].rate_hz = 0.12;
      p.lfo[0].depth = 0.5;
      p.reverb.size = 0.98;
      p.reverb.mix = 0.7;
      p.delay.feedback = 0.65;
      p.delay.mix = 0.3;
    }),
  },
  {
    id: 'starter-glitch-pop',
    name: 'Glitch Pop',
    tags: ['FX'],
    builtIn: true,
    params: fromDefault((p) => {
      // Fast LFO modulating filter — robotic glitch artifact.
      p.osc[0].volume = 1;
      p.osc[0].fm_ratio = 5;
      p.osc[0].fm_depth = 0.4;
      p.filter.cutoff_hz = 1800;
      p.filter.resonance = 0.7;
      p.amp_env.attack_s = 0.001;
      p.amp_env.decay_s = 0.18;
      p.amp_env.sustain = 0;
      p.amp_env.release_s = 0.1;
      p.lfo[0].rate_hz = 12;
      p.lfo[0].depth = 0.85;
      p.delay.time_s = 0.06;
      p.delay.feedback = 0.55;
      p.delay.mix = 0.35;
    }),
  },
  {
    id: 'starter-whoosh-up',
    name: 'Whoosh Up',
    tags: ['FX'],
    builtIn: true,
    params: fromDefault((p) => {
      // Upward filter sweep build-up. Hold one note → riser.
      p.osc[0].volume = 0.8;
      p.osc[0].detune_cents = -20;
      p.osc[1].volume = 0.8;
      p.osc[1].detune_cents = 20;
      p.filter.cutoff_hz = 300;
      p.filter.resonance = 0.4;
      p.filter.env_mod = 0.95;
      p.filter_env.attack_s = 3.5;
      p.filter_env.decay_s = 0.8;
      p.filter_env.sustain = 1;
      p.filter_env.release_s = 0.4;
      p.amp_env.attack_s = 1.5;
      p.amp_env.release_s = 0.6;
      p.reverb.mix = 0.5;
      p.delay.mix = 0.3;
    }),
  },
  {
    id: 'starter-risers',
    name: 'Risers',
    tags: ['FX', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      // Stadium-style EDM riser. Pitch + filter both climbing.
      p.osc[0].volume = 0.9;
      p.osc[0].detune_cents = -12;
      p.osc[1].volume = 0.9;
      p.osc[1].detune_cents = 12;
      p.osc[2].volume = 0.5;
      p.osc[2].semitone_offset = 12;
      p.filter.cutoff_hz = 800;
      p.filter.resonance = 0.35;
      p.filter.env_mod = 0.85;
      p.filter_env.attack_s = 2.8;
      p.filter_env.sustain = 1;
      p.amp_env.attack_s = 0.5;
      p.amp_env.sustain = 1;
      p.amp_env.release_s = 0.4;
      p.lfo[0].rate_hz = 0.3;
      p.lfo[0].depth = 0.3;
      p.reverb.size = 0.88;
      p.reverb.mix = 0.55;
      p.delay.feedback = 0.55;
      p.delay.mix = 0.3;
    }),
  },
  {
    id: 'starter-tonal-drone',
    name: 'Tonal Drone',
    tags: ['FX', 'Pad'],
    builtIn: true,
    params: fromDefault((p) => {
      // Detuned octaves, beating phase — meditative drone.
      p.osc[0].volume = 0.75;
      p.osc[0].semitone_offset = -12;
      p.osc[0].detune_cents = -2;
      p.osc[1].volume = 0.75;
      p.osc[1].semitone_offset = -12;
      p.osc[1].detune_cents = 2;
      p.osc[2].volume = 0.5;
      p.osc[2].semitone_offset = 0;
      p.filter.cutoff_hz = 2000;
      p.filter.resonance = 0.05;
      p.amp_env.attack_s = 2.5;
      p.amp_env.sustain = 1;
      p.amp_env.release_s = 3.5;
      p.reverb.size = 0.95;
      p.reverb.mix = 0.6;
    }),
  },
  {
    id: 'starter-robotic-voice',
    name: 'Robotic Voice',
    tags: ['FX', 'Lead'],
    builtIn: true,
    params: fromDefault((p) => {
      // Formant-flavored vocal stab via dual PWM + slow filter LFO.
      p.osc[0].volume = 0.9;
      p.osc[0].pulse_width = 0.25;
      p.osc[1].volume = 0.7;
      p.osc[1].pulse_width = 0.65;
      p.osc[1].semitone_offset = 7;
      p.filter.cutoff_hz = 1600;
      p.filter.resonance = 0.62;
      p.filter.drive = 0.35;
      p.amp_env.attack_s = 0.04;
      p.amp_env.decay_s = 0.25;
      p.amp_env.sustain = 0.5;
      p.amp_env.release_s = 0.3;
      p.lfo[0].rate_hz = 4.5;
      p.lfo[0].depth = 0.35;
      p.delay.time_s = 0.12;
      p.delay.feedback = 0.4;
      p.delay.mix = 0.25;
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
