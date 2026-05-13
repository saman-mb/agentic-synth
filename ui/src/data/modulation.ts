// ── Modulation matrix data model (Phase 8) ───────────────────────────
//
// Frontend-only routing: the C++ engine still wires LFO/ENV via the
// `lfo.target` enum on the patch itself. This layer captures the UI
// state for drag-to-assign sources → destinations + the constellation
// view, persisted to localStorage so user-created routes survive
// reloads.

export type ModSourceId =
  | 'lfo1'
  | 'lfo2'
  | 'env1'
  | 'env2'
  | 'macro1'
  | 'macro2'
  | 'macro3'
  | 'macro4'
  | 'velocity'
  | 'keytrack';

export interface ModConnection {
  id: string;          // unique
  source: ModSourceId;
  destination: string; // param key like "filter.cutoff_hz" or "osc.0.pulse_width"
  amount: number;      // -1..1 (bipolar)
  enabled: boolean;
}

export interface ModMatrix {
  connections: ModConnection[];
}

export const MOD_MATRIX_STORAGE_KEY = 'timbre:mod-matrix';

export const MOD_SOURCES: ModSourceId[] = [
  'lfo1', 'lfo2', 'env1', 'env2',
  'macro1', 'macro2', 'macro3', 'macro4',
  'velocity', 'keytrack',
];

// Pretty short labels (header dots + matrix rows).
export const MOD_SOURCE_LABELS: Record<ModSourceId, string> = {
  lfo1: 'LFO 1',
  lfo2: 'LFO 2',
  env1: 'ENV 1',
  env2: 'ENV 2',
  macro1: 'Macro 1',
  macro2: 'Macro 2',
  macro3: 'Macro 3',
  macro4: 'Macro 4',
  velocity: 'Velocity',
  keytrack: 'Keytrack',
};

// CSS custom-property reference per source. Kept in one place so the
// dots, mod rings, halos and constellation threads all stay in sync.
export const MOD_SOURCE_VARS: Record<ModSourceId, string> = {
  lfo1: '--mod-lfo1',
  lfo2: '--mod-lfo2',
  env1: '--mod-env1',
  env2: '--mod-env2',
  macro1: '--mod-macro1',
  macro2: '--mod-macro2',
  macro3: '--mod-macro3',
  macro4: '--mod-macro4',
  velocity: '--mod-velocity',
  keytrack: '--mod-keytrack',
};

export function makeConnectionId(): string {
  return `mc-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

export function loadModMatrix(): ModMatrix {
  if (typeof window === 'undefined') return { connections: [] };
  try {
    const raw = window.localStorage.getItem(MOD_MATRIX_STORAGE_KEY);
    if (!raw) return { connections: [] };
    const parsed = JSON.parse(raw) as ModMatrix;
    if (!parsed || !Array.isArray(parsed.connections)) return { connections: [] };
    // Best-effort validation: drop anything missing required fields so a
    // bad shape from an older build doesn't poison the UI.
    const valid = parsed.connections.filter(
      (c) =>
        typeof c === 'object' &&
        c !== null &&
        typeof c.id === 'string' &&
        typeof c.source === 'string' &&
        typeof c.destination === 'string' &&
        typeof c.amount === 'number' &&
        typeof c.enabled === 'boolean',
    );
    return { connections: valid };
  } catch {
    return { connections: [] };
  }
}

export function saveModMatrix(m: ModMatrix): void {
  if (typeof window === 'undefined') return;
  try {
    window.localStorage.setItem(MOD_MATRIX_STORAGE_KEY, JSON.stringify(m));
  } catch {
    // private mode — silently no-op
  }
}

// Find the dominant (strongest |amount|, enabled) connection for a
// destination. Used to drive the single ring/halo rendered on a knob;
// the full list lives in the matrix panel.
export function dominantConnection(
  m: ModMatrix,
  destination: string,
): ModConnection | null {
  let best: ModConnection | null = null;
  let bestAmt = 0;
  for (const c of m.connections) {
    if (!c.enabled) continue;
    if (c.destination !== destination) continue;
    const a = Math.abs(c.amount);
    if (a > bestAmt) {
      best = c;
      bestAmt = a;
    }
  }
  return best;
}

// Pretty labels for known destination keys. Falls back to the raw key.
export function destinationLabel(key: string): string {
  const parts = key.split('.');
  if (parts[0] === 'osc' && parts.length === 3) {
    return `OSC ${Number(parts[1]) + 1} · ${prettyParam(parts[2])}`;
  }
  if (parts[0] === 'lfo' && parts.length === 3) {
    return `LFO ${Number(parts[1]) + 1} · ${prettyParam(parts[2])}`;
  }
  if (parts.length === 2) {
    return `${prettyModule(parts[0])} · ${prettyParam(parts[1])}`;
  }
  if (parts.length === 1) return prettyParam(parts[0]);
  return key;
}

function prettyModule(s: string): string {
  switch (s) {
    case 'filter': return 'Filter';
    case 'amp_env': return 'Amp Env';
    case 'filter_env': return 'Env 2';
    case 'reverb': return 'Reverb';
    case 'delay': return 'Delay';
    default: return s;
  }
}

function prettyParam(s: string): string {
  switch (s) {
    case 'cutoff_hz': return 'Cutoff';
    case 'resonance': return 'Reso';
    case 'env_mod': return 'EnvMod';
    case 'key_track': return 'KeyTrk';
    case 'drive': return 'Drive';
    case 'attack_s': return 'Atk';
    case 'decay_s': return 'Dec';
    case 'sustain': return 'Sus';
    case 'release_s': return 'Rel';
    case 'rate_hz': return 'Rate';
    case 'depth': return 'Depth';
    case 'phase_offset': return 'Phase';
    case 'size': return 'Size';
    case 'damping': return 'Damp';
    case 'width': return 'Width';
    case 'mix': return 'Mix';
    case 'time_s': return 'Time';
    case 'feedback': return 'Feedback';
    case 'stereo': return 'Stereo';
    case 'volume': return 'Vol';
    case 'detune_cents': return 'Detune';
    case 'semitone_offset': return 'Semi';
    case 'pan': return 'Pan';
    case 'wavetable_pos': return 'WT';
    case 'pulse_width': return 'PW';
    case 'fm_ratio': return 'FM Rt';
    case 'fm_depth': return 'FM Dep';
    case 'master_gain': return 'Gain';
    case 'portamento_s': return 'Glide';
    default: return s;
  }
}

// Per-param numeric ranges. Mirrors the (min,max) tuples wired into
// the knob grid (see KnobGrid.tsx). Used by the Phase 13 macro
// projection path to (a) scale a normalized 0..1 macro into a delta
// in the destination's natural units and (b) clamp the resulting
// effective value to a sane range before pushing to the engine.
//
// Keep this in sync with the (min,max) literals in ModulesGrid/KnobGrid.
// If you add a destination in DESTINATION_CATALOG below, add its range
// here too — otherwise the macro projection silently falls back to
// the identity range [0,1] and clamping won't be meaningful.
export interface ParamRange { min: number; max: number; }

export const PARAM_RANGES: Record<string, ParamRange> = (() => {
  const r: Record<string, ParamRange> = {};
  for (const i of [0, 1, 2]) {
    r[`osc.${i}.volume`]          = { min: 0, max: 1 };
    r[`osc.${i}.detune_cents`]    = { min: -100, max: 100 };
    r[`osc.${i}.semitone_offset`] = { min: -48, max: 48 };
    r[`osc.${i}.wavetable_pos`]   = { min: 0, max: 1 };
    r[`osc.${i}.pan`]             = { min: -1, max: 1 };
    r[`osc.${i}.pulse_width`]     = { min: 0.01, max: 0.99 };
    r[`osc.${i}.fm_ratio`]        = { min: 0.5, max: 16 };
    r[`osc.${i}.fm_depth`]        = { min: 0, max: 1 };
  }
  r['filter.cutoff_hz'] = { min: 20, max: 20000 };
  r['filter.resonance'] = { min: 0, max: 1 };
  r['filter.env_mod']   = { min: -1, max: 1 };
  r['filter.key_track'] = { min: 0, max: 1 };
  r['filter.drive']     = { min: 0, max: 1 };
  for (const env of ['amp_env', 'filter_env']) {
    r[`${env}.attack_s`]  = { min: 0, max: 10 };
    r[`${env}.decay_s`]   = { min: 0, max: 10 };
    r[`${env}.sustain`]   = { min: 0, max: 1 };
    r[`${env}.release_s`] = { min: 0, max: 20 };
  }
  for (const i of [0, 1]) {
    r[`lfo.${i}.rate_hz`]      = { min: 0.01, max: 20 };
    r[`lfo.${i}.depth`]        = { min: 0, max: 1 };
    r[`lfo.${i}.phase_offset`] = { min: 0, max: 1 };
  }
  r['reverb.size']    = { min: 0, max: 1 };
  r['reverb.damping'] = { min: 0, max: 1 };
  r['reverb.width']   = { min: 0, max: 1 };
  r['reverb.mix']     = { min: 0, max: 1 };
  r['delay.time_s']   = { min: 0, max: 2 };
  r['delay.feedback'] = { min: 0, max: 0.99 };
  r['delay.stereo']   = { min: 0, max: 1 };
  r['delay.mix']      = { min: 0, max: 1 };
  r['master_gain']    = { min: 0, max: 1 };
  r['portamento_s']   = { min: 0, max: 2 };
  return r;
})();

// Resolve a macro source id (macro1..4) into a 0..3 index. Returns -1
// for non-macro sources.
export function macroIndexOf(source: ModSourceId): number {
  switch (source) {
    case 'macro1': return 0;
    case 'macro2': return 1;
    case 'macro3': return 2;
    case 'macro4': return 3;
    default: return -1;
  }
}

// Catalogue of legal destination keys — used by the "add row" dropdown
// in the matrix list view. Keep this in sync with ModulesGrid knob specs.
export const DESTINATION_CATALOG: string[] = [
  // OSC 1-3
  ...[0, 1, 2].flatMap((i) => [
    `osc.${i}.volume`,
    `osc.${i}.detune_cents`,
    `osc.${i}.semitone_offset`,
    `osc.${i}.wavetable_pos`,
    `osc.${i}.pan`,
    `osc.${i}.pulse_width`,
    `osc.${i}.fm_ratio`,
    `osc.${i}.fm_depth`,
  ]),
  // Filter
  'filter.cutoff_hz', 'filter.resonance', 'filter.env_mod', 'filter.key_track', 'filter.drive',
  // Amp env
  'amp_env.attack_s', 'amp_env.decay_s', 'amp_env.sustain', 'amp_env.release_s',
  // Filter env
  'filter_env.attack_s', 'filter_env.decay_s', 'filter_env.sustain', 'filter_env.release_s',
  // LFO
  ...[0, 1].flatMap((i) => [
    `lfo.${i}.rate_hz`,
    `lfo.${i}.depth`,
    `lfo.${i}.phase_offset`,
  ]),
  // Reverb
  'reverb.size', 'reverb.damping', 'reverb.width', 'reverb.mix',
  // Delay
  'delay.time_s', 'delay.feedback', 'delay.stereo', 'delay.mix',
  // Global
  'master_gain', 'portamento_s',
];

// Window-level signal used during drag (set on pointerdown of a source
// dot, cleared on pointerup). Knob drop handlers read it to know what
// to assign. Typed via a module augmentation in the global.d.ts file —
// here we declare the surface for callers.
declare global {
  // eslint-disable-next-line no-var
  var __timbreDragSource: ModSourceId | null | undefined;
}

export function setDragSource(id: ModSourceId | null): void {
  if (typeof window === 'undefined') return;
  (window as unknown as { __timbreDragSource: ModSourceId | null }).__timbreDragSource = id;
}

export function getDragSource(): ModSourceId | null {
  if (typeof window === 'undefined') return null;
  return (window as unknown as { __timbreDragSource: ModSourceId | null }).__timbreDragSource ?? null;
}
