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
