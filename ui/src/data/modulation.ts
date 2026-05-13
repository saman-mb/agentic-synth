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
    case 'volume': return 'Vol';
    case 'detune_cents': return 'Detune';
    case 'semitone_offset': return 'Semi';
    case 'pan': return 'Pan';
    case 'pulse_width': return 'PW';
    case 'fm_ratio': return 'FM Rt';
    case 'fm_depth': return 'FM Dep';
    case 'master_gain': return 'Gain';
    case 'portamento_s': return 'Glide';
    default: return s;
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
    `osc.${i}.pan`,
    `osc.${i}.pulse_width`,
    `osc.${i}.fm_ratio`,
    `osc.${i}.fm_depth`,
  ]),
  // Filter
  'filter.cutoff_hz', 'filter.resonance', 'filter.env_mod', 'filter.drive',
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
  'delay.time_s', 'delay.feedback', 'delay.mix',
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
